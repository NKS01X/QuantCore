#!/usr/bin/env python3
"""
ZeroMQ Subscriber — IPC Bridge Test
Connects to tcp://127.0.0.1:5555 and prints incoming JSON messages.
Run the C++ trading_engine first, then start this script.
"""

import json
import signal
import sys
import time

import zmq

ZMQ_ADDRESS = "tcp://127.0.0.1:5555"

# Graceful shutdown on Ctrl+C
running = True

def signal_handler(sig, frame):
    global running
    print("\n[SUB] Shutting down...")
    running = False

signal.signal(signal.SIGINT, signal_handler)


def main():
    context = zmq.Context()
    socket = context.socket(zmq.SUB)

    # Subscribe to ALL messages (empty topic filter)
    socket.setsockopt_string(zmq.SUBSCRIBE, "")

    # Connect to the C++ publisher
    socket.connect(ZMQ_ADDRESS)
    print(f"[SUB] Connected to {ZMQ_ADDRESS}")
    print("[SUB] Waiting for messages... (Ctrl+C to quit)\n")

    # Use a poller so recv doesn't block forever (allows clean shutdown)
    poller = zmq.Poller()
    poller.register(socket, zmq.POLLIN)

    msg_count = 0

    while running:
        events = dict(poller.poll(timeout=100))  # 100ms timeout

        if socket in events:
            raw = socket.recv_string()
            msg_count += 1

            try:
                data = json.loads(raw)

                # Pretty-print key fields
                seq = data.get("seq", "?")
                ts = data.get("timestamp_ms", 0)
                symbol = data.get("symbol", "???")
                mid = data.get("mid_price", 0.0)
                obi = data.get("obi", 0.0)
                n_bids = len(data.get("bids", []))
                n_asks = len(data.get("asks", []))

                print(
                    f"[#{msg_count:>6}]  seq={seq:<6}  "
                    f"{symbol}  mid=${mid:,.2f}  "
                    f"OBI={obi:+.4f}  "
                    f"bids={n_bids}  asks={n_asks}  "
                    f"ts={ts}"
                )

            except json.JSONDecodeError:
                print(f"[SUB] Raw (non-JSON): {raw[:120]}")

    # Cleanup
    socket.close()
    context.term()
    print(f"[SUB] Done. Received {msg_count} messages total.")


if __name__ == "__main__":
    main()
