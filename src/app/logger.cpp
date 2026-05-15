/**
 * @file logger.cpp
 * @brief 统一日志系统实现
 */

#include "logger.h"
#include <unistd.h>
#include <sys/stat.h>

Logger &Logger::instance()
{
    static Logger inst;
    return inst;
}

Logger::Logger()
    : level_(LogLevel::DEBUG)
    , fp_(nullptr)
{
}

Logger::~Logger()
{
    close();
}

void Logger::setLevel(LogLevel level)
{
    level_ = level;
}

void Logger::setLogFile(const std::string &path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (fp_) {
        fclose(fp_);
        fp_ = nullptr;
    }
    fp_ = fopen(path.c_str(), "a");
    if (fp_) {
        /* 行缓冲模式：每行日志写入后立即刷新到磁盘，
         * 确保程序异常退出时日志不丢失 */
        setvbuf(fp_, nullptr, _IOLBF, 0);
    }
}

void Logger::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (fp_) {
        fclose(fp_);
        fp_ = nullptr;
    }
}

const char *Logger::levelStr(LogLevel level)
{
    switch (level) {
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO:  return "INFO ";
    case LogLevel::WARN:  return "WARN ";
    case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}

const char *Logger::levelColor(LogLevel level)
{
    switch (level) {
    case LogLevel::DEBUG: return "\033[36m";  /* 青色 - 调试信息 */
    case LogLevel::INFO:  return "\033[32m";  /* 绿色 - 正常信息 */
    case LogLevel::WARN:  return "\033[33m";  /* 黄色 - 警告信息 */
    case LogLevel::ERROR: return "\033[31m";  /* 红色 - 错误信息 */
    }
    return "\033[0m";
}

void Logger::writeLog(LogLevel level, const char *tag, const char *msg)
{
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char timeStr[24];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char *lvl = levelStr(level);
    /* 仅当 stdout 连接终端时才输出 ANSI 颜色代码，
     * 避免管道重定向或日志文件中出现乱码 */
    const char *color = isatty(STDOUT_FILENO) ? levelColor(level) : "";
    const char *reset = isatty(STDOUT_FILENO) ? "\033[0m" : "";

    /* 控制台输出：带颜色，立即刷新确保 VS Code 调试终端可见 */
    fprintf(stdout, "%s[%s] [%s] [%s] %s%s\n",
            color, timeStr, lvl, tag, msg, reset);
    fflush(stdout);

    /* 文件输出：纯文本，无颜色代码 */
    if (fp_) {
        fprintf(fp_, "[%s] [%s] [%s] %s\n", timeStr, lvl, tag, msg);
    }
}

void Logger::log(LogLevel level, const char *tag, const char *fmt, ...)
{
    /* 级别过滤：低于设定级别的日志直接丢弃，减少不必要的格式化开销 */
    if (level < level_) return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* 加锁保护：防止多线程并发写入导致日志行交错 */
    std::lock_guard<std::mutex> lock(mutex_);
    writeLog(level, tag, buf);
}
