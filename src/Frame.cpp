//
//  Frame.cpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#include "Frame.h"

ffmpeg::Frame::Frame():
frame(nullptr),
serial(0),
pts(0.0),              /* presentation timestamp for the frame */
duration(0.0),          /* estimated duration of the frame */
position(0),           /* byte position of the frame in the input file */
width(0),
height(0),
format(0),
uploaded(0),
vflip(0)
{}


