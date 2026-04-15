#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // 重要：禁用Windows的min/max宏

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <algorithm>

// Windows头文件
#include <windows.h>

// 修复：正确定义ssize_t
#ifdef _MSC_VER
typedef SSIZE_T ssize_t;
#else
typedef long ssize_t;
#endif

// 控制命令结构体
struct ControlCommand
{
    int m1Time;
    int m1Direction;
    int m1Speed;
    int m2Time;
    int m2Direction;
    int m2Speed;
    int led;

    ControlCommand() : m1Time(0), m1Direction(0), m1Speed(0),
        m2Time(0), m2Direction(0), m2Speed(0), led(0) {}
};

// 函数声明
void printUsage(const char* program);
bool parseIntStrict(const std::string& text, int* value);
int promptInt(const std::string& prompt, int minValue, int maxValue);
HANDLE serial_open(const std::string& device, std::string* err);
bool serial_configure(HANDLE s, std::string* err);
bool check_serial_status(HANDLE s, std::string* err);
ssize_t serial_write(HANDLE s, const char* buf, size_t len);
ssize_t serial_read(HANDLE s, char* buf, size_t maxlen, int timeoutMs);
void serial_close(HANDLE s);
std::string readReply(HANDLE s, int timeoutMs);
std::string buildJson(const ControlCommand& cmd);
bool findJsonInt(const std::string& json, const std::string& key, int* value);
bool loadCommandFromJsonFile(const std::string& jsonFilePath, ControlCommand* cmd, std::string* error);
bool validateRange(const std::string& name, int value, int minValue, int maxValue, std::string* error);
bool validateCommand(const ControlCommand& cmd, std::string* error);
bool parseCommandLine(int argc, char* argv[], std::string& serialDevice, std::string& jsonFilePath,
    ControlCommand& cmd, bool& useJsonFile, bool& useInteractive);
bool executeCommand(HANDLE s, const ControlCommand& cmd);
void interactiveMode(const std::string& serialDevice, ControlCommand& cmd);

// 打印使用说明
void printUsage(const char* program)
{
    std::cout << "========================================" << std::endl;
    std::cout << "STM32电机控制器 - 串口控制程序" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "用法:" << std::endl;
    std::cout << "  " << program << " [选项]" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  --help, -h              显示此帮助信息" << std::endl;
    std::cout << "  --device COMx           指定串口设备 (默认: COM7)" << std::endl;
    std::cout << "  --json-file FILE        从JSON文件读取控制参数" << std::endl;
    std::cout << "  --m1-time N             电机1运行时间 (ms, 0~60000)" << std::endl;
    std::cout << "  --m1-dir N              电机1方向 (0=正转, 1=反转)" << std::endl;
    std::cout << "  --m1-speed N            电机1速度 (0~3000)" << std::endl;
    std::cout << "  --m2-time N             电机2运行时间 (ms, 0~60000)" << std::endl;
    std::cout << "  --m2-dir N              电机2方向 (0=正转, 1=反转)" << std::endl;
    std::cout << "  --m2-speed N            电机2速度 (0~3000)" << std::endl;
    std::cout << "  --led N                 LED状态 (0=灭, 1=亮)" << std::endl;
    std::cout << std::endl;
    std::cout << "说明:" << std::endl;
    std::cout << "  1) 不带任何参数时进入交互模式" << std::endl;
    std::cout << "  2) 可以只设置部分参数，未设置的参数默认为0" << std::endl;
    std::cout << "  3) --json-file 不能与其他控制参数同时使用" << std::endl;
    std::cout << "  4) 示例: " << program << " --device COM7 --led 1" << std::endl;
    std::cout << "========================================" << std::endl;
}

// 严格解析整数
bool parseIntStrict(const std::string& text, int* value)
{
    if (value == nullptr) return false;
    if (text.empty()) return false;

    char* endptr = nullptr;
    long num = std::strtol(text.c_str(), &endptr, 10);

    if (endptr == nullptr || *endptr != '\0') return false;
    if (num > 2147483647L || num < -2147483648L) return false;

    *value = static_cast<int>(num);
    return true;
}

