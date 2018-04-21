//
//  FrameQueue.cpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#include "FrameQueue.h"
#include "PacketQueue.h"
#include "SDLUtil.h"

namespace ffmpeg {

void FrameQueue::UnrefItem(Frame *f)
{
    av_frame_unref(f->frame);
    avsubtitle_free(&f->subtitle);
}

FrameQueue::FrameQueue():
mRIndex(0),
mWIndex(0),
mSize(0),
mMaxSize(0),
mKeepLast(0),
mRIndexShown(0),
mMutex(nullptr),
mCondVar(nullptr),
mPacketQueue(nullptr)
{
}

FrameQueue::~FrameQueue()
{
    int i;
    for (i = 0; i < mMaxSize; i++) {
        Frame *vp = &mQueue[i];
        UnrefItem(vp);
        av_frame_free(&vp->frame);
    }
    SDL_DestroyMutex(mMutex);
    SDL_DestroyCond(mCondVar);
}

int FrameQueue::init(PacketQueue *pktq, int max_size, int keep_last)
{
    int i;
    if (!(mMutex = SDL_CreateMutex())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    if (!(mCondVar = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    mPacketQueue = pktq;
    mMaxSize = FFMIN(max_size, FRAME_QUEUE_SIZE);
    mKeepLast = !!keep_last;
    for (i = 0; i < mMaxSize; i++)
        if (!(mQueue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

void FrameQueue::signal()
{
    sdl::ScopedLock lock(mMutex);
    SDL_CondSignal(mCondVar);
}

Frame* FrameQueue::peek()
{
    return &mQueue[(mRIndex + mRIndexShown) % mMaxSize];
}

Frame* FrameQueue::peekNext()
{
    return &mQueue[(mRIndex + mRIndexShown + 1) % mMaxSize];
}

Frame* FrameQueue::peekLast()
{
    return &mQueue[mRIndex];
}

Frame* FrameQueue::peekWriteable()
{
    {
        /* wait until we have space to put a new frame */
        sdl::ScopedLock lock(mMutex);
        while (mSize >= mMaxSize &&
               !mPacketQueue->getAbortRequest()) {
            SDL_CondWait(mCondVar, mMutex);
        }
    }
    
    if (mPacketQueue->getAbortRequest())
        return nullptr;
    
    return &mQueue[mWIndex];
}

Frame* FrameQueue::peekReadable()
{
    {
        /* wait until we have a readable a new frame */
        sdl::ScopedLock lock(mMutex);
        while ((mSize - mRIndexShown) <= 0 &&
               !mPacketQueue->getAbortRequest()) {
            SDL_CondWait(mCondVar, mMutex);
        }
    }
    
    if (mPacketQueue->getAbortRequest())
        return NULL;
    
    return &mQueue[(mRIndex + mRIndexShown) % mMaxSize];
}

void FrameQueue::push()
{
    if (++mWIndex == mMaxSize)
        mWIndex = 0;
    {
        sdl::ScopedLock lock(mMutex);
        mSize++;
        SDL_CondSignal(mCondVar);
    }
}

void FrameQueue::next()
{
    if (mKeepLast && !mRIndexShown) {
        mRIndexShown = 1;
        return;
    }
    UnrefItem(&mQueue[mRIndex]);
    if (++mRIndex == mMaxSize){
        mRIndex = 0;
    }
    {
        sdl::ScopedLock lock(mMutex);
        mSize--;
        SDL_CondSignal(mCondVar);
    }
}

/* return the number of undisplayed frames in the queue */
int FrameQueue::numRemaining()const
{
    return mSize - mRIndexShown;
}

/* return last shown position */
int64_t FrameQueue::lastShownPosition()const
{
    const Frame* fp = &mQueue[mRIndex];
    if (mRIndexShown && fp->serial == mPacketQueue->getSerial())
        return fp->position;
    else
        return -1;
}
}//end namespace ffmpeg
