//
//  Queue.cpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#include "PacketQueue.h"
#include "SDLUtil.h"


namespace ffmpeg {

AVPacket PacketQueue::sFlushPacket = AVPacket();
    
PacketQueue::PacketQueue():
mFirstPacket(nullptr),
mLastPacket(nullptr),
mNumPackets(0),
mSizeInBytes(0),
mDuration(0),
mAbortRequest(1),
mSerial(0),
mMutex(nullptr),
mCondVar(nullptr)
{
}

PacketQueue::~PacketQueue()
{
    flush();
    SDL_DestroyMutex(mMutex);
    SDL_DestroyCond(mCondVar);
}

int PacketQueue::init()
{
    mMutex = SDL_CreateMutex();
    if (!mMutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    mCondVar = SDL_CreateCond();
    if (!mCondVar) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    mAbortRequest = 1;
    return 0;
}

void PacketQueue::start()
{
    sdl::ScopedLock lock(mMutex);
    mAbortRequest = 0;
    _put(&sFlushPacket);
}

void PacketQueue::abort()
{
    sdl::ScopedLock lock(mMutex);
    mAbortRequest = 1;
    SDL_CondSignal(mCondVar);
}

void PacketQueue::flush()
{
    Item *pkt, *pkt1;
    
    sdl::ScopedLock lock(mMutex);
    for (pkt = mFirstPacket; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->packet);
        av_freep(&pkt);
    }
    mLastPacket = nullptr;
    mFirstPacket = nullptr;
    mNumPackets = 0;
    mSizeInBytes = 0;
    mDuration = 0;
}

int PacketQueue::_put(AVPacket *pkt)
{
    Item *pkt1;
    
    if (mAbortRequest)
        return -1;
    
    pkt1 = (Item*)av_malloc(sizeof(Item));
    if (!pkt1)
        return -1;
    pkt1->packet = *pkt;
    pkt1->next = nullptr;
    if (pkt == &sFlushPacket)
        mSerial++;
    pkt1->serial = mSerial;
    
    if (!mLastPacket)
        mFirstPacket = pkt1;
    else
        mLastPacket->next = pkt1;
    mLastPacket = pkt1;
    mNumPackets++;
    mSizeInBytes += pkt1->packet.size + sizeof(*pkt1);
    mDuration += pkt1->packet.duration;
    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal(mCondVar);
    return 0;
}
    
int PacketQueue::putNullPacket(int stream)
{
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream;
    return put(pkt);
}

int PacketQueue::put(AVPacket *pkt)
{
    int ret;
    {
        sdl::ScopedLock lock(mMutex);
        ret = _put(pkt);
    }
    
    if (pkt != &sFlushPacket && ret < 0)
        av_packet_unref(pkt);
    
    return ret;
}

int PacketQueue::get(AVPacket *pkt, bool block, int *serial)
{
    Item *pkt1;
    int ret;
    
    sdl::ScopedLock lock(mMutex);
    
    for (;;) {
        if (mAbortRequest) {
            ret = -1;
            break;
        }
        
        pkt1 = mFirstPacket;
        if (pkt1) {
            mFirstPacket = pkt1->next;
            if (!mFirstPacket)
                mLastPacket = nullptr;
            mNumPackets--;
            mSizeInBytes -= pkt1->packet.size + sizeof(*pkt1);
            mDuration -= pkt1->packet.duration;
            *pkt = pkt1->packet;
            if (serial)
                *serial = pkt1->serial;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(mCondVar, mMutex);
        }
    }
    
    return ret;
}
    
}//end namespace ffmpeg