// 交互式输入整数 - 修复第620行问题
int promptInt(const std::string& prompt, int minValue, int maxValue)
{
    int value;
    std::string input;

    while (true)
    {
        std::cout << prompt;
        std::getline(std::cin, input);

        if (input.empty())
        {
            std::cout << "输入不能为空，请重新输入。" << std::endl;
            continue;
        }

        if (!parseIntStrict(input, &value))
        {
            std::cout << "输入无效，请输入整数。" << std::endl;
            continue;
        }

        if (value < minValue || value > maxValue)
        {
            std::cout << "输入超出范围，允许范围: " << minValue << " ~ " << maxValue << std::endl;
            continue;
        }
        return value;
    }
}

// 打开串口
HANDLE serial_open(const std::string& device, std::string* err)
{
    std::string dev = device;

    // 处理COM端口号大于9的情况
    if (dev.length() >= 3 &&
        (dev[0] == 'C' || dev[0] == 'c') &&
        (dev[1] == 'O' || dev[1] == 'o') &&
        (dev[2] == 'M' || dev[2] == 'm'))
    {
        dev = "\\\\.\\" + dev;
    }

    HANDLE h = CreateFileA(dev.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (h == INVALID_HANDLE_VALUE)
    {
        if (err)
        {
            DWORD errorCode = GetLastError();
            std::ostringstream oss;
            oss << "CreateFile失败 (错误码: " << errorCode << ")";
            *err = oss.str();
        }
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

// 配置串口参数
bool serial_configure(HANDLE s, std::string* err)
{
    if (s == INVALID_HANDLE_VALUE)
    {
        if (err) *err = "无效的串口句柄";
        return false;
    }

    DCB dcb;
    SecureZeroMemory(&dcb, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(s, &dcb))
    {
        if (err) *err = "GetCommState失败";
        return false;
    }

    // 配置串口参数
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(s, &dcb))
    {
        if (err) *err = "SetCommState失败";
        return false;
    }

    // 配置超时参数
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.ReadTotalTimeoutConstant = 200;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 200;

    if (!SetCommTimeouts(s, &timeouts))
    {
        if (err) *err = "SetCommTimeouts失败";
        return false;
    }

    PurgeComm(s, PURGE_TXCLEAR | PURGE_RXCLEAR);
    return true;
}

// 检查串口状态
bool check_serial_status(HANDLE s, std::string* err)
{
    DWORD errors;
    COMSTAT status;

    if (!ClearCommError(s, &errors, &status))
    {
        if (err) *err = "ClearCommError失败";
        return false;
    }

    if (errors != 0)
    {
        if (err)
        {
            std::ostringstream oss;
            oss << "串口错误 (错误码: " << errors << ")";
            *err = oss.str();
        }
        return false;
    }

    return true;
}

// 串口写入
ssize_t serial_write(HANDLE s, const char* buf, size_t len)
{
    DWORD written = 0;
    if (!WriteFile(s, buf, static_cast<DWORD>(len), &written, NULL))
        return -1;
    return static_cast<ssize_t>(written);
}

// 串口读取
ssize_t serial_read(HANDLE s, char* buf, size_t maxlen, int timeoutMs)
{
    DWORD readBytes = 0;
    COMMTIMEOUTS originalTimeouts, tempTimeouts;

    if (maxlen == 0 || buf == NULL) return -1;

    if (!GetCommTimeouts(s, &originalTimeouts))
        return -1;

    tempTimeouts = originalTimeouts;
    tempTimeouts.ReadTotalTimeoutConstant = timeoutMs;
    tempTimeouts.ReadTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(s, &tempTimeouts))
        return -1;

    BOOL result = ReadFile(s, buf, static_cast<DWORD>(maxlen), &readBytes, NULL);
    SetCommTimeouts(s, &originalTimeouts);

    if (!result)
    {
        DWORD err = GetLastError();
        if (err == ERROR_TIMEOUT)
            return 0;
        return -1;
    }

    return static_cast<ssize_t>(readBytes);
}

// 关闭串口
void serial_close(HANDLE s)
{
    if (s != INVALID_HANDLE_VALUE)
    {
        PurgeComm(s, PURGE_TXCLEAR | PURGE_RXCLEAR);
        CloseHandle(s);
    }
}

// 读取STM32回复
std::string readReply(HANDLE s, int timeoutMs)
{
    char buf[256];
    std::string out;
    int elapsed = 0;
    const int step = 100;

    while (elapsed < timeoutMs)
    {
        ssize_t n = serial_read(s, buf, sizeof(buf) - 1, step);
        if (n < 0) break;

        if (n > 0)
        {
            buf[n] = '\0';
            out += buf;
            if (out.find("\r\n") != std::string::npos)
                break;
        }
        elapsed += step;
    }

    while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
        out.pop_back();

    return out;
}

// 构建JSON字符串
std::string buildJson(const ControlCommand& cmd)
{
    std::ostringstream oss;
    oss << "{"
        << "\"m1_time\":" << cmd.m1Time << ","
        << "\"m1_dir\":" << cmd.m1Direction << ","
        << "\"m1_speed\":" << cmd.m1Speed << ","
        << "\"m2_time\":" << cmd.m2Time << ","
        << "\"m2_dir\":" << cmd.m2Direction << ","
        << "\"m2_speed\":" << cmd.m2Speed << ","
        << "\"led\":" << cmd.led
        << "}";
    return oss.str();
}

// 从JSON中查找整数值
bool findJsonInt(const std::string& json, const std::string& key, int* value)
{
    size_t keyPos = json.find(key);
    if (keyPos == std::string::npos) return false;

    size_t colonPos = json.find(':', keyPos + key.size());
    if (colonPos == std::string::npos) return false;

    size_t start = colonPos + 1;
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t' ||
        json[start] == '\r' || json[start] == '\n'))
        start++;

    if (start >= json.size()) return false;

    bool negative = false;
    if (json[start] == '-')
    {
        negative = true;
        start++;
    }

    int result = 0;
    bool hasDigit = false;
    while (start < json.size() && json[start] >= '0' && json[start] <= '9')
    {
        hasDigit = true;
        result = result * 10 + (json[start] - '0');
        start++;
    }

    if (!hasDigit) return false;
    if (negative) result = -result;

    *value = result;
    return true;
}

