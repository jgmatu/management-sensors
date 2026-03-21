#pragma once

#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <memory>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace logging {

enum class Level {
    Debug,
    Info,
    Warn,
    Error,
};

inline const char* level_to_cstr(Level lvl)
{
    switch (lvl) {
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO";
    case Level::Warn:  return "WARN";
    case Level::Error: return "ERROR";
    }
    return "UNKNOWN";
}

class Logger {
public:
    static Logger& instance()
    {
        static Logger inst;
        return inst;
    }

    void set_process_name(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        process_name_ = name;
        std::filesystem::create_directories("logs");
        stream_ = std::make_unique<std::ofstream>("logs/" + name + ".log", std::ios::trunc);
    }

    void log(const std::string& component, Level lvl, const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const std::string line = timestamp()
            + " [" + level_to_cstr(lvl) + "]"
            + " [" + component + "] "
            + msg;

        if (stream_ && stream_->is_open()) {
            *stream_ << line << '\n';
            stream_->flush();
        }

        auto& console = (lvl >= Level::Error) ? std::cerr : std::cout;
        console << line << '\n';
    }

    void info (const std::string& c, const std::string& m) { log(c, Level::Info,  m); }
    void warn (const std::string& c, const std::string& m) { log(c, Level::Warn,  m); }
    void error(const std::string& c, const std::string& m) { log(c, Level::Error, m); }
    void debug(const std::string& c, const std::string& m) { log(c, Level::Debug, m); }

private:
    Logger() = default;

    static std::string timestamp()
    {
        using clock = std::chrono::system_clock;
        const auto now = clock::now();
        const auto t   = clock::to_time_t(now);
        const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch()) % 1000;

        std::tm tm{};
        localtime_r(&t, &tm);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    std::mutex mutex_;
    std::string process_name_;
    std::unique_ptr<std::ofstream> stream_;
};

} // namespace logging