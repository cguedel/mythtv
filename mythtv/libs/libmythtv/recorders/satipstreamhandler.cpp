// -*- Mode: c++ -*-

// POSIX headers
#include <fcntl.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/select.h>
#include <sys/ioctl.h>
#endif

// MythTV headers
#include "satipstreamhandler.h"
#include "satipchannel.h"
#include "dtvsignalmonitor.h"
#include "streamlisteners.h"
#include "mpegstreamdata.h"
#include "cardutil.h"
#include "mythlogging.h"
#include "cetonrtsp.h"
#include "satiputils.h"

#define LOC      QString("SATIPSH(%1): ").arg(_device)

QMap<QString, SatIPStreamHandler*> SatIPStreamHandler::s_satiphandlers;
QMap<QString, uint>               SatIPStreamHandler::s_satiphandlers_refcnt;
QMutex                           SatIPStreamHandler::s_satiphandlers_lock;

SatIPStreamHandler *SatIPStreamHandler::Get(const QString &devname)
{
    QMutexLocker locker(&s_satiphandlers_lock);

    QMap<QString, SatIPStreamHandler*>::iterator it = s_satiphandlers.find(devname);

    if (it == s_satiphandlers.end())
    {
        SatIPStreamHandler *newhandler = new SatIPStreamHandler(devname);
        newhandler->Open();
        s_satiphandlers[devname] = newhandler;
        s_satiphandlers_refcnt[devname] = 1;

        LOG(VB_RECORD, LOG_INFO,
            QString("SATIPSH: Creating new stream handler %1")
            .arg(devname));
    }
    else
    {
        s_satiphandlers_refcnt[devname]++;
        uint rcount = s_satiphandlers_refcnt[devname];
        LOG(VB_RECORD, LOG_INFO,
            QString("SATIPSH: Using existing stream handler %1")
            .arg(devname) +
            QString(" (%1 in use)").arg(rcount));
    }

    return s_satiphandlers[devname];
}

void SatIPStreamHandler::Return(SatIPStreamHandler * & ref)
{
    QMutexLocker locker(&s_satiphandlers_lock);

    QString devname = ref->_device;

    QMap<QString, uint>::iterator rit = s_satiphandlers_refcnt.find(devname);
    if (rit == s_satiphandlers_refcnt.end())
        return;

    LOG(VB_RECORD, LOG_INFO, QString("SATIPSH: Return(%1) has %2 handlers")
        .arg(devname).arg(*rit));

    if (*rit > 1)
    {
        ref = NULL;
        (*rit)--;
        return;
    }

    QMap<QString, SatIPStreamHandler*>::iterator it = s_satiphandlers.find(devname);
    if ((it != s_satiphandlers.end()) && (*it == ref))
    {
        LOG(VB_RECORD, LOG_INFO, QString("SATIPSH: Closing handler for %1")
            .arg(devname));
        ref->Stop();
        delete *it;
        s_satiphandlers.erase(it);
    }
    else
    {
        LOG(VB_GENERAL, LOG_ERR,
            QString("SATIPSH Error: Couldn't find handler for %1")
            .arg(devname));
    }

    s_satiphandlers_refcnt.erase(rit);
    ref = NULL;
}

SatIPStreamHandler::SatIPStreamHandler(const QString &device) :
    StreamHandler(device), m_device(device), m_baseurl(NULL), m_rtsp(new SatIPRTSP(this)), m_tunelock(QMutex::Recursive)
{
    setObjectName("SatIPStreamHandler");
}

bool SatIPStreamHandler::UpdateFilters(void)
{
#ifdef DEBUG_PID_FILTERS
    LOG(VB_RECORD, LOG_DEBUG, LOC + "UpdateFilters()");
#endif // DEBUG_PID_FILTERS
    QMutexLocker locker(&_pid_lock);

    QStringList pids;

    PIDInfoMap::const_iterator it = _pid_info.begin();
    for (; it != _pid_info.end(); ++it)
    {
        pids.append(QString("%1").arg(it.key()));
    }

#ifdef DEBUG_PID_FILTERS
    QString msg = QString("PIDS: '%1'").arg(pids.join(","));
    LOG(VB_RECORD, LOG_DEBUG, LOC + msg);
#endif

    if (m_rtsp && m_oldpids != pids)
    {
        m_rtsp->Play(pids);
        m_oldpids = pids;
    }

    return true;
}

