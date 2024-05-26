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

from projectaria_tools.core.vrs import extract_audio_track

def parse_args():
    parser = argparse.ArgumentParser(description="Convert VRS audio to WAV.")
    parser.add_argument("--vrs", type=str, required=True, help="Path to the VRS file.")
    parser.add_argument("--output_wav", type=str, required=True, help="Path to the output WAV file.")
    parser.add_argument("--long_wav", action='store_true', help="Flag to export a long WAV file.")
    return parser.parse_args()

def extract_audio(vrs_file_path: str, output_wav_path: str, long_wav: bool):
    try:
        json_output_string = extract_audio_track(vrs_file_path, output_wav_path, wav64=long_wav)
        json_output = json.loads(json_output_string)
        if json_output.get("status") == "success":
            print(f"Audio extracted successfully to {output_wav_path}")
        else:
            print("Failed to extract audio.")
    except Exception as e:
        print(f"An error occurred during audio extraction: {e}")

def main():
    args = parse_args()
    extract_audio(args.vrs, args.output_wav, args.long_wav)

if __name__ == "__main__":
    main()
