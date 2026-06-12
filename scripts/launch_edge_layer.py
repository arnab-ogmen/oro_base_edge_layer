#!/usr/bin/env python3
import subprocess
import sys
import os
import signal
import time
import threading

MAX_RESTARTS = 20
# Progressive backoff: 0s, 1s, 2s, 4s, 8s, 16s, then caps at 30s
MAX_BACKOFF_S = 30

class ProcessManager:
    def __init__(self):
        # Maps name -> {'cmd': list, 'cwd': str, 'process': Popen,
        #               'restarts': int, 'last_start': float}
        self.nodes = {}
        self.shutting_down = False
        self.lock = threading.RLock()

    def start_process(self, name, cmd, cwd=None, env=None):
        if self.shutting_down:
            return
        
        with self.lock:
            if name not in self.nodes:
                self.nodes[name] = {'cmd': cmd, 'cwd': cwd, 'env': env, 'restarts': 0, 'last_start': 0.0}
            
            info = self.nodes[name]
            restarts = info['restarts']

            # ── Backoff on respawn ────────────────────────────────────────
            if restarts > 0:
                backoff = min(MAX_BACKOFF_S, 2 ** (restarts - 1))
                elapsed = time.monotonic() - info['last_start']
                if elapsed < backoff:
                    wait = backoff - elapsed
                    print(f"[\033[93mBACKOFF\033[0m] Waiting {wait:.1f}s before restarting {name} (restart #{restarts})...")
                    deadline = time.monotonic() + wait
                    while time.monotonic() < deadline and not self.shutting_down:
                        time.sleep(0.5)
                    if self.shutting_down:
                        return

            restart_msg = f" (Restart #{restarts})" if restarts > 0 else ""
            print(f"[\033[92mINFO\033[0m] Starting {name}{restart_msg}...")
            
            try:
                p = subprocess.Popen(
                    cmd,
                    cwd=cwd,
                    env=env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1
                )
                info['process'] = p
                info['last_start'] = time.monotonic()
                
                # Start a new reader thread for this process instance
                t = threading.Thread(target=self._reader_thread, args=(name, p), daemon=True)
                t.start()
            except Exception as e:
                print(f"[\033[91mERROR\033[0m] Failed to start {name}: {e}")

    def _reader_thread(self, name, p):
        colors = {
            'command_executor': '\033[92m',      # Green
            'dog_wellbeing_node': '\033[94m',    # Blue
            'environment_condition': '\033[96m', # Cyan
            'health_monitoring': '\033[95m'       # Magenta
        }
        color = colors.get(name, '\033[97m')
        reset = '\033[0m'
        
        for line in p.stdout:
            sys.stdout.write(f"{color}[{name}]{reset} {line}")
            sys.stdout.flush()

    def terminate_all(self):
        self.shutting_down = True
        print("\n[\033[92mINFO\033[0m] Shutting down all edge nodes...")
        
        with self.lock:
            # Send SIGTERM
            for name, info in self.nodes.items():
                p = info.get('process')
                if p and p.poll() is None:
                    print(f"[\033[92mINFO\033[0m] Terminating {name}...")
                    p.terminate()
            
            # Wait and kill if necessary
            for name, info in self.nodes.items():
                p = info.get('process')
                if p and p.poll() is None:
                    try:
                        p.wait(timeout=3.0)
                    except subprocess.TimeoutExpired:
                        print(f"[\033[91mWARNING\033[0m] {name} did not terminate, killing it...")
                        p.kill()
                        
        print("[\033[92mINFO\033[0m] All edge nodes shut down cleanly.")

    def monitor(self):
        try:
            while not self.shutting_down:
                time.sleep(2.0)
                
                if self.shutting_down:
                    break
                    
                with self.lock:
                    all_dead = True
                    for name, info in list(self.nodes.items()):
                        p = info.get('process')
                        if p:
                            if p.poll() is None:
                                all_dead = False
                            else:
                                if not self.shutting_down:
                                    rc = p.returncode
                                    restarts = info['restarts']

                                    if restarts >= MAX_RESTARTS:
                                        print(f"[\033[91mFATAL\033[0m] {name} has crashed {restarts} times. "
                                              f"Giving up — check config/logs.")
                                        continue

                                    print(f"[\033[91mERROR\033[0m] {name} exited unexpectedly (code {rc}). Respawning...")
                                    info['restarts'] += 1
                                    self.start_process(name, info['cmd'], info['cwd'], info['env'])
                                    all_dead = False
                    
                    if all_dead and self.nodes:
                        pass
        except KeyboardInterrupt:
            pass
        finally:
            self.terminate_all()

