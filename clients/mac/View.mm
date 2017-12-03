#include "View.h"
#include "Renderer.h"
#include "Log.h"

#import <Cocoa/Cocoa.h>
#include <OpenGL/gl.h>
#include <AudioToolbox/AudioQueue.h>
#include <vector>
#include <mutex>

static const int NumAudioBuffers = 3;
static const float AudioBufferSeconds = 0.2;

class ScopedPool
{
public:
    ScopedPool() { mPool = [[NSAutoreleasePool alloc] init]; }
    ~ScopedPool() { [mPool drain]; }

private:
    NSAutoreleasePool* mPool;
};

static void drawTexture()
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex3f(-1.0, -1.0, 0.0);
    glTexCoord2f(1.0, 0.0);
    glVertex3f(1.0, -1.0, 0.0);
    glTexCoord2f(1.0, 1.0);
    glVertex3f(1.0, 1.0, 0.0);
    glTexCoord2f(0.0, 1.0);
    glVertex3f(-1.0, 1.0, 0.0);
    glEnd();
}

@interface GLView : NSOpenGLView
{
    @public CVOpenGLTextureRef texture;
    @public Renderer::ImageBuffer image;
    @public int width;
    @public int height;
}

- (void) drawRect: (NSRect) bounds;
@end

@implementation GLView
-(void) drawRect: (NSRect) bounds
{
    ScopedPool pool;
    @try {
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (texture) {
            GLfloat		texMatrix[16] = {0};
            GLint		saveMatrixMode;

            // Reverses and normalizes the texture
            texMatrix[0]	= (GLfloat)width;
            texMatrix[5]	= -(GLfloat)height;
            texMatrix[10]	= 1.0;
            texMatrix[13]	= (GLfloat)height;
            texMatrix[15]	= 1.0;

            glGetIntegerv(GL_MATRIX_MODE, &saveMatrixMode);
            glMatrixMode(GL_TEXTURE);
            glLoadMatrixf(texMatrix);
            glMatrixMode(saveMatrixMode);

            glEnable(CVOpenGLTextureGetTarget(texture));
            glBindTexture(CVOpenGLTextureGetTarget(texture), CVOpenGLTextureGetName(texture));
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        } else {
            glColor3f(1.0f, 0.85f, 0.35f);
        }
        drawTexture();
        glFlush();
    }
    @catch (NSException *exception) {
        NSLog(@"%@", [exception reason]);
    }
}
@end

@interface WindowController : NSWindowController <NSWindowDelegate> {
    @public GLView* view;
    @public NSWindow* window;
}
@end

@implementation WindowController
- (void)windowDidResize:(NSNotification *)notification {
    glViewport(0, 0, window.frame.size.width, window.frame.size.height);
    [view setNeedsDisplay:YES];
}
@end

class ViewPrivate
{
public:
    ViewPrivate();

    void resetAudioQueue();

    GLView* glview;
    CVOpenGLTextureCacheRef textureCache;
    AudioQueueRef audioQueue;
    struct {
        AudioQueueBufferRef ref;
    } audioBuffers[NumAudioBuffers];
    std::mutex audioMutex;
    std::vector<std::vector<uint8_t> > audioPending;

    void init();

    static void audioOutput(void *inClientData, AudioQueueRef inAQ,
                            AudioQueueBufferRef inBuffer);
};

ViewPrivate::ViewPrivate()
    : audioQueue(0)
{
    for (int i = 0; i < NumAudioBuffers; ++i) {
        audioBuffers[i].ref = 0;
    }
}

