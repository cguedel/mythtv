#include <unistd.h>

// Qt
#include <QString>
#include <QStringList>

// MythTV headers
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
