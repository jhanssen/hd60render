#include "Demuxer.h"
#include <assert.h>

#define AV_BUFFER_SIZE 131072

class DemuxerImpl : public TSDemux::TSDemuxer
{
public:
    DemuxerImpl(Demuxer* d);

    const unsigned char* ReadAV(uint64_t pos, size_t n);

private:
    Buffer current;
    uint64_t lastOffset;
    Demuxer* demuxer;
};

DemuxerImpl::DemuxerImpl(Demuxer* d)
    : lastOffset(0), demuxer(d)
{
    current.resize(AV_BUFFER_SIZE + 1);
}

const unsigned char* DemuxerImpl::ReadAV(uint64_t pos, size_t n)
{
    printf("wanting %zu bytes at %llu\n", n, pos);
    assert(n <= AV_BUFFER_SIZE + 1);
    assert(pos >= lastOffset);
    size_t rem = n;
    int bufpos = 0;
    size_t bufoff = demuxer->mBufferOffset;
    size_t where = 0;
    uint64_t lastoff = lastOffset;
    // skip to pos
    if (pos > 0) {
        uint64_t skiprem = pos - lastoff;
        for (;;) {
            if (bufpos >= demuxer->mBuffers.size()) {
                // we don't have enough data
                printf("returning null 1\n");
                return nullptr;
            }
            const auto& mbuf = demuxer->mBuffers[bufpos];
            if (skiprem < mbuf.size()) {
                bufoff = skiprem;
                break;
            }
            skiprem -= mbuf.size();
            lastoff += mbuf.size();
            bufoff = 0;
            ++bufpos;
        }
    }
    printf("skipped to %d:%zu\n", bufpos, bufoff);
    while (rem > 0) {
        if (bufpos >= demuxer->mBuffers.size()) {
            // we don't have enough data
            printf("returning null 2\n");
            return nullptr;
        }
        const auto& mbuf = demuxer->mBuffers[bufpos];
        // take up to rem bytes starting at mBufferOffset
        assert(bufoff == 0 || bufpos == 0);
        const size_t num = std::min(mbuf.size() - bufoff, rem);
        assert(rem >= num);
        memcpy(current.data() + where, mbuf.data() + bufoff, num);
        rem -= num;
        where += num;
        if (!rem) {
            // we're done
            if (num == mbuf.size() - bufoff) {
                ++bufpos;
                lastoff += mbuf.size();
                bufoff = 0;
            } else {
                bufoff = bufoff + num;
            }
            break;
        }
        bufoff = 0;
        lastoff += mbuf.size();
        ++bufpos;
    }
    if (bufpos > 0) {
        assert(bufpos <= demuxer->mBuffers.size());
        demuxer->mBuffers.erase(demuxer->mBuffers.begin(), demuxer->mBuffers.begin() + bufpos);
    }
    assert(!bufoff || !demuxer->mBuffers.empty());
    demuxer->mBufferOffset = bufoff;
    lastOffset = lastoff;

    assert(!rem);
    printf("returning bytes\n");
    return current.data();
}

Demuxer::Demuxer()
    : mBufferOffset(0)
{
    mDemuxer = std::make_shared<DemuxerImpl>(this);
    mAVContext = std::make_shared<TSDemux::AVContext>(mDemuxer.get(), 0, 0);
}

void Demuxer::feed(Buffer&& buffer)
{
    mBuffers.push_back(std::move(buffer));

    int ret;
    for (;;) {
        ret = mAVContext->TSResync();
        if (ret != TSDemux::AVCONTEXT_CONTINUE) {
            printf("no sync\n");
            return;
        }
        ret = mAVContext->ProcessTSPacket();
        if (mAVContext->HasPIDStreamData()) {
            // stream datas
            printf("has stream data\n");
        }
        if (mAVContext->HasPIDPayload()) {
            // woop
            printf("has payload\n");
            ret = mAVContext->ProcessTSPayload();
            if (ret == TSDemux::AVCONTEXT_PROGRAM_CHANGE) {
                // hey
                printf("program change\n");
            }
        }
        if (ret == TSDemux::AVCONTEXT_TS_ERROR) {
            mAVContext->Shift();
        } else {
            mAVContext->GoNext();
        }
    }
}
