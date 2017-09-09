// MythTV includes
#include "mythlogging.h"
#include "mpegtables.h"
#include "satipchannel.h"
#include "tv_rec.h"
#include "satiputils.h"

#define LOC  QString("SatIPChan[%1](%2): ").arg(m_inputid).arg(GetDevice())

SatIPChannel::SatIPChannel(TVRec *parent, const QString & device) :
    DTVChannel(parent), m_device(device), m_stream_handler(NULL)
{
}

SatIPChannel::~SatIPChannel(void)
{
    if (IsOpen())
        Close();
}

bool SatIPChannel::Open(void)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + "Open()");

    if (IsOpen())
        return true;

    QMutexLocker locker(&m_tune_lock);

    tunerType = SatIP::toTunerType(m_device);

    if (!InitializeInput())
    {
        Close();
        return false;
    }

    OpenStreamHandler();

    return true;
}

void SatIPChannel::Close()
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + "Close()");

    if (IsOpen())
    {
        SatIPStreamHandler::Return(m_stream_handler);
    }
}

void SatIPChannel::OpenStreamHandler(void)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + "OpenStreamHandler()");

    m_stream_handler = SatIPStreamHandler::Get(m_device);
}

void SatIPChannel::CloseStreamHandler(void)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + "CloseStreamHandler()");

    QMutexLocker locker(&m_stream_lock);

    if (m_stream_handler)
    {
        if (m_stream_data)
            m_stream_handler->RemoveListener(m_stream_data);

        SatIPStreamHandler::Return(m_stream_handler);
    }
}

bool SatIPChannel::Tune(const QString &channum)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("Tune(%1)").arg(channum));

    if (!IsOpen())
    {
        LOG(VB_CHANNEL, LOG_ERR, LOC + "Tune failed, not open");
        return false;
    }

    return false;
}

bool SatIPChannel::Tune(const DTVMultiplex &tuning)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("Tune(%1)").arg(tuning.frequency));

    m_stream_handler->Tune(tuning);

    return true;
}

bool SatIPChannel::IsOpen(void) const
{
    QMutexLocker locker(&m_stream_lock);
    bool ret = (m_stream_handler && !m_stream_handler->HasError() &&
        m_stream_handler->IsRunning());
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + QString("IsOpen %1")
        .arg(ret ? "true" : "false"));
    return ret;
}
