#ifndef AAC_H
#define AAC_H

#include <neaacdec.h>
#include <stdint.h>
#include <stddef.h>

class AAC
{
public:
    AAC();
    ~AAC();

    void decode(const uint8_t* data, size_t size);

private:
    NeAACDecHandle mAAC;
};

#endif
