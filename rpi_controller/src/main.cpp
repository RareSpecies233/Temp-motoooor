#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifndef _WIN32
#ifndef CRTSCTS
#define CRTSCTS 0
#endif
#endif

namespace {

std::string trim(const std::string &s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        ++start;
    }

    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }

    return s.substr(start, end - start);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string nowTimestamp(const char *format) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tmVal{};
#ifdef _WIN32
    localtime_s(&tmVal, &t);
#else
    localtime_r(&t, &tmVal);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmVal, format);
    return oss.str();
}

std::string makeLogLineTime() {
    return nowTimestamp("%Y-%m-%d %H:%M:%S");
}

std::string makeLogFileTime() {
    return nowTimestamp("%Y%m%d_%H%M%S");
}

class Logger {
public:
    Logger() {
        try {
            std::filesystem::create_directories("logs");
            logFilePath_ = std::filesystem::path("logs") / (makeLogFileTime() + ".log");
            out_.open(logFilePath_, std::ios::out | std::ios::app);
        } catch (...) {
            // Keep running without file logging if filesystem creation fails.
        }

        if (out_.is_open()) {
            log("INFO", "日志文件已创建: " + logFilePath_.string(), "Log file created: " + logFilePath_.string());
        } else {
            std::cout << "[" << makeLogLineTime() << "] [WARN] "
                      << "日志文件创建失败，将仅输出到控制台 | Failed to create log file, console output only"
                      << std::endl;
        }
    }

    void log(const std::string &level, const std::string &cn, const std::string &en) {
        const std::string line =
            "[" + makeLogLineTime() + "] [" + level + "] " + cn + " | " + en;
        std::cout << line << std::endl;
        if (out_.is_open()) {
            out_ << line << std::endl;
            out_.flush();
        }
    }

private:
    std::filesystem::path logFilePath_;
    std::ofstream out_;
};

struct ControlCommand {
    std::optional<int> ledIdle;
    std::optional<int> m1Dir;
    std::optional<int> m1Speed;
    std::optional<int> m1TimeMs;
    std::optional<int> m2Dir;
    std::optional<int> m2Speed;
    std::optional<int> m2TimeMs;

    bool hasAny() const {
        return ledIdle.has_value() || m1Dir.has_value() || m1Speed.has_value() || m1TimeMs.has_value() ||
               m2Dir.has_value() || m2Speed.has_value() || m2TimeMs.has_value();
    }
};

