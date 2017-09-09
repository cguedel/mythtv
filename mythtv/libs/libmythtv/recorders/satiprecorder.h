#ifndef SATIPRECORDER_H_
#define SATIPRECORDER_H_

// Qt includes
#include <QString>

// MythTV includes
#include "dtvrecorder.h"

class SatIPChannel;
class SatIPStreamHandler;

class SatIPRecorder : public DTVRecorder
{
  public:
    SatIPRecorder(TVRec *rec, SatIPChannel *channel);

    void run(void);

    bool Open(void);
    bool IsOpen(void) const { return _stream_handler; }
    void Close(void);
    void StartNewFile(void);

    QString GetSIStandard(void) const;

  private:
    bool PauseAndWait(int timeout = 100);

  private:
    SatIPChannel       *_channel;
    SatIPStreamHandler *_stream_handler;
};

#endif // SATIPRECORDER_H_
