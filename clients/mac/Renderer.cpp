#include "Renderer.h"
#include "Log.h"

static inline int stream_identifier(int composition_id, int ancillary_id)
{
    return ((composition_id & 0xff00) >> 8)
        | ((composition_id & 0xff) << 8)
        | ((ancillary_id & 0xff00) << 16)
        | ((ancillary_id & 0xff) << 24);
}

void Renderer::decoded(void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status, VTDecodeInfoFlags infoFlags,
                       CVImageBufferRef imageBuffer, CMTime presentationTimeStamp, CMTime presentationDuration)
{
    //printf("decoded\n");
    Renderer* r = static_cast<Renderer*>(sourceFrameRefCon);
    r->mImage(ImageBuffer(imageBuffer), presentationTimeStamp, presentationDuration, r->currentPts());
}

Renderer::Renderer(Options opts)
    : mOptions(opts), mClient(std::make_shared<SocketClient>()), mWidth(-1), mHeight(-1),
      mDecoder(0), mH264Pid(0), mAACPid(0), mCurrentPts(0)
{
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

void Renderer::exec()
{
    mClient->readyRead().connect([this](const SocketClient::SharedPtr&, Buffer&& buffer) {
            mDemuxer.feed(std::move(buffer));
        });
    mClient->connected().connect([](const SocketClient::SharedPtr&) {
            Log::stdout("connected\n");
        });
    mClient->connect(mOptions.host, mOptions.port);

    mDemuxer.info().connect([this](uint16_t pid, TSDemux::STREAM_TYPE type, const TSDemux::STREAM_INFO& info) {
            Log::stdout("dump stream infos for PID %\n", pid);
            Log::stdout("  Codec name     : %\n", TSDemux::ElementaryStream::GetStreamCodecName(type));
            Log::stdout("  Language       : %\n", info.language);
            Log::stdout("  Identifier     : %\n", stream_identifier(info.composition_id, info.ancillary_id));
            Log::stdout("  FPS scale      : %\n", info.fps_scale);
            Log::stdout("  FPS rate       : %\n", info.fps_rate);
            Log::stdout("  Interlaced     : %\n", (info.interlaced ? "true" : "false"));
            Log::stdout("  Height         : %\n", info.height);
            Log::stdout("  Width          : %\n", info.width);
            Log::stdout("  Aspect         : %\n", info.aspect);
            Log::stdout("  Channels       : %\n", info.channels);
            Log::stdout("  Sample rate    : %\n", info.sample_rate);
            Log::stdout("  Block align    : %\n", info.block_align);
            Log::stdout("  Bit rate       : %\n", info.bit_rate);
            Log::stdout("  Bit per sample : %\n", info.bits_per_sample);
            Log::stdout("\n");

            if (type == TSDemux::STREAM_TYPE_VIDEO_H264) {
                mWidth = info.width;
                mHeight = info.height;
                mH264Pid = pid;

                mGeometryChange(mWidth, mHeight);
            } else if (type == TSDemux::STREAM_TYPE_AUDIO_AAC_ADTS) {
                mAACPid = pid;

                //mAudioChange(info);
            } else {
                Log::stdout("eh %\n", type);
                //printf("eh %d\n", type);
            }
        });
    mDemuxer.pkt().connect([this](const TSDemux::STREAM_PKT& pkt) {
            if (pkt.pid == mH264Pid) {
                if (mDecoder) {
                    handlePacket(pkt);
                } else {
                    if (mWidth > 0 && mHeight > 0) {
                        createDecoder(pkt);
                        if (mDecoder)
                            handlePacket(pkt);
                    } else {
                        return;
                    }
                }
            } else if (pkt.pid == mAACPid) {
                //mAudio(pkt.data, pkt.size);
                mAAC.decode(pkt.data, pkt.size, pkt.pts);
            }
        });
    mAAC.samples().connect([this](const void* samples, size_t count, size_t bps, uint64_t pts) {
            mAudio(static_cast<const uint8_t*>(samples), count * bps, pts);
        });
    mAAC.info().connect([this](int rate, int channels, uint64_t pts) {
            mAudioChange(rate, channels, pts);
        });
}

void Renderer::handlePacket(const TSDemux::STREAM_PKT& pkt)
{
    size_t size = 0;
    std::vector<media::H264NALU> nalus;
    mParser.SetStream(pkt.data, pkt.size);
    mCurrentPts = pkt.pts;
    while (true) {
        media::H264NALU nalu;
        media::H264Parser::Result result = mParser.AdvanceToNextNALU(&nalu);
        if (result == media::H264Parser::kEOStream)
            break;
        if (result != media::H264Parser::kOk) {
            // bad
            break;
        }
        switch (nalu.nal_unit_type) {
        case media::H264NALU::kSPS:
        case media::H264NALU::kSPSExt:
        case media::H264NALU::kPPS:
            break;
        default:
            size += 4 + nalu.size;
            nalus.push_back(nalu);
            break;
        }
    }

    if (!size)
        return;

    CMBlockBufferRef data;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        NULL,                 // &memory_block
        size,                 // block_length
        kCFAllocatorDefault,  // block_allocator
        NULL,                 // &custom_block_source
        0,                    // offset_to_data
        size,                 // data_length
        0,                    // flags
        &data);
    if (status != noErr) {
        // ugh
        Log::stderr("couldn't create block buffer\n");
        return;
    }
    size_t offset = 0;
    for (size_t i = 0; i < nalus.size(); i++) {
        media::H264NALU& nalu = nalus[i];
        uint32_t header = htonl(nalu.size);
        status = CMBlockBufferReplaceDataBytes(
            &header, data, offset, 4);
        if (status != noErr) {
            Log::stderr("couldn't replace data bytes (1)\n");
            return;
        }
        offset += 4;
        status = CMBlockBufferReplaceDataBytes(nalu.data, data, offset, nalu.size);
        if (status != noErr) {
            Log::stderr("couldn't replace data bytes (2)\n");
            return;
        }
        offset += nalu.size;
    }

    CMSampleBufferRef frame;
    status = CMSampleBufferCreate(
        kCFAllocatorDefault,
        data,                 // data_buffer
        true,                 // data_ready
        NULL,                 // make_data_ready_callback
        NULL,                 // make_data_ready_refcon
        mVideoFormat,         // format_description
        1,                    // num_samples
        0,                    // num_sample_timing_entries
        NULL,                 // &sample_timing_array
        0,                    // num_sample_size_entries
        NULL,                 // &sample_size_array
        &frame);
    if (status != noErr) {
        Log::stderr("unable to create sample buffer\n");
        CFRelease(data);
        return;
    }

    const VTDecodeFrameFlags decode_flags =
        kVTDecodeFrame_EnableAsynchronousDecompression;

    status = VTDecompressionSessionDecodeFrame(
        mDecoder,
        frame,                                  // sample_buffer
        decode_flags,                           // decode_flags
        this,                                   // source_frame_refcon
        NULL);                                  // &info_flags_out
    if (status != noErr) {
        Log::stderr("unable to decode frame\n");
        //return;
    }
    CFRelease(frame);
    CFRelease(data);
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
    mParser.SetStream(pkt.data, pkt.size);
    while (true) {
        media::H264Parser::Result result = mParser.AdvanceToNextNALU(&nalu);
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
        Log::stderr("no sps and/or pps\n");
        return;
    }

    OSStatus status;

    Log::stdout("sps of % and pps of %\n", lastSps.size, lastPps.size);
    const uint8_t* const parameterSetPointers[2] = { lastSps.data, lastPps.data };
    const size_t parameterSetSizes[2] = { lastSps.size, lastPps.size };
    status = CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL,
                                                                 2,
                                                                 parameterSetPointers,
                                                                 parameterSetSizes,
                                                                 4,
                                                                 &mVideoFormat);

    if (status == noErr) {
        Log::stdout("format descr created!\n");
        // Set the pixel attributes for the destination buffer
        CFMutableDictionaryRef destinationPixelBufferAttributes = CFDictionaryCreateMutable(
            NULL, // CFAllocatorRef allocator
            0,    // CFIndex capacity
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

        //SInt32 destinationPixelType = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        SInt32 destinationPixelType = kCVPixelFormatType_422YpCbCr8;
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
            Log::stderr("error creating session %\n", status);
            return;
        }
        Log::stdout("decoder created\n");
    } else {
        Log::stderr("ugh, format failure %\n", status);
    }
}
