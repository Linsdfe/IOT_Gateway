/**
 * @file logger.h
 * @brief 统一日志系统 - 支持多级别、双输出（控制台+文件）、线程安全
 *
 * 日志系统提供四个级别（DEBUG/INFO/WARN/ERROR），同时输出到 stdout 和日志文件。
 * 控制台输出带 ANSI 颜色（自动检测终端类型），文件输出为纯文本。
 * 日志格式：[时间戳] [级别] [模块标签] 消息内容
 *
 * 使用示例：
 * @code
 *   LOG_I("Sensor", "温度读取: %.1f C", temp);
 *   LOG_E("MQTT", "连接失败: %s", errstr);
 * @endcode
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>

/**
 * @brief 日志级别枚举
 *
 * 按严重程度递增：DEBUG < INFO < WARN < ERROR。
 * 设置某级别后，低于该级别的日志将被过滤。
 */
enum class LogLevel {
    DEBUG,  ///< 调试信息，仅开发阶段使用，包含详细的内部状态
    INFO,   ///< 常规运行信息，如模块初始化成功、传感器数据读取
    WARN,   ///< 警告信息，如非致命错误、重试操作、功能降级
    ERROR   ///< 错误信息，如硬件故障、连接失败、数据异常
};

/**
 * @brief 日志系统单例类
 *
 * 线程安全的日志管理器，支持：
 * - 四级日志过滤（DEBUG/INFO/WARN/ERROR）
 * - 双通道输出（控制台 + 文件）
 * - ANSI 颜色自动适配（检测 stdout 是否为终端）
 * - 行缓冲模式确保即时输出
 *
 * 设计为单例模式，全局唯一实例通过 Logger::instance() 获取。
 */
class Logger {
public:
    /**
     * @brief 获取日志系统全局单例
     * @return Logger& 单例引用
     */
    static Logger &instance();

    /**
     * @brief 设置日志过滤级别
     * @param level 最低输出级别，低于此级别的日志将被丢弃
     *
     * 例如设置为 WARN 后，DEBUG 和 INFO 级别的日志不会输出。
     */
    void setLevel(LogLevel level);

    /**
     * @brief 设置日志文件路径
     * @param path 日志文件的绝对路径
     *
     * 以追加模式打开文件，设置行缓冲（_IOLBF）确保每条日志即时写入。
     * 如果文件无法打开，仅输出到控制台，不影响程序运行。
     */
    void setLogFile(const std::string &path);

    /**
     * @brief 关闭日志文件并释放资源
     */
    void close();

    /**
     * @brief 写入一条日志
     * @param level 日志级别
     * @param tag   模块标签，用于标识日志来源（如 "Sensor"、"MQTT"）
     * @param fmt   printf 风格的格式字符串
     * @param ...   格式参数
     *
     * 内部流程：
     * 1. 级别过滤：低于设定级别的日志直接丢弃
     * 2. 格式化消息体
     * 3. 加锁保护，防止多线程输出交错
     * 4. 同时写入 stdout 和日志文件
     */
    void log(LogLevel level, const char *tag, const char *fmt, ...)
        __attribute__((format(printf, 4, 5)));

private:
    Logger();
    ~Logger();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    /** @brief 将日志级别转换为可读字符串 */
    const char *levelStr(LogLevel level);

    /** @brief 获取日志级别对应的 ANSI 颜色代码 */
    const char *levelColor(LogLevel level);

    /**
     * @brief 实际写入日志到 stdout 和文件
     * @param level 日志级别
     * @param tag   模块标签
     * @param msg   已格式化的消息字符串
     *
     * 输出格式：[2026-05-12 23:45:32] [INFO ] [Sensor] 温度读取成功
     * 控制台带颜色，文件为纯文本。
     */
    void writeLog(LogLevel level, const char *tag, const char *msg);

    LogLevel level_;    ///< 当前日志过滤级别
    FILE *fp_;          ///< 日志文件句柄，nullptr 表示未启用文件输出
    std::mutex mutex_;  ///< 多线程写入保护锁
};

/** @brief 调试级别日志宏 */
#define LOG_D(tag, fmt, ...) Logger::instance().log(LogLevel::DEBUG, tag, fmt, ##__VA_ARGS__)
/** @brief 信息级别日志宏 */
#define LOG_I(tag, fmt, ...) Logger::instance().log(LogLevel::INFO,  tag, fmt, ##__VA_ARGS__)
/** @brief 警告级别日志宏 */
#define LOG_W(tag, fmt, ...) Logger::instance().log(LogLevel::WARN,  tag, fmt, ##__VA_ARGS__)
/** @brief 错误级别日志宏 */
#define LOG_E(tag, fmt, ...) Logger::instance().log(LogLevel::ERROR, tag, fmt, ##__VA_ARGS__)

#endif
