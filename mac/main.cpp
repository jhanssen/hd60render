#include <rct/EventLoop.h>
#include <thread>
#include "Renderer.h"
#include "View.h"

static void render(Renderer* renderer)
{
    auto loop = std::make_shared<EventLoop>();
    loop->init(EventLoop::MainEventLoop);

    renderer->exec("192.168.1.150", 5198);

    loop->exec();
}

int main(int argc, char** argv)
{
    Renderer renderer;
    View view(&renderer);

    std::thread rendererThread(render, &renderer);

    return view.exec();
}
