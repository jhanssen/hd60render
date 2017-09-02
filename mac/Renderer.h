#ifndef RENDERER_H
#define RENDERER_H

#include <memory>
#include <string>
#include <VideoToolbox.h>
#include <rct/SocketClient.h>
#include "Demuxer.h"
#include "h264_parser.h"

class Renderer
{
public:
    Renderer(const std::string& host, uint16_t port);
    ~Renderer();

private:
    void createDecoder(const TSDemux::STREAM_PKT& pkt);

private:
    std::shared_ptr<SocketClient> mClient;
    Demuxer mDemuxer;

    int mWidth, mHeight;
    CMVideoFormatDescriptionRef mVideoFormat;
    VTDecompressionSessionRef mDecoder;
    uint16_t mH264Pid;

    media::H264Parser parser;
};

#endif
