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
    : mClient(std::make_shared<SocketClient>()), mWidth(-1), mHeight(-1), mDecoder(0)
{
    mClient->readyRead().connect([this](const SocketClient::SharedPtr&, Buffer&& buffer) {
            //printf("got data %zu\n", buffer.size());
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
        });
    mDemuxer.pkt().connect([this](const TSDemux::STREAM_PKT& pkt) {
            if (!mDecoder) {
                if (mWidth > 0 && mHeight > 0) {
                    createDecoder(pkt);
                } else {
                    return;
                }
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
    struct {
        size_t spsOffset, ppsOffset;
        size_t spsLength, ppsLength;
    } parsed = { 0, 0, 0, 0 };

    const unsigned char* data = pkt.data;
    const size_t size = pkt.size;

    size_t start = 0, last = 0;
    uint32_t code = 0xffffffff;
    bool done = false;
    while (!done && start + 3 < size) {
        if ((code & 0xffffff00) == 0x00000100) {
            printf("%u at %zu\n", code & 0x9f, start);
            printf("prev %d %d %d %d(%d)\n", data[start - 4], data[start - 3], data[start - 2], data[start - 1], data[start - 1] & 0x1f);
            last = start;
            switch (data[start - 1] & 0x1f) {
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
                done = true;
                break;
            case 0x07: // SPS
                parsed.spsOffset = start;
                printf("WTF SPS %d\n", data[start]);
                break;
            case 0x08: // PPS
                parsed.ppsOffset = start;
                break;
            }
        }
        code = code << 8 | data[start++];
    }
    // for (int i = 3; i < size; i++) {
    //     if (data[i] == 0x01 && data[i-1] == 0x00 && data[i-2] == 0x00 && data[i-3] == 0x00) {
    //         if (startCodeSPSIndex == 0) {
    //             startCodeSPSIndex = i;
    //         }
    //         if (i > startCodeSPSIndex) {
    //             startCodePPSIndex = i;
    //             break;
    //         }
    //     }
    // }

    if (!parsed.spsOffset || !parsed.ppsOffset) {
        printf("no sps and/or pps found\n");
        return;
    }

    parsed.spsLength = parsed.ppsOffset - parsed.spsOffset - 4;
    if (last > parsed.ppsOffset)
        parsed.ppsLength = last - parsed.ppsOffset - 4;
    else
        parsed.ppsLength = size - parsed.ppsOffset;

    printf("sps at %zu(%zu) and pps at %zu(%zu)\n", parsed.spsOffset, parsed.spsLength, parsed.ppsOffset, parsed.ppsLength);
    printf("does this look right %x %x %x %x\n",
           data[parsed.spsOffset + parsed.spsLength],
           data[parsed.spsOffset + parsed.spsLength + 1],
           data[parsed.spsOffset + parsed.spsLength + 2],
           data[parsed.spsOffset + parsed.spsLength + 3]);
    printf("and does this look right grrr %x %x %x %x\n",
           data[parsed.spsOffset - 4],
           data[parsed.spsOffset - 3],
           data[parsed.spsOffset - 2],
           data[parsed.spsOffset - 1]);

    printf("SPS\n");
    for (size_t i = 0; i < parsed.spsLength + 1; ++i)
        printf("0x%x ", data[parsed.spsOffset + i - 1]);
    printf("\nPPS\n");
    for (size_t i = 0; i < parsed.ppsLength + 1; ++i)
        printf("0x%x ", data[parsed.ppsOffset + i - 1]);
    printf("\n");

    OSStatus status;

    const uint8_t* const parameterSetPointers[2] = { data + parsed.spsOffset, data + parsed.ppsOffset };
    const size_t parameterSetSizes[2] = { parsed.spsLength, parsed.ppsLength };
    status = CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL,
                                                                 2,
                                                                 parameterSetPointers,
                                                                 parameterSetSizes,
                                                                 4,
                                                                 &mVideoFormat);

    if (status == noErr) {
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
        printf("decoder created");
    } else {
        printf("ugh, format failure %d\n", status);
    }
}
