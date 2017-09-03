#ifndef AAC_H
#define AAC_H

#include <neaacdec.h>
#include <stdint.h>
#include <stddef.h>
#include <rct/SignalSlot.h>

class AAC
{
public:
    AAC();
    ~AAC();

    void decode(const uint8_t* data, size_t size, uint64_t pts);

    Signal<std::function<void(const void* samples, size_t count, size_t bps, uint64_t pts)> >& samples() { return mSamples; }
    Signal<std::function<void(int rate, int channels, uint64_t pts)> >& info() { return mInfo; }

private:
    NeAACDecHandle mAAC;
    bool mInited;

    Signal<std::function<void(const void* samples, size_t count, size_t bps, uint64_t pts)> > mSamples;
    Signal<std::function<void(int rate, int channels, uint64_t pts)> > mInfo;
};

#endif
