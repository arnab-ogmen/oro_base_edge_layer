#!/usr/bin/env python3
"""
Simple CommandExecutor Test Client
---------------------------------
Sends one valid JSON command directly to the Cloud Receiver over TCP 5555.
"""

import zmq
import json
import time
import uuid

CLOUD_ENDPOINT = "tcp://localhost:5555"
TIMEOUT_MS = 10000

def main():
    ctx = zmq.Context()

    # Socket to SEND command and RECEIVE response (REQ/REP)
    req_sock = ctx.socket(zmq.REQ)
    req_sock.connect(CLOUD_ENDPOINT)
    req_sock.setsockopt(zmq.RCVTIMEO, TIMEOUT_MS)

    # Allow sockets to connect
    time.sleep(0.3)

    # Generate unique command_id
    command_id = f"test_{uuid.uuid4().hex[:8]}"

    # Construct valid command JSON
    command = {
        "header": {
            "signal_id": 84,
            "signal_type": "manual_lid_open_command_event",
            "command_id": command_id,
            "source": "UCES",
            "issued_by": "unit_test_script",
            "event_time": int(time.time())
        },
        "payload": {
            "timeout_ms": 5000
        }
    }

    print("\n📤 Sending Command:")
    print(json.dumps(command, indent=2))

    # Send command
    req_sock.send_string(json.dumps(command))

    try:
        # Wait for response
        raw = req_sock.recv_string()
        response = json.loads(raw)

        print("\n📥 Received Response:")
        print(json.dumps(response, indent=2))

    except zmq.Again:
        print("\n❌ ERROR: Timed out waiting for response")

    finally:
        req_sock.close()
        ctx.term()


if __name__ == "__main__":
    main()
