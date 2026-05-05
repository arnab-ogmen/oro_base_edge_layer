#!/usr/bin/env python3
"""
Interactive Lid Actuation Test Script
-------------------------------------
Prompts the user for a lid selection and an action (OPEN/CLOSE),
then sends the command to the Cloud Receiver over TCP 5555.
"""

import zmq
import json
import time
import uuid

CLOUD_ENDPOINT = "tcp://localhost:5555"
TIMEOUT_MS = 10000

def main():
    print("=======================================")
    print(" Interactive Lid Actuation Test Script ")
    print("=======================================")

    while True:
        try:
            lid_input = input("\nEnter Lid ID (1 or 2, 'q' to quit): ").strip()
            if lid_input.lower() == 'q':
                break
            lid_id = int(lid_input)
            if lid_id not in [1, 2]:
                print("Invalid Lid ID. Please enter 1 or 2.")
                continue

            action_input = input("Enter Action ('o' for OPEN, 'c' for CLOSE, 'q' to quit): ").strip().lower()
            if action_input == 'q':
                break
            elif action_input == 'o':
                action = 1
            elif action_input == 'c':
                action = 0
            else:
                print("Invalid action. Please enter 'o' or 'c'.")
                continue

            # Calculate an appropriate timeout based on the firmware simulation
            # (Firmware uses 1500/1600ms for OPEN and 800/900ms for CLOSE)
            req_timeout_ms = 5000

            ctx = zmq.Context()
            req_sock = ctx.socket(zmq.REQ)
            req_sock.connect(CLOUD_ENDPOINT)
            req_sock.setsockopt(zmq.RCVTIMEO, TIMEOUT_MS)

            time.sleep(0.1) # Small delay for socket connection

            command_id = f"test_lid_{uuid.uuid4().hex[:8]}"
            
            command = {
                "header": {
                    "signal_id": 64,
                    "signal_type": "lid_actuation_command",
                    "command_id": command_id,
                    "source": "UCES",
                    "issued_by": "interactive_test",
                    "event_time": int(time.time())
                },
                "payload": {
                    "lid_id": lid_id,
                    "action": action,
                    "timeout_ms": req_timeout_ms
                }
            }

            print("\n📤 Sending Command:")
            print(json.dumps(command, indent=2))

            req_sock.send_string(json.dumps(command))

            print("⏳ Waiting for response...")
            try:
                raw = req_sock.recv_string()
                response = json.loads(raw)
                print("📥 Received Response:")
                print(json.dumps(response, indent=2))
            except zmq.Again:
                print("❌ ERROR: Timed out waiting for response")
            except json.JSONDecodeError:
                print("❌ ERROR: Invalid JSON received")
            
            req_sock.close()
            ctx.term()

        except ValueError:
            print("Invalid input. Please enter a valid number.")
        except KeyboardInterrupt:
            print("\nExiting...")
            break

if __name__ == "__main__":
    main()
