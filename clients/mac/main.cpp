#include <rct/EventLoop.h>
#include <thread>
#include <cstdio>
#include "Log.h"
#include "Renderer.h"
#include "View.h"
#include "Options.h"

static void render(Renderer* renderer)
{
    auto loop = std::make_shared<EventLoop>();
    loop->init(EventLoop::MainEventLoop);

    renderer->exec();

    loop->exec();
}

int main(int argc, char** argv)
{
    Renderer::Options renderOptions;

    const Options options = Options::parse(argc, argv);
    if (auto host = options.get<std::string>("&host")) {
        renderOptions.host = *host;
    } else {
        std::printf("Need to pass --host name\n");
        return 1;
    }
    renderOptions.port = options.get<int>("&port", 5198);
    const bool verbose = options.enabled("&verbose");
    Log::addSink(
        [verbose](const std::string& msg) {
            if (verbose) {
                std::fprintf(stdout, "%s", msg.c_str());
            }
        },
        [](const std::string& msg) {
            std::fprintf(stderr, "%s", msg.c_str());
        });

    Renderer renderer(renderOptions);
    View view(&renderer);

    std::thread rendererThread(render, &renderer);

    return view.exec();
}
