#!/usr/bin/env python3
"""
TLV UDP Receiver for testing ForkStream module

This script receives and parses TLV packets sent by the ForkStream Asterisk module.
It can handle both signaling and audio packets according to the TLV protocol.
"""

import socket
import struct
import json
import time
import os
from datetime import datetime
from collections import defaultdict

# Packet type constants
PACKET_TYPE_SIGNALING = 0x01
PACKET_TYPE_AUDIO = 0x02

# Direction constants  
DIRECTION_RX = 0x01
DIRECTION_TX = 0x02

def direction_to_string(direction):
    """Convert direction byte to string"""
    if direction == DIRECTION_RX:
        return "RX"
    elif direction == DIRECTION_TX:
        return "TX"
    else:
        return f"UNKNOWN({direction})"

def parse_forkstream_header(data):
    """Parse the 8-byte TLV header"""
    if len(data) < 8:
        return None
    
    # Unpack header: packet_type(1), packet_length(2), stream_id(4), direction(1)
    packet_type, packet_length, stream_id, direction = struct.unpack('!BHI B', data[:8])
    
    return {
        'packet_type': packet_type,
        'packet_length': packet_length,
        'stream_id': stream_id,
        'direction': direction
    }

def parse_signaling_packet(data):
    """Parse signaling packet payload"""
    if len(data) < 164:  # 64+32+32+32+4 = 164 bytes
        return None
    
    # Unpack signaling payload
    channel_id = data[:64].decode('utf-8').rstrip('\x00')
    exten = data[64:96].decode('utf-8').rstrip('\x00') 
    caller_id = data[96:128].decode('utf-8').rstrip('\x00')
    called_id = data[128:160].decode('utf-8').rstrip('\x00')
    timestamp = struct.unpack('!I', data[160:164])[0]
    
    return {
        'channel_id': channel_id,
        'exten': exten,
        'caller_id': caller_id,
        'called_id': called_id,
        'timestamp': timestamp,
        'timestamp_str': datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S')
    }

def parse_audio_packet(data):
    """Parse audio packet payload"""
    if len(data) < 4:
        return None
    
    # Unpack audio payload header
    sequence = struct.unpack('!I', data[:4])[0]
    audio_data = data[4:]
    
    return {
        'sequence': sequence,
        'audio_length': len(audio_data),
        'audio_data': audio_data
    }

def save_audio_file(stream_id, direction, audio_data, call_info):
    """Save accumulated audio data to a file"""
    # Create recordings directory if it doesn't exist
    recordings_dir = "recordings"
    if not os.path.exists(recordings_dir):
        os.makedirs(recordings_dir)
    
    # Create filename with timestamp and call info
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    direction_str = direction_to_string(direction)
    
    # Clean up call info for filename
    caller_clean = call_info.get('caller_id', 'unknown').replace(' ', '_').replace('<', '').replace('>', '')
    called_clean = call_info.get('called_id', 'unknown').replace(' ', '_').replace('<', '').replace('>', '')
    
    filename = f"{recordings_dir}/stream_{stream_id}_{direction_str}_{timestamp}_{caller_clean}_to_{called_clean}.raw"
    
    try:
        with open(filename, 'wb') as f:
            f.write(audio_data)
        
        # Calculate duration (assuming 8kHz, 16-bit, mono)
        # Each sample is 2 bytes, so duration = bytes / (8000 * 2)
        duration_sec = len(audio_data) / (8000 * 2)
        
        print(f"Saved {direction_str} audio: {filename}")
        print(f"  Size: {len(audio_data):,} bytes")
        print(f"  Duration: {duration_sec:.2f} seconds")
        
        return filename
    except Exception as e:
        print(f"Error saving audio file {filename}: {e}")
        return None

