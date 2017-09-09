#include <unistd.h>

// Qt
#include <QString>
#include <QStringList>

// MythTV headers
#include "cardutil.h"
#include "mythlogging.h"
#include "mythtimer.h"
#include "satiputils.h"
#include "ssdp.h"

#define LOC QString("Sat>IP: ")

#define SEARCH_TIME 3000
#define SATIP_URI "urn:ses-com:device:SatIPServer:1"

QStringList SatIP::probeDevices(void)
{
    const int milliseconds = SEARCH_TIME;

    QStringList result = SatIP::doUPNPsearch();

    if (result.count())
        return result;

    SSDP::Instance()->PerformSearch(SATIP_URI, milliseconds / 1000);

    MythTimer totalTime; totalTime.start();
    MythTimer searchTime; searchTime.start();

    while (totalTime.elapsed() < milliseconds)
    {
        usleep(25000);
        int ttl = milliseconds - totalTime.elapsed();
        if (searchTime.elapsed() > 249 && ttl > 1000)
        {
            LOG(VB_GENERAL, LOG_DEBUG, LOC + QString("UPNP search %1 secs").arg(ttl / 1000));
            SSDP::Instance()->PerformSearch(SATIP_URI, ttl / 1000);
            searchTime.start();
        }
    }

    return SatIP::doUPNPsearch();
};

QStringList SatIP::doUPNPsearch(void)
{
    QStringList result;
    
    SSDPCacheEntries *satipservers = SSDP::Instance()->Find(SATIP_URI);

    if (!satipservers)
    {
        LOG(VB_GENERAL, LOG_INFO, LOC + "No UPnP Sat>IP servers found");
        return QStringList();
    }

    int count = satipservers->Count();
    if (count)
    {
        LOG(VB_GENERAL, LOG_INFO, LOC + QString("Found %1 possible Sat>IP servers").arg(count));
    } else 
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "No UPnP Sat>IP servers found, but SSDP::Find() != NULL");
    }

    EntryMap map;
    satipservers->GetEntryMap(map);

    EntryMap::const_iterator it;
    for (it = map.begin(); it != map.end(); ++it)
    {
        DeviceLocation *BE = (*it);
        QString friendlyName = BE->GetFriendlyName();
        UPnpDeviceDesc *desc = BE->GetDeviceDesc();
        QString ip = desc->m_HostUrl.host();

        if (!desc)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + QString("GetDeviceDesc() failed for %1").arg(friendlyName));
            continue;
        }

        QString id = desc->m_rootDevice.GetUDN();
        QList<NameValue> extraAttribs = desc->m_rootDevice.m_lstExtra;
        QList<NameValue>::const_iterator extra_it;

        for (extra_it = extraAttribs.begin(); extra_it != extraAttribs.end(); ++extra_it)
        {
            NameValue attrib = (*extra_it);
            if (attrib.sName == "satip:X_SATIPCAP")
            {
                QStringList caps = attrib.sValue.split(",");
                QStringList::const_iterator caps_it;

                for (caps_it = caps.begin(); caps_it != caps.end(); ++caps_it)
                {
                    QString cap = (*caps_it);
                    QStringList tuner = cap.split("-");

                    if (tuner.size() != 2)
                        continue;

                    int num_tuners = tuner.at(1).toInt();
                    for (int i = 0; i < num_tuners; i++)
                    {
                        QString device = QString("%1 %2 %3 %4 %5")
                                            .arg(id)
                                            .arg(friendlyName.replace(" ", ""))
                                            .arg(ip)
                                            .arg(i)
                                            .arg(tuner.at(0));
                        result << device;
                    }
                }
            }
        }

        BE->DecrRef();
    }

    satipservers->DecrRef();
    satipservers = NULL;

    return result;
};

QString SatIP::findDeviceIP(QString deviceuuid)
{
    QStringList devs = SatIP::probeDevices();

    QStringList::const_iterator it;

    for (it = devs.begin(); it != devs.end(); ++it)
    {
        QString dev = *it;
        QStringList devinfo = dev.split(" ");
        QString id = devinfo.at(0);

        if (id.toUpper() == deviceuuid.toUpper())
        {
            return devinfo.at(2);
        }
    }

    return NULL;
}

int SatIP::toDvbInputType(QString deviceid)
{
    QStringList dev = deviceid.split(":");
    if (dev.length() < 3)
    {
        return CardUtil::ERROR_UNKNOWN;
    }

    QString type = dev.at(2).toUpper();
    if (type == "DVBC2")
    {
        return CardUtil::DVBC; // DVB-C2 is not supported yet.
    }
    if (type == "DVBT2")
    {
        return CardUtil::DVBT2;
    }
    if (type == "DVBS2")
    {
        return CardUtil::DVBS2;
    }

    return CardUtil::ERROR_UNKNOWN;
}

