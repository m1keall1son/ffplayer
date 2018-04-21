//
//  Clock.cpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#include "Clock.h"
#include "Definitions.h"
#include "FFMPEGUtil.h"

namespace ffmpeg {
    
    double Clock::get()
    {
        if (*mQueueSerial != mSerial)
            return NAN;
        if (mPaused) {
            return mPts;
        } else {
            double time = av_gettime_relative() / 1000000.0;
            return mPtsDrift + time - (time - mLastUpdated) * (1.0 - mSpeed);
        }
    }
    
    void Clock::setAt(double pts, int serial, double time)
    {
        mPts = pts;
        mLastUpdated = time;
        mPtsDrift = mPts - time;
        mSerial = serial;
    }
    
    void Clock::set( double pts, int serial)
    {
        double time = av_gettime_relative() / 1000000.0;
        setAt(pts, serial, time);
    }
    
    void Clock::setSpeed(double speed)
    {
        set(get(), mSerial);
        mSpeed = speed;
    }
    
    void Clock::init(int *queue_serial)
    {
        mSpeed = 1.0;
        mPaused = 0;
        mQueueSerial = queue_serial;
        set(NAN, -1);
    }
    
    void Clock::syncToSlave(Clock *slave)
    {
        double clock = get();
        double slave_clock = slave->get();
        if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
            set(slave_clock, slave->getSerial());
    }

    
}//end namespace ffmpeg

