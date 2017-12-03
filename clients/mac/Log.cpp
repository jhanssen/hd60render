#include "Log.h"
#include <mutex>
#include <thread>
#include <vector>

struct Data
{
    std::vector<log::LogFunction> out, err;
};

static Data* sData = nullptr;
static std::once_flag sDataFlag;

static Data* data()
{
    std::call_once(sDataFlag, []() {
            sData = new Data;
        });
    return sData;
}

namespace log {
void addSink(LogFunction&& out, LogFunction&& err)
{
    auto d = data();
    d->out.push_back(std::move(out));
    d->err.push_back(std::move(err));
}

namespace detail {
void forEachOut(std::function<void(LogFunction&)>&& logger)
{
    for (auto l : data()->out) {
        logger(l);
    }
}

void forEachErr(std::function<void(LogFunction&)>&& logger)
{
    for (auto l : data()->err) {
        logger(l);
    }
}
} // namespace detail
} // namespace log
