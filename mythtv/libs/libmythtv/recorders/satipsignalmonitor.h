// -*- Mode: c++ -*-

#ifndef SATIPSIGNALMONITOR_H
#define SATIPSIGNALMONITOR_H

#include "dtvsignalmonitor.h"

class SatIPChannel;
class SatIPStreamHandler;

class SatIPSignalMonitor : public DTVSignalMonitor
{
  public:
    SatIPSignalMonitor(int db_cardnum, SatIPChannel* _channel,
        uint64_t _flags = 0);
    virtual ~SatIPSignalMonitor();

    void Stop(void);

  protected:
    virtual void UpdateValues(void);
    SatIPChannel *GetSatIPChannel(void);
    
  protected:
    bool                streamHandlerStarted;
    SatIPStreamHandler *streamHandler;
};

#endif // SATIPSIGNALMONITOR_H
