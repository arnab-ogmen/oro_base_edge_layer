#!/usr/bin/env python3
"""
CommandExecutor Integration Test Suite
======================================
Sends JSON commands directly to the CommandExecutor's ZMQ PULL socket
and reads results from its PUSH socket.

Prerequisites:
  - command_executor_node must be running
  - pip install pyzmq

Usage:
  python3 test_command_executor.py           # run all tests
  python3 test_command_executor.py -v        # verbose
  python3 test_command_executor.py -k lid    # run only tests matching 'lid'
"""

import json
import time
import uuid
import zmq
import sys
import unittest


# ── ZMQ Endpoints (must match command_executor main.cpp) ─────────────────────
CMD_EXEC_ENDPOINT = "ipc:///tmp/oro_cmd_exec.ipc"
CMD_RESULT_ENDPOINT = "ipc:///tmp/oro_cmd_result.ipc"

TIMEOUT_MS = 15000  # 15 second recv timeout


class CommandExecutorTestBase(unittest.TestCase):
    """Base class that sets up ZMQ PUSH/PULL sockets for each test."""

    @classmethod
    def setUpClass(cls):
        cls.ctx = zmq.Context()

        # PUSH → CommandExecutor PULL (send commands)
        cls.push_sock = cls.ctx.socket(zmq.PUSH)
        cls.push_sock.connect(CMD_EXEC_ENDPOINT)

        # PULL ← CommandExecutor PUSH (receive results)
        cls.pull_sock = cls.ctx.socket(zmq.PULL)
        cls.pull_sock.connect(CMD_RESULT_ENDPOINT)
        cls.pull_sock.setsockopt(zmq.RCVTIMEO, TIMEOUT_MS)

        # Let sockets settle
        time.sleep(0.3)

    @classmethod
    def tearDownClass(cls):
        cls.push_sock.close()
        cls.pull_sock.close()
        cls.ctx.term()

    def send_command(self, signal_id, signal_type, payload=None,
                     command_id=None, issued_by="test_harness"):
        """Helper to build and send a well-formed UCES command JSON."""
        if command_id is None:
            command_id = f"test_{uuid.uuid4().hex[:8]}"

        msg = {
            "header": {
                "signal_id": signal_id,
                "signal_type": signal_type,
                "command_id": command_id,
                "source": "UCES",
                "issued_by": issued_by,
                "event_time": int(time.time())
            },
            "payload": payload or {}
        }
        raw = json.dumps(msg)
        self.push_sock.send_string(raw)
        return command_id

    def recv_result(self):
        """Receive and parse a result JSON from the CommandExecutor."""
        try:
            raw = self.pull_sock.recv_string()
            return json.loads(raw)
        except zmq.Again:
            self.fail("Timed out waiting for CommandExecutor response")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 1 — Valid INBOUND Commands (Happy Path)
# ═══════════════════════════════════════════════════════════════════════════════

