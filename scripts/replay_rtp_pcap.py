#!/usr/bin/env python3

import argparse
import socket
import struct
import sys
import time


PCAP_GLOBAL_HEADER_LEN = 24
PCAP_PACKET_HEADER_LEN = 16
ETHERNET_HEADER_LEN = 14
IPV4_MIN_HEADER_LEN = 20
UDP_HEADER_LEN = 8


def extract_udp_payloads(pcap_path: str) -> list[tuple[float, bytes]]:
    packets: list[tuple[float, bytes]] = []

    with open(pcap_path, "rb") as f:
        header = f.read(PCAP_GLOBAL_HEADER_LEN)
        if len(header) != PCAP_GLOBAL_HEADER_LEN:
            raise ValueError("pcap file too short")

        while True:
            pkt_hdr = f.read(PCAP_PACKET_HEADER_LEN)
            if not pkt_hdr:
                break
            if len(pkt_hdr) != PCAP_PACKET_HEADER_LEN:
                raise ValueError("truncated pcap packet header")

            ts_sec, ts_usec, incl_len, _orig_len = struct.unpack("<IIII", pkt_hdr)
            packet = f.read(incl_len)
            if len(packet) != incl_len:
                raise ValueError("truncated pcap packet body")

            if len(packet) < ETHERNET_HEADER_LEN + IPV4_MIN_HEADER_LEN + UDP_HEADER_LEN:
                continue

            ether_type = struct.unpack(">H", packet[12:14])[0]
            if ether_type != 0x0800:
                continue

            ip_start = ETHERNET_HEADER_LEN
            version_ihl = packet[ip_start]
            version = version_ihl >> 4
            ihl = (version_ihl & 0x0F) * 4
            if version != 4 or ihl < IPV4_MIN_HEADER_LEN:
                continue

            proto = packet[ip_start + 9]
            if proto != 17:
                continue

            udp_start = ip_start + ihl
            if len(packet) < udp_start + UDP_HEADER_LEN:
                continue

            udp_len = struct.unpack(">H", packet[udp_start + 4 : udp_start + 6])[0]
            payload_start = udp_start + UDP_HEADER_LEN
            payload_end = payload_start + max(0, udp_len - UDP_HEADER_LEN)
            payload = packet[payload_start:payload_end]
            if payload:
                packets.append((ts_sec + (ts_usec / 1_000_000.0), payload))

    return packets


def replay(packets: list[tuple[float, bytes]], target_ip: str, target_port: int, startup_delay: float) -> None:
    if startup_delay > 0:
        time.sleep(startup_delay)

    if not packets:
        raise ValueError("no UDP/RTP payloads found in pcap")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    base_ts = packets[0][0]
    start = time.time()

    for ts, payload in packets:
        target_elapsed = ts - base_ts
        actual_elapsed = time.time() - start
        sleep_for = target_elapsed - actual_elapsed
        if sleep_for > 0:
            time.sleep(sleep_for)
        sock.sendto(payload, (target_ip, target_port))


def main() -> int:
    parser = argparse.ArgumentParser(description="Replay RTP packets from a PCAP over UDP.")
    parser.add_argument("--pcap", required=True)
    parser.add_argument("--target-ip", default="127.0.0.1")
    parser.add_argument("--target-port", type=int, required=True)
    parser.add_argument("--startup-delay", type=float, default=2.0)
    args = parser.parse_args()

    packets = extract_udp_payloads(args.pcap)
    replay(packets, args.target_ip, args.target_port, args.startup_delay)
    print(
        f"Replayed {len(packets)} RTP packets from {args.pcap} "
        f"to {args.target_ip}:{args.target_port}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
