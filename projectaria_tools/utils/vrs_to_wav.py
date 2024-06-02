#!/usr/bin/env python3
# Copyright (c) University of Washington and authors Josiah Z, Alanna K, and Olivia B.
# In affiliation with Meta Platforms, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import json
import os

from projectaria_tools.core.vrs import extract_audio_track
from projectaria_tools.core.vrs import extract_audio_track_wav64

def parse_args():
    parser = argparse.ArgumentParser(description="Convert VRS audio to WAV or WAV64.")
    parser.add_argument("--vrs", type=str, required=True, help="Path to the VRS file.")
    parser.add_argument("--output_wav", type=str, required=True, help="Path to the output WAV file.")
    parser.add_argument("--long_wav", action='store_true', help="Flag to export a long WAV file (WAV64).")
    return parser.parse_args()

def adjust_file_extension(file_path: str, long_wav: bool) -> str:
    """Ensure the file extension reflects the audio format (WAV or WAV64)."""
    base, ext = os.path.splitext(file_path)
    if long_wav and ext.lower() in ['.wav', '.w64']:
        return f"{base}.w64"
    elif not long_wav and ext.lower() not in ['.wav', '.w64']:
        return f"{base}.wav"
    return file_path

def extract_audio(vrs_file_path: str, output_wav_path: str, long_wav: bool):
    """Extracts audio from a VRS file and writes to a WAV or WAV64 file based on the flag."""
    output_wav_path = adjust_file_extension(output_wav_path, long_wav)
    try:
        print(f"Starting audio extraction: {vrs_file_path} to {output_wav_path} with long WAV: {long_wav}")
        if long_wav:
            json_output_string = extract_audio_track_wav64(vrs_file_path, output_wav_path)
        else:
            json_output_string = extract_audio_track(vrs_file_path, output_wav_path)

        json_output = json.loads(json_output_string)
        if json_output.get("status") == "success":
            print(f"Audio extracted successfully to {output_wav_path}")
        else:
            print(f"Failed to extract audio: {json_output.get('error', 'Unknown error')}")
    except Exception as e:
        print(f"An error occurred during audio extraction: {e}")

def main():
    args = parse_args()
    extract_audio(args.vrs, args.output_wav, args.long_wav)

if __name__ == "__main__":
    main()