def main():
    script_dir = os.path.dirname(os.path.realpath(__file__))
    edge_root = os.path.dirname(script_dir)

    manager = ProcessManager()

    def signal_handler(sig, frame):
        manager.shutting_down = True

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # ── 1. Update Cron Configurations (One-shot setup) ────────────────
    stm_build_dir = os.path.join(edge_root, "scheduled_task_manager", "build")
    stm_bin = os.path.join(stm_build_dir, "scheduled_task_manager_node")
    if os.path.exists(stm_bin):
        print("[\033[92mINFO\033[0m] Running one-shot scheduled_task_manager --setup-cron...")
        try:
            subprocess.run([stm_bin, "--setup-cron"], cwd=stm_build_dir, check=True)
            print("[\033[92mINFO\033[0m] Scheduled tasks cron rules verified/written successfully.")
        except Exception as e:
            print(f"[\033[91mWARNING\033[0m] Failed to setup cron tasks on startup: {e}")
    else:
        print(f"[\033[93mWARNING\033[0m] Scheduled Task Manager binary not found at {stm_bin}. Skipping cron setup.")

    # ── 2. Clean up any stale ZMQ IPC socket files ────────────────────
    ipc_files = [
        "/tmp/oro_sensors.ipc",
        "/tmp/oro_system.ipc",
        "/tmp/oro_status.ipc",
        "/tmp/oro_cmd_exec.ipc",
        "/tmp/oro_cmd_result.ipc"
    ]
    for ipc_path in ipc_files:
        if os.path.exists(ipc_path):
            try:
                print(f"[\033[92mINFO\033[0m] Removing stale ZMQ IPC file/socket at {ipc_path}...")
                os.remove(ipc_path)
            except Exception as e:
                print(f"[\033[91mWARNING\033[0m] Failed to remove {ipc_path}: {e}")

    # ── 3. Build Subprocess Environment with config path ──────────────
    env = os.environ.copy()
    config_file_path = os.path.join(edge_root, "config", "oro_base_edge_layer_config.json")
    env["ORO_EDGE_CONFIG"] = config_file_path

    # ── 4. Define Persistent Edge Layer Nodes ─────────────────────────
    nodes = [
        ("command_executor", os.path.join(edge_root, "command_executor", "build", "command_executor_node"),
         os.path.join(edge_root, "command_executor", "build")),
        ("dog_wellbeing_node", os.path.join(edge_root, "dog_wellbeing_node", "build", "dog_wellbeing_node"),
         os.path.join(edge_root, "dog_wellbeing_node", "build")),
        ("environment_condition", os.path.join(edge_root, "environment_condition_node", "build", "environment_condition_node"),
         os.path.join(edge_root, "environment_condition_node", "build")),
        ("health_monitoring", os.path.join(edge_root, "health_monitoring_node", "build", "health_monitoring_node"),
         os.path.join(edge_root, "health_monitoring_node", "build"))
    ]

    # ── 5. Start nodes ────────────────────────────────────────────────
    for name, cmd, cwd in nodes:
        if os.path.exists(cmd):
            # Run with stdbuf -oL -eL to force line-buffered stdout/stderr in the pipe
            stdbuf_cmd = ["stdbuf", "-oL", "-eL", cmd]
            manager.start_process(name, stdbuf_cmd, cwd=cwd, env=env)
        else:
            print(f"[\033[93mWARNING\033[0m] Executable {cmd} not found. Skipping {name}.")

    if not manager.nodes:
        print("[\033[91mERROR\033[0m] No persistent edge layer nodes found. Exiting.")
        sys.exit(1)

    manager.monitor()

if __name__ == "__main__":
    main()
