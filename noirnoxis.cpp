#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
const fs::path CUSTOM_DIR = "noirnoxis_utilities";

bool input(const std::string& prompt, std::string& value) {
    std::cout << prompt << std::flush;
    return static_cast<bool>(std::getline(std::cin, value));
}

bool toInt(const std::string& text, int& value) {
    try {
        size_t pos = 0;
        value = std::stoi(text, &pos);
        return pos == text.size();
    } catch (...) {
        return false;
    }
}

bool toDouble(const std::string& text, double& value) {
    try {
        size_t pos = 0;
        value = std::stod(text, &pos);
        return pos == text.size();
    } catch (...) {
        return false;
    }
}

bool validKeyword(const std::string& keyword) {
    if (keyword.empty() || keyword.size() > 32) return false;
    for (char c : keyword) {
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool reservedKeyword(const std::string& keyword) {
    const std::string reserved[] = {
        "help", "exit", "stop", "clear", "add", "list",
        "calculator", "timer", "stopwatch", "random", "password",
        "textstats", "reverse", "temperature", "fileinfo",
        "sysinfo", "notes"
    };
    return std::find(std::begin(reserved), std::end(reserved), keyword)
           != std::end(reserved);
}

// Linux terminal editor for the `add` command.
// Backspace removes a character; on an empty line it returns to the previous line.
// Ctrl+U clears the current line. END on a line by itself saves the code.
struct RawTerminal {
    termios previous{};
    bool enabled = false;

    RawTerminal() {
        if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &previous) != 0)
            return;
        termios settings = previous;
        settings.c_lflag &= ~(ICANON | ECHO);
        settings.c_cc[VMIN] = 1;
        settings.c_cc[VTIME] = 0;
        enabled = tcsetattr(STDIN_FILENO, TCSANOW, &settings) == 0;
    }

    ~RawTerminal() {
        if (enabled)
            tcsetattr(STDIN_FILENO, TCSANOW, &previous);
    }
};

bool readCode(std::string& code) {
    std::vector<std::string> lines;
    std::string current;
    RawTerminal terminal;

    // Standard input redirection has no interactive line editing.
    if (!terminal.enabled) {
        while (std::getline(std::cin, current)) {
            if (current == "END") break;
            if (current == "BACK" && !lines.empty())
                lines.pop_back();
            else
                lines.push_back(current);
        }
        if (!std::cin && !std::cin.eof()) return false;
        if (std::cin.eof()) return false;
    } else {
        std::cout << "> " << std::flush;
        int escape = 0;
        bool previousWasCR = false;
        while (true) {
            int value = std::cin.get();
            if (value == EOF) return false;
            char ch = static_cast<char>(value);

            // Ignore arrow-key escape sequences rather than inserting them into code.
            if (escape == 1) {
                escape = (ch == '[' || ch == 'O') ? 2 : 0;
                continue;
            }
            if (escape == 2) {
                if (ch >= '@' && ch <= '~') escape = 0;
                continue;
            }
            if (ch == 27) {
                escape = 1;
                continue;
            }

            if (ch == '\r' || ch == '\n') {
                if (ch == '\n' && previousWasCR) {
                    previousWasCR = false;
                    continue;
                }
                previousWasCR = (ch == '\r');
                std::cout << "\r\n";
                if (current == "END") break;
                lines.push_back(current);
                current.clear();
                std::cout << "> " << std::flush;
                continue;
            }
            previousWasCR = false;

            if (ch == 127 || ch == 8) {
                if (!current.empty()) {
                    current.pop_back();
                    std::cout << "\b \b" << std::flush;
                } else if (!lines.empty()) {
                    // Erase the current empty prompt and return to the previous line.
                    current = lines.back();
                    lines.pop_back();
                    std::cout << "\r\033[2K\033[1A\r\033[2K> "
                              << current << std::flush;
                }
                continue;
            }
            if (ch == 21) { // Ctrl+U: erase the whole current line.
                current.clear();
                std::cout << "\r\033[2K> " << std::flush;
                continue;
            }
            if (ch == 4) { // Ctrl+D: cancel editing without exiting NoirNoxis.
                std::cout << "\r\nEditor cancelled.\n";
                return true;
            }
            if (static_cast<unsigned char>(ch) >= 32 || ch == '\t') {
                current += ch;
                std::cout << ch << std::flush;
            }
        }
    }

    for (const auto& line : lines)
        code += line + "\n";
    return true;
}

