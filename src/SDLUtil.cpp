//
//  Util.cpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#include "SDLUtil.h"
#include <string>
#include <exception>
#include <map>
#include "Definitions.h"
#include "VideoState.h"

namespace sdl {

static std::unique_ptr<sdl::Window> sWindow = nullptr;
static std::unique_ptr<sdl::Renderer> sRenderer = nullptr;
static SDL_AudioDeviceID sAudioDevice;
static bool sVideoEnabled = true;
static bool sAudioEnabled = true;
static int64_t sAudioCallbackTime = 0;
static std::map<AVPixelFormat, int> sTextureFormatMap;

ScopedLock::ScopedLock(SDL_mutex* mutex):mMutex(mutex){
    SDL_LockMutex(mMutex);
}
ScopedLock::~ScopedLock(){
    SDL_UnlockMutex(mMutex);
}

Settings::Settings():
mFlags(0)
{}

    Window::Window( const Window::Settings& settings ):
    mSDLWindow(nullptr),
    mWidth(settings.getWidth()),
    mHeight(settings.getHeight()),
    mPositionX(settings.getPositionX()),
    mPositionY(settings.getPositiony()),
    mTitle(settings.getTitle()),
    mFullscreen(settings.isFullscreenEnabled()),
    mIsHidden(settings.isHidden())
{
    SDL_assert(settings.getFlags() != 0);
    mSDLWindow = SDL_CreateWindow(mTitle.c_str(), mPositionX, mPositionY, mWidth, mHeight, settings.getFlags());
}

Window::~Window()
{
    if (mSDLWindow)
        SDL_DestroyWindow(mSDLWindow);
}

void Window::setFullScreen(bool set)
{
    mFullscreen = set;
    if(mFullscreen)
        SDL_SetWindowFullscreen(mSDLWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
    else
        SDL_SetWindowFullscreen(mSDLWindow, 0);
}

void Window::setPosition( int x, int y )
{
    mPositionX = x;
    mPositionY = y;
    SDL_SetWindowPosition(mSDLWindow, mPositionX, mPositionY);
}

void Window::setTitle( const std::string& title )
{
    mTitle = title;
    SDL_SetWindowTitle(mSDLWindow, mTitle.c_str());
}
    
void Window::resize( int w, int h )
{
    mWidth = w;
    mHeight = w;
    SDL_SetWindowSize(mSDLWindow, mWidth, mHeight);
}
    
void Window::hide()
{
    SDL_HideWindow(mSDLWindow);
    mIsHidden = true;
}
    
void Window::show()
{
    SDL_ShowWindow(mSDLWindow);
    mIsHidden = false;
}
    
Renderer::Renderer(Window* window, uint32_t flags){
    mSDLRenderer = SDL_CreateRenderer(window->getHandle(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!mSDLRenderer) {
        av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
        mSDLRenderer = SDL_CreateRenderer(window->getHandle(), -1, 0);
    }
    if (mSDLRenderer) {
        SDL_GetRendererInfo(mSDLRenderer, &mInfo);
        av_log(NULL, AV_LOG_INFO, "Initialized %s renderer.\n", mInfo.name);
    }
}
    
Renderer::~Renderer()
{
    if (mSDLRenderer)
        SDL_DestroyRenderer(mSDLRenderer);
}
    
void Renderer::setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    SDL_SetRenderDrawColor(mSDLRenderer, r, g, b, a);
}

void Renderer::clear()
{
    SDL_RenderClear(mSDLRenderer);
}

void Renderer::present()
{
    SDL_RenderPresent(mSDLRenderer);
}

int Startup(const char* program_name, const Settings& settings, const Window::Settings& windowSettings )
{
    if(settings.getFlags() == 0){
        throw std::runtime_error("Settings flags must contain .video() .audio() or .events()");
    }
    
    if(sTextureFormatMap.empty()){
        sTextureFormatMap.insert({ AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 });
        sTextureFormatMap.insert({ AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 });
        sTextureFormatMap.insert({ AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 });
        sTextureFormatMap.insert({ AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 });
        sTextureFormatMap.insert({ AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 });
        sTextureFormatMap.insert({ AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 });
        sTextureFormatMap.insert({ AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 });
        sTextureFormatMap.insert({ AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 });
        sTextureFormatMap.insert({ AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 });
        sTextureFormatMap.insert({ AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 });
        sTextureFormatMap.insert({ AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 });
        sTextureFormatMap.insert({ AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 });
        sTextureFormatMap.insert({ AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 });
        sTextureFormatMap.insert({ AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 });
        sTextureFormatMap.insert({ AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 });
        sTextureFormatMap.insert({ AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 });
        sTextureFormatMap.insert({ AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV });
        sTextureFormatMap.insert({ AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 });
        sTextureFormatMap.insert({ AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY });
        sTextureFormatMap.insert({ AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN });
    }
    
    if (settings.isVideoDisabled()) {
        sVideoEnabled = false;
    }
    
    if (!settings.isAudioDisabled()){
        /* Try to work around an occasional ALSA buffer underflow issue when the
         * period size is NPOT due to ALSA resampling by forcing the buffer size. */
        if (!SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
            SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE","1", 1);
    }else{
        sAudioEnabled = false;
    }

    if (SDL_Init(settings.getFlags())) {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        exit(1);
    }
    
    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);
    
    if (!settings.isVideoDisabled()) {
        
        sWindow.reset(new Window(windowSettings));
       
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        if (sWindow) {
            sRenderer.reset(new Renderer(sWindow.get()));
        }
        if (!sWindow->getHandle() || !sRenderer->getHandle() || !sRenderer->getInfo()->num_texture_formats) {
            av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
            return -1;
        }
    }
    return 1;
}

void Shutdown()
{
    sRenderer.reset();
    sWindow.reset();
    SDL_Quit();
}

Renderer* renderer(){
    if(!sRenderer){
        throw std::runtime_error("SDL Not initialized");
    }
    return sRenderer.get();
}

Window* window(){
    if(!sWindow){
        throw std::runtime_error("SDL Not initialized");
    }
    return sWindow.get();
}
    
SDL_AudioDeviceID& audioDevice()
{
    return sAudioDevice;
}

bool IsAudioEnabled(){
    return sAudioEnabled;
}

bool IsVideoEnabled(){
    return sVideoEnabled;
}
    
int64_t& GetAudioCallbackTime()
{
    return sAudioCallbackTime;
}

void util::FillRectangle(int x, int y, int w, int h)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    if (w && h)
        SDL_RenderFillRect(renderer()->getHandle(), &rect);
}

int util::ReallocTexture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture)
{
    Uint32 format;
    int access, w, h;
    if (!*texture || SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format) {
        void *pixels;
        int pitch;
        if (*texture)
            SDL_DestroyTexture(*texture);
        if (!(*texture = SDL_CreateTexture(renderer()->getHandle(), new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
            return -1;
        if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
            return -1;
        if (init_texture) {
            if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
                return -1;
            memset(pixels, 0, pitch * new_height);
            SDL_UnlockTexture(*texture);
        }
        av_log(NULL, AV_LOG_VERBOSE, "Created %dx%d texture with %s.\n", new_width, new_height, SDL_GetPixelFormatName(new_format));
    }
    return 0;
}

void util::CalcDisplayRect(SDL_Rect *rect, int scr_xleft, int scr_ytop, int scr_width, int scr_height, int pic_width, int pic_height, AVRational pic_sar)
{
    float aspect_ratio;
    int width, height, x, y;
    
    if (pic_sar.num == 0)
        aspect_ratio = 0;
    else
        aspect_ratio = av_q2d(pic_sar);
    
    if (aspect_ratio <= 0.0)
        aspect_ratio = 1.0;
    aspect_ratio *= (float)pic_width / (float)pic_height;
    
    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    width = lrint(height * aspect_ratio) & ~1;
    if (width > scr_width) {
        width = scr_width;
        height = lrint(width / aspect_ratio) & ~1;
    }
    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;
    rect->x = scr_xleft + x;
    rect->y = scr_ytop  + y;
    rect->w = FFMAX(width,  1);
    rect->h = FFMAX(height, 1);
}

void util::GetSDLPixFmtAndBlendMode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode)
{
    *sdl_blendmode = SDL_BLENDMODE_NONE;
    *sdl_pix_fmt = SDL_PIXELFORMAT_UNKNOWN;
    if (format == AV_PIX_FMT_RGB32   ||
        format == AV_PIX_FMT_RGB32_1 ||
        format == AV_PIX_FMT_BGR32   ||
        format == AV_PIX_FMT_BGR32_1)
        *sdl_blendmode = SDL_BLENDMODE_BLEND;
    auto found = sTextureFormatMap.find((AVPixelFormat)format);
    if(found != sTextureFormatMap.end()){
        *sdl_pix_fmt = found->second;
    }
}

int util::UploadTexture(SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx) {
    int ret = 0;
    Uint32 sdl_pix_fmt;
    SDL_BlendMode sdl_blendmode;
    GetSDLPixFmtAndBlendMode(frame->format, &sdl_pix_fmt, &sdl_blendmode);
    if (ReallocTexture(tex, sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_ARGB8888 : sdl_pix_fmt, frame->width, frame->height, sdl_blendmode, 0) < 0)
        return -1;
    switch (sdl_pix_fmt) {
        case SDL_PIXELFORMAT_UNKNOWN:
            /* This should only happen if we are not using avfilter... */
            *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
                                                    frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height,
                                                    AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);
            if (*img_convert_ctx != NULL) {
                uint8_t *pixels[4];
                int pitch[4];
                if (!SDL_LockTexture(*tex, NULL, (void **)pixels, pitch)) {
                    sws_scale(*img_convert_ctx, (const uint8_t * const *)frame->data, frame->linesize,
                              0, frame->height, pixels, pitch);
                    SDL_UnlockTexture(*tex);
                }
            } else {
                av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                ret = -1;
            }
            break;
        case SDL_PIXELFORMAT_IYUV:
            if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0) {
                ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0], frame->linesize[0],
                                           frame->data[1], frame->linesize[1],
                                           frame->data[2], frame->linesize[2]);
            } else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0) {
                ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height                    - 1), -frame->linesize[0],
                                           frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
                                           frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2]);
            } else {
                av_log(NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
                return -1;
            }
            break;
        default:
            if (frame->linesize[0] < 0) {
                ret = SDL_UpdateTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
            } else {
                ret = SDL_UpdateTexture(*tex, NULL, frame->data[0], frame->linesize[0]);
            }
            break;
    }
    return ret;
}
    
    int AudioOpen(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, ffmpeg::AudioParams *audio_hw_params)
{
    SDL_AudioSpec wanted_spec, spec;
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;
    
    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = ffmpeg::VideoState::SDLAudioCallback;
    wanted_spec.userdata = opaque;
    while (!(sAudioDevice = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                av_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    if (spec.format != AUDIO_S16SYS) {
        av_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            av_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }
    
    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    return spec.size;
}
    
}//end namespace sdl