// 从JSON文件加载命令
bool loadCommandFromJsonFile(const std::string& jsonFilePath, ControlCommand* cmd, std::string* error)
{
    std::ifstream ifs(jsonFilePath.c_str());
    if (!ifs.is_open())
    {
        if (error) *error = "无法打开JSON文件: " + jsonFilePath;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    if (content.empty())
    {
        if (error) *error = "JSON文件为空";
        return false;
    }

    *cmd = ControlCommand();

    int foundCount = 0;
    int tempValue;

    if (findJsonInt(content, "\"m1_time\"", &tempValue)) { cmd->m1Time = tempValue; foundCount++; }
    if (findJsonInt(content, "\"m1_dir\"", &tempValue)) { cmd->m1Direction = tempValue; foundCount++; }
    if (findJsonInt(content, "\"m1_speed\"", &tempValue)) { cmd->m1Speed = tempValue; foundCount++; }
    if (findJsonInt(content, "\"m2_time\"", &tempValue)) { cmd->m2Time = tempValue; foundCount++; }
    if (findJsonInt(content, "\"m2_dir\"", &tempValue)) { cmd->m2Direction = tempValue; foundCount++; }
    if (findJsonInt(content, "\"m2_speed\"", &tempValue)) { cmd->m2Speed = tempValue; foundCount++; }
    if (findJsonInt(content, "\"led\"", &tempValue)) { cmd->led = tempValue; foundCount++; }

    if (foundCount == 0)
    {
        if (error) *error = "JSON文件中没有找到可识别字段";
        return false;
    }

    std::cout << "从JSON文件加载了 " << foundCount << " 个参数" << std::endl;
    return true;
}

// 验证范围
bool validateRange(const std::string& name, int value, int minValue, int maxValue, std::string* error)
{
    if (value < minValue || value > maxValue)
    {
        if (error)
        {
            std::ostringstream oss;
            oss << name << " 超出范围: " << value << " (允许 " << minValue << "~" << maxValue << ")";
            *error = oss.str();
        }
        return false;
    }
    return true;
}

// 验证命令
bool validateCommand(const ControlCommand& cmd, std::string* error)
{
    if (!validateRange("m1_time", cmd.m1Time, 0, 60000, error)) return false;
    if (!validateRange("m1_dir", cmd.m1Direction, 0, 1, error)) return false;
    if (!validateRange("m1_speed", cmd.m1Speed, 0, 3000, error)) return false;
    if (!validateRange("m2_time", cmd.m2Time, 0, 60000, error)) return false;
    if (!validateRange("m2_dir", cmd.m2Direction, 0, 1, error)) return false;
    if (!validateRange("m2_speed", cmd.m2Speed, 0, 3000, error)) return false;
    if (!validateRange("led", cmd.led, 0, 1, error)) return false;
    return true;
}

// 解析命令行参数
bool parseCommandLine(int argc, char* argv[], std::string& serialDevice, std::string& jsonFilePath,
    ControlCommand& cmd, bool& useJsonFile, bool& useInteractive)
{
    bool hasDirectControlArg = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            return false;
        }
        else if (arg == "--device")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "错误: --device 需要指定设备路径" << std::endl;
                return false;
            }
            serialDevice = argv[++i];
        }
        else if (arg == "--json-file")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "错误: --json-file 需要指定文件路径" << std::endl;
                return false;
            }
            jsonFilePath = argv[++i];
            useJsonFile = true;
        }
        else if (arg == "--m1-time")
        {
            if (i + 1 >= argc || !parseIntStrict(argv[i + 1], &cmd.m1Time))
            {
                std::cerr << "错误: --m1-time 需要整数参数" << std::endl;
                return false;
            }
            ++i;
            hasDirectControlArg = true;
        }
        else if (arg == "--m1-dir")
        {
            if (i + 1 >= argc || !parseIntStrict(argv[i + 1], &cmd.m1Direction))
            {
                std::cerr << "错误: --m1-dir 需要整数参数(0或1)" << std::endl;
                return false;
            }
            ++i;
            hasDirectControlArg = true;
        }
        else if (arg == "--m1-speed")
        {
            if (i + 1 >= argc || !parseIntStrict(argv[i + 1], &cmd.m1Speed))
            {
                std::cerr << "错误: --m1-speed 需要整数参数(0~3000)" << std::endl;
                return false;
            }
            ++i;
            hasDirectControlArg = true;
        }
        else if (arg == "--m2-time")
        {
            if (i + 1 >= argc || !parseIntStrict(argv[i + 1], &cmd.m2Time))
            {
                std::cerr << "错误: --m2-time 需要整数参数" << std::endl;
                return false;
            }
            ++i;
            hasDirectControlArg = true;
        }
        else if (arg == "--m2-dir")
        {
            if (i + 1 >= argc || !parseIntStrict(argv[i + 1], &cmd.m2Direction))
            {
                std::cerr << "错误: --m2-dir 需要整数参数(0或1)" << std::endl;
                return false;
            }
            ++i;
            hasDirectControlArg = true;
        }
        else if (arg == "--m2-speed")
        {
            if (i + 1 >= argc || !parseIntStrict(argv[i + 1], &cmd.m2Speed))
            {
                std::cerr << "错误: --m2-speed 需要整数参数(0~3000)" << std::endl;
                return false;
            }
            ++i;
            hasDirectControlArg = true;
        }
        else if (arg == "--led")
        {
            if (i + 1 >= argc || !parseIntStrict(argv[i + 1], &cmd.led))
            {
                std::cerr << "错误: --led 需要整数参数(0或1)" << std::endl;
                return false;
            }
            ++i;
            hasDirectControlArg = true;
        }
        else
        {
            std::cerr << "错误: 未知参数 '" << arg << "'" << std::endl;
            return false;
        }
    }

    if (useJsonFile && hasDirectControlArg)
    {
        std::cerr << "错误: --json-file 不能与其他控制参数同时使用" << std::endl;
        return false;
    }

    useInteractive = (!useJsonFile && !hasDirectControlArg);
    return true;
}

