from openai import OpenAI
import json
import time
import pyaudio
import numpy as np
import os
import re
#import whisper
import mlx_whisper
from pynput import keyboard
import requests

import re
import subprocess

with open("prompt.txt", 'r', encoding='utf-8') as file:
    SYSTEM_MESSAGE = file.read().strip()

API_URL = "http://127.0.0.1:8080"
#asr_model = whisper.load_model("medium")

def merge_short_sentences(sens):
    sens_out = []
    for s in sens:
        # If the previous sentence is too short, merge them with
        # the current sentence.
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
    # deal with dirty sentences
    text = re.sub('[。！？；]', '.', text)
    text = re.sub('[，]', ',', text)
    text = re.sub('[“”]', '"', text)
    text = re.sub('[‘’]', "'", text)
    text = re.sub(r"[\<\>\(\)\[\]\"\«\»]+", "", text)
    text = re.sub('[\n\t ]+', ' ', text)
    text = re.sub('([,.!?;])', r'\1 $#!', text)
    # split
    sentences = [s.strip() for s in text.split('$#!')]
    if len(sentences[-1]) == 0: del sentences[-1]

    new_sentences = []
    new_sent = []
    count_len = 0
    for ind, sent in enumerate(sentences):
        # print(sent)
        new_sent.append(sent)
        count_len += len(sent.split(" "))
        if count_len > min_len or ind == len(sentences) - 1:
            count_len = 0
            new_sentences.append(' '.join(new_sent))
            new_sent = []
    return merge_short_sentences(new_sentences)


def play_audio(text):
    texts = split_sentences(text)
    # texts = text
    for t in texts:
        t = re.sub(r'([a-z])([A-Z])', r'\1 \2', t)
        # Assuming tts_model.get_text(t, tts_model.hps, False) returns the text to be spoken
        # stn_tst = tts_model.get_text(t, tts_model.hps, False)
        # Use the macOS say command to speak the text
        subprocess.call(['say', '-v', 'Lekha', t])


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
        data = stream.read(1024, exception_on_overflow = False)
        frames.append(np.frombuffer(data, dtype=np.int16))
    print('Finished recording')

    data = np.hstack(frames, dtype=np.float32) / 32768.0 
    #result = asr_model.transcribe(data)['text']
    result = mlx_whisper.transcribe(data, path_or_hf_repo="mlx-community/whisper-large-v3-mlx")['text']
    print(result)
    stream.stop_stream()
    stream.close()
    p.terminate()
    return result

def conversation():
    conversation_history = [{'role': 'system', 'content': SYSTEM_MESSAGE}]
    while True:
        user_input = record_and_transcribe_audio()
        conversation_history.append({'role': 'user', 'content': user_input})

        response = requests.post(
            f"{API_URL}/completion",
            json={
                "prompt": format_prompt(conversation_history),
                "n_predict": 512,
                "temperature": 0.7,
                "stop": ["\nHuman:", "\nAssistant:"],
                "stream": True
            },
            stream=True
        )

        if response.status_code != 200:
            print(f"Error: {response.status_code}")
            print(response.text)
            continue

        chatbot_response = ""
        for line in response.iter_lines():
            if line:
                line = line.decode('utf-8')
                if line.startswith("data: "):
                    data = json.loads(line[6:])
                    if 'content' in data:
                        chatbot_response += data['content']
                        print(data['content'], end='', flush=True)

        print()  # New line after the complete response
        conversation_history.append({'role': 'assistant', 'content': chatbot_response})
        play_audio(chatbot_response)

        if len(conversation_history) > 20:
            conversation_history = conversation_history[-20:]

def format_prompt(conversation_history):
    formatted_prompt = ""
    for message in conversation_history:
        if message['role'] == 'system':
            formatted_prompt += f"System: {message['content']}\n\n"
        elif message['role'] == 'user':
            formatted_prompt += f"Human: {message['content']}\n"
        elif message['role'] == 'assistant':
            formatted_prompt += f"Assistant: {message['content']}\n"
    formatted_prompt += "Assistant:"
    return formatted_prompt

if __name__=='__main__':
    conversation()
