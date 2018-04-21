//
//  VideoState.cpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#include "VideoState.h"
#include "SDLUtil.h"
#include "FFMPEGUtil.h"

namespace ffmpeg {
    
    VideoState::VideoState():
        mReadThread(nullptr),
        mInputFormat(nullptr),
        mFormatOptions(nullptr),
        mCodecOptions(nullptr),
        mAbortRequest(0),
        mForceRefresh(0),
        mPaused(0),
        mLastPaused(1),
        mQueueAttachmentsReq(0),
        mSeekReq(0),
        mSeekFlags(0),
        mSeekPosition(0),
        mSeekRel(0),
        mReadPauseReturn(0),
        mFormatContext(nullptr),
        mRealtime(0),
        mAudioStream(-1),
        mSyncType(AV_SYNC_VIDEO_MASTER),
        mAudioClockTime(0.0),
        mAudioClockSerial(0),
        mAudioDiffCum(0.0),
        mAudioDifAvgCoef(0.0),
        mAudioDiffThresh(0.0),
        mAudioDiffAvgCount(0),
        mAudioAVStream(nullptr),
        mAudioHWBufferSize(0),
        mAudioBufferSize(0),
        mAudioBuffer(nullptr),
        mAudioBuffer1Size(0),
        mAudioBuffer1(nullptr),
        mAudioBufferIndex(0),
        mAudioWriteBufferSize(0),
        mAudioVolume(0),
        mMuted(0),
        mSwrCtx(nullptr),
        mFrameDropsEarly(0),
        mFrameDropsLate(0),
        mShowMode(ShowMode::SHOW_MODE_NONE),
        mSampleArrayIndex(0),
        mLast_i_Start(0),
        mRDFT(nullptr),
        mRDFTBits(0),
        mRDFTData(nullptr),
        mXPos(0),
        mLastDisplayTime(0.0),
        mAudioVizTexture(nullptr),
        mSubtitleTexture(nullptr),
        mVideoTexture(nullptr),
        mSubtileStream(-1),
        mSubtitleAVStream(nullptr),
        mFrameTimer(0.0),
        mFrameLastReturnedTime(0.0),
        mFrameLastFilterDelay(0.0),
        mVideoStream(-1),
        mVideoAVStream(nullptr),
        mMaxFrameDuration(0.0),
        mImageConvertContext(nullptr),
        mSubConvertContext(nullptr),
        mEOF(0),
        mWidth(0),
        mHeight(0),
        mXLeft(0),
        mYTop(0),
        mWindowWidth(0),
        mWindowHeight(0),
        mStep(0),
        mLastVideoStream(-1),
        mLastAudioStream(-1),
        mLastSubtitleStream(-1),
        mContinueReadThread(nullptr)
    {}
    
    VideoState::~VideoState()
    {
        
    }
    
    int VideoState::DecodeInterruptCallback(void *ctx)
    {
        VideoState *is = (VideoState*)ctx;
        return is->mAbortRequest;
    }
    
    void VideoState::openWindow(const std::string& filename)
    {
        auto window = sdl::window();
        
        if (window->getTitle().empty())
            window->setTitle(filename);
    
        if(!window->isVisible()){
            window->show();
        }
        
        mWidth  = window->getWidth();
        mHeight = window->getHeight();
        
    }
    
    void VideoState::updateVideoPts(double pts, int64_t pos, int serial) {
        /* update current video pts */
        mVideoClock.set(pts, serial);
        mExternalClock.syncToSlave(&mVideoClock);
    }
    
    void VideoState::draw()
    {
        openWindow(mFilename);
        sdl::renderer()->setDrawColor(0, 0, 0, 255);
        sdl::renderer()->clear();
        if (mAudioAVStream && mShowMode != VideoState::SHOW_MODE_VIDEO)
            drawAudioViz();
        else if (mVideoAVStream)
            drawVideo();
        sdl::renderer()->present();
    }
    
    void VideoState::toggleAudioDisplay()
    {
        int next = mShowMode;
        do {
            next = (next + 1) % VideoState::SHOW_MODE_NB;
        } while (next != mShowMode && ((next == VideoState::SHOW_MODE_VIDEO && !mVideoAVStream) || (next != VideoState::SHOW_MODE_VIDEO && !mAudioAVStream)));
        if (mShowMode != next) {
            forceRefresh();
            mShowMode = (VideoState::ShowMode)next;
        }
    }
    
