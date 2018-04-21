//
//  Frame.hpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

extern "C" {
#include "libavcodec/avcodec.h"
}

namespace ffmpeg {

struct Frame {
    Frame();    
    AVFrame *frame;
    AVSubtitle subtitle;
    int serial;
    double pts;           /* presentation timestamp for the frame */
    double duration;      /* estimated duration of the frame */
    int64_t position;     /* byte position of the frame in the input file */
    int width;
    int height;
    int format;
    AVRational sar;
    int uploaded;
    int vflip;
};

}// end namespace ffmpeg

