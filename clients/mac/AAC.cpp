#include "AAC.h"
#include "Log.h"

AAC::AAC()
    : mInited(false)
{
    mAAC = NeAACDecOpen();
    NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(mAAC);
    NeAACDecSetConfiguration(mAAC, conf);
}

AAC::~AAC()
{
    NeAACDecClose(mAAC);
}

void AAC::decode(const uint8_t* data, size_t size, uint64_t pts)
{
    enum { BytesPerSample = 2 }; // 16 bit sample size?

    unsigned long samplerate;
    unsigned char channels;
    if (!mInited) {
        char err = NeAACDecInit(mAAC, const_cast<uint8_t*>(data), size, &samplerate, &channels);
        if (err != 0) {
            Log::stderr("aac init error %\n", err);
            return;
        }
        mInited = true;

        mInfo(samplerate, channels, pts);
    }

    const uint8_t* cur = data;
    size_t rem = size;

    for (;;) {
        void* output;
        NeAACDecFrameInfo info;

        output = NeAACDecDecode(mAAC, &info, const_cast<uint8_t*>(cur), rem);
        if (info.error != 0) {
            Log::stderr("error decoding aac % % with rem %\n", info.error, NeAACDecGetErrorMessage(info.error), rem);
            return;
        }

        if (!info.bytesconsumed) {
            break;
        }

        //Log::stdout("decoded % samples\n", info.samples);
        mSamples(output, info.samples, BytesPerSample, pts);

        cur += info.bytesconsumed;
        rem -= info.bytesconsumed;

        if (!rem)
            break;
    }
}
