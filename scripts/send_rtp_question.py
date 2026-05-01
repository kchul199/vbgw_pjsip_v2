import socket
import time
import struct
import wave

def pcm16_to_ulaw(pcm_sample):
    # Extremely simplified linear to u-law
    # Just for testing energy detection
    if pcm_sample < 0:
        return 0x7F # Simple mapping
    else:
        return 0x00

def send_rtp_from_wav(target_ip, target_port, wav_file):
    print(f"📡 Sending RTP from {wav_file} to {target_ip}:{target_port}...")
    
    try:
        wf = wave.open(wav_file, 'rb')
        if wf.getnchannels() != 1 or wf.getsampwidth() != 2 or wf.getframerate() != 16000:
            print("⚠️ WAV must be 16kHz, 16-bit, Mono. Attempting simple read.")
    except Exception as e:
        print(f"❌ Failed to open WAV: {e}")
        return

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ssrc = 0x12345678
    seq = 0
    ts = 0
    
    start_time = time.time()
    
    # Read 320 samples (20ms at 16kHz) and downsample to 160 samples (20ms at 8kHz)
    while True:
        data = wf.readframes(320)
        if not data:
            break
            
        # Downsample 16kHz -> 8kHz (take every other sample)
        # And convert to 8-bit PCMU (simplified)
        samples = struct.unpack(f"<{len(data)//2}h", data)
        payload = bytearray()
        for i in range(0, len(samples), 2):
            # Take every second sample to downsample
            s = samples[i]
            # Simple "is it loud?" mapping to PCMU
            # 0xFF is silence, 0x00 is max positive
            if abs(s) > 1000:
                payload.append(0x02) # Loud-ish
            else:
                payload.append(0xFF) # Silence
        
        if len(payload) < 160:
            payload.extend([0xFF] * (160 - len(payload)))
            
        header = struct.pack(">BBHII", 0x80, 0, seq, ts, ssrc)
        sock.sendto(header + bytes(payload), (target_ip, target_port))
        
        seq = (seq + 1) & 0xFFFF
        ts += 160 # 8kHz TS increment
        
        # Pacing
        elapsed = time.time() - start_time
        target_elapsed = (seq) * 0.02
        if target_elapsed > elapsed:
            time.sleep(target_elapsed - elapsed)

    print("✅ RTP transmission from WAV finished.")

if __name__ == "__main__":
    time.sleep(2.0)
    send_rtp_from_wav("127.0.0.1", 16000, "src/emulator/sample_tts.wav")
