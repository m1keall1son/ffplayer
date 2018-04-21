//
//  Buffer.cpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#include "Buffer.h"
#include <assert.h>

namespace ffmpeg {
    
    Buffer::Buffer():
    mOwnsData(true),
    mData(nullptr),
    mSize(0)
    {}
    
    Buffer::~Buffer()
    {
        clear();
    }
    
    Buffer::Buffer(uint8_t * data, uint32_t size):
        mOwnsData(false),
        mData(data),
        mSize(size)
    {}
    
    Buffer::Buffer(uint32_t size):
        mOwnsData(true)
    {
        allocate(size);
    }
    
    void Buffer::allocate(uint32_t size)
    {
        mData = new uint8_t[size];
        mSize = sizeof(uint8_t)*size;
        assert(size == sizeof(uint8_t)*size);
    }
    
    void Buffer::clear()
    {
        if(isAllocated() && mOwnsData){
            delete [] mData;
        }
        mSize = 0;
    }
    
}