bool parseIntegerStrict(const std::string &text, int &value) {
    try {
        std::size_t pos = 0;
        int tmp = std::stoi(text, &pos, 10);
        if (pos != text.size()) {
            return false;
        }
        value = tmp;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseBooleanLike(const std::string &text, int &value) {
    const std::string t = toLower(trim(text));
    if (t == "1" || t == "on" || t == "true" || t == "yes") {
        value = 1;
        return true;
    }
    if (t == "0" || t == "off" || t == "false" || t == "no") {
        value = 0;
        return true;
    }
    return false;
}

bool parseDirectionLike(const std::string &text, int &value) {
    const std::string t = toLower(trim(text));
    if (t == "0" || t == "forward" || t == "fwd" || t == "cw") {
        value = 0;
        return true;
    }
    if (t == "1" || t == "reverse" || t == "rev" || t == "ccw") {
        value = 1;
        return true;
    }
    return false;
}

std::string normalizeKey(std::string key) {
    key = toLower(trim(key));
    std::replace(key.begin(), key.end(), '-', '_');
    return key;
}

bool assignCommandField(ControlCommand &cmd,
                        const std::string &rawKey,
                        const std::string &rawValue,
                        std::string &errCn,
                        std::string &errEn) {
    const std::string key = normalizeKey(rawKey);
    const std::string value = trim(rawValue);

    int intValue = 0;
    int dirValue = 0;

    if (key == "led" || key == "led_idle") {
        if (!parseBooleanLike(value, intValue)) {
            errCn = "LED 参数无效，应为 on/off/1/0";
            errEn = "Invalid LED argument, expected on/off/1/0";
            return false;
        }
        cmd.ledIdle = intValue;
        return true;
    }

    if (key == "m1_dir" || key == "m1_direction") {
        if (!parseDirectionLike(value, dirValue)) {
            errCn = "马达1方向无效，应为 forward/reverse 或 0/1";
            errEn = "Invalid motor1 direction, expected forward/reverse or 0/1";
            return false;
        }
        cmd.m1Dir = dirValue;
        return true;
    }

    if (key == "m2_dir" || key == "m2_direction") {
        if (!parseDirectionLike(value, dirValue)) {
            errCn = "马达2方向无效，应为 forward/reverse 或 0/1";
            errEn = "Invalid motor2 direction, expected forward/reverse or 0/1";
            return false;
        }
        cmd.m2Dir = dirValue;
        return true;
    }

    if (key == "m1_speed") {
        if (!parseIntegerStrict(value, intValue) || intValue < 0 || intValue > 3000) {
            errCn = "马达1速度无效，范围应为 0-3000";
            errEn = "Invalid motor1 speed, range should be 0-3000";
            return false;
        }
        cmd.m1Speed = intValue;
        return true;
    }

    if (key == "m2_speed") {
        if (!parseIntegerStrict(value, intValue) || intValue < 0 || intValue > 3000) {
            errCn = "马达2速度无效，范围应为 0-3000";
            errEn = "Invalid motor2 speed, range should be 0-3000";
            return false;
        }
        cmd.m2Speed = intValue;
        return true;
    }

    if (key == "m1_time" || key == "m1_time_ms" || key == "m1_duration_ms") {
        if (!parseIntegerStrict(value, intValue) || intValue < 0 || intValue > 60000) {
            errCn = "马达1时间无效，范围应为 0-60000 毫秒";
            errEn = "Invalid motor1 time, range should be 0-60000 milliseconds";
            return false;
        }
        cmd.m1TimeMs = intValue;
        return true;
    }

    if (key == "m2_time" || key == "m2_time_ms" || key == "m2_duration_ms") {
        if (!parseIntegerStrict(value, intValue) || intValue < 0 || intValue > 60000) {
            errCn = "马达2时间无效，范围应为 0-60000 毫秒";
            errEn = "Invalid motor2 time, range should be 0-60000 milliseconds";
            return false;
        }
        cmd.m2TimeMs = intValue;
        return true;
    }

    errCn = "不支持的参数键: " + key;
    errEn = "Unsupported argument key: " + key;
    return false;
}

std::string buildJsonFromCommand(const ControlCommand &cmd) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;

    auto appendField = [&](const std::string &k, const std::string &v) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << "\"" << k << "\":" << v;
    };

    if (cmd.ledIdle.has_value()) {
        appendField("led_idle", *cmd.ledIdle ? "1" : "0");
        appendField("led", *cmd.ledIdle ? "1" : "0");
    }
    if (cmd.m1Dir.has_value()) {
        appendField("m1_dir", std::to_string(*cmd.m1Dir));
    }
    if (cmd.m1Speed.has_value()) {
        appendField("m1_speed", std::to_string(*cmd.m1Speed));
    }
    if (cmd.m1TimeMs.has_value()) {
        appendField("m1_duration_ms", std::to_string(*cmd.m1TimeMs));
        appendField("m1_time", std::to_string(*cmd.m1TimeMs));
    }
    if (cmd.m2Dir.has_value()) {
        appendField("m2_dir", std::to_string(*cmd.m2Dir));
    }
    if (cmd.m2Speed.has_value()) {
        appendField("m2_speed", std::to_string(*cmd.m2Speed));
    }
    if (cmd.m2TimeMs.has_value()) {
        appendField("m2_duration_ms", std::to_string(*cmd.m2TimeMs));
        appendField("m2_time", std::to_string(*cmd.m2TimeMs));
    }

    oss << "}";
    return oss.str();
}

