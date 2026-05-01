#!/usr/bin/env python3

import argparse
import audioop
import os
import struct
import sys
import wave


def read_wav_pcm16_mono_8k(wav_path: str) -> bytes:
    with wave.open(wav_path, "rb") as wf:
        channels = wf.getnchannels()
        sample_width = wf.getsampwidth()
        sample_rate = wf.getframerate()
        raw = wf.readframes(wf.getnframes())

    if sample_width != 2:
        raise ValueError(f"expected 16-bit PCM WAV, got sample width {sample_width}")

    if channels == 2:
        raw = audioop.tomono(raw, sample_width, 0.5, 0.5)
    elif channels != 1:
        raise ValueError(f"expected mono/stereo WAV, got {channels} channels")

    if sample_rate != 8000:
        raw, _ = audioop.ratecv(raw, sample_width, 1, sample_rate, 8000, None)

    return raw


def load_wav_as_ulaw_frames(wav_path: str) -> list[bytes]:
    raw = read_wav_pcm16_mono_8k(wav_path)
    ulaw = audioop.lin2ulaw(raw, 2)

    frame_size = 160  # 20 ms @ 8 kHz, 8-bit PCMU
    frames: list[bytes] = []
    for offset in range(0, len(ulaw), frame_size):
        chunk = ulaw[offset : offset + frame_size]
        if len(chunk) < frame_size:
            chunk += bytes([0xFF]) * (frame_size - len(chunk))
        frames.append(chunk)

    if not frames:
        frames.append(bytes([0xFF]) * frame_size)

    return frames


def load_wav_as_energy_mapped_frames(wav_path: str) -> list[bytes]:
    with wave.open(wav_path, "rb") as wf:
        channels = wf.getnchannels()
        sample_width = wf.getsampwidth()
        sample_rate = wf.getframerate()
        raw = wf.readframes(wf.getnframes())

    if sample_width != 2:
        raise ValueError(f"expected 16-bit PCM WAV, got sample width {sample_width}")

    if channels == 2:
        raw = audioop.tomono(raw, sample_width, 0.5, 0.5)
    elif channels != 1:
        raise ValueError(f"expected mono/stereo WAV, got {channels} channels")

    if sample_rate != 16000:
        raw, _ = audioop.ratecv(raw, sample_width, 1, sample_rate, 16000, None)

    samples = struct.unpack(f"<{len(raw) // 2}h", raw)
    frames: list[bytes] = []
    source_frame = 320  # 20 ms @ 16 kHz

    for offset in range(0, len(samples), source_frame):
        block = samples[offset : offset + source_frame]
        if len(block) < source_frame:
            block = block + (0,) * (source_frame - len(block))

        payload = bytearray()
        for idx in range(0, len(block), 2):
            payload.append(0x02 if abs(block[idx]) > 1000 else 0xFF)
        frames.append(bytes(payload))

    if not frames:
        frames.append(bytes([0xFF]) * 160)

    return frames


def write_rtp_pcap(frames: list[bytes], output_path: str, src_port: int, dst_port: int) -> None:
    pcap_header = struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1)
    eth_header = (
        b"\x00\x00\x00\x00\x00\x00"
        + b"\x00\x00\x00\x00\x00\x00"
        + b"\x08\x00"
    )

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)

    with open(output_path, "wb") as f:
        f.write(pcap_header)

        for index, payload in enumerate(frames):
            total_len = 20 + 8 + 12 + len(payload)
            ip_header = struct.pack(
                ">BBHHHBBHII",
                0x45,
                0,
                total_len,
                index & 0xFFFF,
                0,
                64,
                17,
                0,
                0x7F000001,
                0x7F000001,
            )
            udp_header = struct.pack(">HHHH", src_port, dst_port, 8 + 12 + len(payload), 0)
            rtp_header = struct.pack(">BBHII", 0x80, 0, index & 0xFFFF, index * 160, 0x12345678)
            packet = eth_header + ip_header + udp_header + rtp_header + payload

            ts_sec = index // 50
            ts_usec = (index % 50) * 20000
            pkt_header = struct.pack("<IIII", ts_sec, ts_usec, len(packet), len(packet))
            f.write(pkt_header)
            f.write(packet)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a PCMU RTP PCAP from a WAV file.")
    parser.add_argument("--wav", required=True, help="Input WAV file (16-bit mono/stereo PCM).")
    parser.add_argument("--out", required=True, help="Output PCAP path.")
    parser.add_argument("--src-port", type=int, default=40000)
    parser.add_argument("--dst-port", type=int, default=16000)
    parser.add_argument(
        "--mode",
        choices=("ulaw", "energy-map"),
        default="ulaw",
        help="PCMU conversion mode. energy-map matches the legacy test sender envelope.",
    )
    args = parser.parse_args()

    if args.mode == "energy-map":
        frames = load_wav_as_energy_mapped_frames(args.wav)
    else:
        frames = load_wav_as_ulaw_frames(args.wav)
    write_rtp_pcap(frames, args.out, args.src_port, args.dst_port)
    print(f"Generated {args.out} from {args.wav} ({len(frames)} RTP frames, mode={args.mode})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
