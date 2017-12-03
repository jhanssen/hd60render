#ifndef LOG_H
#define LOG_H

#include <sstream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class Log {
public:
    typedef std::function<void(const std::string&)> LogFunction;

    static inline void addSink(LogFunction&& out, LogFunction&& err)
    {
        auto d = data();
        d->out.push_back(std::move(out));
        d->err.push_back(std::move(err));
    }

    template<typename ...Args>
    static inline void stdout(const char* format, Args... args)
    {
        const auto str = log(format, args...);
        forEachOut([&](LogFunction& log) {
                log(str);
            });
    }

    template<typename ...Args>
    static inline void stderr(const char* format, Args... args)
    {
        const auto str = log(format, args...);
        forEachErr([&](LogFunction& log) {
                log(str);
            });
    }

private:
    struct Data
    {
        std::vector<LogFunction> out, err;
    };

    static inline void log_helper(std::ostringstream& strm, const char* format)
    {
        strm << format;
    }

    template<typename T, typename ...Rest>
    static inline void log_helper(std::ostringstream& strm, const char* format, T value, Rest... rest)
    {
        for (; *format != '\0'; ++format) {
            if (*format == '%') {
                strm << value;
                log_helper(strm, format + 1, rest...);
                return;
            }
            strm << *format;
        }
    }

    template<typename ...Args>
    static inline std::string log(const char* format, Args... args)
    {
        std::ostringstream strm;
        log_helper(strm, format, args...);
        return strm.str();
    }

    static inline void forEachOut(std::function<void(LogFunction&)>&& logger)
    {
        for (auto l : data()->out) {
            logger(l);
        }
    }

    static inline void forEachErr(std::function<void(LogFunction&)>&& logger)
    {
        for (auto l : data()->err) {
            logger(l);
        }
    }

    static inline Data* data()
    {
        std::call_once(sOnce, []() {
                sData = new Data;
            });
        return sData;
    }

private:
    static Data* sData;
    static std::once_flag sOnce;

private:
    Log() = delete;
};

#endif