std::string squashJsonForUart(const std::string &json) {
    std::string out;
    out.reserve(json.size());
    for (char c : json) {
        if (c == '\r' || c == '\n' || c == '\t') {
            continue;
        }
        out.push_back(c);
    }
    return trim(out);
}

bool readFileToString(const std::string &path, std::string &content) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    content = ss.str();
    return true;
}

class SerialPort {
public:
    ~SerialPort() {
        close();
    }

    bool openPort(const std::string &portName, int baudRate, std::string &err) {
#ifdef _WIN32
        return openPortWindows(portName, baudRate, err);
#else
        return openPortPosix(portName, baudRate, err);
#endif
    }

    bool writeLine(const std::string &line, std::string &err) {
        std::string frame = line;
        if (frame.empty() || frame.back() != '\n') {
            frame += "\r\n";
        }

#ifdef _WIN32
        return writeWindows(frame, err);
#else
        return writePosix(frame, err);
#endif
    }

    bool readLine(std::string &line, int timeoutMs, std::string &err) {
#ifdef _WIN32
        return readLineWindows(line, timeoutMs, err);
#else
        return readLinePosix(line, timeoutMs, err);
#endif
    }

    void close() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;

    static bool isComPortName(const std::string &name) {
        if (name.size() < 4) {
            return false;
        }
        const std::string lower = toLower(name);
        if (lower.rfind("com", 0) != 0) {
            return false;
        }
        for (std::size_t i = 3; i < name.size(); ++i) {
            if (std::isdigit(static_cast<unsigned char>(name[i])) == 0) {
                return false;
            }
        }
        return true;
    }

    static std::string normalizeWindowsPortName(const std::string &name) {
        if (name.rfind("\\\\.\\", 0) == 0) {
            return name;
        }
        if (isComPortName(name)) {
            return "\\\\.\\" + name;
        }
        return name;
    }

    bool openPortWindows(const std::string &portName, int baudRate, std::string &err) {
        const std::string devName = normalizeWindowsPortName(portName);

        handle_ = CreateFileA(devName.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            err = "CreateFile failed, error=" + std::to_string(GetLastError());
            return false;
        }

        SetupComm(handle_, 4096, 4096);

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle_, &dcb)) {
            err = "GetCommState failed, error=" + std::to_string(GetLastError());
            close();
            return false;
        }

        dcb.BaudRate = static_cast<DWORD>(baudRate);
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;

        if (!SetCommState(handle_, &dcb)) {
            err = "SetCommState failed, error=" + std::to_string(GetLastError());
            close();
            return false;
        }

        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 200;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 200;
        timeouts.WriteTotalTimeoutMultiplier = 10;

        if (!SetCommTimeouts(handle_, &timeouts)) {
            err = "SetCommTimeouts failed, error=" + std::to_string(GetLastError());
            close();
            return false;
        }

        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }

    bool writeWindows(const std::string &data, std::string &err) {
        if (handle_ == INVALID_HANDLE_VALUE) {
            err = "Serial port is not open";
            return false;
        }

        std::size_t sent = 0;
        while (sent < data.size()) {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(data.size() - sent);
            if (!WriteFile(handle_, data.data() + sent, chunk, &written, nullptr)) {
                err = "WriteFile failed, error=" + std::to_string(GetLastError());
                return false;
            }
            sent += static_cast<std::size_t>(written);
        }
        return true;
    }

    bool readLineWindows(std::string &line, int timeoutMs, std::string &err) {
        if (handle_ == INVALID_HANDLE_VALUE) {
            err = "Serial port is not open";
            return false;
        }

        line.clear();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            char ch = 0;
            DWORD bytesRead = 0;
            if (!ReadFile(handle_, &ch, 1, &bytesRead, nullptr)) {
                err = "ReadFile failed, error=" + std::to_string(GetLastError());
                return false;
            }

            if (bytesRead == 0) {
                continue;
            }

            if (ch == '\n') {
                return true;
            }
            if (ch != '\r') {
                line.push_back(ch);
            }
        }

        err = "Read timeout";
        return false;
    }
