// VidOptix backend (fast version)
//
// Pipeline:
//   1. FFmpeg encodes the video to a playable H.264/MP4 (optional enhancement
//      filters are applied in the SAME pass, so there is only one encode).
//   2. A custom canonical Huffman coder (written in this file) compresses the
//      finished MP4 into a .vhuf file and decodes it again.
//   3. The decoded bytes are compared with the original, byte for byte.
//
// Why this is fast: the Huffman stage works on the compressed MP4 (megabytes),
// not on raw decoded frames (gigabytes), and there are no temporary raw files.
//
// Build: g++ -std=c++17 -O2 vidoptix_backend.cpp -o vidoptix_backend.exe
// Usage: vidoptix_backend.exe INPUT OUTPUT CRF ENHANCE LEVEL
//        vidoptix_backend.exe --decode INPUT.vhuf OUTPUT.mp4
//
// stdout on success (read by app.py):
//   SUCCESS, HUFFMAN_RAW_BYTES (MP4 size before Huffman), HUFFMAN_ENCODED_BYTES,
//   HUFFMAN_RATIO_PERCENT, HUFFMAN_ROUNDTRIP=verified (+ timing lines)
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static const std::string X264_PRESET = "veryfast";   // faster = quicker demo; "slow" = smaller file

// ---------- small helpers ----------

static double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}
static std::string quotePath(const std::string& p) {
    if (p.find('"') != std::string::npos) throw std::runtime_error("Double quotes in paths are not supported.");
    return "\"" + p + "\"";
}
static bool samePath(const std::string& a, const std::string& b) {
    return fs::absolute(fs::path(a)).lexically_normal() == fs::absolute(fs::path(b)).lexically_normal();
}
static bool runCommand(const std::string& command) {
    std::cerr << "[VidOptix] Running: " << command << "\n";
    return std::system(command.c_str()) == 0;
}
static const char* filtersFor(const std::string& level) {
    if (level == "mild")     return "hqdn3d=1.5:1.5:3:3,eq=contrast=1.03:brightness=0.01,unsharp=5:5:0.3";
    if (level == "balanced") return "hqdn3d=2:1.5:3:2.25,eq=contrast=1.05:brightness=0.02,unsharp=5:5:0.5";
    if (level == "strong")   return "hqdn3d=3:2:4:3,eq=contrast=1.08:brightness=0.03,unsharp=5:5:0.7";
    throw std::runtime_error("Enhancement level must be mild, balanced, or strong.");
}
static std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Could not open file: " + p.string());
    const std::streamsize n = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(n));
    if (n > 0 && !in.read(reinterpret_cast<char*>(data.data()), n))
        throw std::runtime_error("Could not read file: " + p.string());
    return data;
}
static void writeFile(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream out(p, std::ios::binary);
    if (!out) throw std::runtime_error("Could not create file: " + p.string());
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out) throw std::runtime_error("Failed while writing file: " + p.string());
}

// ---------- custom canonical Huffman coder ----------
// .vhuf layout: 8-byte magic "VIDHUF2\0" | 8-byte original length (little endian)
//               | 256 bytes: code length of every byte value | Huffman bit stream.
// Canonical codes mean only the code lengths need to be stored (272-byte header).

static const char     MAGIC[8]    = {'V', 'I', 'D', 'H', 'U', 'F', '2', '\0'};
static const int      MAX_LEN     = 56;   // codes always fit the 64-bit bit buffer

// Builds a Huffman tree from byte frequencies and returns each byte's code length.
static std::array<uint8_t, 256> codeLengths(const std::array<uint64_t, 256>& freq) {
    struct Node { int left, right, symbol; };
    std::vector<Node> nodes;
    using Item = std::pair<uint64_t, int>;   // (frequency, node index)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;

    for (int s = 0; s < 256; ++s) {
        if (freq[s]) {
            nodes.push_back({-1, -1, s});
            queue.push({freq[s], static_cast<int>(nodes.size()) - 1});
        }
    }

    std::array<uint8_t, 256> len{};
    if (nodes.empty()) return len;
    if (nodes.size() == 1) { len[nodes[0].symbol] = 1; return len; }

    while (queue.size() > 1) {
        const Item a = queue.top(); queue.pop();
        const Item b = queue.top(); queue.pop();
        nodes.push_back({a.second, b.second, -1});
        queue.push({a.first + b.first, static_cast<int>(nodes.size()) - 1});
    }

    std::vector<std::pair<int, int>> stack{{queue.top().second, 0}};   // (node, depth)
    while (!stack.empty()) {
        const auto [index, depth] = stack.back();
        stack.pop_back();
        if (nodes[index].symbol >= 0) {
            if (depth > MAX_LEN) throw std::runtime_error("Huffman code length limit exceeded.");
            len[nodes[index].symbol] = static_cast<uint8_t>(depth);
        } else {
            stack.push_back({nodes[index].left, depth + 1});
            stack.push_back({nodes[index].right, depth + 1});
        }
    }
    return len;
}

