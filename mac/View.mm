#include "View.h"
#include "Renderer.h"

#import <Cocoa/Cocoa.h>
#include <OpenGL/gl.h>

#define INITIALWIDTH 1280
#define INITIALHEIGHT 720

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
            GLfloat		texMatrix[16]	= {0};
            GLint		saveMatrixMode;

            const size_t _texWidth = 1280;
            const size_t _texHeight = 720;

            // Reverses and normalizes the texture
            texMatrix[0]	= (GLfloat)_texWidth;
            texMatrix[5]	= -(GLfloat)_texHeight;
            texMatrix[10]	= 1.0;
            texMatrix[13]	= (GLfloat)_texHeight;
            texMatrix[15]	= 1.0;

            glGetIntegerv(GL_MATRIX_MODE, &saveMatrixMode);
            glMatrixMode(GL_TEXTURE);
            glPushMatrix();
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

class ViewPrivate
{
public:
    GLView* glview;
    CVOpenGLTextureCacheRef textureCache;

    void init();
};

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
        printf("failed to create texture cache %d\n", err);
    }
}

View::View(Renderer* r)
    : mPriv(new ViewPrivate), mRenderer(r)
{
    init();
}

View::~View()
{
    delete mPriv;
}

void View::init()
{
    mRenderer->image().connect([this](CVImageBufferRef cvImage, CMTime timestamp, CMTime duration) {
            ScopedPool pool;
            CFRetain(cvImage);
            printf("got frame\n");
            dispatch_sync(dispatch_get_main_queue(), ^{
                    ScopedPool pool;
                    @try {
                        printf("frame received by main thread\n");
                        CVReturn err;
                        CVOpenGLTextureRef texture;
                        err = CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                         mPriv->textureCache,
                                                                         cvImage,
                                                                         NULL,
                                                                         &texture);
                        if (err != kCVReturnSuccess) {
                            printf("failed to create texture %d\n", err);
                            return;
                        }
                        if (mPriv->glview->texture) {
                            CFRelease(mPriv->glview->texture);
                            CVOpenGLTextureCacheFlush(mPriv->textureCache, 0);
                        }
                        //printf("image!\n");
                        mPriv->glview->texture = texture;
                        [mPriv->glview setNeedsDisplay:YES];

                        CFRelease(cvImage);
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

    NSRect rect = NSMakeRect(0, 0, INITIALWIDTH, INITIALHEIGHT);
    NSWindow *window = [[NSWindow alloc] initWithContentRect:rect
                                         styleMask:(NSResizableWindowMask | NSClosableWindowMask | NSTitledWindowMask | NSMiniaturizableWindowMask)
                                         backing:NSBackingStoreBuffered defer:NO];
    [window retain];
    [window center];
    [window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];

    NSView* content_view = [window contentView];
    [content_view setAutoresizesSubviews:YES];
    GLView* glview = [[[GLView alloc]
                                  initWithFrame:[content_view bounds]]
                         autorelease];
    [glview setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [content_view addSubview:glview];
    [glview retain];

    glview->texture = 0;

    mPriv->glview = glview;
    mPriv->init();

    [app activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:window];

    [app run];

    return 0;
}
