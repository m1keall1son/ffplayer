
#include <iostream>
#include <assert.h>
#include "SDLUtil.h"
#include "FFMPEGUtil.h"
#include "VideoState.h"

void do_exit(ffmpeg::VideoState* vs)
{
    if (vs) {
        vs->streamClose();
    }
    sdl::Shutdown();
    ffmpeg::Shutdown();
    exit(0);
}

void refresh_loop_wait_event(ffmpeg::VideoState *is, SDL_Event *event) {
    double remaining_time = 0.0;
    SDL_PumpEvents();
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
//        if (!cursor_hidden && av_gettime_relative() - cursor_last_shown > CURSOR_HIDE_DELAY) {
//            SDL_ShowCursor(0);
//            cursor_hidden = 1;
//        }
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        if (is->getShowMode() != ffmpeg::VideoState::SHOW_MODE_NONE && (!is->isPaused() || is->getForceRefresh()))
            is->videoRefresh(&remaining_time);
        SDL_PumpEvents();
    }
}

void event_loop(ffmpeg::VideoState* state){
    SDL_Event event;
    double incr, pos, frac;
    
    for (;;) {
        double x;
        refresh_loop_wait_event(state, &event);
        switch (event.type) {
            case SDL_KEYDOWN:
//                if (exit_on_keydown) {
//                    do_exit(cur_stream);
//                    break;
//                }
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                    case SDLK_q:
                        do_exit(state);
                        break;
                    case SDLK_f:
                        sdl::window()->setFullScreen();
                        state->forceRefresh();
                        break;
                    case SDLK_p:
                    case SDLK_SPACE:
                        state->togglePause();
                        break;
                    case SDLK_m:
                        state->toggleMute();
                        break;
                    case SDLK_KP_MULTIPLY:
                    case SDLK_0:
                        state->updateVolume(1, SDL_VOLUME_STEP);
                        break;
                    case SDLK_KP_DIVIDE:
                    case SDLK_9:
                        state->updateVolume(-1, SDL_VOLUME_STEP);
                        break;
                    case SDLK_s: // S: Step to next frame
                        state->stepToNextFrame();
                        break;
                    case SDLK_a:
                        //stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
                        break;
                    case SDLK_v:
                        //stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
                        break;
                    case SDLK_c:
                        //stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
                        //stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
                        //stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
                        break;
                    case SDLK_t:
                        //stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
                        break;
                    case SDLK_w:
#if CONFIG_AVFILTER
                        if (state->getShowMode() == VideoState::SHOW_MODE_VIDEO && cur_stream->vfilter_idx < nb_vfilters - 1) {
                            if (++cur_stream->vfilter_idx >= nb_vfilters)
                                cur_stream->vfilter_idx = 0;
                        } else {
                            cur_stream->vfilter_idx = 0;
                            toggle_audio_display(cur_stream);
                        }
#else
                        state->toggleAudioDisplay();
#endif
                        break;
                        
                        
                        
                    case SDLK_PAGEUP:
//                        if (cur_stream->ic->nb_chapters <= 1) {
//                            incr = 600.0;
//                            goto do_seek;
//                        }
//                        seek_chapter(cur_stream, 1);
                        break;
                    case SDLK_PAGEDOWN:
//                        if (cur_stream->ic->nb_chapters <= 1) {
//                            incr = -600.0;
//                            goto do_seek;
//                        }
//                        seek_chapter(cur_stream, -1);
                        break;
                    case SDLK_LEFT:
                        incr = -10.0;
                        state->seek(incr);
                    case SDLK_RIGHT:
                        incr = 10.0;
                        state->seek(incr);
                    case SDLK_UP:
                        incr = 60.0;
                        state->seek(incr);
                    case SDLK_DOWN:
                        incr = -60.0;
                        state->seek(incr);
                        break;
                    default:
                        break;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
//                if (exit_on_mousedown) {
//                    do_exit(cur_stream);
//                    break;
//                }
//                if (event.button.button == SDL_BUTTON_LEFT) {
//                    static int64_t last_mouse_left_click = 0;
//                    if (av_gettime_relative() - last_mouse_left_click <= 500000) {
//                        toggle_full_screen(cur_stream);
//                        cur_stream->force_refresh = 1;
//                        last_mouse_left_click = 0;
//                    } else {
//                        last_mouse_left_click = av_gettime_relative();
//                    }
//                }
            case SDL_MOUSEMOTION:
//                if (cursor_hidden) {
//                    SDL_ShowCursor(1);
//                    cursor_hidden = 0;
//                }
//                cursor_last_shown = av_gettime_relative();
//                if (event.type == SDL_MOUSEBUTTONDOWN) {
//                    if (event.button.button != SDL_BUTTON_RIGHT)
//                        break;
//                    x = event.button.x;
//                } else {
//                    if (!(event.motion.state & SDL_BUTTON_RMASK))
//                        break;
//                    x = event.motion.x;
//                }
//                if (seek_by_bytes || cur_stream->ic->duration <= 0) {
//                    uint64_t size =  avio_size(cur_stream->ic->pb);
//                    stream_seek(cur_stream, size*x/cur_stream->width, 0, 1);
//                } else {
//                    int64_t ts;
//                    int ns, hh, mm, ss;
//                    int tns, thh, tmm, tss;
//                    tns  = cur_stream->ic->duration / 1000000LL;
//                    thh  = tns / 3600;
//                    tmm  = (tns % 3600) / 60;
//                    tss  = (tns % 60);
//                    frac = x / cur_stream->width;
//                    ns   = frac * tns;
//                    hh   = ns / 3600;
//                    mm   = (ns % 3600) / 60;
//                    ss   = (ns % 60);
//                    av_log(NULL, AV_LOG_INFO,
//                           "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac*100,
//                           hh, mm, ss, thh, tmm, tss);
//                    ts = frac * cur_stream->ic->duration;
//                    if (cur_stream->ic->start_time != AV_NOPTS_VALUE)
//                        ts += cur_stream->ic->start_time;
//                    stream_seek(cur_stream, ts, 0, 0);
//                }
                break;
            case SDL_WINDOWEVENT:
//                switch (event.window.event) {
//                    case SDL_WINDOWEVENT_RESIZED:
//                        screen_width  = cur_stream->width  = event.window.data1;
//                        screen_height = cur_stream->height = event.window.data2;
//                        if (cur_stream->vis_texture) {
//                            SDL_DestroyTexture(cur_stream->vis_texture);
//                            cur_stream->vis_texture = NULL;
//                        }
//                    case SDL_WINDOWEVENT_EXPOSED:
//                        cur_stream->force_refresh = 1;
//                }
                break;
                         
                         
                         
            case SDL_QUIT:
            case FF_QUIT_EVENT:
                do_exit(state);
                break;
            default:
                break;
        }
    }
}

int main(int argc, char **argv)
{
    ffmpeg::StartUp();
    sdl::Startup("test", sdl::Settings().video().timer(), sdl::Window::Settings().resizeable().hidden());
        
    ffmpeg::VideoState state;
    
    //file_iformat no options ATM
    auto ret = state.streamOpen("/Users/michaelallison/code/sixmonths/Cartier-HudsonYards-flipdot.mov", nullptr);
    if (!ret) {
        av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
        state.streamClose();
        exit(0);
    }
    
    event_loop(&state);
    
    return 0;
}