def main():
    # Configuration
    listen_ip = '0.0.0.0'
    listen_port = 4444
    
    print(f"TLV UDP Receiver starting on {listen_ip}:{listen_port}")
    print("Waiting for packets from ForkStream module...")
    print("Audio recordings will be saved to 'recordings/' directory")
    print("-" * 60)
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((listen_ip, listen_port))
    
    # Statistics
    stats = {
        'signaling_rx': 0,
        'signaling_tx': 0,
        'audio_rx': 0,
        'audio_tx': 0,
        'total_audio_bytes': 0,
        'errors': 0
    }
    
    # Audio accumulation storage
    # Structure: {stream_id: {direction: {'audio_data': bytes, 'call_info': dict, 'last_activity': timestamp}}}
    audio_buffers = defaultdict(lambda: {
        DIRECTION_RX: {'audio_data': b'', 'call_info': {}, 'last_activity': None},
        DIRECTION_TX: {'audio_data': b'', 'call_info': {}, 'last_activity': None}
    })
    
    # Track active streams
    active_streams = set()
    
    try:
        while True:
            # Receive packet
            data, addr = sock.recvfrom(65536)
            receive_time = datetime.now().strftime('%H:%M:%S.%f')[:-3]
            
            # Parse header
            header = parse_forkstream_header(data)
            if not header:
                print(f"[{receive_time}] ERROR: Invalid header from {addr}")
                stats['errors'] += 1
                continue
            
            # Validate packet length
            if len(data) != header['packet_length']:
                print(f"[{receive_time}] ERROR: Packet length mismatch. Expected {header['packet_length']}, got {len(data)}")
                stats['errors'] += 1
                continue
            
            stream_id = header['stream_id']
            direction = header['direction']
            direction_str = direction_to_string(direction)
            
            # Process packet based on type
            if header['packet_type'] == PACKET_TYPE_SIGNALING:
                payload = parse_signaling_packet(data[8:])
                if payload:
                    print(f"[{receive_time}] SIGNALING {direction_str} from {addr}")
                    print(f"  Stream ID: {stream_id}")
                    print(f"  Channel: {payload['channel_id']}")
                    print(f"  Extension: {payload['exten']}")
                    print(f"  Caller ID: {payload['caller_id']}")
                    print(f"  Called ID: {payload['called_id']}")
                    print(f"  Timestamp: {payload['timestamp_str']}")
                    
                    # Store call info for this stream
                    audio_buffers[stream_id][direction]['call_info'] = payload
                    audio_buffers[stream_id][direction]['last_activity'] = time.time()
                    active_streams.add(stream_id)
                    
                    if direction_str == "RX":
                        stats['signaling_rx'] += 1
                    else:
                        stats['signaling_tx'] += 1
                else:
                    print(f"[{receive_time}] ERROR: Invalid signaling packet")
                    stats['errors'] += 1
                    
            elif header['packet_type'] == PACKET_TYPE_AUDIO:
                payload = parse_audio_packet(data[8:])
                if payload:
                    print(f"[{receive_time}] AUDIO {direction_str} from {addr} - Stream: {stream_id}, Seq: {payload['sequence']}, Size: {payload['audio_length']} bytes")
                    
                    # Accumulate audio data
                    audio_buffers[stream_id][direction]['audio_data'] += payload['audio_data']
                    audio_buffers[stream_id][direction]['last_activity'] = time.time()
                    active_streams.add(stream_id)
                    
                    stats['total_audio_bytes'] += payload['audio_length']
                    if direction_str == "RX":
                        stats['audio_rx'] += 1
                    else:
                        stats['audio_tx'] += 1
                else:
                    print(f"[{receive_time}] ERROR: Invalid audio packet")
                    stats['errors'] += 1
                    
            else:
                print(f"[{receive_time}] ERROR: Unknown packet type {header['packet_type']}")
                stats['errors'] += 1
            
            # Check for inactive streams (no activity for 30 seconds) and save audio
            current_time = time.time()
            inactive_streams = set()
            
            for stream_id in active_streams:
                stream_inactive = True
                for direction in [DIRECTION_RX, DIRECTION_TX]:
                    buffer = audio_buffers[stream_id][direction]
                    if buffer['last_activity'] and (current_time - buffer['last_activity']) < 30:
                        stream_inactive = False
                        break
                
                if stream_inactive:
                    inactive_streams.add(stream_id)
                    print(f"\n[INFO] Stream {stream_id} inactive for 30 seconds, saving audio files...")
                    
                    # Save audio for both directions
                    for direction in [DIRECTION_RX, DIRECTION_TX]:
                        buffer = audio_buffers[stream_id][direction]
                        if buffer['audio_data']:
                            save_audio_file(stream_id, direction, buffer['audio_data'], buffer['call_info'])
                    
                    # Clear buffers for this stream
                    del audio_buffers[stream_id]
            
            # Remove inactive streams from tracking
            active_streams -= inactive_streams
            
            # Print stats every 100 packets
            total_packets = stats['signaling_rx'] + stats['signaling_tx'] + stats['audio_rx'] + stats['audio_tx']
            if total_packets > 0 and total_packets % 100 == 0:
                print(f"\n--- STATS (Total: {total_packets}) ---")
                print(f"Signaling: RX={stats['signaling_rx']}, TX={stats['signaling_tx']}")
                print(f"Audio: RX={stats['audio_rx']}, TX={stats['audio_tx']}")
                print(f"Total audio data: {stats['total_audio_bytes']:,} bytes")
                print(f"Active streams: {len(active_streams)}")
                print(f"Errors: {stats['errors']}")
                print("-" * 60)
                
    except KeyboardInterrupt:
        print(f"\n\nSaving remaining audio data...")
        
        # Save all remaining audio data
        for stream_id in active_streams:
            print(f"Saving audio for stream {stream_id}...")
            for direction in [DIRECTION_RX, DIRECTION_TX]:
                buffer = audio_buffers[stream_id][direction]
                if buffer['audio_data']:
                    save_audio_file(stream_id, direction, buffer['audio_data'], buffer['call_info'])
        
        print(f"\nFinal Statistics:")
        print(f"Signaling packets: RX={stats['signaling_rx']}, TX={stats['signaling_tx']}")
        print(f"Audio packets: RX={stats['audio_rx']}, TX={stats['audio_tx']}")
        print(f"Total audio data: {stats['total_audio_bytes']:,} bytes")
        print(f"Errors: {stats['errors']}")
        print("Receiver stopped.")
    finally:
        sock.close()

if __name__ == "__main__":
    main() 