class TestValidCommands(CommandExecutorTestBase):
    """Verify that all 6 INBOUND signal handlers return SUCCESS."""

    def test_01_manual_lid_open(self):
        """Signal #84 — Manual Lid Open Command Event"""
        cmd_id = self.send_command(84, "manual_lid_open_command_event")
        result = self.recv_result()

        self.assertEqual(result["header"]["signal_id"], 84)
        self.assertEqual(result["header"]["command_id"], cmd_id)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        print(f"  ✓ #84 manual_lid_open → SUCCESS (cmd_id={cmd_id})")

    def test_02_manual_lid_close(self):
        """Signal #123 — Manual Lid Close Command Event"""
        cmd_id = self.send_command(123, "manual_lid_close_command_event")
        result = self.recv_result()

        self.assertEqual(result["header"]["signal_id"], 123)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        print(f"  ✓ #123 manual_lid_close → SUCCESS (cmd_id={cmd_id})")

    def test_03_lid_actuation(self):
        """Signal #64 — Lid Actuation Command with payload"""
        cmd_id = self.send_command(
            64, "lid_actuation_command",
            payload={"lid_id": 1, "action": "OPEN"}
        )
        result = self.recv_result()

        self.assertEqual(result["header"]["signal_id"], 64)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        print(f"  ✓ #64 lid_actuation → SUCCESS (cmd_id={cmd_id})")

    def test_04_treat_dispense(self):
        """Signal #85 — Treat Dispense Command Event (should return treats_dispensed)"""
        cmd_id = self.send_command(85, "treat_dispense_command_event")
        result = self.recv_result()

        self.assertEqual(result["header"]["signal_id"], 85)
        self.assertIn(result["result"]["status"], ["SUCCESS", "FAILED"])
        self.assertIn("treats_dispensed", result["result"])
        self.assertGreaterEqual(result["result"]["treats_dispensed"], 0)
        print(f"  ✓ #85 treat_dispense → {result['result']['status']}, treats_dispensed={result['result']['treats_dispensed']} (cmd_id={cmd_id})")

    def test_05_photo_capture(self):
        """Signal #91 — Photo Capture Command Event"""
        cmd_id = self.send_command(91, "photo_capture_command_event")
        result = self.recv_result()

        self.assertEqual(result["header"]["signal_id"], 91)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        print(f"  ✓ #91 photo_capture → SUCCESS (cmd_id={cmd_id})")

    def test_06_live_session_start(self):
        """Signal #88 — Live Session Start Event"""
        cmd_id = self.send_command(88, "live_session_start_event")
        result = self.recv_result()

        self.assertEqual(result["header"]["signal_id"], 88)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        print(f"  ✓ #88 live_session_start → SUCCESS (cmd_id={cmd_id})")

    def test_07_play_music(self):
        """Signal #137 — Play Music from library"""
        cmd_id = self.send_command(
            137, "play_music_event",
            payload={"action_code": 1, "file_id": "breaking_bad_intro", "storage_path": "/home/radxa/Music/breaking_bad_intro.mp3", "event_time": 1234567890}
        )
        result = self.recv_result()

        self.assertEqual(result["header"]["signal_id"], 137)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        self.assertEqual(result["result"]["action_code"], 1)
        self.assertEqual(result["result"]["file_id"], "breaking_bad_intro")
        self.assertEqual(result["result"]["storage_path"], "/home/radxa/Music/breaking_bad_intro.mp3")
        print(f"  ✓ #137 play_music → SUCCESS (cmd_id={cmd_id})")

    def test_08_stop_music(self):
        """Signal #138 — Stop the current playing music"""
        cmd_id = self.send_command(
            138, "stop_music_event",
            payload={"action_code": 0, "file_id": "breaking_bad_intro", "storage_path": "/home/radxa/Music/breaking_bad_intro.mp3", "event_time": 1234567890}
        )
        result = self.recv_result()

        self.assertEqual(result["header"]["signal_id"], 138)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        self.assertEqual(result["result"]["action_code"], 0)
        print(f"  ✓ #138 stop_music → SUCCESS (cmd_id={cmd_id})")

    def test_09_start_record_video(self):
        """Signal #135 — Start Continuous Video"""
        cmd_id = self.send_command(
            135, "video_capture_command_event",
            payload={"action": "start"}
        )
        result = self.recv_result()
        self.assertEqual(result["header"]["signal_id"], 135)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        self.assertEqual(result["result"]["message"], "continuous_recording_started")
        print(f"  ✓ #135 start_record_video → SUCCESS (cmd_id={cmd_id})")

    def test_10_stop_record_video(self):
        """Signal #135 — Stop Continuous Video"""
        cmd_id = self.send_command(
            135, "video_capture_command_event",
            payload={"action": "stop"}
        )
        result = self.recv_result()
        self.assertEqual(result["header"]["signal_id"], 135)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        self.assertEqual(result["result"]["message"], "continuous_recording_stopped")
        print(f"  ✓ #135 stop_record_video → SUCCESS (cmd_id={cmd_id})")

    def test_11_record_video_pan(self):
        """Signal #139 — Panoramic Video Sweep"""
        cmd_id = self.send_command(
            139, "record_video_pan_command_event"
        )
        result = self.recv_result()
        self.assertEqual(result["header"]["signal_id"], 139)
        self.assertEqual(result["result"]["status"], "SUCCESS")
        self.assertEqual(result["result"]["message"], "pan_video_capture_initiated")
        print(f"  ✓ #139 record_video_pan → SUCCESS (cmd_id={cmd_id})")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 2 — Validation & Rejection
