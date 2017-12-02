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
    struct Options
    {
        std::string host;
        uint16_t port;
    };

    Renderer(Options opts);
    ~Renderer();

    void exec();

    class ImageBuffer
    {
    public:
        ImageBuffer();
        ImageBuffer(ImageBuffer&& buffer);
        ~ImageBuffer();

        ImageBuffer& operator=(ImageBuffer&& other);
        void release();

        CVImageBufferRef ref() const;

    private:
        ImageBuffer(CVImageBufferRef r);
        ImageBuffer(const ImageBuffer&) = delete;
        ImageBuffer& operator=(const ImageBuffer&) = delete;

        CVImageBufferRef mRef;

        friend class Renderer;
    };

    Signal<std::function<void(int, int)> >& geometryChange() { return mGeometryChange; }
    Signal<std::function<void(ImageBuffer&& image, CMTime timestamp, CMTime duration, uint64_t pts)> >& image() { return mImage; }

    Signal<std::function<void(int rate, int channels, uint64_t pts)> >& audioChange() { return mAudioChange; }
    Signal<std::function<void(const uint8_t* data, size_t size, uint64_t pts)> >& audio() { return mAudio; }

    uint64_t currentPts() const { return mCurrentPts; }

private:
    void createDecoder(const TSDemux::STREAM_PKT& pkt);
    void handlePacket(const TSDemux::STREAM_PKT& pkt);

    static void decoded(void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status, VTDecodeInfoFlags infoFlags,
                        CVImageBufferRef imageBuffer, CMTime presentationTimeStamp, CMTime presentationDuration);

private:
    Options mOptions;
    std::shared_ptr<SocketClient> mClient;
    Demuxer mDemuxer;
    AAC mAAC;

    int mWidth, mHeight;
    CMVideoFormatDescriptionRef mVideoFormat;
    VTDecompressionSessionRef mDecoder;
    uint16_t mH264Pid, mAACPid;
    uint64_t mCurrentPts;

    media::H264Parser mParser;

    Signal<std::function<void(int, int)> > mGeometryChange;
    Signal<std::function<void(ImageBuffer&& image, CMTime timestamp, CMTime duration, uint64_t pts)> > mImage;

    Signal<std::function<void(int rate, int channels, uint64_t pts)> > mAudioChange;
    Signal<std::function<void(const uint8_t* data, size_t size, uint64_t pts)> > mAudio;
};

inline Renderer::ImageBuffer::ImageBuffer()
    : mRef(0)
{
}

inline Renderer::ImageBuffer::ImageBuffer(CVImageBufferRef r)
{
    mRef = r;
    if (mRef)
        CVPixelBufferRetain(mRef);
}

inline Renderer::ImageBuffer::ImageBuffer(ImageBuffer&& buffer)
    : mRef(buffer.mRef)
{
    buffer.mRef = 0;
}

inline Renderer::ImageBuffer::~ImageBuffer()
{
    if (mRef)
        CVPixelBufferRelease(mRef);
}

inline Renderer::ImageBuffer& Renderer::ImageBuffer::operator=(ImageBuffer&& other)
{
    if (mRef)
        CVPixelBufferRelease(mRef);
    mRef = other.mRef;
    other.mRef = 0;
    return *this;
}

inline CVImageBufferRef Renderer::ImageBuffer::ref() const
{
    return mRef;
}

inline void Renderer::ImageBuffer::release()
{
    if (mRef) {
        CVPixelBufferRelease(mRef);
        mRef = 0;
    }
}

#endif
