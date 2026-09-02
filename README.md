# VidOptix — Video Compression Pipeline

VidOptix is a 3rd-semester Project-Based Learning (PBL) project exploring core computer science concepts through a custom video compression pipeline. The goal is to balance bandwidth and storage efficiency while maintaining 75–80% visual fidelity.

---

## 📌 Project Overview
The pipeline breaks video sequences into individual frames and applies classical transformation, quantization, and entropy encoding algorithms.

### Key Components & DSA Concepts
- **Discrete Cosine Transform (DCT):** Converts spatial domain pixel matrices into frequency domain representations.
- **Quantization & Zigzag Traversal:** Suppresses less perceptible high-frequency details and arranges coefficients linearly for efficient compression.
- **Run-Length Encoding (RLE):** Compresses consecutive sequences of identical symbols (especially zero-runs).
- **Huffman Coding:** Implements greedy prefix coding using Min-Heaps / Priority Queues and Binary Trees for lossless entropy encoding.

---