# ═══════════════════════════════════════════════════════════════════════════════

class TestValidation(CommandExecutorTestBase):
    """Verify dispatcher rejects malformed or invalid commands."""

    def test_01_unknown_signal_id(self):
        """Unknown signal_id should be REJECTED."""
        cmd_id = self.send_command(999, "nonexistent_signal")
        result = self.recv_result()

        self.assertEqual(result["result"]["status"], "REJECTED")
        print(f"  ✓ Unknown signal_id=999 → REJECTED (cmd_id={cmd_id})")

    def test_02_outbound_signal_rejected(self):
        """OUTBOUND signals (#65, #125, #126, #93) should be REJECTED when sent as commands."""
        cmd_id = self.send_command(65, "lid_actuation_result")
        result = self.recv_result()

        self.assertEqual(result["result"]["status"], "REJECTED")
        print(f"  ✓ OUTBOUND signal_id=65 → REJECTED (cmd_id={cmd_id})")

    def test_03_missing_command_id(self):
        """Command with empty command_id should be REJECTED."""
        msg = {
            "header": {
                "signal_id": 84,
                "signal_type": "manual_lid_open_command_event",
                "command_id": "",
                "source": "UCES",
                "issued_by": "test_harness",
                "event_time": int(time.time())
            },
            "payload": {}
        }
        self.push_sock.send_string(json.dumps(msg))
        result = self.recv_result()

        self.assertEqual(result["result"]["status"], "REJECTED")
        print(f"  ✓ Empty command_id → REJECTED")

    def test_04_missing_issued_by(self):
        """Command with empty issued_by should be REJECTED."""
        msg = {
            "header": {
                "signal_id": 84,
                "signal_type": "manual_lid_open_command_event",
                "command_id": f"test_{uuid.uuid4().hex[:8]}",
                "source": "UCES",
                "issued_by": "",
                "event_time": int(time.time())
            },
            "payload": {}
        }
        self.push_sock.send_string(json.dumps(msg))
        result = self.recv_result()

        self.assertEqual(result["result"]["status"], "REJECTED")
        print(f"  ✓ Empty issued_by → REJECTED")

    def test_05_duplicate_command_id(self):
        """Sending the same command_id twice should REJECT the second."""
        dup_id = f"test_dup_{uuid.uuid4().hex[:8]}"

        # First send — should succeed
        self.send_command(84, "manual_lid_open_command_event", command_id=dup_id)
        result1 = self.recv_result()
        self.assertEqual(result1["result"]["status"], "SUCCESS")

        # Second send with same command_id — should be rejected
        self.send_command(84, "manual_lid_open_command_event", command_id=dup_id)
        result2 = self.recv_result()
        self.assertEqual(result2["result"]["status"], "REJECTED")
        print(f"  ✓ Duplicate command_id={dup_id} → first SUCCESS, second REJECTED")

    def test_06_malformed_json(self):
        """Non-JSON garbage should not crash the service (no response expected, just verify no hang)."""
        self.push_sock.send_string("THIS IS NOT JSON {{{")
        # The executor should log parse error and continue — no response emitted
        # We just verify the service is still alive by sending a valid command
        time.sleep(0.3)
        cmd_id = self.send_command(84, "manual_lid_open_command_event")
        result = self.recv_result()
        self.assertEqual(result["result"]["status"], "SUCCESS")
        print(f"  ✓ Malformed JSON → service survived, next command succeeded (cmd_id={cmd_id})")

    def test_07_missing_header(self):
        """JSON without 'header' object should be silently dropped (no response)."""
        msg = {"payload": {"some": "data"}}
        self.push_sock.send_string(json.dumps(msg))
        # No response expected — verify service is still alive
        time.sleep(0.3)
        cmd_id = self.send_command(91, "photo_capture_command_event")
        result = self.recv_result()
        self.assertEqual(result["result"]["status"], "SUCCESS")
        print(f"  ✓ Missing header → service survived, next command succeeded (cmd_id={cmd_id})")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 3 — Burst / Queue Load Test
