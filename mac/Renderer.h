#ifndef RENDERER_H
#define RENDERER_H

#include <memory>
#include <string>
#include <VideoToolbox.h>
#include <AudioToolbox.h>
#include <rct/SocketClient.h>
#include <rct/SignalSlot.h>
#include "Demuxer.h"
#include "AAC.h"
#include "h264_parser.h"

class Renderer
{
public:
    Renderer();
    ~Renderer();

    void exec(const std::string& host, uint16_t port);

    Signal<std::function<void(int, int)> >& geometryChange() { return mGeometryChange; }
    Signal<std::function<void(CVImageBufferRef cvImage, CMTime timestamp, CMTime duration)> >& image() { return mImage; }

    Signal<std::function<void(const TSDemux::STREAM_INFO&)> >& audioChange() { return mAudioChange; }
    Signal<std::function<void(const uint8_t* data, size_t size)> >& audio() { return mAudio; }

private:
    void createDecoder(const TSDemux::STREAM_PKT& pkt);
    void handlePacket(const TSDemux::STREAM_PKT& pkt);

    static void decoded(void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status, VTDecodeInfoFlags infoFlags,
                        CVImageBufferRef imageBuffer, CMTime presentationTimeStamp, CMTime presentationDuration);

private:
    std::shared_ptr<SocketClient> mClient;
    Demuxer mDemuxer;
    AAC mAAC;

    int mWidth, mHeight;
    CMVideoFormatDescriptionRef mVideoFormat;
    VTDecompressionSessionRef mDecoder;
    uint16_t mH264Pid, mAACPid;

    media::H264Parser parser;

    Signal<std::function<void(int, int)> > mGeometryChange;
    Signal<std::function<void(CVImageBufferRef cvImage, CMTime timestamp, CMTime duration)> > mImage;

    Signal<std::function<void(const TSDemux::STREAM_INFO&)> > mAudioChange;
    Signal<std::function<void(const uint8_t* data, size_t size)> > mAudio;
};

#endif
