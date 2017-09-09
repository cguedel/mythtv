#ifndef _SATIP_CHANNEL_H_
#define _SATIP_CHANNEL_H_

// Qt headers
#include <QString>

// MythTV headers
#include "dtvchannel.h"
#include "satipstreamhandler.h"

class SatIPChannel : public DTVChannel
{
  public:
    SatIPChannel(TVRec *parent, const QString & device);
    ~SatIPChannel(void);

    // Commands
    virtual bool Open(void);
    virtual void Close(void);

    using DTVChannel::Tune;
    virtual bool Tune(const DTVMultiplex&);
    virtual bool Tune(const QString &channum);

    // Gets
    virtual bool IsOpen(void) const;
    virtual QString GetDevice(void) const { return m_device; }
    virtual bool IsPIDTuningSupported(void) const { return true; }

    virtual bool IsMaster(void) const { return true;  }
    
    SatIPStreamHandler *GetStreamHandler(void) const { return m_stream_handler; }

  private:
    void OpenStreamHandler(void);
    void CloseStreamHandler(void);

  private:
    QString             m_device;
    QStringList         m_args;
    mutable QMutex      m_tune_lock;
    volatile bool       m_firsttune;
    mutable QMutex      m_stream_lock;
    SatIPStreamHandler *m_stream_handler;
    MPEGStreamData     *m_stream_data;
    QString             m_videodev;
};

#endif // _SATIP_CHANNEL_H_