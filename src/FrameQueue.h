//
//  FrameQueue.hpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

#include "Frame.h"
#include "Definitions.h"
#include <SDL.h>
#include <SDL_thread.h>

namespace ffmpeg {

class PacketQueue;
    
class FrameQueue {
public:
    
    FrameQueue();
    ~FrameQueue();
    
    int init( PacketQueue *pktq, int max_size, int keep_last);
    void signal();
    Frame* peek();
    Frame* peekNext();
    Frame* peekLast();
    Frame* peekWriteable();
    Frame* peekReadable();
    void push();
    void next();
    int numRemaining()const;
    int64_t lastShownPosition()const;
    SDL_mutex* getMutex(){return mMutex;}
    inline int getRIndexShown(){return mRIndexShown;}
    
    static void UnrefItem(Frame* f);
    
private:
    Frame mQueue[FRAME_QUEUE_SIZE];
    int mRIndex;
    int mWIndex;
    int mSize;
    int mMaxSize;
    int mKeepLast;
    int mRIndexShown;
    SDL_mutex *mMutex;
    SDL_cond *mCondVar;
    PacketQueue *mPacketQueue;
};

}//end namespace ffmpeg
