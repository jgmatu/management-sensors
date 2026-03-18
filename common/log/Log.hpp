#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
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
    // Obtiene el logger singleton global
    static Logger& instance()
    {
        static Logger inst;
        return inst;
    }

    // Escribe una línea de log para un componente concreto
    void log(const std::string& component, Level lvl, const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::ostream& os = stream_for(component);
        os << timestamp() << " [" << level_to_cstr(lvl) << "] " << msg << '\n';
        os.flush();
    }

    // Helpers de conveniencia
    void info (const std::string& component, const std::string& msg) { log(component, Level::Info,  msg); }
    void warn (const std::string& component, const std::string& msg) { log(component, Level::Warn,  msg); }
    void error(const std::string& component, const std::string& msg) { log(component, Level::Error, msg); }
    void debug(const std::string& component, const std::string& msg) { log(component, Level::Debug, msg); }

private:
    Logger() = default;

    std::ostream& stream_for(const std::string& component)
    {
        auto it = streams_.find(component);
        if (it != streams_.end()) {
            return *it->second;
        }
        const std::string log_dir = "logs";
        const std::string filename = log_dir + "/" + component + ".log";
        std::filesystem::create_directories(log_dir);
        auto ofs = std::make_unique<std::ofstream>(filename, std::ios::app);
        if (!ofs->is_open()) {
            static std::ofstream null_stream;
            return null_stream;
        }
        std::ostream& ref = *ofs;
        streams_[component] = std::move(ofs);
        return ref;
    }

    static std::string timestamp()
    {
        using clock = std::chrono::system_clock;
        const auto now = clock::now();
        const auto t   = clock::to_time_t(now);
        const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm{};
        localtime_r(&t, &tm);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<std::ofstream>> streams_;
};

} // namespace logging