// Count the frequency of each byte
// Generate Huffman codes
// Store the encoded bits
static std::vector<uint8_t> huffmanEncode(const std::vector<uint8_t>& data) {
    std::array<uint64_t, 256> freq{};
    for (uint8_t b : data) ++freq[b];

    const std::array<uint8_t, 256> len = codeLengths(freq);

    int maxLen = 0;
    for (uint8_t l : len) maxLen = std::max<int>(maxLen, l);

    // Canonical code assignment: shorter codes first, ties broken by byte value.
    std::array<uint64_t, 256> code{};
    uint64_t next = 0;
    for (int l = 1; l <= maxLen; ++l) {
        for (int s = 0; s < 256; ++s) if (len[s] == l) code[s] = next++;
        next <<= 1;
    }

    std::vector<uint8_t> out;
    out.reserve(data.size() + 300);
    out.insert(out.end(), MAGIC, MAGIC + 8);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((static_cast<uint64_t>(data.size()) >> (8 * i)) & 0xFF));
    out.insert(out.end(), len.begin(), len.end());

    uint64_t buffer = 0;
    int bits = 0;
    for (uint8_t b : data) {
        buffer = (buffer << len[b]) | code[b];
        bits += len[b];
        while (bits >= 8) {
            out.push_back(static_cast<uint8_t>((buffer >> (bits - 8)) & 0xFF));
            bits -= 8;
        }
    }
    if (bits > 0) out.push_back(static_cast<uint8_t>((buffer << (8 - bits)) & 0xFF));
    return out;
}

static std::vector<uint8_t> huffmanDecode(const std::vector<uint8_t>& enc) {
    if (enc.size() < 272 || !std::equal(MAGIC, MAGIC + 8, enc.begin()))
        throw std::runtime_error("Not a valid VidOptix Huffman (.vhuf) file.");

    uint64_t total = 0;
    for (int i = 0; i < 8; ++i) total |= static_cast<uint64_t>(enc[8 + i]) << (8 * i);

    std::array<uint8_t, 256> len{};
    int maxLen = 0;
    for (int s = 0; s < 256; ++s) {
        len[s] = enc[16 + s];
        if (len[s] > MAX_LEN) throw std::runtime_error("Corrupt Huffman header.");
        maxLen = std::max<int>(maxLen, len[s]);
    }

    std::vector<uint8_t> out(static_cast<size_t>(total));
    if (total == 0) return out;
    if (maxLen == 0) throw std::runtime_error("Corrupt Huffman header.");

    // Canonical decoding tables: first code, count and symbol offset for each length.
    std::array<uint64_t, MAX_LEN + 2> firstCode{}, count{}, offset{};
    std::vector<uint8_t> symbols;                     // bytes sorted by (length, value)
    uint64_t next = 0;
    for (int l = 1; l <= maxLen; ++l) {
        firstCode[l] = next;
        offset[l] = symbols.size();
        for (int s = 0; s < 256; ++s) if (len[s] == l) { symbols.push_back(static_cast<uint8_t>(s)); ++count[l]; ++next; }
        next <<= 1;
    }

    size_t pos = 272;
    int bitsLeft = 0;
    uint8_t current = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        uint64_t c = 0;
        int l = 0;
        while (true) {
            if (bitsLeft == 0) {
                if (pos >= enc.size()) throw std::runtime_error("Huffman bit stream ended early.");
                current = enc[pos++];
                bitsLeft = 8;
            }
            c = (c << 1) | ((current >> (--bitsLeft)) & 1u);
            if (++l > maxLen) throw std::runtime_error("Invalid Huffman bit stream.");
            const uint64_t index = c - firstCode[l];
            if (index < count[l]) { out[i] = symbols[offset[l] + index]; break; }
        }
    }
    return out;
}

struct HuffmanStats {
    uint64_t beforeBytes = 0, afterBytes = 0;
    double encodeSeconds = 0, decodeSeconds = 0;
};

