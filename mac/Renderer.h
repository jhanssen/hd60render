#ifndef RENDERER_H
#define RENDERER_H

#include <memory>
#include <string>
#include <rct/SocketClient.h>
#include "Demuxer.h"

class Renderer
{
public:
    Renderer(const std::string& host, uint16_t port);
    ~Renderer();

private:
    std::shared_ptr<SocketClient> mClient;
    Demuxer mDemuxer;
};

#endif