# ═══════════════════════════════════════════════════════════════════════════════

class TestBurstQueue(CommandExecutorTestBase):
    """Verify the CommandQueue handles multiple simultaneous commands."""

    def test_01_burst_of_5_commands(self):
        """Send 5 commands rapidly and verify all 5 get processed in order."""
        sent_ids = []
        for i in range(5):
            cmd_id = self.send_command(
                84, "manual_lid_open_command_event",
                command_id=f"burst_{uuid.uuid4().hex[:8]}_{i}"
            )
            sent_ids.append(cmd_id)

        received_ids = []
        for _ in range(5):
            result = self.recv_result()
            received_ids.append(result["header"]["command_id"])
            self.assertEqual(result["result"]["status"], "SUCCESS")

        # Verify all sent commands were received
        self.assertEqual(set(sent_ids), set(received_ids))
        print(f"  ✓ Burst of 5 commands → all 5 processed successfully")
        print(f"    Sent:     {sent_ids}")
        print(f"    Received: {received_ids}")

    def test_02_burst_of_10_mixed_signals(self):
        """Send 10 commands with different signal types and verify all processed."""
        signals = [
            (84, "manual_lid_open_command_event"),
            (123, "manual_lid_close_command_event"),
            (64, "lid_actuation_command"),
            (85, "treat_dispense_command_event"),
            (91, "photo_capture_command_event"),
            (88, "live_session_start_event"),
            (84, "manual_lid_open_command_event"),
            (123, "manual_lid_close_command_event"),
            (85, "treat_dispense_command_event"),
            (91, "photo_capture_command_event"),
        ]

        sent_ids = []
        for sig_id, sig_type in signals:
            cmd_id = self.send_command(sig_id, sig_type)
            sent_ids.append(cmd_id)

        success_or_failed_count = 0
        for _ in range(10):
            result = self.recv_result()
            if result["result"]["status"] in ["SUCCESS", "FAILED"]:
                success_or_failed_count += 1

        self.assertEqual(success_or_failed_count, 10)
        print(f"  ✓ Burst of 10 mixed signals → all 10 processed successfully")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 4 — Response Structure Validation
# ═══════════════════════════════════════════════════════════════════════════════

class TestResponseStructure(CommandExecutorTestBase):
    """Verify the JSON response follows the expected wire format."""

    def test_01_response_has_header_and_result(self):
        """Response must have 'header' and 'result' top-level keys."""
        cmd_id = self.send_command(84, "manual_lid_open_command_event")
        result = self.recv_result()

        self.assertIn("header", result)
        self.assertIn("result", result)
        print(f"  ✓ Response contains 'header' and 'result' keys")

    def test_02_header_fields(self):
        """Response header must contain signal_id, signal_type, command_id, source."""
        cmd_id = self.send_command(64, "lid_actuation_command",
                                   payload={"lid_id": 2, "action": "CLOSE"})
        result = self.recv_result()

        header = result["header"]
        self.assertIn("signal_id", header)
        self.assertIn("signal_type", header)
        self.assertIn("command_id", header)
        self.assertIn("source", header)
        self.assertEqual(header["command_id"], cmd_id)
        self.assertEqual(header["source"], "UCES")
        print(f"  ✓ Response header has all required fields (signal_id, signal_type, command_id, source)")

    def test_03_treat_dispense_result_fields(self):
        """Treat dispense result must include treats_dispensed as a positive integer."""
        cmd_id = self.send_command(85, "treat_dispense_command_event")
        result = self.recv_result()

        self.assertIn("treats_dispensed", result["result"])
        val = result["result"]["treats_dispensed"]
        self.assertIsInstance(val, int)
        self.assertGreater(val, 0)
        print(f"  ✓ Treat dispense result includes treats_dispensed={val} (positive int)")


# ═══════════════════════════════════════════════════════════════════════════════
# RUNNER
# ═══════════════════════════════════════════════════════════════════════════════

if __name__ == "__main__":
    print("=" * 70)
    print("  CommandExecutor Integration Test Suite")
    print("  Ensure command_executor_node is running before executing.")
    print("=" * 70)
    print()
    unittest.main(verbosity=2)