bool addUtility() {
    std::cout << "Enter your FULL C++ program (including int main()).\n"
              << "Type END on its own line when finished.\n"
              << "Backspace edits text and can return to the previous line.\n"
              << "Ctrl+U clears the current line; Ctrl+D cancels.\n";

    std::string code;
    if (!readCode(code)) return false;

    if (code.empty()) {
        std::cout << "No code entered.\n";
        return true;
    }

    std::string keyword;
    if (!input("Keyword: ", keyword)) return false;

    if (!validKeyword(keyword) || reservedKeyword(keyword)) {
        std::cout << "Invalid or reserved keyword. Use 1-32 letters, digits, _ or -.\n";
        return true;
    }

    std::error_code ec;
    fs::create_directories(CUSTOM_DIR, ec);
    if (ec) {
        std::cout << "Cannot create utility folder: " << ec.message() << "\n";
        return true;
    }

    fs::path source = CUSTOM_DIR / (keyword + ".cpp");
    fs::path binary = CUSTOM_DIR / (keyword + ".bin");

    if (fs::exists(source) || fs::exists(binary)) {
        std::cout << "That keyword already exists.\n";
        return true;
    }

    {
        std::ofstream out(source);
        if (!out || !(out << code)) {
            std::cout << "Couldn't save source code.\n";
            return true;
        }
    }

    std::cout << "Compiling " << keyword << "...\n";
    // The keyword is restricted to shell-safe characters above.
    const std::string command = "g++ -std=c++17 -pthread " +
        source.string() + " -o " + binary.string();
    const int result = std::system(command.c_str());

    if (result != 0 || !fs::exists(binary)) {
        std::cout << "Compile failed; source saved to " << source << "\n";
        return true;
    }

    std::cout << "Added! Type '" << keyword << "' to run it.\n";
    return true;
}

