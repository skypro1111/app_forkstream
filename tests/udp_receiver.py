#!/usr/bin/env python3
"""
Simple UDP receiver for testing ForkStream module

This script listens for UDP packets sent by the ForkStream module
and displays information about received audio data.
"""

import socket
import sys
import struct
import time
from datetime import datetime

def format_bytes(data, max_display=32):
    """Format bytes for display, showing hex values"""
    if len(data) <= max_display:
        return ' '.join(f'{b:02x}' for b in data)
    else:
        displayed = data[:max_display]
        return ' '.join(f'{b:02x}' for b in displayed) + f' ... ({len(data)} bytes total)'

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 udp_receiver.py <ip> <port>")
        print("Example: python3 udp_receiver.py 127.0.0.1 8080")
        sys.exit(1)
    
    host = sys.argv[1]
    port = int(sys.argv[2])
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        # Bind to the specified address
        sock.bind((host, port))
        print(f"UDP receiver listening on {host}:{port}")
        print("Waiting for ForkStream packets... (Press Ctrl+C to stop)")
        print("-" * 70)
        
        packet_count = 0
        total_bytes = 0
        start_time = time.time()
        
        while True:
            try:
                # Receive data
                data, addr = sock.recvfrom(65536)  # Max UDP packet size
                packet_count += 1
                total_bytes += len(data)
                
                timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                
                print(f"[{timestamp}] Packet #{packet_count:4d} from {addr[0]}:{addr[1]}")
                print(f"  Size: {len(data):4d} bytes")
                print(f"  Data: {format_bytes(data)}")
                
                # Try to detect common audio patterns
                if len(data) >= 4:
                    # Check for common audio frame sizes
                    if len(data) in [160, 320, 480, 960]:  # Common for 8kHz/16kHz audio
                        print(f"  Note: Size suggests possible G.711/G.722 audio frame")
                    elif len(data) in [20, 33]:  # Common for G.729
                        print(f"  Note: Size suggests possible G.729 audio frame")
                
                print()
                
            except socket.timeout:
                continue
                
    except KeyboardInterrupt:
        elapsed = time.time() - start_time
        print(f"\n{'-' * 70}")
        print(f"Statistics:")
        print(f"  Packets received: {packet_count}")
        print(f"  Total bytes: {total_bytes}")
        print(f"  Duration: {elapsed:.1f} seconds")
        if elapsed > 0:
            print(f"  Average rate: {packet_count/elapsed:.1f} packets/sec")
            print(f"  Average bandwidth: {total_bytes*8/elapsed/1000:.1f} kbps")
        
    except Exception as e:
        print(f"Error: {e}")
        
    finally:
        sock.close()
        print("UDP receiver stopped.")

if __name__ == "__main__":
    main() 