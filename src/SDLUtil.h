//
//  Util.h
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

#include <string>
#include <SDL.h>
#include <SDL_thread.h>
#include "AudioParams.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
}

namespace sdl {
    
    class ScopedLock {
    public:
        ScopedLock(SDL_mutex* mutex);
        ~ScopedLock();
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;
        ScopedLock(ScopedLock&&) = delete;
        ScopedLock& operator=(ScopedLock&&) = delete;
        
    private:
        SDL_mutex* mMutex;
    };
    
    class Window {
    public:
        
        class Settings {
        public:
            inline Settings& title(const std::string& title){ mTitle = title; return *this; }
            inline Settings& fullscreen(){ mInitFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP; return *this; }
            inline Settings& borderless(){ mInitFlags |= SDL_WINDOW_BORDERLESS; return *this; }
            inline Settings& resizeable(){ mInitFlags |= SDL_WINDOW_RESIZABLE; return *this; }
            inline Settings& hidden(){ mInitFlags |= SDL_WINDOW_HIDDEN; return *this; }
            inline Settings& size( int w, int h ){ mWidth = w; mHeight = h; return *this; }
            inline Settings& position( int x, int y ){ mPosX = x; mPosY = y; return *this; }
            inline Settings& centerWindow(){ mPosX = SDL_WINDOWPOS_CENTERED; mPosY = SDL_WINDOWPOS_CENTERED; return *this; }
            
            inline bool isFullscreenEnabled()const{return mInitFlags & SDL_WINDOW_FULLSCREEN;}
            inline bool isBorderlessEnabled()const{return mInitFlags & SDL_WINDOW_BORDERLESS;}
            inline bool isHidden()const{return mInitFlags & SDL_WINDOW_HIDDEN;}

            inline int getFlags()const{return mInitFlags;}
            inline int getWidth()const{return mWidth;}
            inline int getHeight()const{return mHeight;}
            inline std::string getTitle()const{return mTitle;}
            inline int getPositionX()const{ return mPosX; }
            inline int getPositiony()const{ return mPosY; }

        private:
            int mPosX{SDL_WINDOWPOS_CENTERED};
            int mPosY{SDL_WINDOWPOS_CENTERED};
            int mWidth{640};
            int mHeight{480};
            int mInitFlags{0};
            std::string mTitle{""};
        };
        
        Window( const Window::Settings& settings );
        ~Window();
        
        void setFullScreen(bool set = true);
        void setTitle( const std::string& title );
        void setPosition( int x, int y );
        void resize( int w, int h );
        void hide();
        void show();
        
        inline SDL_Window* getHandle(){ return mSDLWindow; }
        inline const std::string& getTitle()const{return mTitle;}
        inline bool isVisible()const{return !mIsHidden;}
        inline bool isFullscreen()const { return mFullscreen; }
        inline int getWidth()const { return mWidth; }
        inline int getHeight()const { return mHeight; }
        
    private:
        SDL_Window* mSDLWindow;
        int mWidth;
        int mHeight;
        int mPositionX;
        int mPositionY;
        std::string mTitle;
        bool mFullscreen;
        bool mIsHidden;
    };
    
    class Settings {
    public:
        Settings();
        
        inline Settings& video(){ mFlags |= SDL_INIT_VIDEO; return *this; }
        inline Settings& audio(){ mFlags |= SDL_INIT_AUDIO; return *this; }
        inline Settings& timer(){ mFlags |= SDL_INIT_TIMER; return *this; }
        inline Settings& joystick(){ mFlags |= SDL_INIT_JOYSTICK; return *this; }
        inline Settings& haptic(){ mFlags |= SDL_INIT_HAPTIC; return *this; }
        inline Settings& gameController(){ mFlags |= SDL_INIT_GAMECONTROLLER; return *this; }
        inline Settings& events(){ mFlags |= SDL_INIT_EVENTS; return *this; }

        inline uint32_t getFlags() const { return mFlags; }
        inline bool isVideoDisabled()const{return !(SDL_INIT_VIDEO & mFlags);}
        inline bool isAudioDisabled()const{return !(SDL_INIT_AUDIO & mFlags);}

    private:
        uint32_t mFlags;
    };
    
    class Renderer {
    public:
        
        Renderer( Window* window, uint32_t flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC );
        ~Renderer();
        
        void setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
        void clear();
        void present();
        
        inline SDL_RendererInfo* getInfo(){return &mInfo;}
        inline SDL_Renderer* getHandle(){return mSDLRenderer;}

    private:
        SDL_Renderer* mSDLRenderer;
        SDL_RendererInfo mInfo{0};
    };
    
    int Startup(const char* program_name, const Settings& settings, const Window::Settings& windowSettings );
    void Shutdown();
    Renderer* renderer();
    Window* window();
    int AudioOpen(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, ffmpeg::AudioParams *audio_hw_params);
    SDL_AudioDeviceID& audioDevice();
    bool IsAudioEnabled();
    bool IsVideoEnabled();
    int64_t& GetAudioCallbackTime();
    
    namespace util {
        
        void FillRectangle(int x, int y, int w, int h);
        int ReallocTexture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);
        void CalcDisplayRect(SDL_Rect *rect, int scr_xleft, int scr_ytop, int scr_width, int scr_height, int pic_width, int pic_height, AVRational pic_sar);
        void GetSDLPixFmtAndBlendMode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode);
        int UploadTexture(SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx);
        
    }//end namespace sdl::util
    
} //end namespace sdl




