//
//  FFMPEGUtils.hpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

#include <iostream>

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

namespace ffmpeg {
    
    void StartUp();
    void Shutdown();
    
    void PrintError(const char *filename, int err);
    
    namespace util {
        
        int IsRealtime(AVFormatContext *s);
        int ComputeMod(int a, int b);
        int64_t GetValidChannelLayout(int64_t channel_layout, int channels);
        int CompareAudioFormats(AVSampleFormat fmt1, int64_t channel_count1, AVSampleFormat fmt2, int64_t channel_count2);
        
    }//end namespace ffmpeg::util
    
    namespace opts {
        
        int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec);
        AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id, AVFormatContext *s, AVStream *st, AVCodec *codec);
        AVDictionary **setup_find_stream_info_opts(AVFormatContext *s, AVDictionary *codec_opts);
        int getPts();
        int findStreamInfo();
        int64_t& startTime();
        bool showStatus();
        static const char* wantedStreamSpec[AVMEDIA_TYPE_NB] = {0};
        bool subtitlesEnabled();
        int showMode();
        int& infiniteBuffer();
        int& loopCount();
        bool autoexit();
        const char* videoCodec();
        const char* audioCodec();
        const char* subtitleCodec();
        bool fast();
        int lowres();
        int64_t& duration();
        int framedrop();
        double rdftspeed();

        
    }//end namespace opts
    
}//end namespace ffmpeg