#else
    int fd_ = -1;

    static speed_t baudToPosix(int baudRate) {
        switch (baudRate) {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
#ifdef B230400
        case 230400:
            return B230400;
#endif
        default:
            return static_cast<speed_t>(0);
        }
    }

    bool openPortPosix(const std::string &portName, int baudRate, std::string &err) {
        fd_ = ::open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (fd_ < 0) {
            err = "open failed: " + std::string(std::strerror(errno));
            return false;
        }

        struct termios tty {};
        if (tcgetattr(fd_, &tty) != 0) {
            err = "tcgetattr failed: " + std::string(std::strerror(errno));
            close();
            return false;
        }

        const speed_t speed = baudToPosix(baudRate);
        if (speed == 0) {
            err = "unsupported baud rate on POSIX";
            close();
            return false;
        }

        if (cfsetispeed(&tty, speed) != 0 || cfsetospeed(&tty, speed) != 0) {
            err = "cfset speed failed: " + std::string(std::strerror(errno));
            close();
            return false;
        }

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
        tty.c_iflag &= static_cast<tcflag_t>(~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON));
        tty.c_oflag &= static_cast<tcflag_t>(~OPOST);
        tty.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL | ICANON | ISIG | IEXTEN));

        tty.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
        tty.c_cflag &= static_cast<tcflag_t>(~(PARENB | PARODD));
        tty.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
        tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);

        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            err = "tcsetattr failed: " + std::string(std::strerror(errno));
            close();
            return false;
        }

        tcflush(fd_, TCIOFLUSH);
        return true;
    }

    bool writePosix(const std::string &data, std::string &err) {
        if (fd_ < 0) {
            err = "Serial port is not open";
            return false;
        }

        std::size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = ::write(fd_, data.data() + sent, data.size() - sent);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                err = "write failed: " + std::string(std::strerror(errno));
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }

        return true;
    }

    bool readLinePosix(std::string &line, int timeoutMs, std::string &err) {
        if (fd_ < 0) {
            err = "Serial port is not open";
            return false;
        }

        line.clear();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(fd_, &readSet);

            const auto now = std::chrono::steady_clock::now();
            const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            long waitMs = remain.count();
            if (waitMs < 0) {
                waitMs = 0;
            }
            if (waitMs > 200) {
                waitMs = 200;
            }

            timeval tv{};
            tv.tv_sec = waitMs / 1000;
            tv.tv_usec = (waitMs % 1000) * 1000;

            const int ready = select(fd_ + 1, &readSet, nullptr, nullptr, &tv);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                err = "select failed: " + std::string(std::strerror(errno));
                return false;
            }
            if (ready == 0) {
                continue;
            }

            char ch = 0;
            const ssize_t n = ::read(fd_, &ch, 1);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                err = "read failed: " + std::string(std::strerror(errno));
                return false;
            }
            if (n == 0) {
                continue;
            }

            if (ch == '\n') {
                return true;
            }
            if (ch != '\r') {
                line.push_back(ch);
            }
        }

        err = "Read timeout";
        return false;
    }
#endif
};

bool ackIsSuccess(const std::string &ack) {
    const std::string lower = toLower(ack);
    return lower.find("\"ok\":true") != std::string::npos || lower.find("\"ok\":1") != std::string::npos ||
           lower == "ok" || lower.find("success") != std::string::npos;
}

bool ackIsFailure(const std::string &ack) {
    const std::string lower = toLower(ack);
    return lower.find("\"ok\":false") != std::string::npos || lower.find("\"ok\":0") != std::string::npos ||
           lower.find("fail") != std::string::npos || lower.find("error") != std::string::npos;
}