// 执行命令
bool executeCommand(HANDLE s, const ControlCommand& cmd)
{
    std::string json = buildJson(cmd);
    std::string frame = json + "\r\n";

    std::cout << "发送命令: " << json << std::endl;

    ssize_t written = serial_write(s, frame.c_str(), frame.size());
    if (written < 0 || static_cast<size_t>(written) != frame.size())
    {
        std::cerr << "串口发送失败" << std::endl;
        return false;
    }

    std::cout << "命令已发送，等待STM32响应..." << std::endl;

    std::string reply = readReply(s, 3000);
    if (!reply.empty())
    {
        std::cout << "STM32响应: " << reply << std::endl;
        return true;
    }
    else
    {
        std::cout << "未收到STM32响应" << std::endl;
        return true;
    }
}

// 交互模式 - 修复第620行问题
void interactiveMode(const std::string& serialDevice, ControlCommand& cmd)
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "STM32电机控制器 - 交互模式" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "使用串口设备: " << serialDevice << std::endl;
    std::cout << std::endl;
    std::cout << "参数说明:" << std::endl;
    std::cout << "  - 方向: 0=正转, 1=反转" << std::endl;
    std::cout << "  - 速度范围: 0~3000" << std::endl;
    std::cout << "  - 时间范围: 0~60000 毫秒" << std::endl;
    std::cout << "  - LED: 0=灭, 1=亮" << std::endl;
    std::cout << std::endl;

    // 修复：避免使用 numeric_limits<streamsize>::max()
    std::cin.clear();
    std::cin.ignore(10000, '\n');  // 使用大数值代替 max()

    std::cout << "--- 电机1参数 ---" << std::endl;
    cmd.m1Time = promptInt("电机1运行时间 (ms, 0~60000): ", 0, 60000);
    cmd.m1Direction = promptInt("电机1方向 (0=正转, 1=反转): ", 0, 1);
    cmd.m1Speed = promptInt("电机1速度 (0~3000): ", 0, 3000);

    std::cout << std::endl << "--- 电机2参数 ---" << std::endl;
    cmd.m2Time = promptInt("电机2运行时间 (ms, 0~60000): ", 0, 60000);
    cmd.m2Direction = promptInt("电机2方向 (0=正转, 1=反转): ", 0, 1);
    cmd.m2Speed = promptInt("电机2速度 (0~3000): ", 0, 3000);

    std::cout << std::endl << "--- LED参数 ---" << std::endl;
    cmd.led = promptInt("LED状态 (0=灭, 1=亮): ", 0, 1);

    std::cout << std::endl;
}

