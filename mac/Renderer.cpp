#include "Renderer.h"

static inline int stream_identifier(int composition_id, int ancillary_id)
{
    return ((composition_id & 0xff00) >> 8)
        | ((composition_id & 0xff) << 8)
        | ((ancillary_id & 0xff00) << 16)
        | ((ancillary_id & 0xff) << 24);
}

static void decoded(void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status, VTDecodeInfoFlags infoFlags,
                    CVImageBufferRef imageBuffer, CMTime presentationTimeStamp, CMTime presentationDuration)
{
}

Renderer::Renderer(const std::string& host, uint16_t port)
    : mClient(std::make_shared<SocketClient>()), mWidth(-1), mHeight(-1), mDecoder(0), mH264Pid(0)
{
    FILE* f2 = fopen("/tmp/ts.ts", "w");
    FILE* f3 = fopen("/tmp/ts.aac", "w");
    mClient->readyRead().connect([this, f2](const SocketClient::SharedPtr&, Buffer&& buffer) {
            //printf("got data %zu\n", buffer.size());
            fwrite(buffer.data(), 1, buffer.size(), f2);
            fflush(f2);

            mDemuxer.feed(std::move(buffer));
        });
    mClient->connected().connect([](const SocketClient::SharedPtr&) {
            printf("connected\n");
        });
    mClient->connect(host, port);

    mDemuxer.info().connect([this](uint16_t pid, TSDemux::STREAM_TYPE type, const TSDemux::STREAM_INFO& info) {
            printf("dump stream infos for PID %.4x\n", pid);
            printf("  Codec name     : %s\n", TSDemux::ElementaryStream::GetStreamCodecName(type));
            printf("  Language       : %s\n", info.language);
            printf("  Identifier     : %.8x\n", stream_identifier(info.composition_id, info.ancillary_id));
            printf("  FPS scale      : %d\n", info.fps_scale);
            printf("  FPS rate       : %d\n", info.fps_rate);
            printf("  Interlaced     : %s\n", (info.interlaced ? "true" : "false"));
            printf("  Height         : %d\n", info.height);
            printf("  Width          : %d\n", info.width);
            printf("  Aspect         : %3.3f\n", info.aspect);
            printf("  Channels       : %d\n", info.channels);
            printf("  Sample rate    : %d\n", info.sample_rate);
            printf("  Block align    : %d\n", info.block_align);
            printf("  Bit rate       : %d\n", info.bit_rate);
            printf("  Bit per sample : %d\n", info.bits_per_sample);
            printf("\n");

            mWidth = info.width;
            mHeight = info.height;

            if (type == TSDemux::STREAM_TYPE_VIDEO_H264)
                mH264Pid = pid;
        });
    mDemuxer.pkt().connect([this, f3](const TSDemux::STREAM_PKT& pkt) {
            if (pkt.pid == mH264Pid) {
                static int pktc = 0;
                char fn[1024];
                snprintf(fn, sizeof(fn), "/tmp/tspkt-%d.nalu", ++pktc);
                FILE* f = fopen(fn, "w");
                fwrite(pkt.data, 1, pkt.size, f);
                fclose(f);
                if (!mDecoder) {
                    if (mWidth > 0 && mHeight > 0) {
                        printf("PKT!!!! %d\n", pktc);
                        createDecoder(pkt);
                    } else {
                        return;
                    }
                }
            } else {
                fwrite(pkt.data, 1, pkt.size, f3);
                fflush(f3);
            }
        });
}

Renderer::~Renderer()
{
    if (mDecoder) {
        VTDecompressionSessionFinishDelayedFrames(mDecoder);
        /* Block until our callback has been called with the last frame. */
        VTDecompressionSessionWaitForAsynchronousFrames(mDecoder);
        
        /* Clean up. */
        VTDecompressionSessionInvalidate(mDecoder);
        CFRelease(mDecoder);
        CFRelease(mVideoFormat);
    }
}

void Renderer::createDecoder(const TSDemux::STREAM_PKT& pkt)
{
    struct NaluData
    {
        NaluData() : data(0), size(0) { }

        void assign(const uint8_t* d, size_t s) { data = d; size = s; }

        const uint8_t* data;
        size_t size;
    };
    NaluData lastSps, lastPps;

    media::H264NALU nalu;
    parser.SetStream(pkt.data, pkt.size);
    while (true) {
        media::H264Parser::Result result = parser.AdvanceToNextNALU(&nalu);
        if (result == media::H264Parser::kEOStream)
            break;
        if (result != media::H264Parser::kOk) {
            // bad
            break;
        }
        switch (nalu.nal_unit_type) {
        case media::H264NALU::kSPS:
            lastSps.assign(nalu.data, nalu.size);
            break;
        case media::H264NALU::kPPS:
            lastPps.assign(nalu.data, nalu.size);
            break;
        }
    }

    if (!lastSps.data || !lastPps.data) {
        printf("no sps and/or pps\n");
        return;
    }

    OSStatus status;

    printf("sps of %zu and pps of %zu\n", lastSps.size, lastPps.size);
    const uint8_t* const parameterSetPointers[2] = { lastSps.data, lastPps.data };
    const size_t parameterSetSizes[2] = { lastSps.size, lastPps.size };
    status = CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL,
                                                                 2,
                                                                 parameterSetPointers,
                                                                 parameterSetSizes,
                                                                 4,
                                                                 &mVideoFormat);

    if (status == noErr) {
        printf("format descr created!\n");
        // Set the pixel attributes for the destination buffer
        CFMutableDictionaryRef destinationPixelBufferAttributes = CFDictionaryCreateMutable(
            NULL, // CFAllocatorRef allocator
            0,    // CFIndex capacity
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

        SInt32 destinationPixelType = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        CFDictionarySetValue(destinationPixelBufferAttributes, kCVPixelBufferPixelFormatTypeKey, CFNumberCreate(NULL, kCFNumberSInt32Type, &destinationPixelType));
        CFDictionarySetValue(destinationPixelBufferAttributes, kCVPixelBufferWidthKey, CFNumberCreate(NULL, kCFNumberSInt32Type, &mWidth));
        CFDictionarySetValue(destinationPixelBufferAttributes, kCVPixelBufferHeightKey, CFNumberCreate(NULL, kCFNumberSInt32Type, &mHeight));
        CFDictionarySetValue(destinationPixelBufferAttributes, kCVPixelBufferOpenGLCompatibilityKey, kCFBooleanTrue);

        // Set the Decoder Parameters
        CFMutableDictionaryRef decoderParameters = CFDictionaryCreateMutable(
            NULL, // CFAllocatorRef allocator
            0,    // CFIndex capacity
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

        CFDictionarySetValue(decoderParameters, kVTDecompressionPropertyKey_RealTime, kCFBooleanTrue);

        // Create the decompression session
        // Throws Error -8971 (codecExtensionNotFoundErr)

        const VTDecompressionOutputCallbackRecord callback = { decoded, this };

        status = VTDecompressionSessionCreate(NULL, mVideoFormat, decoderParameters, destinationPixelBufferAttributes, &callback, &mDecoder);

        // release the dictionaries
        CFRelease(destinationPixelBufferAttributes);
        CFRelease(decoderParameters);

        // Check the Status
        if(status != noErr) {
            printf("error creating session %d\n", status);
            return;
        }
        printf("decoder created\n");
    } else {
        printf("ugh, format failure %d\n", status);
    }
}