bool sendAndReceive(SerialPort &serial, const std::string &jsonCmd, Logger &logger) {
    std::string err;

    logger.log("INFO", "发送指令: " + jsonCmd, "Sending command: " + jsonCmd);
    if (!serial.writeLine(jsonCmd, err)) {
        logger.log("ERROR", "串口发送失败: " + err, "Serial write failed: " + err);
        return false;
    }

    std::string ack;
    if (!serial.readLine(ack, 3000, err)) {
        if (toLower(err).find("timeout") != std::string::npos) {
            logger.log("WARN",
                       "未收到STM32回复（超时），兼容模式下按发送成功处理",
                       "No STM32 reply (timeout), treated as send success in compatibility mode");
            return true;
        }
        logger.log("ERROR", "串口读取失败: " + err, "Serial read failed: " + err);
        return false;
    }

    logger.log("INFO", "收到STM32回复: " + ack, "Received STM32 reply: " + ack);

    if (ackIsFailure(ack)) {
        logger.log("WARN", "设备返回失败", "Device reported failure");
        return false;
    }

    if (ackIsSuccess(ack)) {
        logger.log("INFO", "设备执行成功", "Device execution succeeded");
        return true;
    }

    logger.log("WARN", "设备返回未知状态，兼容模式下按成功处理", "Device returned unknown status, treated as success");
    return true;
}

struct Options {
    std::string port;
    int baud = 115200;
    bool interactive = false;
    std::optional<std::string> jsonFile;
    std::optional<std::string> inlineJson;
    ControlCommand cliCommand;
    bool help = false;
};

void printUsage() {
    std::cout
        << "用法 / Usage:\n"
        << "  rpi5_stm32_controller --port <串口> [模式参数]\n"
        << "  rpi5_stm32_controller --device <串口> [模式参数]  (兼容旧版)\n\n"
        << "模式参数 / Mode options:\n"
        << "  --interactive                    交互模式（默认无命令参数时进入） / Interactive mode\n"
        << "  --json-file <path>               从JSON文件读取控制指令 / Read command from JSON file\n"
        << "  --json '<json>'                  直接传入JSON字符串 / Inline JSON string\n"
        << "  --led-idle <on|off|1|0>          LED空闲状态 / Idle LED state\n"
        << "  --led <on|off|1|0>               兼容旧版 LED 参数 / Legacy LED option\n"
        << "  --m1-dir <forward|reverse|0|1>   马达1方向 / Motor1 direction\n"
        << "  --m1-speed <0-3000>              马达1速度 / Motor1 speed\n"
        << "  --m1-time <ms>                   马达1持续时间 / Motor1 duration ms\n"
        << "  --m2-dir <forward|reverse|0|1>   马达2方向 / Motor2 direction\n"
        << "  --m2-speed <0-3000>              马达2速度 / Motor2 speed\n"
        << "  --m2-time <ms>                   马达2持续时间 / Motor2 duration ms\n"
        << "  --baud <baudrate>                波特率(默认115200) / Baudrate (default 115200)\n"
        << "  --help                           显示帮助 / Show help\n\n"
        << "交互模式输入示例 / Interactive examples:\n"
        << "  led=on m1_dir=forward m1_speed=1000 m1_time=1500\n"
        << "  m2_dir=reverse m2_speed=800 m2_time=1000\n"
        << "  {\"led_idle\":1,\"m1_dir\":\"forward\",\"m1_speed\":1200,\"m1_duration_ms\":1000}\n"
        << std::endl;
}

