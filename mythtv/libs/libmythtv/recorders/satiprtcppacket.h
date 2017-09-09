#ifndef SATIPRTCPPACKET_H
#define SATIPRTCPPACKET_H

// Qt includes
#include <QString>
#include <QtEndian>
#include <QIODevice>
#include <QDataStream>
#include "mythlogging.h"

// MythTV includes
#define RTCP_TYPE_APP  204

class SatIPRTCPPacket
{
  public:
    SatIPRTCPPacket(QByteArray &data);

    bool IsValid() const { return m_satip_data.length() > 0; };
    QString Data() const { return m_satip_data; };

  private:
    void parse();

  private:
    QByteArray m_data;
    QString m_satip_data;
};

#endif // SATIPRTCPPACKET_H