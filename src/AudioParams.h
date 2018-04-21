//
//  AudioParams.h
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

#include <stdint.h>

extern "C" {
#include "libavformat/avformat.h"
}

namespace ffmpeg {
    
struct AudioParams {
    int freq;
    int channels;
    int64_t channel_layout;
    AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
};
    
}//end namespace ffmpeg