void SatIPStreamHandler::run(void)
{
    int tunerLock = 0;
    char *error = NULL;

    SetRunning(true, false, false);

    LOG(VB_RECORD, LOG_INFO, LOC + "RunTS(): begin");

    int remainder = 0;
    QTime last_update;

    QUrl old_tuningurl;
    m_setupinvoked = false;

    while (_running_desired && !_error)
    {
        {
            QMutexLocker locker(&m_tunelock);

            if (old_tuningurl != m_tuningurl)
            {
                if (m_setupinvoked)
                {
                    m_rtsp->Teardown();
                }

                m_rtsp->Setup(m_tuningurl);
                m_setupinvoked = true;
                old_tuningurl = m_tuningurl;

                last_update.restart();
            }
        }

        int elapsed = !last_update.isValid() ? -1 : last_update.elapsed();
        elapsed = (elapsed < 0) ? 1000 : elapsed;
        if (elapsed > 100)
        {
            UpdateFiltersFromStreamData();
            UpdateFilters();
            last_update.restart();
        }
    }
    LOG(VB_RECORD, LOG_INFO, LOC + "RunTS(): " + "shutdown");

    RemoveAllPIDFilters();

    if (m_setupinvoked && !m_rtsp->Teardown())
    {
        LOG(VB_RECORD, LOG_ERR, LOC + "Failed to teardown RTSP stream");
    }

    SetRunning(false, false, false);
    RunEpilog();
}

void SatIPStreamHandler::Tune(const DTVMultiplex &tuning)
{
    QMutexLocker locker(&m_tunelock);

    // build the query string
    QStringList qry;

    if (m_tunertype == DTVTunerType::kTunerTypeDVBC)
    {
        qry.append(QString("freq=%1").arg(SatIP::freq(tuning.frequency)));
        qry.append(QString("sr=%1").arg(tuning.symbolrate / 1000)); // symbolrate in ksymb/s
        qry.append("msys=dvbc");
        qry.append(QString("mtype=%1").arg(SatIP::mtype(tuning.modulation)));

        // TODO: DVB-C2 parameters
    }
    else if (m_tunertype == DTVTunerType::kTunerTypeDVBT || m_tunertype == DTVTunerType::kTunerTypeDVBT2)
    {
        qry.append(QString("freq=%1").arg(SatIP::freq(tuning.frequency)));
        qry.append(QString("bw=%1").arg(SatIP::bw(tuning.bandwidth)));  
        qry.append(QString("msys=%1").arg(SatIP::msys(tuning.mod_sys)));
        qry.append(QString("tmode=%1").arg(SatIP::tmode(tuning.trans_mode)));
        qry.append(QString("mtype=%1").arg(SatIP::mtype(tuning.modulation)));
        qry.append(QString("gi=%1").arg(SatIP::gi(tuning.guard_interval)));
        qry.append(QString("fec=%1").arg(SatIP::fec(tuning.fec)));
        
        // TODO: DVB-T2 parameters
    }
    else if (m_tunertype == DTVTunerType::kTunerTypeDVBS1)
    {
        // TODO
    }

    qry.append("pids=none"); // pids are handled in UpdateFilters()

    QUrl url = QUrl(m_baseurl);
    url.setQuery(qry.join("&"));
    
    m_tuningurl = url;
}

bool SatIPStreamHandler::Open(void)
{
    QUrl url;
    url.setScheme("rtsp");
    url.setPort(554);
    url.setPath("/");

    // discover the device using SSDP
    QStringList devinfo = m_device.split(":");
    if (devinfo.at(0).toUpper() == "UUID")
    {
        QString deviceId = QString("uuid:%1").arg(devinfo.at(1));

        QString ip = SatIP::findDeviceIP(deviceId);
        if (ip != NULL)
        {
            LOG(VB_RECORD, LOG_INFO, LOC + QString("Discovered device %1 at %2").arg(deviceId).arg(ip));
        } else
        {
            LOG(VB_RECORD, LOG_ERR, LOC + QString("Failed to discover device %1, no IP found").arg(deviceId));
            return false;
        }

        url.setHost(ip);
    } else
    {
        // TODO: Handling of manual IP devices
    }

    m_tunertype = SatIP::toTunerType(m_device);
    m_baseurl = url;

    return true;
}

void SatIPStreamHandler::Close(void)
{
    delete m_rtsp;
    m_rtsp = NULL;
    m_baseurl = NULL;
}
