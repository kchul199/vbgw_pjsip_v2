import struct
import time

def create_pcap(filename, duration_sec=2.0):
    # PCAP Global Header
    # magic_number (4), version_major (2), version_minor (2), thiszone (4), sigfigs (4), snaplen (4), network (4)
    pcap_header = struct.pack("<IHHIIII", 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1) # 1 = Ethernet

    # Ethernet Header (14 bytes)
    eth_header = b"\x00\x00\x00\x00\x00\x00" + b"\x00\x00\x00\x00\x00\x00" + b"\x08\x00"
    
    # IP Header (20 bytes)
    # version/IHL, TOS, Total Len, ID, Flags/Offset, TTL, Proto, Checksum, Src, Dst
    ip_header_template = struct.pack(">BBHHHBBHII", 0x45, 0, 0, 0, 0, 64, 17, 0, 0x7f000001, 0x7f000001)

    # UDP Header (8 bytes)
    # Src Port, Dst Port, Len, Checksum
    udp_header_template = struct.pack(">HHHH", 12345, 5060, 0, 0)

    # RTP Header (12 bytes)
    # V/P/X/CC, PT, Seq, TS, SSRC
    rtp_header_template = struct.pack(">BBHII", 0x80, 0, 0, 0, 0x12345678) # PT 0 = PCMU

    with open(filename, "wb") as f:
        f.write(pcap_header)
        
        start_time = time.time()
        for i in range(int(duration_sec * 50)): # 20ms chunks
            payload = b"\xff" * 160 # 160 bytes of silence (PCMU)
            
            # Update Headers
            total_len = 20 + 8 + 12 + len(payload)
            ip_header = struct.pack(">BBHHHBBHII", 0x45, 0, total_len, i, 0, 64, 17, 0, 0x7f000001, 0x7f000001)
            udp_header = struct.pack(">HHHH", 12345, 16000, 8 + 12 + len(payload), 0)
            rtp_header = struct.pack(">BBHII", 0x80, 0, i, i * 160, 0x12345678)
            
            packet_data = eth_header + ip_header + udp_header + rtp_header + payload
            
            # PCAP Packet Header
            # ts_sec, ts_usec, incl_len, orig_len
            ts_sec = int(start_time + i * 0.02)
            ts_usec = int(((start_time + i * 0.02) % 1) * 1000000)
            pkt_header = struct.pack("<IIII", ts_sec, ts_usec, len(packet_data), len(packet_data))
            
            f.write(pkt_header)
            f.write(packet_data)

if __name__ == "__main__":
    create_pcap("scripts/question.pcap")
    print("Generated scripts/question.pcap")