int SatIP::toTunerType(QString deviceid)
{
    QStringList devinfo = deviceid.split(":");
    if (devinfo.length() < 3)
    {
        return DTVTunerType::kTunerTypeUnknown;
    }

    QString type = devinfo.at(2).toUpper();

    if (type.startsWith("DVBC")) // DVB-C2 is not supported yet.
    {
        return DTVTunerType::kTunerTypeDVBC;
    }
    
    if (type == "DVBT")
    {
        return DTVTunerType::kTunerTypeDVBT;
    }
    
    if (type == "DVBT2")
    {
        return DTVTunerType::kTunerTypeDVBT2;
    }
    
    if (type == "DVBS")
    {
        return DTVTunerType::kTunerTypeDVBS1;
    }
    
    if (type == "DVBS2")
    {
        return DTVTunerType::kTunerTypeDVBS2;
    }

    return DTVTunerType::kTunerTypeUnknown;
}

QString SatIP::bw(DTVBandwidth bw)
{
    if (bw == DTVBandwidth::kBandwidth6MHz)
    {
        return "6";
    }
    
    if (bw == DTVBandwidth::kBandwidth7MHz)
    {
        return "7";
    }

    if (bw == DTVBandwidth::kBandwidth8MHz)
    {
        return "8";
    }

    return "auto"; // TODO: this is not in the spec.
}

QString SatIP::freq(uint64_t freq)
{
    return QString::number(freq / 1000000.0, 'f', 2);
}

QString SatIP::msys(DTVModulationSystem msys)
{
    if (msys == DTVModulationSystem::kModulationSystem_DVBS)
    {
        return "dvbs";
    }

    if (msys == DTVModulationSystem::kModulationSystem_DVBS2)
    {
        return "dvbs2";
    }

    if (msys == DTVModulationSystem::kModulationSystem_DVBT)
    {
        return "dvbt";
    }

    if (msys == DTVModulationSystem::kModulationSystem_DVBT2)
    {
        return "dvbt2";
    }

    return "unsupported";
}

QString SatIP::mtype(DTVModulation mtype)
{
    if (mtype == DTVModulation::kModulationQPSK)
    {
        return "qpsk";
    }

    if (mtype == DTVModulation::kModulation8PSK)
    {
        return "8psk";
    }

    if (mtype == DTVModulation::kModulationQAM16)
    {
        return "16qam";
    }

    if (mtype == DTVModulation::kModulationQAM32)
    {
        return "32qam";
    }

    if (mtype == DTVModulation::kModulationQAM64)
    {
        return "64qam";
    }

    if (mtype == DTVModulation::kModulationQAM128)
    {
        return "128qam";
    }

    if (mtype == DTVModulation::kModulationQAM256)
    {
        return "256qam";
    }

    return "unknownqam";
}

QString SatIP::tmode(DTVTransmitMode tmode)
{
    if (tmode == DTVTransmitMode::kTransmissionMode2K)
    {
        return "2k";
    }

    if (tmode == DTVTransmitMode::kTransmissionMode8K)
    {
        return "8k";
    }

    return "auto"; // TODO: this is not in the spec.
}

QString SatIP::gi(DTVGuardInterval gi)
{
    if (gi == DTVGuardInterval::kGuardInterval_1_4)
    {
        return "14";
    }

    if (gi == DTVGuardInterval::kGuardInterval_1_8)
    {
        return "18";
    }

    if (gi == DTVGuardInterval::kGuardInterval_1_16)
    {
        return "116";
    }

    if (gi == DTVGuardInterval::kGuardInterval_1_32)
    {
        return "132";
    }

    return "auto"; // TODO: this is not in the spec.
}

QString SatIP::fec(DTVCodeRate fec)
{
    if (fec == DTVCodeRate::kFEC_1_2)
    {
        return "12";
    }

    if (fec == DTVCodeRate::kFEC_2_3)
    {
        return "23";
    }

    if (fec == DTVCodeRate::kFEC_3_4)
    {
        return "34";
    }

    if (fec == DTVCodeRate::kFEC_3_5)
    {
        return "35";
    }

    if (fec == DTVCodeRate::kFEC_4_5)
    {
        return "45";
    }

    if (fec == DTVCodeRate::kFEC_5_6)
    {
        return "56";
    }

    if (fec == DTVCodeRate::kFEC_6_7)
    {
        return "67";
    }

    if (fec == DTVCodeRate::kFEC_7_8)
    {
        return "78";
    }

    if (fec == DTVCodeRate::kFEC_8_9)
    {
        return "89";
    }

    if (fec == DTVCodeRate::kFEC_9_10)
    {
        return "910";
    }

    return "auto"; // TODO: this is not in the spec.
}
