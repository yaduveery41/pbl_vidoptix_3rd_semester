# VidOptix — connected frontend + C++/FFmpeg backend (Windows)

This project connects the VidOptix HTML UI to Flask and a C++ backend. The backend now includes a custom byte-level Huffman encoder and decoder for an intermediate raw YUV video-frame stream, verifies that decoding restores the exact raw frame bytes, then encodes those frames into a normal playable H.264/MP4 with FFmpeg. The Flask API, custom output filename, output details, download endpoint, and optional enhancement workflow remain in place.

## Requirements
- Windows 10/11
- FFmpeg and FFprobe installed and available on PATH (`ffmpeg -version`, `ffprobe -version`)
- g++ with C++17 support (`g++ --version`)
- Python 3.10+ (`python --version`)

## First-time setup
1. Extract this folder somewhere writable, for example `Desktop\VidOptix_Connected`.
2. Open that folder in VS Code.
3. Double-click `build.bat` (or run it from the VS Code terminal). It builds `vidoptix_backend.exe`.
4. Double-click `start.bat`. It installs the small Flask dependency if needed and starts the local server.
5. Keep the server terminal window open. Open **http://127.0.0.1:8765** in Chrome/Edge. Do not open `index.html` by double-clicking it.

## Use
1. Click **Browse video** and select an MP4.
2. Set CRF (18 = higher quality/larger output; 30 = smaller/lower quality).
3. Choose optional enhancement and Mild/Balanced/Strong if desired.
4. Click **Start processing**. FFmpeg runs locally; keep the server window open.
5. When finished, the browser downloads a file named like `myvideo_vidoptix_crf27_ab12cd.mp4` to your Downloads folder (or the browser's configured download folder).

The backend also keeps a copy in `work\outputs` inside this project folder. This is useful if your browser's automatic download setting is enabled or you need to retrieve it again.

## What Huffman coding does here (important)

- The Huffman coder is custom C++ code in `vidoptix_backend.cpp` (canonical Huffman: frequency table, tree, code lengths, bit packing). It is not a label placed on FFmpeg.
- **Pipeline:** (1) FFmpeg encodes the video to H.264/MP4 in a single pass, with the optional enhancement filters applied in the same pass. (2) The custom coder compresses the finished MP4 into a `.vhuf` file (saved next to the MP4 in `work\outputs`). (3) The `.vhuf` is decoded again and compared with the MP4 **byte for byte**; the job fails if they differ.
- **Why it is fast:** the Huffman stage runs on the compressed MP4 (megabytes), not on raw decoded frames (gigabytes), so it adds well under a second for typical demo videos. There are no temporary raw files.
- **Restore an MP4 from a `.vhuf`:** `vidoptix_backend.exe --decode file.mp4.vhuf restored.mp4`
- **Honest limitation:** H.264 and AAC already entropy-code their data, so a second byte-level Huffman pass gives about 0% extra reduction (expect roughly 98-101% of the MP4 size). The size reduction you see comes from the H.264 CRF setting. The UI reports the MP4 size before Huffman, the Huffman file size and the ratio.
- Final MP4 maps the first video stream and the optional first audio stream; other tracks, subtitles and rotation tags are not preserved.

## Troubleshooting
- **Cannot reach backend:** ensure `start.bat` is running and visit `http://127.0.0.1:8765`.
- **C++ backend missing:** run `build.bat` successfully.
- **FFmpeg/libx264 error:** run `ffmpeg -encoders` and check that `libx264` appears; reinstall FFmpeg if needed.
- **Temporary disk space error:** raw video frames can require many gigabytes. Free disk space and retry with a shorter or lower-resolution source.
- **g++ not found:** install MinGW-w64 or MSYS2 and add its `bin` directory to PATH, then reopen VS Code.
- **Output larger than input:** this can happen when the original video is already highly compressed. Increase CRF to try to reduce size.
- **This is a local-only demo:** server listens on 127.0.0.1, not your network. Do not change it to 0.0.0.0 without adding authentication and security controls.

## Notes
- The frontend only accepts MP4 files.
- FFmpeg handles decoding, enhancement filters, and final H.264/MP4 encoding. The custom Huffman stage is a reversible intermediate stage around raw video-frame bytes; it is not integrated into the H.264 bitstream.
- Enhancement filters are conventional denoise/contrast/sharpen filters; they do not use AI.


The interface now includes a custom output filename field and an Input / Output details section with file name, size, resolution, duration (input), encoding, size change, and percentage saved.
