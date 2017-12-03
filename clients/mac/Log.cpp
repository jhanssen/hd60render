#include "Log.h"

Log::Data* Log::sData = nullptr;
std::once_flag Log::sOnce;
