#include <rct/EventLoop.h>
#include <thread>
#include "Renderer.h"
#include "View.h"
#include "Options.h"

static void render(Renderer* renderer)
{
    auto loop = std::make_shared<EventLoop>();
    loop->init(EventLoop::MainEventLoop);

    renderer->exec(renderer->options().host, renderer->options().port);

    loop->exec();
}

int main(int argc, char** argv)
{
    Renderer::Options renderOptions;

    const Options options = Options::parse(argc, argv);
    if (options.has<std::string>("&host")) {
        renderOptions.host = options.value<std::string>("&host");
    } else {
        printf("Need to pass --host name\n");
        return 1;
    }
    if (options.has<int>("&port")) {
        renderOptions.port = options.value<int>("&port");
    } else {
        renderOptions.port = 5198;
    }

    Renderer renderer(renderOptions);
    View view(&renderer);

    std::thread rendererThread(render, &renderer);

    return view.exec();
}
