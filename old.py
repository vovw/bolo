import pyaudio
import wave
import numpy as np
from datetime import datetime
from scipy.io.wavfile import write
import tempfile
import os
from lightning_whisper_mlx import LightningWhisperMLX

# Constants
SAMPLE_RATE = 16000
CHUNK_SIZE = 1600
SAMPLE_WIDTH = 2
VAD_THRESHOLD = 1e-5
FREQ_THRESHOLD = 100.0
STEP_MS = 0  # 0 to enable VAD
LENGTH_MS = 4000
VAD_WINDOW_SIZE_MS = 1000

def vad_simple(pcmf32, sample_rate, window_size_ms, threshold, freq_threshold, output_probs=False):
    """Simple voice activity detection algorithm."""
    window_size = int(sample_rate * window_size_ms / 1000)
    sample_count = len(pcmf32)

    if sample_count < window_size:
        # print("Error: sample_count < window_size", sample_count, window_size)
        return False

    pcmf32_mono = pcmf32
    if len(pcmf32_mono.shape) == 2:
        pcmf32_mono = np.mean(pcmf32_mono, axis=1)

    energy = np.mean(pcmf32_mono ** 2)
    energy_threshold = threshold ** 2

    if energy < energy_threshold:
        # print("Energy below threshold", energy, energy_threshold)
        return False

    fft = np.fft.rfft(pcmf32_mono)
    freq = np.fft.rfftfreq(sample_count, d=1.0/sample_rate)

    fft_energy = np.abs(fft) ** 2
    cutoff_idx = np.where(freq >= freq_threshold)[0][0]
    fft_low_freq_energy = np.sum(fft_energy[:cutoff_idx])
    fft_total_energy = np.sum(fft_energy)

    low_freq_ratio = fft_low_freq_energy / fft_total_energy

    if output_probs:
        return low_freq_ratio
    # print("Low freq ratio", low_freq_ratio)
    return low_freq_ratio > 0.1

def main():
    # Initialize PyAudio
    audio = pyaudio.PyAudio()
    stream = audio.open(format=pyaudio.paInt16,
                        channels=1,
                        rate=SAMPLE_RATE,
                        input=True,
                        frames_per_buffer=CHUNK_SIZE)

    # Initialize Whisper model
    whisper = LightningWhisperMLX(model="large-v3", batch_size=12, quant=None)

    # Initialize variables
    is_running = True
    n_iter = 0
    pcmf32_vad = np.array([], dtype=np.float32)

    print("[Start speaking]")

    # Main loop
    while is_running:
        # Read audio data from the stream
        data = stream.read(CHUNK_SIZE, exception_on_overflow=False)
        pcm16 = np.frombuffer(data, dtype=np.int16)
        pcmf32_new = pcm16.astype(np.float32) / 32768.0

        # Accumulate audio data for VAD
        pcmf32_vad = np.concatenate((pcmf32_vad, pcmf32_new))

        if len(pcmf32_vad) >= VAD_WINDOW_SIZE_MS * SAMPLE_RATE / 1000:
            if vad_simple(pcmf32_vad, SAMPLE_RATE, VAD_WINDOW_SIZE_MS, VAD_THRESHOLD, FREQ_THRESHOLD, False):
                # Voice activity detected, accumulate audio data for Whisper
                pcmf32 = pcmf32_vad.copy()
                while len(pcmf32) < LENGTH_MS * SAMPLE_RATE / 1000:
                    data = stream.read(CHUNK_SIZE, exception_on_overflow=False)
                    pcm16 = np.frombuffer(data, dtype=np.int16)
                    pcmf32_new = pcm16.astype(np.float32) / 32768.0
                    pcmf32 = np.concatenate((pcmf32, pcmf32_new))

                # Save audio data to a WAV file
                with tempfile.NamedTemporaryFile(delete=False, suffix='.wav') as tmpfile:
                    write(tmpfile.name, SAMPLE_RATE, (pcmf32 * 32767).astype(np.int16))
                    # Transcribe audio using Whisper
                    result = whisper.transcribe(audio_path=tmpfile.name)
                    print(result['text'])
                    os.unlink(tmpfile.name)  # Delete the temporary file after use

                n_iter += 1

            # Reset VAD buffer
            pcmf32_vad = np.array([], dtype=np.float32)

    # Close the stream and terminate PyAudio
    stream.stop_stream()
    stream.close()
    audio.terminate()

if __name__ == "__main__":
    main()

