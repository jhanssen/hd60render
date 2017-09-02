#ifndef DEMUXER_H
#define DEMUXER_H

#include <tsDemuxer.h>
#include <rct/Buffer.h>
#include <vector>
#include <memory>

class DemuxerImpl;

class Demuxer
{
public:
    Demuxer();

    void feed(Buffer&& buffer);

private:
    std::shared_ptr<DemuxerImpl> mDemuxer;
    std::shared_ptr<TSDemux::AVContext> mAVContext;

    std::vector<Buffer> mBuffers;
    size_t mBufferOffset;

    friend class DemuxerImpl;
};

#endif
