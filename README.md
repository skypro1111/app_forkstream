# ForkStream - Advanced Asterisk Audio Stream Forking Module

[![License](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](https://www.gnu.org/licenses/gpl-2.0.html)
[![Asterisk](https://img.shields.io/badge/asterisk-16%2B-orange.svg)](https://www.asterisk.org/)
[![Language](https://img.shields.io/badge/language-C99-green.svg)](https://en.wikipedia.org/wiki/C99)
[![Production Ready](https://img.shields.io/badge/status-production%20ready-brightgreen.svg)](CHANGELOG.md)
[![Protocol](https://img.shields.io/badge/protocol-TLV-blue.svg)](https://en.wikipedia.org/wiki/Type-length-value)

## 🎯 Overview

The `app_forkstream` module provides a sophisticated dialplan application that **forks bidirectional audio streams** from Asterisk channels to UDP destinations using an industry-standard TLV (Type-Length-Value) protocol. Perfect for advanced call monitoring, speech analytics, compliance recording, and voice biometrics integration with full metadata support.

### ✨ Key Features

- 🔄 **Bidirectional Audio Capture** - Separate RX/TX streams with direction identification
- 🚀 **Real-time Processing** - Sub-millisecond latency audio forwarding
- 📡 **TLV Protocol** - Industry-standard Type-Length-Value packet format
- 📊 **Rich Metadata** - Channel info, caller/called IDs, extensions, timestamps
- 🔢 **Sequence Tracking** - Independent sequence numbers for RX/TX streams
- 🆔 **Unique Stream IDs** - Correlation across multiple channels and sessions
- 🔧 **Flexible Parameters** - Extended syntax with optional metadata fields
- 📞 **Call Signaling** - Initial signaling packets with complete call context
- 🛡️ **Memory Safe** - Automatic resource cleanup, zero memory leaks
- ⚡ **High Performance** - 1000+ frames/sec, optimized scatter-gather I/O
- 🎨 **Codec Agnostic** - Works with all Asterisk audio codecs
- 📊 **Production Ready** - Comprehensive testing and monitoring tools
- 🔧 **CLI Control** - Dynamic logging control via CLI commands

### 🏢 Use Cases

- **Call Center Monitoring** - Real-time quality assessment
- **Speech Analytics** - Integration with STT and AI services  
- **Compliance Recording** - Legal and regulatory requirements
- **Voice Biometrics** - Speaker identification and verification
- **Real-time Analytics** - Sentiment analysis and coaching
- **Security Applications** - Fraud detection and monitoring

## 🚀 Quick Start

### Basic Usage

```bash
# Simple audio forking (basic TLV packets)
exten => 1000,1,Answer()
exten => 1000,2,ForkStream(192.168.1.100:8080)
exten => 1000,3,Dial(SIP/agent)
exten => 1000,4,Hangup()
```

### Advanced Usage with Metadata

```bash
# Full metadata support
exten => 2000,1,Answer()
exten => 2000,2,ForkStream(analytics.company.com:9000,SIP/support-queue,2000,+1234567890,+0987654321)
exten => 2000,3,Queue(support)
exten => 2000,4,Hangup()
```

This creates TLV packets with:
- **Signaling packets** containing call metadata (channel, extension, caller/called IDs)
- **Audio packets** with sequence numbers and direction flags (RX/TX)
- **Unique stream ID** for correlating all packets from this call

## Prerequisites

### System Requirements

- **Asterisk Version**: 16.x, 18.x, 20.x LTS or newer
- **Operating System**: Linux (tested on Ubuntu, CentOS, Debian)
- **Architecture**: x86_64, ARM64
- **Memory**: Minimum 512MB RAM (1GB+ recommended for production)
- **Network**: UDP connectivity to destination endpoints

### Dependencies

- GCC compiler with C99 support
- Asterisk development headers
- Standard C library with socket support

## Installation Methods

### Method 1: Integration with Asterisk Build System (Recommended)

1. **Copy the module source to Asterisk apps directory:**
   ```bash
   cp app_forkstream.c /usr/src/asterisk/apps/
   ```

2. **Run menuselect to enable the module:**
   ```bash
   cd /usr/src/asterisk
   make menuselect
   # Navigate to: Applications -> app_forkstream
   # Press 'x' to enable, then 'q' to quit and save
   ```

3. **Compile and install:**
   ```bash
   make apps
   make install
   ```

### Method 2: Standalone Compilation

1. **Install Asterisk development headers:**
   ```bash
   # Ubuntu/Debian
   sudo apt-get install asterisk-dev
   
   # CentOS/RHEL
   sudo yum install asterisk-devel
   ```

2. **Compile the module:**
   ```bash
   gcc -fPIC -shared -std=c99 \
       -I/usr/include/asterisk \
       -o app_forkstream.so \
       app_forkstream.c
   ```

3. **Install manually:**
   ```bash
   sudo cp app_forkstream.so /usr/lib/asterisk/modules/
   sudo chown asterisk:asterisk /usr/lib/asterisk/modules/app_forkstream.so
   sudo chmod 755 /usr/lib/asterisk/modules/app_forkstream.so
   ```

## Configuration

### Loading the Module

1. **Load the module in Asterisk CLI:**
   ```
   asterisk -r
   CLI> module load app_forkstream
   ```

2. **Verify the module is loaded:**
   ```
   CLI> module show app_forkstream
   CLI> core show applications | grep Fork
   ```

3. **Auto-load on startup (optional):**
   Edit `/etc/asterisk/modules.conf`:
   ```ini
   [modules]
   load => app_forkstream.so
   ```

### Dialplan Configuration

#### Syntax

```
ForkStream(ip:port[,channel_id][,exten][,caller_id][,called_id])
```

**Parameters:**
- `ip:port` - **Required** - Destination IP address and UDP port
- `channel_id` - **Optional** - Custom channel identifier (defaults to actual channel name)
- `exten` - **Optional** - Extension number for call context
- `caller_id` - **Optional** - Caller phone number 
- `called_id` - **Optional** - Called/destination phone number

#### Basic Usage

```ini
; extensions.conf - Simple forking
[default]
exten => 1000,1,Answer()
exten => 1000,2,ForkStream(192.168.1.100:8080)
exten => 1000,3,Playback(demo-congrats)
exten => 1000,4,Hangup()
```

#### Advanced Examples

```ini
; Complete metadata for analytics
exten => 2000,1,Answer()
exten => 2000,2,Set(ANALYTICS_SERVER=analytics.company.com:9000)
exten => 2000,3,ForkStream(${ANALYTICS_SERVER},Support-Line,2000,${CALLERID(num)},${EXTEN})
exten => 2000,4,Queue(support)
exten => 2000,5,Hangup()

; Conference with session tracking
exten => 3000,1,Answer()
exten => 3000,2,Set(CONF_ID=conf-${UNIQUEID})
exten => 3000,3,ForkStream(conference-analytics:8080,${CONF_ID},3000)
exten => 3000,4,ConfBridge(sales_meeting)
exten => 3000,5,Hangup()

; Dynamic routing with metadata
exten => _9XXX,1,Answer()
exten => _9XXX,2,Set(DEST_NUM=${EXTEN:1})
exten => _9XXX,3,ForkStream(compliance-server:7070,Outbound-${CHANNEL},${EXTEN},${CALLERID(num)},${DEST_NUM})
exten => _9XXX,4,Dial(SIP/provider/${DEST_NUM})
exten => _9XXX,5,Hangup()

; Conditional forking with full context
exten => 4000,1,Answer()
exten => 4000,2,GotoIf($["${CALLERID(num)}" = "VIP"]?vip:normal)
exten => 4000,3(vip),ForkStream(vip-analytics:8080,VIP-Support,4000,${CALLERID(num)},VIP-Line)
exten => 4000,4(vip),Goto(handle)
exten => 4000,5(normal),ForkStream(standard-monitor:8080,Standard-Support,4000,${CALLERID(num)})
exten => 4000,6(handle),Queue(premium_support)
exten => 4000,7,Hangup()
```

## 📡 TLV Protocol Format

The module uses a Type-Length-Value (TLV) protocol for structured data transmission, enabling rich metadata and easy parsing on the receiver side.

### Packet Structure

#### Header (8 bytes - common to all packets)

```c
struct forkstream_header {
    uint8_t  packet_type;    // 0x01=Signaling, 0x02=Audio
    uint16_t packet_length;  // Total packet length (network byte order)
    uint32_t stream_id;      // Unique stream identifier (network byte order)
    uint8_t  direction;      // 0x01=RX (incoming), 0x02=TX (outgoing)
} __attribute__((packed));
```

#### Signaling Packet (172 bytes total)

Sent once per direction (RX/TX) when ForkStream initializes:

```c
struct signaling_payload {
    char channel_id[64];     // Channel identifier (null-terminated)
    char exten[32];          // Extension number (null-terminated)
    char caller_id[32];      // Caller phone number (null-terminated)
    char called_id[32];      // Called phone number (null-terminated)
    uint32_t timestamp;      // Unix timestamp (network byte order)
} __attribute__((packed));
```

#### Audio Packet (variable length)

Sent for each audio frame:

```c
struct audio_payload {
    uint32_t sequence;       // Sequence number (network byte order)
    // Raw audio data follows immediately
} __attribute__((packed));
```

### Packet Flow Example

```
Call Start:
1. SIGNALING RX packet → Channel metadata for incoming audio
2. SIGNALING TX packet → Channel metadata for outgoing audio

During Call:
3. AUDIO RX packets → seq=1,2,3... (incoming audio frames)
4. AUDIO TX packets → seq=1,2,3... (outgoing audio frames)

Call End:
5. Automatic cleanup when channel terminates
```

### Receiver Implementation

See `tests/tlv_receiver.py` for a complete Python example:

```python
# Parse header
packet_type, packet_length, stream_id, direction = struct.unpack('!BHI B', data[:8])

if packet_type == 0x01:  # Signaling
    # Parse metadata
elif packet_type == 0x02:  # Audio
    sequence = struct.unpack('!I', data[8:12])[0]
    audio_data = data[12:]
```

### Benefits of TLV Protocol

- **Extensible**: Easy to add new packet types and fields
- **Self-describing**: Each packet contains its length and type
- **Network-efficient**: Minimal overhead, binary format
- **Industry standard**: Well-known pattern used in many protocols
- **Parser-friendly**: Simple state machine for receivers

### Security Considerations

#### Network Security

1. **Firewall Configuration:**
   ```bash
   # Allow UDP traffic to destination
   sudo iptables -A OUTPUT -p udp --dport 8080 -j ACCEPT
   sudo iptables -A INPUT -p udp --sport 8080 -j ACCEPT
   ```

2. **Private Networks:**
   - Use private IP ranges (10.x.x.x, 172.16.x.x, 192.168.x.x)
   - Implement VPN for remote destinations
   - Consider IPSec for encrypted transport

#### Access Control

1. **Dialplan Restrictions:**
   ```ini
   ; Restrict ForkStream to specific contexts
   [internal-only]
   exten => _X.,1,ForkStream(internal-server:8080)
   
   [public]
   ; No ForkStream access for external calls
   exten => _X.,1,Dial(SIP/${EXTEN})
   ```

2. **Source IP Validation:**
   Configure receiving applications to accept UDP only from Asterisk servers.

## Verification and Testing

### Module Verification

1. **Check module status:**
   ```
   CLI> module show app_forkstream
   Module                         Description                              Use Count  Status      Support Level
   app_forkstream.so              Fork Stream Application                  0          Running              core
   ```

2. **Test application availability:**
   ```
   CLI> core show application ForkStream
   ```

### CLI Commands for Logging Control

The module provides CLI commands to control detailed logging without requiring module reload:

#### Enable Detailed Logging
```
CLI> forkstream set logger on
ForkStream logging enabled
```

#### Disable Detailed Logging
```
CLI> forkstream set logger off
ForkStream logging disabled
```

#### Check Logging Status
```
CLI> forkstream show logger
ForkStream logging is disabled
```

#### Usage Examples
```bash
# Enable logging for debugging
lv-ast-arce*CLI> forkstream set logger on
ForkStream logging enabled

# Make test call to see detailed output
lv-ast-arce*CLI> originate Local/1000@default extension s@test

# Disable logging for production
lv-ast-arce*CLI> forkstream set logger off
ForkStream logging disabled
```

**Benefits:**
- ✅ **No module reload required** - Commands work instantly
- ✅ **Performance control** - Disable logging in production
- ✅ **Debug flexibility** - Enable only when needed
- ✅ **Active calls unaffected** - Logging changes don't impact ongoing calls

### Network Testing

#### TLV Protocol Testing

1. **Start the TLV receiver:**
   ```bash
   cd tests/
   python3 tlv_receiver.py
   ```

2. **Configure test dialplan:**
   ```ini
   ; Test extension with full metadata
   exten => 8888,1,Answer()
   exten => 8888,2,ForkStream(127.0.0.1:5060,TEST-CHANNEL,8888,+1234567890,+0987654321)
   exten => 8888,3,Playback(demo-congrats)
   exten => 8888,4,Wait(5)
   exten => 8888,5,Hangup()
   ```

3. **Make test call:**
   ```
   CLI> originate Local/8888@default extension s@test
   ```

4. **Verify packet reception:**
   The TLV receiver will show:
   ```
   [12:34:56.789] SIGNALING RX from ('127.0.0.1', 54321)
     Stream ID: 1234567890
     Channel: TEST-CHANNEL
     Extension: 8888
     Caller ID: +1234567890
     Called ID: +0987654321
     Timestamp: 2024-01-15 12:34:56
   
   [12:34:56.790] SIGNALING TX from ('127.0.0.1', 54321)
     Stream ID: 1234567890
     ...
   
   [12:34:56.810] AUDIO RX from ('127.0.0.1', 54321) - Stream: 1234567890, Seq: 1, Size: 160 bytes
   [12:34:56.830] AUDIO RX from ('127.0.0.1', 54321) - Stream: 1234567890, Seq: 2, Size: 160 bytes
   ```

#### Legacy UDP Testing

1. **Start simple UDP receiver:**
   ```bash
   python3 tests/udp_receiver.py 127.0.0.1 8080
   ```

2. **Use basic syntax:**
   ```ini
   exten => 7777,1,ForkStream(127.0.0.1:8080)
   ```

### Performance Testing

1. **Run stress test:**
   ```bash
   ./stress_test 127.0.0.1 8080 10
   ```

2. **Monitor system resources:**
   ```bash
   top -p $(pgrep asterisk)
   iostat 1
   netstat -su | grep -i udp
   ```

## Troubleshooting

### Common Issues

#### Module Load Failures

**Symptoms:** Module fails to load with "module not found" error.

**Solutions:**
- Verify file permissions: `ls -la /usr/lib/asterisk/modules/app_forkstream.so`
- Check dependencies: `ldd /usr/lib/asterisk/modules/app_forkstream.so`
- Review Asterisk logs: `grep forkstream /var/log/asterisk/messages`

#### Network Connectivity Issues

**Symptoms:** No UDP packets received at destination.

**Diagnosis:**
```bash
# Check socket creation
sudo netstat -anp | grep asterisk | grep udp

# Test basic UDP connectivity
echo "test" | nc -u target-ip target-port

# Monitor network traffic
sudo tcpdump -i any udp port 8080
```

**Solutions:**
- Verify firewall rules
- Check routing tables
- Validate IP:port format in dialplan

#### High CPU Usage

**Symptoms:** Asterisk process consuming excessive CPU.

**Investigation:**
```bash
# Profile Asterisk process
perf top -p $(pgrep asterisk)

# Monitor call volume
asterisk -r -x "core show channels"
```

**Mitigation:**
- Reduce logging verbosity
- Limit concurrent ForkStream instances
- Optimize destination server performance

### Log Analysis

#### Enable Debug Logging

```ini
; logger.conf
[logfiles]
debug => debug,verbose,notice,warning,error
messages => notice,warning,error
```

#### Control ForkStream Logging

For detailed ForkStream frame-by-frame logging:

```bash
# Enable detailed logging
CLI> forkstream set logger on

# Disable detailed logging (recommended for production)
CLI> forkstream set logger off

# Check current status
CLI> forkstream show logger
```

#### Key Log Messages

```
[DEBUG] ForkStream: Parsed arguments - IP: 192.168.1.100, Port: 8080, Channel: SIP/alice, Exten: 1000, Caller: +1234567890, Called: +0987654321
[VERBOSE] ForkStream: Generated unique stream ID: 3847562910
[VERBOSE] ForkStream: Sent signaling packet (stream_id: 3847562910, direction: RX)
[VERBOSE] ForkStream: Sent signaling packet (stream_id: 3847562910, direction: TX)
[VERBOSE] ForkStream: Attached framehook (ID: 123) to channel SIP/alice-00000001
[VERBOSE] ForkStream: Sent RX frame on channel SIP/alice-00000001: 160 bytes (seq: 1)
[VERBOSE] ForkStream: Sent TX frame on channel SIP/alice-00000001: 160 bytes (seq: 1)
[WARNING] ForkStream: Failed to send signaling packet (direction: RX): Network unreachable
[WARNING] ForkStream: Failed to send audio packet (direction: TX, seq: 25): Connection refused
[VERBOSE] ForkStream: Cleaning up resources for framehook ID 123
```

## Performance Tuning

### System Optimization

1. **Network Buffer Tuning:**
   ```bash
   # Increase UDP buffer sizes
   echo 'net.core.rmem_max = 16777216' >> /etc/sysctl.conf
   echo 'net.core.wmem_max = 16777216' >> /etc/sysctl.conf
   sysctl -p
   ```

2. **Logging Optimization:**
   ```bash
   # Disable detailed logging in production for better performance
   CLI> forkstream set logger off
   
   # Enable only when debugging is needed
   CLI> forkstream set logger on
   ```

2. **Asterisk Configuration:**
   ```ini
   ; asterisk.conf
   [options]
   maxload = 4.0
   highpriority = yes
   
   ; rtp.conf
   [general]
   rtpstart = 10000
   rtpend = 20000
   ```

### Monitoring

1. **System Metrics:**
   - CPU usage per channel
   - Memory consumption
   - Network throughput
   - UDP packet loss

2. **Application Metrics:**
   - Active ForkStream instances
   - Frame processing rate
   - Error rates

## Production Deployment

### Deployment Checklist

- [ ] Module compiled for target Asterisk version
- [ ] Network connectivity tested
- [ ] Security policies implemented
- [ ] Monitoring systems configured
- [ ] Backup procedures established
- [ ] Documentation provided to operations team

### Maintenance

1. **Regular Tasks:**
   - Monitor log files for errors
   - Verify destination server availability
   - Check system resource usage
   - Review security logs

2. **Updates:**
   - Test module updates in staging environment
   - Plan maintenance windows for production updates
   - Maintain configuration backups

## Support

### Documentation
- See `README.md` for basic usage
- Review source code comments for technical details
- Check `examples/` directory for configuration samples

### Community
- Report issues on project repository
- Join Asterisk community forums
- Contribute improvements via pull requests

### Commercial Support
Contact your Asterisk support provider for enterprise-level assistance.

## 🔄 Migration Guide

### From Previous Versions

If you're upgrading from a pre-TLV version:

#### What Changed
- **Protocol**: Now uses TLV format instead of raw audio
- **Syntax**: Extended to support optional metadata parameters
- **Packets**: Signaling packets added for call context
- **Sequence**: Audio packets now include sequence numbers

#### Migration Steps

1. **Update receiving applications** to handle TLV format
2. **Test with new `tlv_receiver.py`** utility
3. **Gradually update dialplan** to use extended syntax
4. **Monitor logs** for any compatibility issues

#### Backward Compatibility

The module is **not backward compatible** with raw UDP receivers. You must:

- Update all receiving applications to parse TLV format
- Use the provided `tlv_receiver.py` as a reference implementation
- Consider running both old and new versions during transition

#### Quick TLV Receiver Template

```python
import socket, struct

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', 5060))

while True:
    data, addr = sock.recvfrom(65536)
    # Parse 8-byte header
    packet_type, length, stream_id, direction = struct.unpack('!BHI B', data[:8])
    
    if packet_type == 0x01:  # Signaling
        print(f"Call setup: Stream {stream_id}")
        # Parse metadata from data[8:]
    elif packet_type == 0x02:  # Audio
        sequence = struct.unpack('!I', data[8:12])[0]
        audio = data[12:]
        print(f"Audio: Stream {stream_id}, Seq {sequence}, {len(audio)} bytes")
```

## 📋 Project Structure

```
app_forkstream/
├── app_forkstream.c          # Main module source with TLV protocol
├── README.md                 # This documentation
├── USER_GUIDE.md             # Detailed user guide
├── CHANGELOG.md              # Development history
├── examples.conf             # Dialplan examples
├── Makefile                  # Build system
└── tests/
    ├── tlv_receiver.py       # TLV protocol testing utility
    ├── udp_receiver.py       # Legacy UDP testing utility
    ├── test_args.c           # Argument parsing tests
    ├── test_framehook.c      # Audio processing tests
    ├── test_hangup.c         # Resource management tests
    └── stress_test.c         # Performance tests
```

## 🤝 Contributing

We welcome contributions! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes following Asterisk coding standards
4. Add tests for new functionality
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

## 📄 License

This project is licensed under the GPL-2.0 License. See the [GNU GPL v2.0](https://www.gnu.org/licenses/gpl-2.0.html) for details.

## 🎖️ Acknowledgments

- Asterisk development team for the excellent framehook API
- Community contributors and testers
- Everyone who provided feedback during development

## 📊 Project Stats

![GitHub repo size](https://img.shields.io/github/repo-size/username/app_forkstream)
![GitHub last commit](https://img.shields.io/github/last-commit/username/app_forkstream)
![GitHub issues](https://img.shields.io/github/issues/username/app_forkstream)
![GitHub stars](https://img.shields.io/github/stars/username/app_forkstream)

---

**Made with ❤️ for the Asterisk community**