//
//  Buffer.hpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

#include <stdint.h>

namespace ffmpeg {
    
    class Buffer {
    public:
        
        Buffer();
        ~Buffer();
    
        Buffer(uint8_t* data, uint32_t size);
        Buffer(uint32_t size);

        void allocate(uint32_t size);
        void clear();
        
        inline uint8_t* data(){return mData;}
        inline uint32_t size()const{return mSize;}
        inline bool isAllocated()const{ return mData; }
        
    private:
        bool mOwnsData;
        uint8_t* mData;
        uint32_t mSize;
    };
    
}//end namespace ffmpeg
