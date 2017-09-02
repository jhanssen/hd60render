#include <rct/EventLoop.h>
#include "Renderer.h"

int main(int argc, char** argv)
{
    auto loop = std::make_shared<EventLoop>();
    loop->init(EventLoop::MainEventLoop);

    Renderer renderer("192.168.1.150", 5198);

    loop->exec();
}
