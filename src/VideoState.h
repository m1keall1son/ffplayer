//
//  VideoState.hpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

#include <string>

extern "C" {
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"
}

#include "Definitions.h"
#include "Clock.h"
#include "FrameQueue.h"
#include "PacketQueue.h"
#include "Decoder.h"
#include "AudioParams.h"
#include "Buffer.h"

enum AVSyncType {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

namespace ffmpeg {

class VideoState {
public:
    
    static void SDLAudioCallback(void* is, Uint8 *stream, int len);
    
    enum ShowMode {
        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
    };
    
    VideoState();
    ~VideoState();
    
    void draw();
    bool streamOpen(const std::string& filename, AVInputFormat *iformat);
    void streamClose();
    void stepToNextFrame();
    void toggleStreamPause();
    void togglePause();
    void toggleMute();
    void updateVolume(int sign, double step);
    void setDefaultWindowSize(int width, int height, AVRational sar);
    int getMasterSyncType();
    double getMasterClock();
    void toggleAudioDisplay();
    bool hasVideoStream();
    bool hasAudioStream();
    bool hasSubtitleStream();
    void seek(int amount);
    
    FrameQueue& getVideoFrameQueue(){return mPictureQueue;}
    FrameQueue& getAudioFrameQueue(){return mSampleQueue;}
    FrameQueue& getSubtitleFrameQueue(){return mSubtitleQueue;}

    inline void forceRefresh(){ mForceRefresh = 1; }
    inline int getForceRefresh(){ return mForceRefresh; }
    inline ShowMode getShowMode()const{return mShowMode;}
    inline int isPaused()const{return mPaused;}
    inline void seekByBytes(bool set = true){mSeekByBytes = set;}

    void videoRefresh(double *remaining_time);
    
private:
    
    static int ReadThread( void* is );
    static int VideoThread( void* is );
    static int AudioThread( void* is );
    static int SubtitleThread( void* is );
    void streamSeek(int64_t pos, int64_t rel, bool seek_by_bytes);
    void openWindow(const std::string& filename);
    static int StreamHasEnoughPackets(AVStream *st, int stream_id, PacketQueue *queue);
    static int DecodeInterruptCallback(void *ctx);
    int streamComponentOpen(int stream_index);
    void streamComponentClose(int stream_index);
    void drawAudioViz();
    void drawVideo();
    int decodeAudioFrame();
    int synchronizeAudio(int nb_samples);
    void updateSampleDisplay(short *samples, int samples_size);
    int getFrame(AVFrame *frame);
    int queuePicture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);
    void updateVideoPts(double pts, int64_t pos, int serial);
    void checkExternalClockSpeed();
    double vp_duration(Frame *vp, Frame *nextvp);
    double computeTargetDelay(double delay);
    
    SDL_Thread *mReadThread;
    AVInputFormat *mInputFormat;
    AVDictionary* mFormatOptions;
    AVDictionary* mCodecOptions;
    int mAbortRequest;
    int mForceRefresh;
    int mPaused;
    int mLastPaused;
    int mQueueAttachmentsReq;
    int mSeekReq;
    int mSeekFlags;
    int64_t mSeekPosition;
    int64_t mSeekRel;
    bool mSeekByBytes;
    int mReadPauseReturn;
    AVFormatContext *mFormatContext;
    int mRealtime;
    
    Clock mAudioClock;
    Clock mVideoClock;
    Clock mExternalClock;
    
    FrameQueue mPictureQueue;
    FrameQueue mSubtitleQueue;
    FrameQueue mSampleQueue;
    
    Decoder mAudioDecoder;
    Decoder mVideoDecoder;
    Decoder mSubDecoder;
    
    int mAudioStream;
    
    AVSyncType mSyncType;
    
    double mAudioClockTime;
    int mAudioClockSerial;
    
    double mAudioDiffCum; /* used for AV difference average computation */
    double mAudioDifAvgCoef;
    double mAudioDiffThresh;
    int mAudioDiffAvgCount;
    
    AVStream *mAudioAVStream;
    PacketQueue mAudioPacketQueue;
    int mAudioHWBufferSize;
    unsigned int mAudioBufferSize;
    uint8_t* mAudioBuffer;
    unsigned int mAudioBuffer1Size;
    uint8_t* mAudioBuffer1;
    int mAudioBufferIndex; /* in bytes */
    int mAudioWriteBufferSize;
    int mAudioVolume;
    int mMuted;
    AudioParams mAudioSource;
#if CONFIG_AVFILTER
    AudioParams mAudioFilterSource;
#endif
    AudioParams mAudioTarget;
    SwrContext *mSwrCtx;
    int mFrameDropsEarly;
    int mFrameDropsLate;
    
    ShowMode mShowMode;
    
    int16_t mSampleArray[SAMPLE_ARRAY_SIZE];
    int mSampleArrayIndex;
    int mLast_i_Start;
    RDFTContext *mRDFT;
    int mRDFTBits;
    FFTSample *mRDFTData;
    int mXPos;
    double mLastDisplayTime;
    SDL_Texture *mAudioVizTexture;
    SDL_Texture *mSubtitleTexture;
    SDL_Texture *mVideoTexture;
    
    int mSubtileStream;
    AVStream *mSubtitleAVStream;
    PacketQueue mSubtitlePacketQueue;
    
    double mFrameTimer;
    double mFrameLastReturnedTime;
    double mFrameLastFilterDelay;
    
    int mVideoStream;
    AVStream *mVideoAVStream;
    PacketQueue mVideoPacketQueue;
    
    double mMaxFrameDuration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    struct SwsContext *mImageConvertContext;
    struct SwsContext *mSubConvertContext;
    int mEOF;
    
    std::string mFilename;
    int mWidth, mHeight, mXLeft, mYTop;
    int mWindowWidth, mWindowHeight;
    int mStep;
    
#if CONFIG_AVFILTER
    int mVFilterIdX;
    AVFilterContext *mInVideoFiler;   // the first filter in the video chain
    AVFilterContext *mOutVideoFilter;  // the last filter in the video chain
    AVFilterContext *mInAudioFilter;   // the first filter in the audio chain
    AVFilterContext *mOutAudioFilter;  // the last filter in the audio chain
    AVFilterGraph *mAudioFilterGraph;  // audio filter graph
#endif
    
    int mLastVideoStream, mLastAudioStream, mLastSubtitleStream;
    
    SDL_cond *mContinueReadThread;
};
}//end namespace ffmepg