    bool VideoState::streamOpen( const std::string& filename, AVInputFormat *iformat)
    {
        mFilename = filename;
        if (mFilename.empty()){
            av_log(NULL, AV_LOG_ERROR, "must first open a video file!");
            streamClose();
            return false;
        }
        mInputFormat = iformat;
        mYTop    = 0;
        mXLeft   = 0;
        
        /* start video display */
        if (mPictureQueue.init(&mVideoPacketQueue, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0){
            av_log(NULL, AV_LOG_ERROR, "couldn't init the picture queue");
            streamClose();
            return false;
        }
        if (mSubtitleQueue.init(&mSubtitlePacketQueue, SUBPICTURE_QUEUE_SIZE, 0) < 0){
            av_log(NULL, AV_LOG_ERROR, "couldn't init the subpicture queue");
            streamClose();
            return false;
        }
        
        if (mSampleQueue.init(&mAudioPacketQueue, SAMPLE_QUEUE_SIZE, 1) < 0){
            av_log(NULL, AV_LOG_ERROR, "couldn't init the sample queue");
            streamClose();
            return false;
        }
        
        if (mVideoPacketQueue.init() < 0 || mAudioPacketQueue.init() < 0 || mSubtitlePacketQueue.init() < 0){
            av_log(NULL, AV_LOG_ERROR, "couldn't init one of the the packet queues");
            streamClose();
            return false;
        }
        
        if (!(mContinueReadThread = SDL_CreateCond())) {
            av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
            streamClose();
            return false;
        }
        
        //sync packet queues with clocks
        mVideoClock.init(mVideoPacketQueue.getSerialPtr());
        mAudioClock.init(mAudioPacketQueue.getSerialPtr());
        mExternalClock.init(mExternalClock.getSerialPtr());
        
        mAudioClockSerial = -1;
        
        mAudioVolume = av_clip(SDL_MIX_MAXVOLUME * 50 / 100, 0, SDL_MIX_MAXVOLUME);
        mMuted = 0;
        //TODO options?
        mSyncType = AV_SYNC_VIDEO_MASTER;
        mReadThread = SDL_CreateThread(&VideoState::ReadThread, "VideoState::ReadThread", (void*)this);
        if (!mReadThread) {
            av_log(NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
            streamClose();
            return false;
        }
        
        return true;
    }
    
    void VideoState::stepToNextFrame()
    {
        /* if the stream is paused unpause it, then step */
        if (mPaused)
            toggleStreamPause();
        mStep = 1;
    }
    
    void VideoState::toggleStreamPause()
    {
        if (mPaused) {
            mFrameTimer += av_gettime_relative() / 1000000.0 - mVideoClock.getLastUpdated();
            if (mReadPauseReturn != AVERROR(ENOSYS)) {
                mVideoClock.setPaused(0);
            }
            mVideoClock.set(mVideoClock.get(), mVideoClock.getSerial());
        }
        mExternalClock.set( mExternalClock.get(), mExternalClock.getSerial());
        auto val = !mPaused;
        mPaused = val;
        mAudioClock.setPaused(val);
        mVideoClock.setPaused(val);
        mExternalClock.setPaused(val);
    }
    
    void VideoState::togglePause()
    {
        toggleStreamPause();
        mStep = 0;
    }
    
    void VideoState::toggleMute()
    {
        mMuted = !mMuted;
    }
    
    void VideoState::updateVolume(int sign, double step)
    {
        double volume_level = mAudioVolume ? (20 * log(mAudioVolume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
        int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
        mAudioVolume = av_clip(mAudioVolume == new_volume ? (mAudioVolume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
    }
    
    void VideoState::streamSeek(int64_t pos, int64_t rel, bool seek_by_bytes)
    {
        if (!mSeekReq) {
            mSeekPosition = pos;
            mSeekRel = rel;
            mSeekFlags &= ~AVSEEK_FLAG_BYTE;
            if (seek_by_bytes)
                mSeekFlags |= AVSEEK_FLAG_BYTE;
            mSeekReq = 1;
            SDL_CondSignal(mContinueReadThread);
        }
    }
    
    int VideoState::StreamHasEnoughPackets(AVStream *st, int stream_id, PacketQueue *queue) {
        return stream_id < 0 ||
        queue->getAbortRequest() ||
        (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
        ((queue->getNumPackets() > MIN_FRAMES) && (!queue->getDuration() || av_q2d(st->time_base) * queue->getDuration() > 1.0));
    }
    
    void VideoState::setDefaultWindowSize(int width, int height, AVRational sar)
    {
        SDL_Rect rect;
        sdl::util::CalcDisplayRect(&rect, 0, 0, INT_MAX, height, width, height, sar);
        mWindowWidth  = rect.w;
        mWindowHeight = rect.h;
    }
    
    /* copy samples for viewing in editor window */
    void VideoState::updateSampleDisplay(short *samples, int samples_size)
    {
        int size, len;
        size = samples_size / sizeof(short);
        while (size > 0) {
            len = SAMPLE_ARRAY_SIZE - mSampleArrayIndex;
            if (len > size)
                len = size;
            memcpy(mSampleArray + mSampleArrayIndex, samples, len * sizeof(short));
            samples += len;
            mSampleArrayIndex += len;
            if (mSampleArrayIndex >= SAMPLE_ARRAY_SIZE)
                mSampleArrayIndex = 0;
            size -= len;
        }
    }
    
    int VideoState::getMasterSyncType() {
        if (mSyncType == AV_SYNC_VIDEO_MASTER) {
            if (mVideoAVStream)
                return AV_SYNC_VIDEO_MASTER;
            else
                return AV_SYNC_AUDIO_MASTER;
        } else if (mSyncType == AV_SYNC_AUDIO_MASTER) {
            if (mAudioAVStream)
                return AV_SYNC_AUDIO_MASTER;
            else
                return AV_SYNC_EXTERNAL_CLOCK;
        } else {
            return AV_SYNC_EXTERNAL_CLOCK;
        }
    }
    
    /* get the current master clock value */
    double VideoState::getMasterClock()
    {
        double val;
        
        switch (getMasterSyncType()) {
            case AV_SYNC_VIDEO_MASTER:
                val = mVideoClock.get();
                break;
            case AV_SYNC_AUDIO_MASTER:
                val = mAudioClock.get();
                break;
            default:
                val = mExternalClock.get();
                break;
        }
        return val;
    }
    
    /* return the wanted number of samples to get better sync if sync_type is video
     * or external master clock */
    int VideoState::synchronizeAudio(int nb_samples)
    {
        int wanted_nb_samples = nb_samples;
        
        /* if not master, then we try to remove or add samples to correct the clock */
        if (getMasterSyncType() != AV_SYNC_AUDIO_MASTER) {
            double diff, avg_diff;
            int min_nb_samples, max_nb_samples;
            
            diff = mAudioClock.get() - getMasterClock();
            
            if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
                mAudioDiffCum = diff + mAudioDifAvgCoef * mAudioDiffCum;
                if (mAudioDiffAvgCount < AUDIO_DIFF_AVG_NB) {
                    /* not enough measures to have a correct estimate */
                    mAudioDiffAvgCount++;
                } else {
                    /* estimate the A-V difference */
                    avg_diff = mAudioDiffCum * (1.0 -mAudioDifAvgCoef);
                    
                    if (fabs(avg_diff) >= mAudioDiffThresh) {
                        wanted_nb_samples = nb_samples + (int)(diff * mAudioSource.freq);
                        min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                        max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                        wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                    }
                    av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                           diff, avg_diff, wanted_nb_samples - nb_samples,
                           mAudioClockTime, mAudioDiffThresh);
                }
            } else {
                /* too big difference : may be initial PTS errors, so
                 reset A-V filter */
                mAudioDiffAvgCount = 0;
                mAudioDifAvgCoef       = 0;
            }
        }
        
        return wanted_nb_samples;
    }
    
    int VideoState::decodeAudioFrame()
    {
        int data_size, resampled_data_size;
        int64_t dec_channel_layout;
        av_unused double audio_clock0;
        int wanted_nb_samples;
        Frame *af;
        
        if (mPaused)
            return -1;
        
        do {
#if defined(_WIN32)
            while (mSampleQueue.numRemaining() == 0) {
                if ((av_gettime_relative() - sdl::GetAudioCallbackTime()) > 1000000LL * mAudioHWBufferSize / mAudioTarget.bytes_per_sec / 2)
                    return -1;
                av_usleep (1000);
            }
#endif
            if (!(af = mSampleQueue.peekReadable()))
                return -1;
            mSampleQueue.next();
        } while (af->serial != mAudioPacketQueue.getSerial());
        
        data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
                                               af->frame->nb_samples,
                                               (AVSampleFormat)af->frame->format, 1);
        
        dec_channel_layout =
        (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
        wanted_nb_samples = synchronizeAudio(af->frame->nb_samples);
        
        if (af->frame->format        != mAudioSource.fmt            ||
            dec_channel_layout       != mAudioSource.channel_layout ||
            af->frame->sample_rate   != mAudioSource.freq           ||
            (wanted_nb_samples       != af->frame->nb_samples && !mSwrCtx)) {
            swr_free(&mSwrCtx);
            mSwrCtx = swr_alloc_set_opts(NULL,
                                             mAudioTarget.channel_layout, mAudioTarget.fmt, mAudioTarget.freq,
                                             dec_channel_layout,           (AVSampleFormat)af->frame->format, af->frame->sample_rate,
                                             0, NULL);
            if (!mSwrCtx || swr_init(mSwrCtx) < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                       af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->channels,
                       mAudioTarget.freq, av_get_sample_fmt_name(mAudioTarget.fmt), mAudioTarget.channels);
                swr_free(&mSwrCtx);
                return -1;
            }
            mAudioSource.channel_layout = dec_channel_layout;
            mAudioSource.channels       = af->frame->channels;
            mAudioSource.freq = af->frame->sample_rate;
            mAudioSource.fmt = (AVSampleFormat)af->frame->format;
        }
        
        if (mSwrCtx) {
            const uint8_t **in = (const uint8_t **)af->frame->extended_data;
            uint8_t **out = &mAudioBuffer1;
            int out_count = (int64_t)wanted_nb_samples * mAudioTarget.freq / af->frame->sample_rate + 256;
            int out_size  = av_samples_get_buffer_size(NULL, mAudioTarget.channels, out_count, mAudioTarget.fmt, 0);
            int len2;
            if (out_size < 0) {
                av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
                return -1;
            }
            if (wanted_nb_samples != af->frame->nb_samples) {
                if (swr_set_compensation(mSwrCtx, (wanted_nb_samples - af->frame->nb_samples) * mAudioTarget.freq / af->frame->sample_rate,
                                         wanted_nb_samples * mAudioTarget.freq / af->frame->sample_rate) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                    return -1;
                }
            }
            av_fast_malloc(&mAudioBuffer1, &mAudioBuffer1Size, out_size);
            if (!mAudioBuffer1)
                return AVERROR(ENOMEM);
            len2 = swr_convert(mSwrCtx, out, out_count, in, af->frame->nb_samples);
            if (len2 < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
                return -1;
            }
            if (len2 == out_count) {
                av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
                if (swr_init(mSwrCtx) < 0)
                    swr_free(&mSwrCtx);
            }
            mAudioBuffer = mAudioBuffer1;
            resampled_data_size = len2 * mAudioTarget.channels * av_get_bytes_per_sample(mAudioTarget.fmt);
        } else {
            mAudioBuffer = af->frame->data[0];
            resampled_data_size = data_size;
        }
        
        audio_clock0 = mAudioClockTime;
        /* update the audio clock with the pts */
        if (!isnan(af->pts))
            mAudioClockTime = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
        else
            mAudioClockTime = NAN;
        mAudioClockSerial = af->serial;
#ifdef DEBUG
        {
            static double last_clock;
            printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
                   mAudioClockTime - last_clock,
                   mAudioClockTime, audio_clock0);
            last_clock = mAudioClockTime;
        }
#endif
        return resampled_data_size;
    }
    
    /* prepare a new audio buffer */
    void VideoState::SDLAudioCallback(void *opaque, Uint8 *stream, int len)
    {
        VideoState *is = (VideoState*)opaque;
        int audio_size, len1;
        
        sdl::GetAudioCallbackTime() = av_gettime_relative();
        
        while (len > 0) {
            if (is->mAudioBufferIndex >= is->mAudioBufferSize) {
                audio_size = is->decodeAudioFrame();
                if (audio_size < 0) {
                    /* if error, just output silence */
                    is->mAudioBuffer = NULL;
                    is->mAudioBufferSize = SDL_AUDIO_MIN_BUFFER_SIZE / is->mAudioTarget.frame_size * is->mAudioTarget.frame_size;
                } else {
                    if (is->mShowMode != VideoState::SHOW_MODE_VIDEO)
                        is->updateSampleDisplay((int16_t *)is->mAudioBuffer, audio_size);
                    is->mAudioBufferSize = audio_size;
                }
                is->mAudioBufferIndex = 0;
            }
            len1 = is->mAudioBufferSize - is->mAudioBufferIndex;
            if (len1 > len)
                len1 = len;
            if (!is->mMuted && is->mAudioBuffer && is->mAudioVolume == SDL_MIX_MAXVOLUME)
                memcpy(stream, (uint8_t *)is->mAudioBuffer + is->mAudioBufferIndex, len1);
            else {
                memset(stream, 0, len1);
                if (!is->mMuted && is->mAudioBuffer)
                    SDL_MixAudioFormat(stream, (uint8_t *)is->mAudioBuffer + is->mAudioBufferIndex, AUDIO_S16SYS, len1, is->mAudioVolume);
            }
            len -= len1;
            stream += len1;
            is->mAudioBufferIndex += len1;
        }
        is->mAudioWriteBufferSize = is->mAudioBufferSize - is->mAudioBufferIndex;
        /* Let's assume the audio driver that is used by SDL has two periods. */
        if (!isnan(is->mAudioClockTime)) {
            is->mAudioClock.setAt(is->mAudioClockTime - (double)(2 * is->mAudioHWBufferSize + is->mAudioWriteBufferSize) / is->mAudioTarget.bytes_per_sec, is->mAudioClockSerial, sdl::GetAudioCallbackTime() / 1000000.0);
            is->mExternalClock.syncToSlave(&is->mAudioClock);
        }
    }
    
    int VideoState::streamComponentOpen(int stream_index)
    {
        AVFormatContext *ic = mFormatContext;
        AVCodecContext *avctx;
        AVCodec *codec;
        const char *forced_codec_name = NULL;
        AVDictionary *opts = NULL;
        AVDictionaryEntry *t = NULL;
        int sample_rate, nb_channels;
        int64_t channel_layout;
        int ret = 0;
        int stream_lowres = opts::lowres();
        
        if (stream_index < 0 || stream_index >= ic->nb_streams)
            return -1;
        
        avctx = avcodec_alloc_context3(NULL);
        if (!avctx)
            return AVERROR(ENOMEM);
        
        ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
        if (ret < 0)
            goto fail;
        avctx->pkt_timebase = ic->streams[stream_index]->time_base;
        
        codec = avcodec_find_decoder(avctx->codec_id);
        
        switch(avctx->codec_type){
            case AVMEDIA_TYPE_AUDIO   : mLastAudioStream    = stream_index; forced_codec_name =    opts::audioCodec(); break;
            case AVMEDIA_TYPE_SUBTITLE: mLastSubtitleStream = stream_index; forced_codec_name = opts::subtitleCodec(); break;
            case AVMEDIA_TYPE_VIDEO   : mLastVideoStream    = stream_index; forced_codec_name =    opts::videoCodec(); break;
            default: break;
        }
        if (forced_codec_name)
            codec = avcodec_find_decoder_by_name(forced_codec_name);
        if (!codec) {
            if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
                                          "No codec could be found with name '%s'\n", forced_codec_name);
            else                   av_log(NULL, AV_LOG_WARNING,
                                          "No codec could be found with id %d\n", avctx->codec_id);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        
        avctx->codec_id = codec->id;
        if (stream_lowres > codec->max_lowres) {
            av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                   codec->max_lowres);
            stream_lowres = codec->max_lowres;
        }
        avctx->lowres = stream_lowres;
        
        if (opts::fast())
            avctx->flags2 |= AV_CODEC_FLAG2_FAST;
        
        opts = opts::filter_codec_opts(mCodecOptions, avctx->codec_id, ic, ic->streams[stream_index], codec);
        if (!av_dict_get(opts, "threads", NULL, 0))
            av_dict_set(&opts, "threads", "auto", 0);
        if (stream_lowres)
            av_dict_set_int(&opts, "lowres", stream_lowres, 0);
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
            av_dict_set(&opts, "refcounted_frames", "1", 0);
        if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
            goto fail;
        }
        if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
            av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
            ret =  AVERROR_OPTION_NOT_FOUND;
            goto fail;
        }
        
        mEOF = 0;
        ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
        switch (avctx->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
#if CONFIG_AVFILTER
            {
                AVFilterContext *sink;
                
                is->audio_filter_src.freq           = avctx->sample_rate;
                is->audio_filter_src.channels       = avctx->channels;
                is->audio_filter_src.channel_layout = get_valid_channel_layout(avctx->channel_layout, avctx->channels);
                is->audio_filter_src.fmt            = avctx->sample_fmt;
                if ((ret = configure_audio_filters(is, afilters, 0)) < 0)
                    goto fail;
                sink = is->out_audio_filter;
                sample_rate    = av_buffersink_get_sample_rate(sink);
                nb_channels    = av_buffersink_get_channels(sink);
                channel_layout = av_buffersink_get_channel_layout(sink);
            }
#else
                sample_rate    = avctx->sample_rate;
                nb_channels    = avctx->channels;
                channel_layout = avctx->channel_layout;
#endif
                
                /* prepare audio output */
                if ((ret = sdl::AudioOpen((void*)this, channel_layout, nb_channels, sample_rate, &mAudioTarget)) < 0)
                    goto fail;
                mAudioHWBufferSize = ret;
                mAudioSource = mAudioTarget;
                mAudioBufferSize = 0;
                mAudioBufferIndex = 0;
                
                /* init averaging filter */
                mAudioDifAvgCoef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
                mAudioDiffAvgCount = 0;
                /* since we do not have a precise anough audio FIFO fullness,
                 we correct audio sync only if larger than this threshold */
                mAudioDiffThresh = (double)(mAudioHWBufferSize) / mAudioTarget.bytes_per_sec;
                
                mAudioStream = stream_index;
                mAudioAVStream = ic->streams[stream_index];
                
                mAudioDecoder.init(avctx, &mAudioPacketQueue, mContinueReadThread);
                if ((mFormatContext->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !mFormatContext->iformat->read_seek) {
                    mAudioDecoder.setStartPts(mAudioAVStream->start_time);
                    mAudioDecoder.setStartPtsTimeBase(mAudioAVStream->time_base);
                }
                if ((ret = mAudioDecoder.start(VideoState::AudioThread, (void*)this)) < 0)
                    goto out;
                SDL_PauseAudioDevice(sdl::audioDevice(), 0);
                break;
            case AVMEDIA_TYPE_VIDEO:
                mVideoStream = stream_index;
                mVideoAVStream = ic->streams[stream_index];
                
                mVideoDecoder.init(avctx, &mVideoPacketQueue, mContinueReadThread);
                if ((ret = mVideoDecoder.start(VideoState::VideoThread, (void*)this)) < 0)
                    goto out;
                mQueueAttachmentsReq = 1;
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                mSubtileStream = stream_index;
                mSubtitleAVStream = ic->streams[stream_index];
                
                mSubDecoder.init(avctx, &mSubtitlePacketQueue, mContinueReadThread);
                if ((ret = mSubDecoder.start(VideoState::SubtitleThread, (void*)this)) < 0)
                    goto out;
                break;
            default:
                break;
        }
        goto out;
        
    fail:
        avcodec_free_context(&avctx);
    out:
        av_dict_free(&opts);
        
        return ret;
    }
    
    void VideoState::streamComponentClose(int stream_index)
    {
        AVFormatContext *ic = mFormatContext;
        AVCodecParameters *codecpar;
        
        if (stream_index < 0 || stream_index >= ic->nb_streams)
            return;
        codecpar = ic->streams[stream_index]->codecpar;
        
        switch (codecpar->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                mAudioDecoder.abort(&mSampleQueue);
                SDL_CloseAudioDevice(sdl::audioDevice());
                mAudioDecoder.destroy();
                swr_free(&mSwrCtx);
                av_freep(&mAudioBuffer1);
                mAudioBuffer1Size = 0;
                mAudioBuffer = NULL;
                
                if (mRDFT) {
                    av_rdft_end(mRDFT);
                    av_freep(&mRDFTData);
                    mRDFT = NULL;
                    mRDFTBits = 0;
                }
                break;
            case AVMEDIA_TYPE_VIDEO:
                mVideoDecoder.abort(&mPictureQueue);
                mVideoDecoder.destroy();
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                mSubDecoder.abort(&mSubtitleQueue);
                mSubDecoder.destroy();
                break;
            default:
                break;
        }
        
        ic->streams[stream_index]->discard = AVDISCARD_ALL;
        switch (codecpar->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                mAudioAVStream = NULL;
                mAudioStream = -1;
                break;
            case AVMEDIA_TYPE_VIDEO:
                mVideoAVStream = NULL;
                mVideoStream = -1;
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                mSubtitleAVStream = NULL;
                mSubtileStream = -1;
                break;
            default:
                break;
        }
    }
    
    int VideoState::ReadThread(void *arg)
    {
        VideoState *is = (VideoState*)arg;
        AVFormatContext *ic = NULL;
        int err, i, ret;
        int st_index[AVMEDIA_TYPE_NB];
        AVPacket pkt1, *pkt = &pkt1;
        int64_t stream_start_time;
        int pkt_in_play_range = 0;
        AVDictionaryEntry *t;
        SDL_mutex *wait_mutex = SDL_CreateMutex();
        int scan_all_pmts_set = 0;
        int64_t pkt_ts;
        
        if (!wait_mutex) {
            av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        
        memset(st_index, -1, sizeof(st_index));
        is->mLastVideoStream = is->mVideoStream = -1;
        is->mLastAudioStream = is->mAudioStream = -1;
        is->mLastSubtitleStream = is->mSubtileStream = -1;
        is->mEOF = 0;
        
        ic = avformat_alloc_context();
        if (!ic) {
            av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        
        ic->interrupt_callback.callback = VideoState::DecodeInterruptCallback;
        ic->interrupt_callback.opaque = is;
        if (!av_dict_get(is->mFormatOptions, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
            av_dict_set(&is->mFormatOptions, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
            scan_all_pmts_set = 1;
        }
        
        err = avformat_open_input(&ic, is->mFilename.c_str(), is->mInputFormat, &is->mFormatOptions);
        
        if (err < 0) {
            PrintError(is->mFilename.c_str(), err);
            ret = -1;
            goto fail;
        }
        
        if (scan_all_pmts_set)
            av_dict_set(&is->mFormatOptions, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
        
        if ((t = av_dict_get(is->mFormatOptions, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
            av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
            ret = AVERROR_OPTION_NOT_FOUND;
            goto fail;
        }
        
        is->mFormatContext = ic;
        
        if (opts::getPts())
            ic->flags |= AVFMT_FLAG_GENPTS;
        
        av_format_inject_global_side_data(ic);
        
        if (opts::findStreamInfo()) {
            AVDictionary **opts = opts::setup_find_stream_info_opts(ic, is->mCodecOptions);
            int orig_nb_streams = ic->nb_streams;
            
            err = avformat_find_stream_info(ic, opts);
            
            for (i = 0; i < orig_nb_streams; i++)
                av_dict_free(&opts[i]);
            av_freep(&opts);
            
            if (err < 0) {
                av_log(NULL, AV_LOG_WARNING,
                       "%s: could not find codec parameters\n", is->mFilename.c_str());
                ret = -1;
                goto fail;
            }
        }
        
        if (ic->pb)
            ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end
        
        if (opts::seekByBytes() < 0)
            opts::seekByBytes() = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);
        
        is->mMaxFrameDuration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
        
//        if (!window_title && (t = av_dict_get(ic->metadata, "title", NULL, 0)))
//            window_title = av_asprintf("%s - %s", t->value, input_filename);
        
        /* if seeking requested, we execute it */
        if (opts::startTime() != AV_NOPTS_VALUE) {
            int64_t timestamp;
            
            timestamp = opts::startTime();
            /* add the stream start time */
            if (ic->start_time != AV_NOPTS_VALUE)
                timestamp += ic->start_time;
            ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
            if (ret < 0) {
                av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n", is->mFilename.c_str(), (double)timestamp / AV_TIME_BASE);
            }
        }
        
        is->mRealtime = util::IsRealtime(ic);
        
        if (opts::showStatus())
            av_dump_format(ic, 0, is->mFilename.c_str(), 0);
        
        for (i = 0; i < ic->nb_streams; i++) {
            AVStream *st = ic->streams[i];
            enum AVMediaType type = st->codecpar->codec_type;
            st->discard = AVDISCARD_ALL;
            if (type >= 0 && opts::wantedStreamSpec[type] && st_index[type] == -1)
                if (avformat_match_stream_specifier(ic, st, opts::wantedStreamSpec[type]) > 0)
                    st_index[type] = i;
        }
        for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
            if (opts::wantedStreamSpec[i] && st_index[i] == -1) {
                av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", opts::wantedStreamSpec[i], av_get_media_type_string((AVMediaType)i));
                st_index[i] = INT_MAX;
            }
        }
        
        if (sdl::IsVideoEnabled())
            st_index[AVMEDIA_TYPE_VIDEO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
        
        if (sdl::IsAudioEnabled())
            st_index[AVMEDIA_TYPE_AUDIO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                NULL, 0);
        
        if (sdl::IsVideoEnabled() && opts::subtitlesEnabled())
            st_index[AVMEDIA_TYPE_SUBTITLE] =
            av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                                st_index[AVMEDIA_TYPE_SUBTITLE],
                                (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                 st_index[AVMEDIA_TYPE_AUDIO] :
                                 st_index[AVMEDIA_TYPE_VIDEO]),
                                NULL, 0);
        
        is->mShowMode = (ShowMode)opts::showMode();
        
        if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
            AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
            AVCodecParameters *codecpar = st->codecpar;
            AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
            if (codecpar->width)
                is->setDefaultWindowSize(codecpar->width, codecpar->height, sar);
        }
        
        /* open the streams */
        if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
            is->streamComponentOpen(st_index[AVMEDIA_TYPE_AUDIO]);
        }
        
        ret = -1;
        if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
            ret = is->streamComponentOpen(st_index[AVMEDIA_TYPE_VIDEO]);
        }
        
        if (is->mShowMode == VideoState::SHOW_MODE_NONE)
            is->mShowMode = ret >= 0 ? VideoState::SHOW_MODE_VIDEO : VideoState::SHOW_MODE_RDFT;
        
        if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
            is->streamComponentOpen(st_index[AVMEDIA_TYPE_SUBTITLE]);
        }
        
        if (is->mVideoStream < 0 && is->mAudioStream < 0) {
            av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n", is->mFilename.c_str());
            ret = -1;
            goto fail;
        }
        
        if (opts::infiniteBuffer() < 0 && is->mRealtime)
            opts::infiniteBuffer() = 1;
        
        for (;;) {
            if (is->mAbortRequest)
                break;
            if (is->mPaused != is->mLastPaused) {
                is->mLastPaused = is->mPaused;
                if (is->mPaused)
                    is->mReadPauseReturn = av_read_pause(ic);
                else
                    av_read_play(ic);
            }
#if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
            if (is->mPaused &&
                (!strcmp(ic->iformat->name, "rtsp") ||
                 (ic->pb && !strncmp(is->mFilename.c_str(), "mmsh:", 5)))) {
                    /* wait 10 ms to avoid trying to get another packet */
                    /* XXX: horrible */
                    SDL_Delay(10);
                    continue;
                }
#endif
            if (is->mSeekReq) {
                int64_t seek_target = is->mSeekPosition;
                int64_t seek_min    = is->mSeekRel > 0 ? seek_target - is->mSeekRel + 2: INT64_MIN;
                int64_t seek_max    = is->mSeekRel < 0 ? seek_target - is->mSeekRel - 2: INT64_MAX;
                // FIXME the +-2 is due to rounding being not done in the correct direction in generation
                //      of the seek_pos/seek_rel variables
                
                ret = avformat_seek_file(is->mFormatContext, -1, seek_min, seek_target, seek_max, is->mSeekFlags);
                
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR,
                           //"%s: error while seeking\n", is->mFormatContext->url);
                           "%s: error while seeking\n", is->mFormatContext->filename);
                } else {
                    if (is->mAudioStream >= 0) {
                        is->mAudioPacketQueue.flush();
                        is->mAudioPacketQueue.put(&PacketQueue::sFlushPacket);
                    }
                    if (is->mSubtileStream >= 0) {
                        is->mSubtitlePacketQueue.flush();
                        is->mSubtitlePacketQueue.put(&PacketQueue::sFlushPacket);
                    }
                    if (is->mVideoStream >= 0) {
                        is->mVideoPacketQueue.flush();
                        is->mVideoPacketQueue.put(&PacketQueue::sFlushPacket);
                    }
                    if (is->mSeekFlags & AVSEEK_FLAG_BYTE) {
                        is->mExternalClock.set(NAN, 0);
                    } else {
                        is->mExternalClock.set(seek_target / (double)AV_TIME_BASE, 0);
                    }
                }
                
                is->mSeekReq = 0;
                is->mQueueAttachmentsReq = 1;
                is->mEOF = 0;
                if (is->mPaused)
                    is->stepToNextFrame();
            }
            if (is->mQueueAttachmentsReq) {
                if (is->mVideoAVStream && is->mVideoAVStream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                    AVPacket copy = { 0 };
                    if ((ret = av_packet_ref(&copy, &is->mVideoAVStream->attached_pic)) < 0)
                        goto fail;
                    is->mVideoPacketQueue.put(&copy);
                    is->mVideoPacketQueue.putNullPacket(is->mVideoStream);
                }
                is->mQueueAttachmentsReq = 0;
            }
            
            /* if the queue are full, no need to read more */
            if (opts::infiniteBuffer()<1 &&
                (is->mAudioPacketQueue.size() + is->mVideoPacketQueue.size() + is->mSubtitlePacketQueue.size() > MAX_QUEUE_SIZE
                 || (StreamHasEnoughPackets(is->mAudioAVStream, is->mAudioStream, &is->mAudioPacketQueue) &&
                     StreamHasEnoughPackets(is->mVideoAVStream, is->mVideoStream, &is->mVideoPacketQueue) &&
                     StreamHasEnoughPackets(is->mSubtitleAVStream, is->mSubtileStream, &is->mSubtitlePacketQueue)))) {
                     /* wait 10 ms */
                     SDL_LockMutex(wait_mutex);
                     SDL_CondWaitTimeout(is->mContinueReadThread, wait_mutex, 10);
                     SDL_UnlockMutex(wait_mutex);
                     continue;
                 }
            if (!is->mPaused &&
                (!is->mAudioStream || (is->mAudioDecoder.getFinished() == is->mAudioPacketQueue.getSerial() && is->mSampleQueue.numRemaining() == 0)) &&
                (!is->mVideoStream || (is->mVideoDecoder.getFinished() == is->mVideoPacketQueue.getSerial() && is->mPictureQueue.numRemaining() == 0))) {
                if (opts::loopCount() != 1 && (!opts::loopCount() || --opts::loopCount())) {
                    is->streamSeek(opts::startTime() != AV_NOPTS_VALUE ? opts::startTime() : 0, 0, 0);
                } else if (opts::autoexit()) {
                    ret = AVERROR_EOF;
                    goto fail;
                }
            }
            ret = av_read_frame(ic, pkt);
            if (ret < 0) {
                if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->mEOF) {
                    if (is->mVideoStream >= 0)
                        is->mVideoPacketQueue.putNullPacket(is->mVideoStream);
                    if (is->mAudioStream >= 0)
                        is->mAudioPacketQueue.putNullPacket(is->mAudioStream);
                    if (is->mSubtileStream >= 0)
                        is->mSubtitlePacketQueue.putNullPacket(is->mSubtileStream);
                    is->mEOF = 1;
                }
                if (ic->pb && ic->pb->error)
                    break;
                SDL_LockMutex(wait_mutex);
                SDL_CondWaitTimeout(is->mContinueReadThread, wait_mutex, 10);
                SDL_UnlockMutex(wait_mutex);
                continue;
            } else {
                is->mEOF = 0;
            }
            /* check if packet is in play range specified by user, then queue, otherwise discard */
            stream_start_time = ic->streams[pkt->stream_index]->start_time;
            pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
            pkt_in_play_range = opts::duration() == AV_NOPTS_VALUE ||
            (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
            av_q2d(ic->streams[pkt->stream_index]->time_base) -
            (double)(opts::startTime() != AV_NOPTS_VALUE ? opts::startTime() : 0) / 1000000
            <= ((double)opts::duration() / 1000000);
            if (pkt->stream_index == is->mAudioStream && pkt_in_play_range) {
                is->mAudioPacketQueue.put(pkt);
            } else if (pkt->stream_index == is->mVideoStream && pkt_in_play_range
                       && !(is->mVideoAVStream->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                is->mVideoPacketQueue.put(pkt);
            } else if (pkt->stream_index == is->mSubtileStream && pkt_in_play_range) {
                is->mSubtitlePacketQueue.put(pkt);
            } else {
                av_packet_unref(pkt);
            }
        }
        
        ret = 0;
    fail:
        if (ic && !is->mFormatContext)
            avformat_close_input(&ic);
        
        if (ret != 0) {
            SDL_Event event;
            
            event.type = FF_QUIT_EVENT;
            event.user.data1 = is;
            SDL_PushEvent(&event);
        }
        SDL_DestroyMutex(wait_mutex);
        return 0;
    }
    
    void VideoState::streamClose()
    {
        /* XXX: use a special url_shutdown call to abort parse cleanly */
        mAbortRequest = 1;
        SDL_WaitThread(mReadThread, NULL);
        
        /* close each stream */
        if (mAudioStream >= 0)
            streamComponentClose(mAudioStream);
        if (mVideoStream >= 0)
            streamComponentClose(mVideoStream);
        if (mSubtileStream >= 0)
            streamComponentClose(mSubtileStream);

        avformat_close_input(&mFormatContext);
        
        //destroyed by destructors
//        packet_queue_destroy(&is->videoq);
//        packet_queue_destroy(&is->audioq);
//        packet_queue_destroy(&is->subtitleq);
//
//        /* free all pictures */
//        frame_queue_destory(&is->pictq);
//        frame_queue_destory(&is->sampq);
//        frame_queue_destory(&is->subpq);
        
        SDL_DestroyCond(mContinueReadThread);
        sws_freeContext(mImageConvertContext);
        sws_freeContext(mSubConvertContext);
        
        if (mAudioVizTexture)
            SDL_DestroyTexture(mAudioVizTexture);
        if (mVideoTexture)
            SDL_DestroyTexture(mVideoTexture);
        if (mSubtitleTexture)
            SDL_DestroyTexture(mSubtitleTexture);
    }
    
    int VideoState::queuePicture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
    {
        Frame *vp;
        
#if defined(DEBUG_SYNC)
        printf("frame_type=%c pts=%0.3f\n",
               av_get_picture_type_char(src_frame->pict_type), pts);
#endif
        
        if (!(vp = mPictureQueue.peekWriteable()))
            return -1;
        
        vp->sar = src_frame->sample_aspect_ratio;
        vp->uploaded = 0;
        
        vp->width = src_frame->width;
        vp->height = src_frame->height;
        vp->format = src_frame->format;
        
        vp->pts = pts;
        vp->duration = duration;
        vp->position = pos;
        vp->serial = serial;
        
        setDefaultWindowSize(vp->width, vp->height, vp->sar);
        
        av_frame_move_ref(vp->frame, src_frame);
        mPictureQueue.push();
        return 0;
    }
    
    void VideoState::drawVideo()
    {
       
        Frame *vp;
        Frame *sp = NULL;
        SDL_Rect rect;
        
        vp = mPictureQueue.peekLast();
        if (mSubtitleAVStream) {
            if (mSubtitleQueue.numRemaining() > 0) {
                sp = mSubtitleQueue.peek();
                
                if (vp->pts >= sp->pts + ((float) sp->subtitle.start_display_time / 1000)) {
                    if (!sp->uploaded) {
                        uint8_t* pixels[4];
                        int pitch[4];
                        int i;
                        if (!sp->width || !sp->height) {
                            sp->width = vp->width;
                            sp->height = vp->height;
                        }
                        if (sdl::util::ReallocTexture(&mSubtitleTexture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
                            return;
                        
                        for (i = 0; i < sp->subtitle.num_rects; i++) {
                            AVSubtitleRect *sub_rect = sp->subtitle.rects[i];
                            
                            sub_rect->x = av_clip(sub_rect->x, 0, sp->width );
                            sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
                            sub_rect->w = av_clip(sub_rect->w, 0, sp->width  - sub_rect->x);
                            sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);
                            
                            mSubConvertContext = sws_getCachedContext(mSubConvertContext,
                                                                       sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
                                                                       sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
                                                                       0, NULL, NULL, NULL);
                            if (!mSubConvertContext) {
                                av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                                return;
                            }
                            if (!SDL_LockTexture(mSubtitleTexture, (SDL_Rect *)sub_rect, (void **)pixels, pitch)) {
                                sws_scale(mSubConvertContext, (const uint8_t * const *)sub_rect->data, sub_rect->linesize,
                                          0, sub_rect->h, pixels, pitch);
                                SDL_UnlockTexture(mSubtitleTexture);
                            }
                        }
                        sp->uploaded = 1;
                    }
                } else
                    sp = NULL;
            }
        }
        
        sdl::util::CalcDisplayRect(&rect, mXLeft, mYTop, mWidth, mHeight, vp->width, vp->height, vp->sar);
        
        if (!vp->uploaded) {
            if (sdl::util::UploadTexture(&mVideoTexture, vp->frame, &mImageConvertContext) < 0)
                return;
            vp->uploaded = 1;
            vp->vflip = vp->frame->linesize[0] < 0;
        }
        
        SDL_RenderCopyEx(sdl::renderer()->getHandle(), mVideoTexture, NULL, &rect, 0, NULL, vp->vflip ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
        if (sp) {
#if USE_ONEPASS_SUBTITLE_RENDER
            SDL_RenderCopy(renderer, is->sub_texture, NULL, &rect);
#else
            int i;
            double xratio = (double)rect.w / (double)sp->width;
            double yratio = (double)rect.h / (double)sp->height;
            for (i = 0; i < sp->subtitle.num_rects; i++) {
                SDL_Rect *sub_rect = (SDL_Rect*)sp->subtitle.rects[i];
                SDL_Rect target = {.x = (int)(rect.x + sub_rect->x * xratio),
                    .y = (int)(rect.y + sub_rect->y * yratio),
                    .w = (int)(sub_rect->w * xratio),
                    .h = (int)(sub_rect->h * yratio)};
                SDL_RenderCopy(sdl::renderer()->getHandle(), mSubtitleTexture, sub_rect, &target);
            }
#endif
        }
    }
    
    void VideoState::drawAudioViz()
    {
        int i, i_start, x, y1, y, ys, delay, n, nb_display_channels;
        int ch, channels, h, h2;
        int64_t time_diff;
        int rdft_bits, nb_freq;
        
        for (rdft_bits = 1; (1 << rdft_bits) < 2 * mHeight; rdft_bits++);
        
        nb_freq = 1 << (rdft_bits - 1);
        
        /* compute display index : center on currently output samples */
        channels = mAudioTarget.channels;
        nb_display_channels = channels;
        if (!mPaused) {
            int data_used = mShowMode == VideoState::SHOW_MODE_WAVES ? mWidth : (2*nb_freq);
            n = 2 * channels;
            delay = mAudioWriteBufferSize;
            delay /= n;
            
            /* to be more precise, we take into account the time spent since
             the last buffer computation */
            if (sdl::GetAudioCallbackTime()) {
                time_diff = av_gettime_relative() - sdl::GetAudioCallbackTime();
                delay -= (time_diff * mAudioTarget.freq) / 1000000;
            }
            
            delay += 2 * data_used;
            if (delay < data_used)
                delay = data_used;
            
            i_start= x = ffmpeg::util::ComputeMod(mSampleArrayIndex - delay * channels, SAMPLE_ARRAY_SIZE);
            if (mShowMode == VideoState::SHOW_MODE_WAVES) {
                h = INT_MIN;
                for (i = 0; i < 1000; i += channels) {
                    int idx = (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
                    int a = mSampleArray[idx];
                    int b = mSampleArray[(idx + 4 * channels) % SAMPLE_ARRAY_SIZE];
                    int c = mSampleArray[(idx + 5 * channels) % SAMPLE_ARRAY_SIZE];
                    int d = mSampleArray[(idx + 9 * channels) % SAMPLE_ARRAY_SIZE];
                    int score = a - d;
                    if (h < score && (b ^ c) < 0) {
                        h = score;
                        i_start = idx;
                    }
                }
            }
            
            mLast_i_Start = i_start;
        } else {
            i_start = mLast_i_Start;
        }
        
        if (mShowMode == VideoState::SHOW_MODE_WAVES) {
            
            sdl::renderer()->setDrawColor(255, 255, 255, 255);
            
            /* total height for one channel */
            h = mHeight / nb_display_channels;
            /* graph height / 2 */
            h2 = (h * 9) / 20;
            for (ch = 0; ch < nb_display_channels; ch++) {
                i = i_start + ch;
                y1 = mYTop + ch * h + (h / 2); /* position of center line */
                for (x = 0; x < mWidth; x++) {
                    y = (mSampleArray[i] * h2) >> 15;
                    if (y < 0) {
                        y = -y;
                        ys = y1 - y;
                    } else {
                        ys = y1;
                    }
                    sdl::util::FillRectangle(mXLeft + x, ys, 1, y);
                    i += channels;
                    if (i >= SAMPLE_ARRAY_SIZE)
                        i -= SAMPLE_ARRAY_SIZE;
                }
            }
            
            sdl::renderer()->setDrawColor(0, 0, 255, 255);
            
            for (ch = 1; ch < nb_display_channels; ch++) {
                y = mYTop + ch * h;
                sdl::util::FillRectangle(mXLeft, y, mWidth, 1);
            }
        } else {
            if (sdl::util::ReallocTexture(&mAudioVizTexture, SDL_PIXELFORMAT_ARGB8888, mWidth, mHeight, SDL_BLENDMODE_NONE, 1) < 0)
                return;
            
            nb_display_channels= FFMIN(nb_display_channels, 2);
            if (rdft_bits != mRDFTBits) {
                av_rdft_end(mRDFT);
                av_free(mRDFTData);
                mRDFT = av_rdft_init(rdft_bits, DFT_R2C);
                mRDFTBits = rdft_bits;
                mRDFTData = (FFTSample*)av_malloc_array(nb_freq, 4 *sizeof(*mRDFTData));
            }
            if (!mRDFT || !mRDFTData){
                av_log(NULL, AV_LOG_ERROR, "Failed to allocate buffers for RDFT, switching to waves display\n");
                mShowMode = VideoState::SHOW_MODE_WAVES;
            } else {
                FFTSample *data[2];
                SDL_Rect rect = {.x = mXPos, .y = 0, .w = 1, .h = mHeight};
                uint32_t *pixels;
                int pitch;
                for (ch = 0; ch < nb_display_channels; ch++) {
                    data[ch] = mRDFTData + 2 * nb_freq * ch;
                    i = i_start + ch;
                    for (x = 0; x < 2 * nb_freq; x++) {
                        double w = (x-nb_freq) * (1.0 / nb_freq);
                        data[ch][x] = mSampleArray[i] * (1.0 - w * w);
                        i += channels;
                        if (i >= SAMPLE_ARRAY_SIZE)
                            i -= SAMPLE_ARRAY_SIZE;
                    }
                    av_rdft_calc(mRDFT, data[ch]);
                }
                /* Least efficient way to do this, we should of course
                 * directly access it but it is more than fast enough. */
                if (!SDL_LockTexture(mAudioVizTexture, &rect, (void **)&pixels, &pitch)) {
                    pitch >>= 2;
                    pixels += pitch * mHeight;
                    for (y = 0; y < mHeight; y++) {
                        double w = 1 / sqrt(nb_freq);
                        int a = sqrt(w * sqrt(data[0][2 * y + 0] * data[0][2 * y + 0] + data[0][2 * y + 1] * data[0][2 * y + 1]));
                        int b = (nb_display_channels == 2 ) ? sqrt(w * hypot(data[1][2 * y + 0], data[1][2 * y + 1]))
                        : a;
                        a = FFMIN(a, 255);
                        b = FFMIN(b, 255);
                        pixels -= pitch;
                        *pixels = (a << 16) + (b << 8) + ((a+b) >> 1);
                    }
                    SDL_UnlockTexture(mAudioVizTexture);
                }
                SDL_RenderCopy(sdl::renderer()->getHandle(), mAudioVizTexture, NULL, NULL);
            }
            if (!mPaused)
                mXPos++;
            if (mXPos >= mWidth)
                mXPos= mXLeft;
        }
    }
    
    
    int VideoState::VideoThread( void* arg )
    {
        VideoState *is = (VideoState*)arg;
        AVFrame *frame = av_frame_alloc();
        double pts;
        double duration;
        int ret;
        AVRational tb = is->mVideoAVStream->time_base;
        AVRational frame_rate = av_guess_frame_rate(is->mFormatContext, is->mVideoAVStream, NULL);
        
#if CONFIG_AVFILTER
        AVFilterGraph *graph = avfilter_graph_alloc();
        AVFilterContext *filt_out = NULL, *filt_in = NULL;
        int last_w = 0;
        int last_h = 0;
        enum AVPixelFormat last_format = (AVPixelFormat)-2;
        int last_serial = -1;
        int last_vfilter_idx = 0;
        if (!graph) {
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }
        
#endif
        
        if (!frame) {
#if CONFIG_AVFILTER
            avfilter_graph_free(&graph);
#endif
            return AVERROR(ENOMEM);
        }
        
        for (;;) {
            ret = is->getFrame(frame);
            if (ret < 0)
                goto the_end;
            if (!ret)
                continue;
            
#if CONFIG_AVFILTER
            if (   last_w != frame->width
                || last_h != frame->height
                || last_format != frame->format
                || last_serial != is->viddec.pkt_serial
                || last_vfilter_idx != is->vfilter_idx) {
                av_log(NULL, AV_LOG_DEBUG,
                       "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                       last_w, last_h,
                       (const char *)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                       frame->width, frame->height,
                       (const char *)av_x_if_null(av_get_pix_fmt_name((AVPixelFormat)frame->format), "none"), is->viddec.pkt_serial);
                avfilter_graph_free(&graph);
                graph = avfilter_graph_alloc();
                if ((ret = configure_video_filters(graph, is, vfilters_list ? vfilters_list[is->vfilter_idx] : NULL, frame)) < 0) {
                    SDL_Event event;
                    event.type = FF_QUIT_EVENT;
                    event.user.data1 = is;
                    SDL_PushEvent(&event);
                    goto the_end;
                }
                filt_in  = is->in_video_filter;
                filt_out = is->out_video_filter;
                last_w = frame->width;
                last_h = frame->height;
                last_format = (AVPixelFormat)frame->format;
                last_serial = is->viddec.pkt_serial;
                last_vfilter_idx = is->vfilter_idx;
                frame_rate = av_buffersink_get_frame_rate(filt_out);
            }
            
            ret = av_buffersrc_add_frame(filt_in, frame);
            if (ret < 0)
                goto the_end;
            
            while (ret >= 0) {
                is->frame_last_returned_time = av_gettime_relative() / 1000000.0;
                
                ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
                if (ret < 0) {
                    if (ret == AVERROR_EOF)
                        is->viddec.finished = is->viddec.pkt_serial;
                    ret = 0;
                    break;
                }
                
                is->frame_last_filter_delay = av_gettime_relative() / 1000000.0 - is->frame_last_returned_time;
                if (fabs(is->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
                    is->frame_last_filter_delay = 0;
                tb = av_buffersink_get_time_base(filt_out);
#endif
                duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
                pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                ret = is->queuePicture(frame, pts, opts::duration(), frame->pkt_pos, is->mVideoDecoder.getPacketSerial());
                av_frame_unref(frame);
#if CONFIG_AVFILTER
            }
#endif
            
            if (ret < 0)
                goto the_end;
        }
    the_end:
#if CONFIG_AVFILTER
        avfilter_graph_free(&graph);
#endif
        av_frame_free(&frame);
        return 0;
    }
    
    int VideoState::AudioThread( void* arg )
    {
        VideoState *is = (VideoState*)arg;
        AVFrame *frame = av_frame_alloc();
        Frame *af;
#if CONFIG_AVFILTER
        int last_serial = -1;
        int64_t dec_channel_layout;
        int reconfigure;
#endif
        int got_frame = 0;
        AVRational tb;
        int ret = 0;
        
        if (!frame)
            return AVERROR(ENOMEM);
        
        do {
            if ((got_frame = is->mAudioDecoder.decodeFrame( frame, NULL)) < 0)
                goto the_end;
            
            if (got_frame) {
                tb = (AVRational){1, frame->sample_rate};
                
#if CONFIG_AVFILTER
                dec_channel_layout = get_valid_channel_layout(frame->channel_layout, frame->channels);
                
                reconfigure =
                cmp_audio_fmts(is->audio_filter_src.fmt, is->audio_filter_src.channels,
                               (AVSampleFormat)frame->format, frame->channels)    ||
                is->audio_filter_src.channel_layout != dec_channel_layout ||
                is->audio_filter_src.freq           != frame->sample_rate ||
                is->auddec.pkt_serial               != last_serial;
                
                if (reconfigure) {
                    char buf1[1024], buf2[1024];
                    av_get_channel_layout_string(buf1, sizeof(buf1), -1, is->audio_filter_src.channel_layout);
                    av_get_channel_layout_string(buf2, sizeof(buf2), -1, dec_channel_layout);
                    av_log(NULL, AV_LOG_DEBUG,
                           "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                           is->audio_filter_src.freq, is->audio_filter_src.channels, av_get_sample_fmt_name(is->audio_filter_src.fmt), buf1, last_serial,
                           frame->sample_rate, frame->channels, av_get_sample_fmt_name((AVSampleFormat)frame->format), buf2, is->auddec.pkt_serial);
                    
                    is->audio_filter_src.fmt            = (AVSampleFormat)frame->format;
                    is->audio_filter_src.channels       = frame->channels;
                    is->audio_filter_src.channel_layout = dec_channel_layout;
                    is->audio_filter_src.freq           = frame->sample_rate;
                    last_serial                         = is->auddec.pkt_serial;
                    
                    if ((ret = configure_audio_filters(is, afilters, 1)) < 0)
                        goto the_end;
                }
                
                if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
                    goto the_end;
                
                while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0) {
                    tb = av_buffersink_get_time_base(is->out_audio_filter);
#endif
                    if (!(af = is->mSampleQueue.peekWriteable()))
                        goto the_end;
                    
                    af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                    af->position = frame->pkt_pos;
                    af->serial = is->mAudioDecoder.getPacketSerial();
                    af->duration = av_q2d((AVRational){frame->nb_samples, frame->sample_rate});
                    
                    av_frame_move_ref(af->frame, frame);
                    is->mSampleQueue.push();
                    
#if CONFIG_AVFILTER
                    if (is->mAudioPacketQueue.getSerial() != is->mAudioDecoder.getPacketSerial())
                        break;
                }
                if (ret == AVERROR_EOF)
                    is->auddec.finished = is->auddec.pkt_serial;
#endif
            }
        } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
    the_end:
#if CONFIG_AVFILTER
        avfilter_graph_free(&is->agraph);
#endif
        av_frame_free(&frame);
        return ret;
    }
    
    int VideoState::SubtitleThread( void* arg )
    {
        VideoState *is = (VideoState*)arg;
        Frame *sp;
        int got_subtitle;
        double pts;
        
        for (;;) {
            if (!(sp = is->mSubtitleQueue.peekWriteable()))
                return 0;
            
            if ((got_subtitle = is->mSubDecoder.decodeFrame(NULL, &sp->subtitle)) < 0)
                break;
            
            pts = 0;
            
            if (got_subtitle && sp->subtitle.format == 0) {
                if (sp->subtitle.pts != AV_NOPTS_VALUE)
                    pts = sp->subtitle.pts / (double)AV_TIME_BASE;
                sp->pts = pts;
                sp->serial = is->mSubDecoder.getPacketSerial();
                sp->width = is->mSubDecoder.getAVContext()->width;
                sp->height = is->mSubDecoder.getAVContext()->height;
                sp->uploaded = 0;
                
                /* now we can update the picture count */
                is->mSubtitleQueue.push();
            } else if (got_subtitle) {
                avsubtitle_free(&sp->subtitle);
            }
        }
        return 0;
    }
    
    int VideoState::getFrame(AVFrame *frame)
    {
        int got_picture;
        
        if ((got_picture = mVideoDecoder.decodeFrame(frame, NULL)) < 0)
            return -1;
        
        if (got_picture) {
            double dpts = NAN;
            
            if (frame->pts != AV_NOPTS_VALUE)
                dpts = av_q2d(mVideoAVStream->time_base) * frame->pts;
            
            frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(mFormatContext, mVideoAVStream, frame);
            
            if (opts::framedrop()>0 || (opts::framedrop() && getMasterSyncType() != AV_SYNC_VIDEO_MASTER)) {
                if (frame->pts != AV_NOPTS_VALUE) {
                    double diff = dpts - getMasterClock();
                    if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                        diff - mFrameLastFilterDelay < 0 &&
                        mVideoDecoder.getPacketSerial() == mVideoClock.getSerial() &&
                        mVideoPacketQueue.getNumPackets()) {
                        mFrameDropsEarly++;
                        av_frame_unref(frame);
                        got_picture = 0;
                    }
                }
            }
        }
        
        return got_picture;
    }
    
    void VideoState::checkExternalClockSpeed()
    {
        if ((mVideoStream >= 0 && mVideoPacketQueue.getNumPackets() <= EXTERNAL_CLOCK_MIN_FRAMES) ||
            (mAudioStream >= 0 && mAudioPacketQueue.getNumPackets() <= EXTERNAL_CLOCK_MIN_FRAMES)) {
            mExternalClock.setSpeed(FFMAX(EXTERNAL_CLOCK_SPEED_MIN, mExternalClock.getSpeed() - EXTERNAL_CLOCK_SPEED_STEP));
        } else if ((mVideoStream < 0 || mVideoPacketQueue.getNumPackets() > EXTERNAL_CLOCK_MAX_FRAMES) &&
                   (mAudioStream < 0 || mAudioPacketQueue.getNumPackets() > EXTERNAL_CLOCK_MAX_FRAMES)) {
            mExternalClock.setSpeed(FFMIN(EXTERNAL_CLOCK_SPEED_MAX, mExternalClock.getSpeed() + EXTERNAL_CLOCK_SPEED_STEP));
        } else {
            double speed = mExternalClock.getSpeed();
            if (speed != 1.0)
                mExternalClock.setSpeed(speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
        }
    }
    
    double VideoState::vp_duration(Frame *vp, Frame *nextvp) {
        if (vp->serial == nextvp->serial) {
            double duration = nextvp->pts - vp->pts;
            if (isnan(duration) || duration <= 0 || duration > mMaxFrameDuration)
                return vp->duration;
            else
                return duration;
        } else {
            return 0.0;
        }
    }
    
    double VideoState::computeTargetDelay(double delay)
    {
        double sync_threshold, diff = 0;
        
        /* update delay to follow master synchronisation source */
        if (getMasterSyncType() != AV_SYNC_VIDEO_MASTER) {
            /* if video is slave, we try to correct big delays by
             duplicating or deleting a frame */
            diff = mVideoClock.get() - getMasterClock();
            
            /* skip or repeat frame. We take into account the
             delay to compute the threshold. I still don't know
             if it is the best guess */
            sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
            if (!isnan(diff) && fabs(diff) < mMaxFrameDuration) {
                if (diff <= -sync_threshold)
                    delay = FFMAX(0, delay + diff);
                else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                    delay = delay + diff;
                else if (diff >= sync_threshold)
                    delay = 2 * delay;
            }
        }
        
        av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
               delay, -diff);
        
        return delay;
    }
    
    void VideoState::videoRefresh(double *remaining_time)
    {
        double time;
        
        Frame *sp, *sp2;
        
        if (!mPaused && getMasterSyncType() == AV_SYNC_EXTERNAL_CLOCK && mRealtime)
            checkExternalClockSpeed();
        
        if (sdl::IsVideoEnabled() && mShowMode != VideoState::SHOW_MODE_VIDEO && mAudioAVStream) {
            time = av_gettime_relative() / 1000000.0;
            if (mForceRefresh || mLastDisplayTime + opts::rdftspeed() < time) {
                draw();
                mLastDisplayTime = time;
            }
            *remaining_time = FFMIN(*remaining_time, mLastDisplayTime + opts::rdftspeed() - time);
        }
        
        if (mVideoAVStream) {
        retry:
            if (mPictureQueue.numRemaining() == 0) {
                // nothing to do, no picture to display in the queue
            } else {
                double last_duration, duration, delay;
                Frame *vp, *lastvp;
                
                /* dequeue the picture */
                lastvp = mPictureQueue.peekLast();
                vp = mPictureQueue.peek();
                
                if (vp->serial != mVideoPacketQueue.getSerial()) {
                    mPictureQueue.next();
                    goto retry;
                }
                
                if (lastvp->serial != vp->serial)
                    mFrameTimer = av_gettime_relative() / 1000000.0;
                
                if (mPaused)
                    goto display;
                
                /* compute nominal last_duration */
                last_duration = vp_duration(lastvp, vp);
                delay = computeTargetDelay(last_duration);
                
                time= av_gettime_relative()/1000000.0;
                if (time < mFrameTimer + delay) {
                    *remaining_time = FFMIN(mFrameTimer + delay - time, *remaining_time);
                    goto display;
                }
                
                mFrameTimer += delay;
                if (delay > 0 && time - mFrameTimer > AV_SYNC_THRESHOLD_MAX)
                    mFrameTimer = time;
                
                {
                    sdl::ScopedLock lock(mPictureQueue.getMutex());
                    if (!isnan(vp->pts))
                        updateVideoPts(vp->pts, vp->position, vp->serial);
                }
                
                if (mPictureQueue.numRemaining() > 1) {
                    Frame *nextvp = mPictureQueue.peekNext();
                    duration = vp_duration(vp, nextvp);
                    if(!mStep && (opts::framedrop()>0 || (opts::framedrop() && getMasterSyncType() != AV_SYNC_VIDEO_MASTER)) && time > mFrameTimer + duration){
                        mFrameDropsLate++;
                        mPictureQueue.next();
                        goto retry;
                    }
                }
                
                if (mSubtitleAVStream) {
                    while (mSubtitleQueue.numRemaining() > 0) {
                        sp = mSubtitleQueue.peek();
                        
                        if (mSubtitleQueue.numRemaining() > 1)
                            sp2 = mSubtitleQueue.peekNext();
                        else
                            sp2 = NULL;
                        
                        if (sp->serial != mSubtitlePacketQueue.getSerial()
                            || (mVideoClock.getPts() > (sp->pts + ((float) sp->subtitle.end_display_time / 1000)))
                            || (sp2 && mVideoClock.getPts() > (sp2->pts + ((float) sp2->subtitle.start_display_time / 1000))))
                        {
                            if (sp->uploaded) {
                                int i;
                                for (i = 0; i < sp->subtitle.num_rects; i++) {
                                    AVSubtitleRect *sub_rect = sp->subtitle.rects[i];
                                    uint8_t *pixels;
                                    int pitch, j;
                                    
                                    if (!SDL_LockTexture(mSubtitleTexture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
                                        for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                            memset(pixels, 0, sub_rect->w << 2);
                                        SDL_UnlockTexture(mSubtitleTexture);
                                    }
                                }
                            }
                            mSubtitleQueue.next();
                        } else {
                            break;
                        }
                    }
                }
                
                mPictureQueue.next();
                forceRefresh();
                
                if (mStep && !mPaused)
                    toggleStreamPause();
            }
        display:
            /* display picture */
            if (sdl::IsVideoEnabled() && getForceRefresh() && mShowMode == VideoState::SHOW_MODE_VIDEO && mPictureQueue.getRIndexShown())
                draw();
        }
        mForceRefresh = 0;
        if (opts::showStatus()) {
            static int64_t last_time;
            int64_t cur_time;
            int aqsize, vqsize, sqsize;
            double av_diff;
            
            cur_time = av_gettime_relative();
            if (!last_time || (cur_time - last_time) >= 30000) {
                aqsize = 0;
                vqsize = 0;
                sqsize = 0;
                if (mAudioAVStream)
                    aqsize = mAudioPacketQueue.size();
                if (mVideoAVStream)
                    vqsize = mVideoPacketQueue.size();
                if (mSubtitleAVStream)
                    sqsize = mSubtitlePacketQueue.size();
                av_diff = 0;
                if (mAudioAVStream && mVideoAVStream)
                    av_diff = mAudioClock.get() - mVideoClock.get();
                else if (mVideoAVStream)
                    av_diff = getMasterClock() - mVideoClock.get();
                else if (mAudioAVStream)
                    av_diff = getMasterClock() - mAudioClock.get();
                av_log(NULL, AV_LOG_INFO,
                       "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%" PRId64 "/%" PRId64 "   \r",
                       getMasterClock(),
                       (mAudioStream && mVideoAVStream) ? "A-V" : (mVideoAVStream ? "M-V" : (mAudioAVStream ? "M-A" : "   ")),
                       av_diff,
                       mFrameDropsEarly + mFrameDropsLate,
                       aqsize / 1024,
                       vqsize / 1024,
                       sqsize,
                       mVideoAVStream ? mVideoDecoder.getAVContext()->pts_correction_num_faulty_dts : 0,
                       mVideoAVStream ? mVideoDecoder.getAVContext()->pts_correction_num_faulty_pts : 0);
                fflush(stdout);
                last_time = cur_time;
            }
        }
    }
    
}//end namespace ffmpeg