void ViewPrivate::audioOutput(void *inClientData, AudioQueueRef inAQ,
                              AudioQueueBufferRef inBuffer)
{
    //printf("want audio output\n");
    ViewPrivate* priv = static_cast<ViewPrivate*>(inClientData);
    // see if we have any pending data
    std::unique_lock<std::mutex> locker(priv->audioMutex);
    if (!priv->audioPending.empty()) {
        // take as much as possible
        size_t total = 0;
        do {
            // can we take the entire current block?
            auto& data = priv->audioPending.front();
            const size_t taken = std::min<size_t>(data.size(), inBuffer->mAudioDataBytesCapacity - total);
            memcpy(static_cast<uint8_t*>(inBuffer->mAudioData) + total, &data[0], taken);
            total += taken;
            if (taken == data.size()) {
                // yep
                priv->audioPending.erase(priv->audioPending.begin());
                if (priv->audioPending.empty()) {
                    // done
                    break;
                }
            } else {
                // no, we'll have to memmove sadly
                memmove(&data[0], &data[taken], data.size() - taken);
                data.resize(data.size() - taken);
            }
        } while (total < inBuffer->mAudioDataBytesCapacity);
        inBuffer->mAudioDataByteSize = total;
        OSStatus err = AudioQueueEnqueueBuffer(priv->audioQueue, inBuffer, 0, NULL);
        if (err != noErr) {
            Log::stderr("enqueing (callback) with % (err %)\n", total, err);
        }
    } else {
        // add silence
        Log::stdout("audio underrun\n");
        const size_t taken = std::min<size_t>(1000, inBuffer->mAudioDataBytesCapacity);
        memset(inBuffer->mAudioData, '\0', taken);
        inBuffer->mAudioDataByteSize = taken;
        OSStatus err = AudioQueueEnqueueBuffer(priv->audioQueue, inBuffer, 0, NULL);
        if (err != noErr) {
            Log::stderr("enqueing (callback) silence (err %)\n", err);
        }
    }
}

void ViewPrivate::init()
{
    ScopedPool pool;
    CVReturn err;
    [[glview openGLContext] makeCurrentContext];
    err = CVOpenGLTextureCacheCreate(kCFAllocatorDefault,
                                     0,
                                     [[glview openGLContext] CGLContextObj],
                                     [[NSOpenGLView defaultPixelFormat] CGLPixelFormatObj],
                                     0,
                                     &textureCache);
    if (err != kCVReturnSuccess) {
        Log::stderr("failed to create texture cache %\n", err);
    }
}

void ViewPrivate::resetAudioQueue()
{
    Log::stdout("!! audio queue reset\n");
    for (int i = 0; i < NumAudioBuffers; ++i) {
        if (audioBuffers[i].ref) {
            AudioQueueFreeBuffer(audioQueue, audioBuffers[i].ref);
            audioBuffers[i].ref = 0;
        }
    }
    if (audioQueue) {
        AudioQueueStop(audioQueue, false);
        AudioQueueDispose(audioQueue, false);
        audioQueue = 0;
    }
}