bool parseOptions(int argc, char **argv, Options &opts, std::string &errCn, std::string &errEn) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto needValue = [&](const std::string &name) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                errCn = "参数缺少值: " + name;
                errEn = "Missing value for option: " + name;
                return std::nullopt;
            }
            ++i;
            return std::string(argv[i]);
        };

        if (arg == "--help" || arg == "-h") {
            opts.help = true;
            return true;
        }

        if (arg == "--interactive") {
            opts.interactive = true;
            continue;
        }

        if (arg == "--port" || arg == "--device") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            opts.port = *v;
            continue;
        }

        if (arg == "--baud") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            int baud = 0;
            if (!parseIntegerStrict(*v, baud) || baud <= 0) {
                errCn = "波特率无效";
                errEn = "Invalid baudrate";
                return false;
            }
            opts.baud = baud;
            continue;
        }

        if (arg == "--json-file") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            opts.jsonFile = *v;
            continue;
        }

        if (arg == "--json") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            opts.inlineJson = *v;
            continue;
        }

        if (arg == "--led-idle" || arg == "--led") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            if (!assignCommandField(opts.cliCommand, "led_idle", *v, errCn, errEn)) {
                return false;
            }
            continue;
        }

        if (arg == "--m1-dir") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            if (!assignCommandField(opts.cliCommand, "m1_dir", *v, errCn, errEn)) {
                return false;
            }
            continue;
        }

        if (arg == "--m1-speed") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            if (!assignCommandField(opts.cliCommand, "m1_speed", *v, errCn, errEn)) {
                return false;
            }
            continue;
        }

        if (arg == "--m1-time") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            if (!assignCommandField(opts.cliCommand, "m1_time_ms", *v, errCn, errEn)) {
                return false;
            }
            continue;
        }

        if (arg == "--m2-dir") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            if (!assignCommandField(opts.cliCommand, "m2_dir", *v, errCn, errEn)) {
                return false;
            }
            continue;
        }

        if (arg == "--m2-speed") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            if (!assignCommandField(opts.cliCommand, "m2_speed", *v, errCn, errEn)) {
                return false;
            }
            continue;
        }

        if (arg == "--m2-time") {
            auto v = needValue(arg);
            if (!v.has_value()) {
                return false;
            }
            if (!assignCommandField(opts.cliCommand, "m2_time_ms", *v, errCn, errEn)) {
                return false;
            }
            continue;
        }

        errCn = "未知参数: " + arg;
        errEn = "Unknown option: " + arg;
        return false;
    }

    if (opts.jsonFile.has_value() && (opts.inlineJson.has_value() || opts.cliCommand.hasAny())) {
        errCn = "--json-file 不能与 --json 或其他控制参数同时使用";
        errEn = "--json-file cannot be used together with --json or direct control options";
        return false;
    }

    if (opts.inlineJson.has_value() && opts.cliCommand.hasAny()) {
        errCn = "--json 不能与其他控制参数同时使用";
        errEn = "--json cannot be used together with direct control options";
        return false;
    }

    return true;
}

bool buildJsonFromInteractiveLine(const std::string &line,
                                  std::string &json,
                                  std::string &errCn,
                                  std::string &errEn) {
    const std::string content = trim(line);
    if (content.empty()) {
        errCn = "输入为空";
        errEn = "Input is empty";
        return false;
    }

    if (!content.empty() && content.front() == '{') {
        json = squashJsonForUart(content);
        return true;
    }

    std::istringstream iss(content);
    std::string token;
    ControlCommand cmd;

    while (iss >> token) {
        if (toLower(token) == "set") {
            continue;
        }

        const std::size_t pos = token.find('=');
        if (pos == std::string::npos || pos == 0 || pos == token.size() - 1) {
            errCn = "交互输入格式错误，应为 key=value";
            errEn = "Invalid interactive format, expected key=value";
            return false;
        }

        const std::string key = token.substr(0, pos);
        const std::string value = token.substr(pos + 1);
        if (!assignCommandField(cmd, key, value, errCn, errEn)) {
            return false;
        }
    }

    if (!cmd.hasAny()) {
        errCn = "未解析到任何有效控制字段";
        errEn = "No valid control field was parsed";
        return false;
    }

    json = buildJsonFromCommand(cmd);
    return true;
}

} // namespace

