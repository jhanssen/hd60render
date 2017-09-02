#ifndef DEMUXER_H
#define DEMUXER_H

#include <tsDemuxer.h>
#include <elementaryStream.h>
#include <rct/Buffer.h>
#include <rct/SignalSlot.h>
#include <vector>
#include <memory>

class DemuxerImpl;

class Demuxer
{
public:
    Demuxer();

    void feed(Buffer&& buffer);

    Signal<std::function<void(uint16_t pid, TSDemux::STREAM_TYPE, const TSDemux::STREAM_INFO&)> >& info() { return mSignalInfo; }
    Signal<std::function<void(const TSDemux::STREAM_PKT&)> >& pkt() { return mSignalPkt; }

private:
    std::shared_ptr<DemuxerImpl> mDemuxer;
    std::shared_ptr<TSDemux::AVContext> mAVContext;

    std::vector<Buffer> mBuffers;
    size_t mBufferOffset;

    Signal<std::function<void(uint16_t pid, TSDemux::STREAM_TYPE, const TSDemux::STREAM_INFO&)> > mSignalInfo;
    Signal<std::function<void(const TSDemux::STREAM_PKT&)> > mSignalPkt;

    friend class DemuxerImpl;
};

#endif