View::View(Renderer* r)
    : mPriv(new ViewPrivate), mRenderer(r)
{
    mRenderer->geometryChange().connect([this](int w, int h) {
        dispatch_sync(dispatch_get_main_queue(), ^{
                ScopedPool pool;
                NSApplication* app = [NSApplication sharedApplication];

                NSRect rect = NSMakeRect(0, 0, w, h);
                NSWindow *window = [[NSWindow alloc] initWithContentRect:rect
                                                               styleMask:(NSWindowStyleMaskResizable | NSWindowStyleMaskClosable | NSWindowStyleMaskTitled | NSWindowStyleMaskMiniaturizable)
                                                                 backing:NSBackingStoreBuffered defer:NO];
                [window retain];
                [window center];
                [window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];

                WindowController* controller = [[WindowController alloc] init];
                controller->window = window;
                [window setDelegate:controller];

                NSView* content_view = [window contentView];
                [content_view setAutoresizesSubviews:YES];
                GLView* glview = [[[GLView alloc]
                                      initWithFrame:[content_view bounds]]
                                     autorelease];
                [glview setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
                [content_view addSubview:glview];
                [glview retain];

                controller->view = glview;

                glview->texture = 0;
                glview->width = w;
                glview->height = h;

                mPriv->glview = glview;
                mPriv->init();

                this->init();

                Log::stdout("view created\n");

                [app activateIgnoringOtherApps:YES];
                [window makeKeyAndOrderFront:window];
            });
    });
    mRenderer->audioChange().connect([this](int rate, int channels, uint64_t pts) {
            AudioStreamBasicDescription format;
            memset(&format, '\0', sizeof(AudioStreamBasicDescription));
            format.mFormatID = kAudioFormatLinearPCM;
            format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
            format.mSampleRate = rate;
            format.mChannelsPerFrame = channels;
            format.mBitsPerChannel = 16;
            format.mBytesPerFrame = 2 * format.mChannelsPerFrame;
            format.mBytesPerPacket = 2 * format.mChannelsPerFrame;
            format.mFramesPerPacket = 1;
            Log::stdout("audio change\n");

            dispatch_sync(dispatch_get_main_queue(), ^{
                    mPriv->resetAudioQueue();

                    OSStatus err;
                    err = AudioQueueNewOutput(&format, ViewPrivate::audioOutput, mPriv, NULL, NULL, 0, &mPriv->audioQueue);
                    if (err != noErr) {
                        Log::stderr("unable to create new audio queue\n");
                        return;
                    }

                    const auto bufferByteSize = static_cast<uint32_t>(format.mSampleRate * AudioBufferSeconds / 8);

                    for (int i = 0; i < NumAudioBuffers; ++i) {
                        auto& buf = mPriv->audioBuffers[i].ref;
                        err = AudioQueueAllocateBufferWithPacketDescriptions(mPriv->audioQueue,
                                                                             bufferByteSize,
                                                                             0,
                                                                             &buf);
                        if (err != noErr) {
                            Log::stderr("unable to create audio buffer %\n", i);
                            return;
                        }
                        Log::stdout("buffer % cap %\n", i, buf->mAudioDataBytesCapacity);

                        const size_t taken = std::min<size_t>(1000, buf->mAudioDataBytesCapacity);
                        memset(buf->mAudioData, '\0', taken);
                        buf->mAudioDataByteSize = taken;
                        OSStatus err = AudioQueueEnqueueBuffer(mPriv->audioQueue, buf, 0, NULL);
                        if (err != noErr) {
                            Log::stderr("enqueing % silence (err %)\n", i, err);
                        }
                    }

                    err = AudioQueueStart(mPriv->audioQueue, NULL);
                    if (err != noErr) {
                        Log::stderr("unable to start audio queue %\n", err);
                    } else {
                        Log::stdout("audio queue started 1\n");
                    }
                });
        });
    mRenderer->audio().connect([this](const uint8_t* data, size_t size, uint64_t pts) {
            if (!size)
                return;

            //printf("audio pts %llu\n", pts);
            dispatch_sync(dispatch_get_main_queue(), ^{
                    std::vector<uint8_t> pending;
                    pending.resize(size);
                    memcpy(&pending[0], data, size);
                    std::unique_lock<std::mutex> locker(mPriv->audioMutex);
                    mPriv->audioPending.push_back(std::move(pending));
                });
        });
}

View::~View()
{
    mPriv->resetAudioQueue();
    delete mPriv;
}

void View::init()
{
    mRenderer->image().connect([this](Renderer::ImageBuffer&& image, CMTime timestamp, CMTime duration, uint64_t pts) {
            //printf("image pts %llu\n", pts);
            ScopedPool pool;
            // printf("got frame\n");
            dispatch_sync(dispatch_get_main_queue(), ^{
                    ScopedPool pool;
                    @try {
                        // printf("frame received by main thread\n");
                        CVReturn err;
                        CVOpenGLTextureRef texture;
                        err = CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                         mPriv->textureCache,
                                                                         image.ref(),
                                                                         NULL,
                                                                         &texture);
                        if (err != kCVReturnSuccess) {
                            Log::stderr("failed to create texture %\n", err);
                            return;
                        }
                        if (mPriv->glview->texture) {
                            CFRelease(mPriv->glview->texture);
                            CVOpenGLTextureCacheFlush(mPriv->textureCache, 0);
                        }
                        //printf("image!\n");
                        mPriv->glview->image = std::move(image);
                        mPriv->glview->texture = texture;
                        [mPriv->glview setNeedsDisplay:YES];
                    }
                    @catch (NSException *exception) {
                        NSLog(@"%@", [exception reason]);
                    }
                });
        });
}

int View::exec()
{
    ScopedPool pool;
    NSApplication* app = [NSApplication sharedApplication];

    [app run];

    return 0;
}
