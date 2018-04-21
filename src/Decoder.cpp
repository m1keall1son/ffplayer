//
//  Decoder.cpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#include "Decoder.h"
#include "FrameQueue.h"

namespace ffmpeg {

int Decoder::REORDER_PTS = -1;

Decoder::Decoder():
mPacket(),
mQueue(nullptr),
mAVContext(nullptr),
mPacketSerial(-1),
mFinished(0),
mPacketPending(0),
mQueueEmptyCondVar(nullptr),
mStartPTS(AV_NOPTS_VALUE),
mStartPTS_TB({0,0}),
mNextPTS(0),
mNextPTS_TB({0,0}),
mDecoderThread(nullptr)
{}

Decoder::~Decoder()
{
}
    
void Decoder::init(AVCodecContext *avctx, ffmpeg::PacketQueue *queue, SDL_cond *empty_queue_cond)
{
    mAVContext = avctx;
    mQueue = queue;
    mQueueEmptyCondVar = empty_queue_cond;
    mStartPTS = AV_NOPTS_VALUE;
    mPacketSerial = -1;
}

void Decoder::destroy()
{
    av_packet_unref(&mPacket);
    avcodec_free_context(&mAVContext);
}
    
int Decoder::decodeFrame(AVFrame *frame, AVSubtitle *sub)
{
    int ret = AVERROR(EAGAIN);
    
    for (;;) {
        AVPacket pkt;
        
        if (mQueue->getSerial() == mPacketSerial) {
            do {
                if (mQueue->getAbortRequest())
                    return -1;
                
                switch (mAVContext->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                        ret = avcodec_receive_frame(mAVContext, frame);
                        if (ret >= 0) {
                            if (REORDER_PTS == -1) {
                                frame->pts = frame->best_effort_timestamp;
                            } else if (!REORDER_PTS) {
                                frame->pts = frame->pkt_dts;
                            }
                        }
                        break;
                    case AVMEDIA_TYPE_AUDIO:
                        ret = avcodec_receive_frame(mAVContext, frame);
                        if (ret >= 0) {
                            AVRational tb = (AVRational){1, frame->sample_rate};
                            if (frame->pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(frame->pts, mAVContext->pkt_timebase, tb);
                            else if (mNextPTS != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(mNextPTS, mNextPTS_TB, tb);
                            if (frame->pts != AV_NOPTS_VALUE) {
                                mNextPTS = frame->pts + frame->nb_samples;
                                mNextPTS_TB = tb;
                            }
                        }
                        break;
                    default: break;
                }
                if (ret == AVERROR_EOF) {
                    mFinished = mPacketSerial;
                    avcodec_flush_buffers(mAVContext);
                    return 0;
                }
                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }
        
        do {
            if (mQueue->getNumPackets() == 0)
                SDL_CondSignal(mQueueEmptyCondVar);
            if (mPacketPending) {
                av_packet_move_ref(&pkt, &mPacket);
                mPacketPending = 0;
            } else {
                if (mQueue->get(&pkt, true, &mPacketSerial) < 0)
                    return -1;
            }
        } while (mQueue->getSerial() != mPacketSerial);
        
        if (pkt.data == PacketQueue::sFlushPacket.data) {
            avcodec_flush_buffers(mAVContext);
            mFinished = 0;
            mNextPTS = mStartPTS;
            mNextPTS_TB = mStartPTS_TB;
        } else {
            if (mAVContext->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                int got_frame = 0;
                ret = avcodec_decode_subtitle2(mAVContext, sub, &got_frame, &pkt);
                if (ret < 0) {
                    ret = AVERROR(EAGAIN);
                } else {
                    if (got_frame && !pkt.data) {
                        mPacketPending = 1;
                        av_packet_move_ref(&mPacket, &pkt);
                    }
                    ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
                }
            } else {
                if (avcodec_send_packet(mAVContext, &pkt) == AVERROR(EAGAIN)) {
                    av_log(mAVContext, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                    mPacketPending = 1;
                    av_packet_move_ref(&mPacket, &pkt);
                }
            }
            av_packet_unref(&pkt);
        }
    }
}

void Decoder::abort(FrameQueue *fq)
{
    mQueue->abort();
    fq->signal();
    SDL_WaitThread(mDecoderThread, NULL);
    mDecoderThread = nullptr;
    mQueue->flush();
}

int Decoder::start(int(*threadFn)(void*), void *arg)
{
    mQueue->start();
    mDecoderThread = SDL_CreateThread(threadFn, "decoder", arg);
    if (!mDecoderThread) {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateThread(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}
    
}//end namespace ffmpeg
