/** -*- Mode: c++ -*-*/

#ifndef SATIPRTSP_H
#define SATIPRTSP_H

#include <stdint.h>

#include <QObject>
#include <QMap>
#include <QString>
#include <QMutex>
#include <QUrl>
#include <QTimerEvent>
#include <QUdpSocket>
#include "packetbuffer.h"

class SatIPRTSP;
class SatIPStreamHandler;
typedef QMap<QString, QString> Headers;

class SatIPRTSPReadHelper : QObject
{
    friend class SatIPRTSP;
    Q_OBJECT

  public:
    SatIPRTSPReadHelper(SatIPRTSP *p);
    ~SatIPRTSPReadHelper();

  public slots:
    void ReadPending(void);

  protected:
    QUdpSocket *m_socket;

  private:
    SatIPRTSP *m_parent;
};

class SatIPRTCPReadHelper : QObject
{
    friend class SatIPRTSP;
    Q_OBJECT

  public:
    SatIPRTCPReadHelper(SatIPRTSP *p);
    ~SatIPRTCPReadHelper();

  public slots:
    void ReadPending(void);

  protected:
    QUdpSocket *m_socket;

  private:
    SatIPRTSP *m_parent;
};

class SatIPRTSPWriteHelper : QObject
{
    Q_OBJECT

  public:
    SatIPRTSPWriteHelper(SatIPRTSP*, const SatIPStreamHandler*);

  protected:
    void timerEvent(QTimerEvent*);

  private:
    SatIPRTSP                *m_parent;
    const SatIPStreamHandler *m_streamhandler;
    int                       m_timer;
    uint                      m_last_sequence_number;
    uint                      m_last_timestamp;
    uint                      m_previous_last_sequence_number;
    int                       m_lost;
    int                       m_lost_interval;
};

class SatIPRTSP : QObject
{
    friend class SatIPRTSPReadHelper;
    friend class SatIPRTCPReadHelper;
    friend class SatIPRTSPWriteHelper;
    friend class SatIPSignalMonitor;

    Q_OBJECT

  public:
    explicit SatIPRTSP(const SatIPStreamHandler*);
    ~SatIPRTSP();

    bool Setup(QUrl url);
    bool Play(QStringList &pids);
    bool Teardown();

    bool HasLock();
    int  GetSignalStrength();

  protected:
    void timerEvent(QTimerEvent*);
    void SetSigmonValues(bool hasLock, int signalStrength);

  signals:
    void startKeepalive(int timeout);
    void stopKeepalive(void);

  protected slots:
    void startKeepaliveRequested(int timeout);
    void stopKeepaliveRequested(void);

  protected:
    QUrl m_request_url;
    PacketBuffer *m_buffer;

  private:
    bool sendMessage(QUrl url, QString msg, QStringList* additionalHeaders = NULL); 

  private:
    const SatIPStreamHandler *m_streamhandler;

    uint    m_cseq;
    QString m_sessionid;
    QString m_streamid;
    Headers m_headers;

    int m_timer;
    int m_timeout;

    QMutex m_ctrlsocket_lock;
    QMutex m_sigmon_lock;

    bool m_hasLock;
    int  m_signalStrength;

    SatIPRTSPReadHelper  *m_readhelper;
    SatIPRTSPWriteHelper *m_writehelper;
    SatIPRTCPReadHelper  *m_rtcp_readhelper;
};

#endif // SATIPRTSP_H