// Encodes the MP4 into .vhuf, decodes it again and verifies it byte for byte.
static HuffmanStats huffmanStage(const fs::path& mp4, const fs::path& vhuf) {
    const std::vector<uint8_t> original = readFile(mp4);

    HuffmanStats stats;
    auto t = Clock::now();
    const std::vector<uint8_t> encoded = huffmanEncode(original);
    stats.encodeSeconds = secondsSince(t);
    writeFile(vhuf, encoded);

    t = Clock::now();
    const std::vector<uint8_t> decoded = huffmanDecode(readFile(vhuf));
    stats.decodeSeconds = secondsSince(t);

    if (decoded != original)
        throw std::runtime_error("Huffman round-trip verification failed: decoded data differs from the original.");

    stats.beforeBytes = original.size();
    stats.afterBytes = encoded.size();
    return stats;
}

// ---------- main ----------

int main(int argc, char** argv) {
    fs::path vhuf;
    try {
        // Extra tool: restore an MP4 from a .vhuf file (proves the Huffman file is real).
        if (argc == 4 && std::string(argv[1]) == "--decode") {
            const std::vector<uint8_t> restored = huffmanDecode(readFile(argv[2]));
            writeFile(argv[3], restored);
            std::cout << "DECODED_BYTES=" << restored.size() << "\n";
            return 0;
        }

        if (argc != 6) { std::cerr << "Usage: vidoptix_backend.exe INPUT OUTPUT CRF ENHANCE LEVEL\n"; return 2; }

        const std::string input = argv[1], output = argv[2], level = argv[5];
        int crf = 0;
        try { crf = std::stoi(argv[3]); } catch (...) { throw std::runtime_error("CRF must be a whole number."); }
        const bool enhance = std::string(argv[4]) == "1";

        if (crf < 18 || crf > 30) throw std::runtime_error("CRF must be between 18 and 30.");
        if (!fs::exists(input) || !fs::is_regular_file(input)) throw std::runtime_error("Input file does not exist.");
        if (samePath(input, output)) throw std::runtime_error("Input and output paths must differ.");
        if (enhance) filtersFor(level);   // validates the level

        fs::path parent = fs::path(output).parent_path();
        if (parent.empty()) parent = ".";
        fs::create_directories(parent);

        // ----- Step 1: one FFmpeg pass (enhancement is optional) -----
        // The scale filter rounds width/height down to even numbers, which H.264 + yuv420p requires.
        std::string filters = "scale=trunc(iw/2)*2:trunc(ih/2)*2";
        if (enhance) filters += "," + std::string(filtersFor(level));

        const std::string command =
            "ffmpeg -nostdin -hide_banner -y -i " + quotePath(input) +
            " -map 0:v:0 -map \"0:a?\" -vf " + quotePath(filters) +
            " -c:v libx264 -preset " + X264_PRESET + " -crf " + std::to_string(crf) +
            " -pix_fmt yuv420p -c:a aac -b:a 96k -movflags +faststart " + quotePath(output);

        const auto ffmpegStart = Clock::now();
        if (!runCommand(command))
            throw std::runtime_error("FFmpeg failed. Check that FFmpeg is installed, on PATH, and supports libx264.");
        if (!fs::exists(output) || fs::file_size(output) == 0)
            throw std::runtime_error("Output file was not created.");
        const double ffmpegSeconds = secondsSince(ffmpegStart);

        // ----- Step 2: custom Huffman coding + verification -----
        vhuf = output + ".vhuf";
        const HuffmanStats h = huffmanStage(output, vhuf);
        const double ratio = h.beforeBytes ? 100.0 * static_cast<double>(h.afterBytes) / static_cast<double>(h.beforeBytes) : 0.0;

        std::cout << std::fixed << std::setprecision(2)
                  << "SUCCESS\n"
                  << "HUFFMAN_RAW_BYTES=" << h.beforeBytes << "\n"
                  << "HUFFMAN_ENCODED_BYTES=" << h.afterBytes << "\n"
                  << "HUFFMAN_RATIO_PERCENT=" << ratio << "\n"
                  << "HUFFMAN_ROUNDTRIP=verified\n"
                  << "HUFFMAN_FILE=" << fs::absolute(vhuf).string() << "\n"
                  << "FFMPEG_SECONDS=" << ffmpegSeconds << "\n"
                  << "HUFFMAN_SECONDS=" << (h.encodeSeconds + h.decodeSeconds) << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::error_code ec;
        if (!vhuf.empty()) fs::remove(vhuf, ec);
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
