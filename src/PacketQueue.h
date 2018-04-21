//
//  Queue.hpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

extern "C" {
#include "libavcodec/avcodec.h"
}
#include <SDL.h>
#include <SDL_thread.h>

namespace ffmpeg {

class PacketQueue {
public:
    
    static AVPacket sFlushPacket;
    
    PacketQueue();
    ~PacketQueue();
    int init();
    void start();
    void flush();
    void abort();
    int put(AVPacket *pkt);
    int putNullPacket(int stream);
    int get(AVPacket *pkt, bool block, int *serial = nullptr);
    
    inline int size() const { return mSizeInBytes; }
    inline int getSerial() const { return mSerial; }
    inline int* getSerialPtr() { return &mSerial; }
    inline int getAbortRequest() const { return mAbortRequest; }
    inline int getNumPackets() const { return mNumPackets; }
    inline int64_t getDuration() const { return mDuration; }

private:
    
    int _put(AVPacket *pkt);
    
    struct Item {
        AVPacket packet;
        Item *next;
        int serial;
    };
    
    Item *mFirstPacket, *mLastPacket;
    int mNumPackets;
    int mSizeInBytes;
    int64_t mDuration;
    int mAbortRequest;
    int mSerial;
    SDL_mutex *mMutex;
    SDL_cond *mCondVar;
};
    
}
