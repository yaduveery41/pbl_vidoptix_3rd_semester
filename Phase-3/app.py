from flask import Flask, request, jsonify, send_file
from werkzeug.utils import secure_filename
from pathlib import Path
import subprocess, uuid, os, shutil, re

ROOT = Path(__file__).resolve().parent
UPLOADS = ROOT / "work" / "uploads"
OUTPUTS = ROOT / "work" / "outputs"
UPLOADS.mkdir(parents=True, exist_ok=True)
OUTPUTS.mkdir(parents=True, exist_ok=True)
app = Flask(__name__, static_folder=None)
app.config["MAX_CONTENT_LENGTH"] = 4 * 1024 * 1024 * 1024  # 4 GB upload limit
ALLOWED = {".mp4"}
BACKEND_EXE = ROOT / "vidoptix_backend.exe"

@app.get("/")
def index():
    return send_file(ROOT / "index.html")

@app.get("/api/health")
def health():
    ffmpeg = shutil.which("ffmpeg")
    return jsonify({"ok": True, "backend_exists": BACKEND_EXE.exists(), "ffmpeg_found": bool(ffmpeg)})

@app.errorhandler(413)
def too_large(_):
    return jsonify(error="File exceeds the 4 GB upload limit."), 413

@app.post("/api/process")
def process_video():
    uploaded = request.files.get("video")
    if uploaded is None or not uploaded.filename:
        return jsonify(error="Please select an MP4 video."), 400
    original_name = secure_filename(uploaded.filename)
    if Path(original_name).suffix.lower() not in ALLOWED:
        return jsonify(error="Only MP4 files are supported by this interface."), 400
    if not BACKEND_EXE.exists():
        return jsonify(error="C++ backend executable is missing. Run build.bat first."), 500
    try:
        crf = int(request.form.get("crf", "27"))
    except ValueError:
        return jsonify(error="CRF must be an integer from 18 to 30."), 400
    if not 18 <= crf <= 30:
        return jsonify(error="CRF must be between 18 and 30."), 400
    enhance = request.form.get("enhance", "false").lower() == "true"
    level = request.form.get("level", "balanced").lower()
    if level not in {"mild", "balanced", "strong"}:
        return jsonify(error="Invalid enhancement level."), 400

    job_id = uuid.uuid4().hex
    input_path = UPLOADS / f"{job_id}_{original_name}"
    requested_name = request.form.get("output_name", "").strip()
    # Keep the user-provided base name, but strip unsafe characters and enforce .mp4.
    raw_stem = Path(requested_name).stem if requested_name else (Path(original_name).stem + "_compressed")
    safe_stem = secure_filename(raw_stem).strip(" ._-")[:100] or "vidoptix_output"
    output_name = f"{safe_stem}.mp4"
    # Avoid overwriting an earlier result with the same chosen name.
    if (OUTPUTS / output_name).exists():
        output_name = f"{safe_stem}_{job_id[:6]}.mp4"
    output_path = OUTPUTS / output_name
    try:
        uploaded.save(input_path)
        input_size = input_path.stat().st_size
        cmd = [str(BACKEND_EXE), str(input_path), str(output_path), str(crf),
               "1" if enhance else "0", level]
        completed = subprocess.run(cmd, capture_output=True, text=True, timeout=6 * 60 * 60)
        if completed.returncode != 0 or not output_path.exists() or output_path.stat().st_size == 0:
            message = (completed.stderr or completed.stdout or "C++ backend failed.")[-3000:]
            return jsonify(error=message), 500
        output_size = output_path.stat().st_size
        saved = (1 - output_size / input_size) * 100 if input_size else 0.0
        # These values describe the reversible custom Huffman stage on raw video frames.
        backend_log = completed.stdout or ""
        def backend_value(key, cast=int):
            match = re.search(r"^" + re.escape(key) + r"=(.+)$", backend_log, re.MULTILINE)
            if not match:
                return None
            try:
                return cast(match.group(1).strip())
            except (TypeError, ValueError):
                return None
        huffman_raw_bytes = backend_value("HUFFMAN_RAW_BYTES")
        huffman_encoded_bytes = backend_value("HUFFMAN_ENCODED_BYTES")
        huffman_ratio_percent = backend_value("HUFFMAN_RATIO_PERCENT", float)
        huffman_roundtrip = backend_value("HUFFMAN_ROUNDTRIP", str) == "verified"
        output_resolution = "Unavailable"
        ffprobe = shutil.which("ffprobe")
        if ffprobe:
            try:
                probe = subprocess.run(
                    [ffprobe, "-v", "error", "-select_streams", "v:0",
                     "-show_entries", "stream=width,height", "-of", "json", str(output_path)],
                    capture_output=True, text=True, timeout=30, check=True
                )
                import json
                streams = json.loads(probe.stdout).get("streams", [])
                if streams:
                    output_resolution = f'{streams[0].get("width", "?")} × {streams[0].get("height", "?")}'
            except Exception:
                pass
        return jsonify({
            "ok": True, "output_name": output_name,
            "output_size": output_size, "input_size": input_size,
            "output_resolution": output_resolution,
            "saved_percent": saved, "download_url": f"/api/download/{output_name}",
            "huffman_raw_bytes": huffman_raw_bytes,
            "huffman_encoded_bytes": huffman_encoded_bytes,
            "huffman_ratio_percent": huffman_ratio_percent,
            "huffman_roundtrip_verified": huffman_roundtrip
        })
    except subprocess.TimeoutExpired:
        return jsonify(error="Processing timed out after 6 hours."), 504
    except Exception as exc:
        app.logger.exception("Video processing failed")
        return jsonify(error=f"Processing failed: {exc}"), 500
    finally:
        try:
            input_path.unlink(missing_ok=True)
        except Exception:
            pass

@app.get("/api/download/<path:filename>")
def download(filename):
    # Only serve files created in OUTPUTS; disallow path traversal.
    safe = Path(filename).name
    path = OUTPUTS / safe
    if not path.exists() or not path.is_file() or path.parent.resolve() != OUTPUTS.resolve():
        return jsonify(error="Output file not found. Process the video again."), 404
    return send_file(path, as_attachment=True, download_name=safe, mimetype="video/mp4")

if __name__ == "__main__":
    if not shutil.which("ffmpeg"):
        print("WARNING: FFmpeg was not found on PATH. Install FFmpeg and reopen this terminal.")
    print("VidOptix local server: http://127.0.0.1:8765")
    app.run(host="127.0.0.1", port=8765, debug=False, threaded=True)