void listUtilities() {
    std::cout << "Your added utilities:\n";
    std::error_code ec;
    if (!fs::exists(CUSTOM_DIR, ec)) {
        std::cout << "  (none yet)\n";
        return;
    }
    int count = 0;
    for (fs::directory_iterator it(CUSTOM_DIR, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& path = it->path();
        if (path.extension() == ".bin" && fs::is_regular_file(path)) {
            std::cout << "  " << path.stem().string() << "\n";
            ++count;
        }
    }
    if (ec) std::cout << "  Could not list utilities: " << ec.message() << "\n";
    else if (count == 0) std::cout << "  (none yet)\n";
}

bool runCustomUtility(const std::string& keyword) {
    if (!validKeyword(keyword)) return false;
    fs::path binary = CUSTOM_DIR / (keyword + ".bin");
    if (!fs::is_regular_file(binary)) return false;
    const std::string command = "./" + binary.string();
    const int status = std::system(command.c_str());
    if (status != 0) std::cout << "Utility exited with an error.\n";
    return true;
}

int main() {
    std::cout << "Loading..." << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::system("clear");
    std::cout << "NOIRNOXIS UTILITY\nType help for commands\n\n";

    while (true) {
        std::string util;
        if (!input("$ ", util)) break;

        if (util == "help") {
            std::cout <<
                "\n---- COMMANDS ----\n"
                "help        - Show commands\n"
                "add         - Add your own C++ utility\n"
                "list        - Show added utilities\n"
                "calculator  - Calculator\n"
                "timer       - Countdown timer\n"
                "stopwatch   - Stopwatch\n"
                "random      - Random numbers\n"
                "password    - Password generator\n"
                "textstats   - Text statistics\n"
                "reverse     - Reverse text\n"
                "temperature - Temperature converter\n"
                "fileinfo    - File information\n"
                "sysinfo     - System information\n"
                "notes       - Notes manager\n"
                "clear       - Clear terminal\n"
                "exit        - Exit\n\n";
                "about - About this";
        }
        else if (util == "exit" || util == "stop") {
            std::cout << "Goodbye!\n";
            break;
        }
        else if (util == "clear") {
            std::system("clear");
        }
        else if (util == "add") {
            if (!addUtility()) break;
        }
        else if (util == "list") {
            listUtilities();
        }
        else if (util == "calculator") {
            std::string x, y, op;
            if (!input("Number 1: ", x) ||
                !input("Operator (+ - * /): ", op) ||
                !input("Number 2: ", y)) break;
            double a, b;
            if (!toDouble(x, a) || !toDouble(y, b)) {
                std::cout << "Invalid numbers!\n";
                continue;
            }
            if (op == "+") std::cout << a + b << "\n";
            else if (op == "-") std::cout << a - b << "\n";
            else if (op == "*") std::cout << a * b << "\n";
            else if (op == "/") {
                if (b == 0) std::cout << "Cannot divide by zero!\n";
                else std::cout << a / b << "\n";
            }
            else std::cout << "Unknown operator!\n";
        }
        else if (util == "timer") {
            std::string text;
            if (!input("Seconds: ", text)) break;
            int seconds;
            if (!toInt(text, seconds) || seconds < 1 || seconds > 3600) {
                std::cout << "Enter 1-3600 seconds.\n";
                continue;
            }
            for (int i = seconds; i > 0; --i) {
                std::cout << "\rRemaining: " << i << " seconds   " << std::flush;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            std::cout << "\nTIME UP!\n";
        }
        else if (util == "stopwatch") {
            std::string unused;
            if (!input("Press ENTER to start...", unused)) break;
            auto start = std::chrono::steady_clock::now();
            if (!input("Press ENTER to stop...", unused)) break;
            double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            std::cout << "Elapsed: " << seconds << " seconds\n";
        }
        else if (util == "random") {
            std::string x, y;
            if (!input("Minimum: ", x) || !input("Maximum: ", y)) break;
            int a, b;
            if (!toInt(x, a) || !toInt(y, b) || a > b) {
                std::cout << "Invalid range!\n";
                continue;
            }
            std::random_device rd;
            std::mt19937 generator(rd());
            std::uniform_int_distribution<int> dist(a, b);
            std::cout << "Random number: " << dist(generator) << "\n";
        }
        else if (util == "password") {
            std::string text;
            if (!input("Password length (8-128): ", text)) break;
            int length;
            if (!toInt(text, length) || length < 8 || length > 128) {
                std::cout << "Invalid length!\n";
                continue;
            }
            const std::string chars =
                "abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "0123456789"
                "!@#$%^&*_-+=?";
            std::ifstream rng("/dev/urandom", std::ios::binary);
            if (!rng) {
                std::cout << "Secure randomness unavailable.\n";
                continue;
            }
            std::string password;
            const unsigned int range = chars.size();
            const unsigned int limit = 256 - (256 % range);
            while (static_cast<int>(password.size()) < length) {
                char raw;
                if (!rng.get(raw)) break;
                unsigned char byte = static_cast<unsigned char>(raw);
                if (byte >= limit) continue;
                password += chars[byte % range];
            }
            if (static_cast<int>(password.size()) == length)
                std::cout << "Password: " << password << "\n";
            else std::cout << "Password generation failed.\n";
        }
        else if (util == "textstats") {
            std::string text;
            if (!input("Text: ", text)) break;
            std::istringstream stream(text);
            std::string word;
            int words = 0;
            while (stream >> word) ++words;
            std::cout << "Characters: " << text.size()
                      << "\nWords: " << words << "\n";
        }
        else if (util == "reverse") {
            std::string text;
            if (!input("Text: ", text)) break;
            std::reverse(text.begin(), text.end());
            std::cout << "Reversed: " << text << "\n";
        }
        else if (util == "temperature") {
            std::cout << "1. Celsius to Fahrenheit\n"
                      << "2. Fahrenheit to Celsius\n";
            std::string option, text;
            if (!input("Choice: ", option) ||
                !input("Temperature: ", text)) break;
            double temp;
            if (!toDouble(text, temp)) {
                std::cout << "Invalid temperature!\n";
                continue;
            }
            if (option == "1") std::cout << (temp * 9.0 / 5.0) + 32 << " F\n";
            else if (option == "2") std::cout << (temp - 32) * 5.0 / 9.0 << " C\n";
            else std::cout << "Unknown option.\n";
        }

        else if (util == "about") {
            std::cout << "--About NoirNoxis--\n\n- License MIT\nIdea: NoirNoxis is commited to making terminal better\n- Next feature: GTk\n";
        }
        else if (util == "fileinfo") {
            std::string path;
            if (!input("File path: ", path)) break;
            std::error_code ec;
            if (!fs::exists(path, ec) || ec) {
                std::cout << "Path does not exist!\n";
                continue;
            }
            std::cout << "Path: " << path << "\n";
            if (fs::is_directory(path, ec)) std::cout << "Type: Directory\n";
            else if (fs::is_regular_file(path, ec)) {
                auto size = fs::file_size(path, ec);
                std::cout << "Type: File\n";
                if (!ec) std::cout << "Size: " << size << " bytes\n";
            }
            else std::cout << "Type: Other\n";
        }
        else if (util == "sysinfo") {
            std::cout << "\n---- SYSTEM ----\nCPU logical threads: "
                      << std::thread::hardware_concurrency() << "\n";
            std::ifstream mem("/proc/meminfo");
            std::string label;
            unsigned long ram;
            if (mem >> label >> ram)
                std::cout << "Total RAM: " << ram / 1024 << " MB\n";
            std::error_code ec;
            auto space = fs::space(".", ec);
            if (!ec)
                std::cout << "Disk available: "
                          << space.available / (1024 * 1024) << " MB\n";
        }
        else if (util == "notes") {
            std::cout << "1. Read notes\n2. Add note\n";
            std::string choice;
            if (!input("Choice: ", choice)) break;
            if (choice == "1") {
                std::ifstream file("notes.txt");
                if (!file) {
                    std::cout << "No notes yet.\n";
                    continue;
                }
                std::cout << "\n---- NOTES ----\n" << file.rdbuf() << "\n";
            }
            else if (choice == "2") {
                std::string note;
                if (!input("Note: ", note)) break;
                std::ofstream file("notes.txt", std::ios::app);
                if (!file) {
                    std::cout << "Cannot save note!\n";
                    continue;
                }
                file << note << "\n";
                std::cout << (file ? "Note saved!\n" : "Error saving note!\n");
            }
            else std::cout << "Unknown option.\n";
        }
        else if (runCustomUtility(util)) {
            // Custom utility ran successfully or returned its own error.
        }
        else if (!util.empty()) {
            std::cout << "Unknown command: " << util << "\n"
                      << "Type help for commands.\n";
        }
    }
    return 0;
}
