//
//  FFMPEGUtils.cpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#include "FFMPEGUtil.h"
#include "PacketQueue.h"
#include "VideoState.h"

namespace ffmpeg {
    
    void StartUp(){
        av_register_all();
        av_init_packet(&PacketQueue::sFlushPacket);
        PacketQueue::sFlushPacket.data = (uint8_t *)&PacketQueue::sFlushPacket;
        avformat_network_init();
        av_log_set_flags(AV_LOG_SKIP_REPEATED);
    }
    
    void Shutdown(){
        avformat_network_deinit();
        av_log(NULL, AV_LOG_QUIET, "%s", "");
    }
    
    void PrintError(const char *filename, int err)
    {
        char errbuf[128];
        const char *errbuf_ptr = errbuf;
        
        if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
            errbuf_ptr = strerror(AVUNERROR(err));
        av_log(NULL, AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
    }
    
    int util::IsRealtime(AVFormatContext *s)
    {
        if(   !strcmp(s->iformat->name, "rtp")
           || !strcmp(s->iformat->name, "rtsp")
           || !strcmp(s->iformat->name, "sdp")
           )
            return 1;
        
        if(s->pb && (   !strncmp(s->filename, "rtp:", 4)
                     || !strncmp(s->filename, "udp:", 4)
                     )
           )
            return 1;
        return 0;
    }
    
    int util::ComputeMod(int a, int b)
    {
        return a < 0 ? a%b + b : a%b;
    }
    
    int64_t util::GetValidChannelLayout(int64_t channel_layout, int channels)
    {
        if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
            return channel_layout;
        else
            return 0;
    }
    
    int util::CompareAudioFormats(AVSampleFormat fmt1, int64_t channel_count1, AVSampleFormat fmt2, int64_t channel_count2)
    {
        /* If channel count == 1, planar and non-planar formats are the same */
        if (channel_count1 == 1 && channel_count2 == 1)
            return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
        else
            return channel_count1 != channel_count2 || fmt1 != fmt2;
    }
    
    int opts::check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
    {
        int ret = avformat_match_stream_specifier(s, st, spec);
        if (ret < 0)
            av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
        return ret;
    }
    
    AVDictionary* opts::filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id, AVFormatContext *s, AVStream *st, AVCodec *codec)
    {
        AVDictionary    *ret = NULL;
        AVDictionaryEntry *t = NULL;
        int            flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM : AV_OPT_FLAG_DECODING_PARAM;
        char          prefix = 0;
        const AVClass    *cc = avcodec_get_class();
        
        if (!codec)
            codec            = s->oformat ? avcodec_find_encoder(codec_id)
            : avcodec_find_decoder(codec_id);
        
        switch (st->codecpar->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                prefix  = 'v';
                flags  |= AV_OPT_FLAG_VIDEO_PARAM;
                break;
            case AVMEDIA_TYPE_AUDIO:
                prefix  = 'a';
                flags  |= AV_OPT_FLAG_AUDIO_PARAM;
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                prefix  = 's';
                flags  |= AV_OPT_FLAG_SUBTITLE_PARAM;
                break;
            default: break;
        }
        
        while ((t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX))) {
            char *p = strchr(t->key, ':');
            
            /* check stream specification in opt name */
            if (p)
                switch (check_stream_specifier(s, st, p + 1)) {
                    case  1: *p = 0; break;
                    case  0:         continue;
                    default:
                        av_log(NULL, AV_LOG_FATAL, "stream specifier ran into a problem...");
                        exit(1);
                }
            
            if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) || !codec || (codec->priv_class && av_opt_find(&codec->priv_class, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ)))
                av_dict_set(&ret, t->key, t->value, 0);
            else if (t->key[0] == prefix && av_opt_find(&cc, t->key + 1, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ))
                av_dict_set(&ret, t->key + 1, t->value, 0);
            
            if (p)
                *p = ':';
        }
        return ret;
    }
    
    AVDictionary** opts::setup_find_stream_info_opts(AVFormatContext *s, AVDictionary *codec_opts)
    {
        int i;
        AVDictionary **opts;
        
        if (!s->nb_streams)
            return NULL;
        opts = (AVDictionary**)av_mallocz_array(s->nb_streams, sizeof(*opts));
        if (!opts) {
            av_log(NULL, AV_LOG_ERROR,
                   "Could not alloc memory for stream options.\n");
            return NULL;
        }
        for (i = 0; i < s->nb_streams; i++)
            opts[i] = filter_codec_opts(codec_opts, s->streams[i]->codecpar->codec_id, s, s->streams[i], NULL);
        return opts;
    }

    int opts::getPts(){ return 0; }
    int opts::findStreamInfo(){ return 1; }
    static int sSeekByBytes = -1;
    int& opts::seekByBytes(){ return sSeekByBytes; }
    static int64_t sStartTime = AV_NOPTS_VALUE;
    int64_t& opts::startTime(){ return sStartTime; }
    bool opts::showStatus(){ return true; }
    bool opts::subtitlesEnabled(){ return false; }
    int opts::showMode(){ return VideoState::SHOW_MODE_VIDEO; }
    static int sInfiniteBuffer = -1;
    int& opts::infiniteBuffer(){ return sInfiniteBuffer; }
    static int sLoopCount = 1;
    int& opts::loopCount(){return sLoopCount;}
    bool opts::autoexit(){return false;}
    const char* sVideoCodec = nullptr;
    const char* sAudioCodec = nullptr;
    const char* sSubtitleCodec = nullptr;
    const char* opts::videoCodec(){return sVideoCodec;}
    const char* opts::audioCodec(){return sAudioCodec;}
    const char* opts::subtitleCodec(){return sSubtitleCodec;}
    bool opts::fast(){return false;}
    int opts::lowres(){return 0;}
    static int64_t sDuration = AV_NOPTS_VALUE;
    int64_t& opts::duration(){ return sDuration; }
    int opts::framedrop(){return -1;}
    double opts::rdftspeed(){return 0.02;};


}// end namespace