// 主函数
int main(int argc, char* argv[])
{
    std::string serialDevice = "COM7";
    std::string jsonFilePath;
    ControlCommand cmd;
    bool useJsonFile = false;
    bool useInteractive = false;
    std::string error;

    if (!parseCommandLine(argc, argv, serialDevice, jsonFilePath, cmd, useJsonFile, useInteractive))
    {
        printUsage(argv[0]);
        system("pause");
        return 1;
    }

    if (useInteractive)
    {
        interactiveMode(serialDevice, cmd);
    }

    if (useJsonFile)
    {
        if (!loadCommandFromJsonFile(jsonFilePath, &cmd, &error))
        {
            std::cerr << "读取JSON失败: " << error << std::endl;
            system("pause");
            return 1;
        }
    }

    if (!validateCommand(cmd, &error))
    {
        std::cerr << "控制参数无效: " << error << std::endl;
        system("pause");
        return 1;
    }

    if (!useInteractive)
    {
        std::cout << "========================================" << std::endl;
        std::cout << "使用串口设备: " << serialDevice << std::endl;
        std::cout << "控制参数:" << std::endl;
        std::cout << "  电机1: 时间=" << cmd.m1Time << "ms, 方向=" << cmd.m1Direction
            << ", 速度=" << cmd.m1Speed << std::endl;
        std::cout << "  电机2: 时间=" << cmd.m2Time << "ms, 方向=" << cmd.m2Direction
            << ", 速度=" << cmd.m2Speed << std::endl;
        std::cout << "  LED: " << (cmd.led ? "亮" : "灭") << std::endl;
        std::cout << "========================================" << std::endl;
    }

    std::string serr;
    HANDLE s = serial_open(serialDevice, &serr);
    if (s == INVALID_HANDLE_VALUE)
    {
        std::cerr << "打开串口失败: " << serr << std::endl;
        system("pause");
        return 1;
    }

    if (!serial_configure(s, &serr))
    {
        std::cerr << "配置串口失败: " << serr << std::endl;
        serial_close(s);
        system("pause");
        return 1;
    }

    if (!check_serial_status(s, &serr))
    {
        std::cerr << "串口状态异常: " << serr << std::endl;
        serial_close(s);
        system("pause");
        return 1;
    }

    bool success = executeCommand(s, cmd);
    serial_close(s);

    if (!success)
    {
        std::cerr << "命令执行失败" << std::endl;
        system("pause");
        return 1;
    }

    std::cout << "\n程序执行完成！" << std::endl;
    system("pause");
    return 0;
}