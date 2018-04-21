//
//  Clock.hpp
//  sixmonths
//
//  Created by Michael Allison on 4/19/18.
//

#pragma once

namespace ffmpeg {
    
    class Clock {
    public:
 
        double get();
        void setAt(double pts, int serial, double time);
        void set(double pts, int serial);
        void setSpeed(double speed);
        void init(int *queue_serial);
        void syncToSlave(Clock *slave);
        inline void setPaused(int pause){mPaused = pause;}
        inline int getSerial()const{return mSerial;}
        inline int* getSerialPtr(){return &mSerial;}
        inline double getLastUpdated()const{return mLastUpdated;}
        inline double getSpeed()const{return mSpeed;}
        inline double getPts()const{return mPts;}

    private:
        double mPts;           /* clock base */
        double mPtsDrift;     /* clock base minus time at which we updated the clock */
        double mLastUpdated;
        double mSpeed;
        int mSerial;           /* clock is based on a packet with this serial */
        int mPaused;
        int *mQueueSerial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
    };
    
}//end namespace ffmpeg

