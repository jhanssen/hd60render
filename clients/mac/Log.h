#ifndef LOG_H
#define LOG_H

#include <sstream>
#include <functional>

namespace log {
typedef std::function<void(const std::string&)> LogFunction;

namespace detail {
inline void log_helper(std::ostringstream& strm, const char* format)
{
    strm << format;
}

template<typename T, typename ...Rest>
void log_helper(std::ostringstream& strm, const char* format, T value, Rest... rest)
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
std::string log(const char* format, Args... args)
{
    std::ostringstream strm;
    log_helper(strm, format, args...);
    return strm.str();
}

void forEachOut(std::function<void(LogFunction&)>&& logger);
void forEachErr(std::function<void(LogFunction&)>&& logger);
} // namespace detail

void addSink(LogFunction&& out, LogFunction&& err);

template<typename ...Args>
void stdout(const char* format, Args... args)
{
    const auto str = detail::log(format, args...);
    detail::forEachOut([&](LogFunction& log) {
            log(str);
        });
}

template<typename ...Args>
void stderr(const char* format, Args... args)
{
    const auto str = detail::log(format, args...);
    detail::forEachErr([&](LogFunction& log) {
            log(str);
        });
}
} // namespace log
#endif
