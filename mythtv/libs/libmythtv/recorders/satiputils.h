#ifndef _SATIP_UTILS_H_
#define _SATIP_UTILS_H_

// Qt headers
#include <QString>

// MythTV headers
#include "dtvconfparserhelpers.h"

class SatIP
{
  public:
    static QStringList probeDevices(void);
    static QString findDeviceIP(QString deviceuuid);

    static int toDvbInputType(QString deviceid);
    static int toTunerType(QString deviceid);

    static QString freq(uint64_t freq);
    static QString bw(DTVBandwidth bw);
    static QString msys(DTVModulationSystem msys);
    static QString mtype(DTVModulation mtype);
    static QString tmode(DTVTransmitMode tmode);
    static QString gi(DTVGuardInterval gi);
    static QString fec(DTVCodeRate fec);

  private:
    static QStringList doUPNPsearch(void);
};

#endif // _SATIP_UTILS_H
