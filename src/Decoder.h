//
//  Decoder.hpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

#include "PacketQueue.h"
#include <functional>

namespace ffmpeg {

class Decoder {
public:
    
    static int REORDER_PTS;

    Decoder();
    ~Decoder();
    
    void init(AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond);
    void destroy();
    int decodeFrame(AVFrame *frame, AVSubtitle *sub);
    void abort(class FrameQueue* fq);
    int start(int(*threadFn)(void*), void *arg);
    
    inline int getFinished()const{return mFinished;}
    inline void setStartPts(int64_t start_pts){ mStartPTS = start_pts; }
    inline void setStartPtsTimeBase(AVRational start_pts_tb){ mStartPTS_TB = start_pts_tb; }
    inline AVRational getStartPtsTimeBase()const{return mStartPTS_TB;}
    inline int64_t getStartPts()const{return mStartPTS;}
    inline int getPacketSerial()const{return mPacketSerial;}
    inline AVCodecContext* getAVContext(){return mAVContext;}

private:
    AVPacket mPacket;
    PacketQueue *mQueue;
    AVCodecContext *mAVContext;
    int mPacketSerial;
    int mFinished;
    int mPacketPending;
    SDL_cond *mQueueEmptyCondVar;
    int64_t mStartPTS;
    AVRational mStartPTS_TB;
    int64_t mNextPTS;
    AVRational mNextPTS_TB;
    SDL_Thread *mDecoderThread;
};
    
}//end namespace ffmpeg
