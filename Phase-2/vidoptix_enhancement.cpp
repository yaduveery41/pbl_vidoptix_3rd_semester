
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <stdexcept>

namespace fs = std::filesystem;

std::string quotePath(const std::string& path) {
    if (path.find('"') != std::string::npos) {
        throw std::runtime_error(
            "Paths containing double quotes are not supported."
        );
    }
    return "\"" + path + "\"";
}

std::string readLine(const std::string& prompt) {
    std::string value;
    std::cout << prompt;
    std::getline(std::cin, value);
    return value;
}

int main() {
    try {
        std::cout << "\n====================================\n";
        std::cout << "     VidOptix - Video Enhancement\n";
        std::cout << "====================================\n";

        std::string input = readLine(
            "\nEnter input video path: "
        );

        if (input.empty() || !fs::exists(input)) {
            std::cerr << "ERROR: Input video not found.\n";
            return 1;
        }

        std::string output = readLine(
            "Enter output video path [enhanced.mp4]: "
        );

        if (output.empty()) {
            output = "enhanced.mp4";
        }

        if (fs::absolute(input).lexically_normal() ==
            fs::absolute(output).lexically_normal()) {
            std::cerr << "ERROR: Input and output must differ.\n";
            return 1;
        }

        std::cout << "\nChoose enhancement level:\n";
        std::cout << "1. Mild enhancement\n";
        std::cout << "2. Balanced enhancement\n";
        std::cout << "3. Strong enhancement\n";

        std::string choice = readLine("Enter choice (1-3): ");

        std::string filters;

        if (choice == "1") {
            filters =
                "hqdn3d=1.5:1.5:3:3,"
                "eq=contrast=1.03:brightness=0.01,"
                "unsharp=5:5:0.3";
        } else if (choice == "2") {
            filters =
                "hqdn3d=2:1.5:3:2.25,"
                "eq=contrast=1.05:brightness=0.02,"
                "unsharp=5:5:0.5";
        } else if (choice == "3") {
            filters =
                "hqdn3d=3:2:4:3,"
                "eq=contrast=1.08:brightness=0.03,"
                "unsharp=5:5:0.7";
        } else {
            std::cerr << "ERROR: Choose 1, 2, or 3.\n";
            return 1;
        }

        std::string command =
            "ffmpeg -hide_banner -y -i " + quotePath(input) +
            " -map 0:v:0 -map \"0:a?\" "
            "-vf \"" + filters + "\" "
            "-c:v libx264 -preset medium -crf 20 "
            "-pix_fmt yuv420p -c:a aac -b:a 128k "
            "-movflags +faststart " + quotePath(output);

        std::cout << "\nEnhancing video...\n";

        int result = std::system(command.c_str());

        if (result != 0) {
            std::cerr << "\nERROR: FFmpeg enhancement failed.\n";
            return 1;
        }

        std::cout << "\nEnhancement completed!\n";
        std::cout << "Output: "
                  << fs::absolute(output).string() << "\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
