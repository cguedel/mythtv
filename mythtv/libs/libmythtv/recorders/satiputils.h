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

  private:
    static QStringList doUPNPsearch(void);
};

#endif // _SATIP_UTILS_H