int main(int argc, char **argv) {
    Logger logger;

    Options opts;
    std::string errCn;
    std::string errEn;

    if (!parseOptions(argc, argv, opts, errCn, errEn)) {
        logger.log("ERROR", errCn, errEn);
        printUsage();
        return 1;
    }

    if (opts.help) {
        printUsage();
        return 0;
    }

    if (opts.port.empty()) {
#ifdef _WIN32
        std::cout << "请输入串口号（例如 COM3）/ Please enter serial port (e.g. COM3): ";
#else
        std::cout << "请输入串口路径（例如 /dev/ttyUSB0）/ Please enter serial port (e.g. /dev/ttyUSB0): ";
#endif
        std::getline(std::cin, opts.port);
        opts.port = trim(opts.port);
    }

    if (opts.port.empty()) {
        logger.log("ERROR", "串口不能为空", "Serial port cannot be empty");
        return 1;
    }

    SerialPort serial;
    std::string openErr;
    if (!serial.openPort(opts.port, opts.baud, openErr)) {
        logger.log("ERROR", "打开串口失败: " + openErr, "Failed to open serial port: " + openErr);
        return 1;
    }

    logger.log("INFO", "串口已打开: " + opts.port + "，波特率=" + std::to_string(opts.baud),
               "Serial port opened: " + opts.port + ", baud=" + std::to_string(opts.baud));

    const bool hasNonInteractiveInput =
        opts.jsonFile.has_value() || opts.inlineJson.has_value() || opts.cliCommand.hasAny();
    const bool interactiveMode = opts.interactive || !hasNonInteractiveInput;

    if (interactiveMode) {
        logger.log("INFO", "进入交互模式，输入 exit 或 quit 退出", "Entering interactive mode, type exit or quit to leave");
        logger.log("INFO", "可输入 JSON，或 key=value 组合", "You can type JSON or key=value pairs");

        while (true) {
            std::cout << "\n命令> / Command> ";
            std::string line;
            if (!std::getline(std::cin, line)) {
                break;
            }

            const std::string t = trim(line);
            const std::string lower = toLower(t);
            if (lower == "exit" || lower == "quit") {
                logger.log("INFO", "退出交互模式", "Leaving interactive mode");
                break;
            }

            if (lower == "help") {
                printUsage();
                continue;
            }

            if (t.empty()) {
                continue;
            }

            std::string jsonCmd;
            std::string parseErrCn;
            std::string parseErrEn;
            if (!buildJsonFromInteractiveLine(t, jsonCmd, parseErrCn, parseErrEn)) {
                logger.log("ERROR", parseErrCn, parseErrEn);
                continue;
            }

            sendAndReceive(serial, jsonCmd, logger);
        }

        return 0;
    }

    std::string jsonCmd;
    if (opts.jsonFile.has_value()) {
        std::string fileContent;
        if (!readFileToString(*opts.jsonFile, fileContent)) {
            logger.log("ERROR", "读取JSON文件失败: " + *opts.jsonFile,
                       "Failed to read JSON file: " + *opts.jsonFile);
            return 1;
        }
        jsonCmd = squashJsonForUart(fileContent);
        logger.log("INFO", "已从文件加载JSON: " + *opts.jsonFile,
                   "JSON loaded from file: " + *opts.jsonFile);
    } else if (opts.inlineJson.has_value()) {
        jsonCmd = squashJsonForUart(*opts.inlineJson);
    } else {
        jsonCmd = buildJsonFromCommand(opts.cliCommand);
    }

    if (jsonCmd.empty() || jsonCmd == "{}") {
        logger.log("ERROR", "没有可发送的有效控制指令", "No valid control command to send");
        return 1;
    }

    const bool ok = sendAndReceive(serial, jsonCmd, logger);
    return ok ? 0 : 2;
}
