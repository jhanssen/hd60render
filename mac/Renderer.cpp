#include "Renderer.h"

Renderer::Renderer(const std::string& host, uint16_t port)
    : mClient(std::make_shared<SocketClient>())
{
    mClient->readyRead().connect([this](const SocketClient::SharedPtr&, Buffer&& buffer) {
            printf("got data %zu\n", buffer.size());
            mDemuxer.feed(std::move(buffer));
        });
    mClient->connected().connect([](const SocketClient::SharedPtr&) {
            printf("connected\n");
        });
    mClient->connect(host, port);
}

Renderer::~Renderer()
{
}
