#include "Demuxer.h"
#include "Log.h"
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

static inline int stream_identifier(int composition_id, int ancillary_id)
{
    return ((composition_id & 0xff00) >> 8)
        | ((composition_id & 0xff) << 8)
        | ((ancillary_id & 0xff00) << 16)
        | ((ancillary_id & 0xff) << 24);
}

static void show_stream_info(TSDemux::ElementaryStream* es, uint16_t channel)
{
    if (!es)
        return;

    log::stdout("dump stream infos for channel % PID %\n", channel, es->pid);
    log::stdout("  Codec name     : %\n", es->GetStreamCodecName());
    log::stdout("  Language       : %\n", es->stream_info.language);
    log::stdout("  Identifier     : %\n", stream_identifier(es->stream_info.composition_id, es->stream_info.ancillary_id));
    log::stdout("  FPS scale      : %\n", es->stream_info.fps_scale);
    log::stdout("  FPS rate       : %\n", es->stream_info.fps_rate);
    log::stdout("  Interlaced     : %\n", (es->stream_info.interlaced ? "true" : "false"));
    log::stdout("  Height         : %\n", es->stream_info.height);
    log::stdout("  Width          : %\n", es->stream_info.width);
    log::stdout("  Aspect         : %\n", es->stream_info.aspect);
    log::stdout("  Channels       : %\n", es->stream_info.channels);
    log::stdout("  Sample rate    : %\n", es->stream_info.sample_rate);
    log::stdout("  Block align    : %\n", es->stream_info.block_align);
    log::stdout("  Bit rate       : %\n", es->stream_info.bit_rate);
    log::stdout("  Bit per sample : %\n", es->stream_info.bits_per_sample);
    log::stdout("\n");
}

const unsigned char* DemuxerImpl::ReadAV(uint64_t pos, size_t n)
{
    // printf("wanting %zu bytes at %llu\n", n, pos);
    assert(n <= AV_BUFFER_SIZE + 1);
    assert(pos >= lastOffset);
    size_t rem = n;
    int bufpos = 0;
    size_t bufoff = demuxer->mBufferOffset;
    size_t where = 0;
    uint64_t lastoff = lastOffset;
    // skip to pos
    //printf("is at %d:%zu\n", bufpos, bufoff);
    if (pos > 0) {
        uint64_t skiprem = pos - lastoff;
        for (;;) {
            if (bufpos >= demuxer->mBuffers.size()) {
                // we don't have enough data
                //printf("returning null 1\n");
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
    //printf("skipped to %d:%zu\n", bufpos, bufoff);
    // printf("!!!START MEMCPY\n");
    while (rem > 0) {
        if (bufpos >= demuxer->mBuffers.size()) {
            // we don't have enough data
            //printf("returning null 2\n");
            return nullptr;
        }
        const auto& mbuf = demuxer->mBuffers[bufpos];
        // take up to rem bytes starting at mBufferOffset
        assert(bufoff == 0 || bufpos == 0);
        const size_t num = std::min(mbuf.size() - bufoff, rem);
        assert(rem >= num);
        // printf("!!! SUB MEMCPY to %zu from %zu with %zu bytes\n", where, bufoff, num);
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
    //printf("returning bytes\n");
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
            //printf("no sync\n");
            return;
        }
        ret = mAVContext->ProcessTSPacket();
        if (mAVContext->HasPIDStreamData()) {
            TSDemux::STREAM_PKT pkt;
            for (;;) {
                TSDemux::ElementaryStream* es = mAVContext->GetPIDStream();
                if (!es)
                    break;
                if (!es->GetStreamPacket(&pkt))
                    break;
                if (pkt.streamChange) {
                    mSignalInfo(pkt.pid, es->stream_type, es->stream_info);
                    //show_stream_info(es, mAVContext->GetChannel(pkt.pid));
                }
                if (pkt.size > 0 && pkt.data) {
                    //printf("data for pid %.4x %zu\n", pkt.pid, pkt.size);
                    mSignalPkt(pkt);
                }
            }
            // stream datas
            //printf("has stream data\n");
        }
        if (mAVContext->HasPIDPayload()) {
            // woop
            //printf("has payload\n");
            ret = mAVContext->ProcessTSPayload();
            if (ret == TSDemux::AVCONTEXT_PROGRAM_CHANGE) {
                // hey
                log::stdout("program change\n");
                const std::vector<TSDemux::ElementaryStream*> streams = mAVContext->GetStreams();
                for (std::vector<TSDemux::ElementaryStream*>::const_iterator it = streams.begin(); it != streams.end(); ++it) {
                    mAVContext->StartStreaming((*it)->pid);
                    if ((*it)->has_stream_info) {
                        //show_stream_info(*it, mAVContext->GetChannel((*it)->pid));
                        mSignalInfo((*it)->pid, (*it)->stream_type, (*it)->stream_info);
                    }
                }
            }
        }
        if (ret == TSDemux::AVCONTEXT_TS_ERROR) {
            mAVContext->Shift();
        } else {
            mAVContext->GoNext();
        }
    }
}
