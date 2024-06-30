import time
import pyaudio
import numpy as np
import os
import re
from lightning_whisper_mlx import LightningWhisperMLX
from pynput import keyboard
import subprocess
import requests
import json

# Load the system message from a file
with open("prompt.txt", 'r', encoding='utf-8') as file:
    SYSTEM_MESSAGE = file.read().strip()

# Initialize the Whisper model for speech recognition
whisper = LightningWhisperMLX(model="large-v3", batch_size=12, quant=None)

# MLX LM server URL
MLX_LM_URL = "http://localhost:8080/v1/chat/completions"

def merge_short_sentences(sens):
    sens_out = []
    for s in sens:
        if len(sens_out) > 0 and len(sens_out[-1].split(" ")) <= 2:
            sens_out[-1] = sens_out[-1] + " " + s
        else:
            sens_out.append(s)
    try:
        if len(sens_out[-1].split(" ")) <= 2:
            sens_out[-2] = sens_out[-2] + " " + sens_out[-1]
            sens_out.pop(-1)
    except:
        pass
    return sens_out

def split_sentences(text, min_len=10):
    text = re.sub('[。！？；]', '.', text)
    text = re.sub('[，]', ',', text)
    text = re.sub('[""]', '"', text)
    text = re.sub('['']', "'", text)
    text = re.sub(r"[\<\>\(\)\[\]\"\«\»]+", "", text)
    text = re.sub('[\n\t ]+', ' ', text)
    text = re.sub('([,.!?;])', r'\1 $#!', text)
    sentences = [s.strip() for s in text.split('$#!')]
    if len(sentences[-1]) == 0: del sentences[-1]

    new_sentences = []
    new_sent = []
    count_len = 0
    for ind, sent in enumerate(sentences):
        new_sent.append(sent)
        count_len += len(sent.split(" "))
        if count_len > min_len or ind == len(sentences) - 1:
            count_len = 0
            new_sentences.append(' '.join(new_sent))
            new_sent = []
    return merge_short_sentences(new_sentences)

def play_audio(text):
    texts = split_sentences(text)
    for t in texts:
        t = re.sub(r'([a-z])([A-Z])', r'\1 \2', t)
        subprocess.call(['say', '-v', 'Karen', t])

def record_and_transcribe_audio():
    recording = False
    def on_press(key):
        nonlocal recording
        if key == keyboard.Key.shift:
            recording = True

    def on_release(key):
        nonlocal recording
        if key == keyboard.Key.shift:
            recording = False
            return False

    listener = keyboard.Listener(
        on_press=on_press,
        on_release=on_release)
    listener.start()

    print('Press shift to record...')
    while not recording:
        time.sleep(0.1)
    print('Start recording...')

    p = pyaudio.PyAudio()
    stream = p.open(format=pyaudio.paInt16, channels=1, rate=16000, frames_per_buffer=1024, input=True)
    frames = []
    while recording:
        data = stream.read(1024, exception_on_overflow=False)
        frames.append(np.frombuffer(data, dtype=np.int16))
    print('Finished recording')

    data = np.hstack(frames, dtype=np.float32) / 32768.0 
    result = whisper.transcribe(data)['text']
    stream.stop_stream()
    stream.close()
    p.terminate()
    return result

def conversation():
    conversation_history = [{'role': 'system', 'content': SYSTEM_MESSAGE}]
    while True:
        user_input = record_and_transcribe_audio()
        conversation_history.append({'role': 'user', 'content': user_input})

        try:
            response = client.chat.completions.create(
                model="mistral",  # The model name isn't used by the server, but is required by the client
                messages=conversation_history,
                temperature=0.7,
                max_tokens=100
            )
            chatbot_response = response.choices[0].message.content
            conversation_history.append({'role': 'assistant', 'content': chatbot_response})
            print(conversation_history)
            play_audio(chatbot_response)

        except Exception as e:
            print(f"An error occurred: {e}")
            # You might want to implement a retry mechanism or fallback behavior here

        if len(conversation_history) > 20:
            conversation_history = conversation_history[-20:]

if __name__ == '__main__':
    conversation()

