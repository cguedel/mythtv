// -*- Mode: c++ -*-

#ifndef _SATIPSTREAMHANDLER_H_
#define _SATIPSTREAMHANDLER_H_

#include "satiprtsp.h"
using namespace std;

#include <QString>
#include <QMutex>
#include <QMap>

#include "mpegstreamdata.h"
#include "streamhandler.h"
#include "dtvmultiplex.h"
#include "dtvconfparserhelpers.h"

class SatIPStreamHandler;
class DTVSignalMonitor;
class SatIPChannel;
class DeviceReadBuffer;

class SatIPStreamHandler : public StreamHandler
{
    friend class SatIPRTSPWriteHelper;
    friend class SatIPRTSPReadHelper;
    friend class SatIPSignalMonitor;

  public:
    static SatIPStreamHandler *Get(const QString &devicename);
    static void Return(SatIPStreamHandler * & ref);

    virtual void AddListener(MPEGStreamData *data,
        bool allow_section_reader = false,
        bool needs_drb = false,
        QString output_file = QString())
    {
        StreamHandler::AddListener(data, false, false, output_file);
    } // StreamHandler

    virtual bool UpdateFilters();

    void Tune(const DTVMultiplex &tuning);

  private:
    SatIPStreamHandler(const QString &);

    bool Open(void);
    void Close(void);

    virtual void run(void); // MThread

    // for implementing Get & Return
    static QMutex                             s_satiphandlers_lock;
    static QMap<QString, SatIPStreamHandler*> s_satiphandlers;
    static QMap<QString, uint>                s_satiphandlers_refcnt;

    static QMutex s_cseq_lock;
    static uint s_cseq;

  protected:
    SatIPRTSP   *m_rtsp;

  private:
    DTVTunerType m_tunertype;
    QString      m_device;
    QUrl         m_baseurl;
    QUrl         m_tuningurl;
    bool         m_setupinvoked;
    QMutex       m_tunelock;
    QStringList  m_oldpids;
};

#endif // _SATIPSTREAMHANDLER_H_
