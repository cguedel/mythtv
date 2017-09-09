/** -*- Mode: c++ -*- */

#include <QStringList>
#include <QTcpSocket>
#include <QUrl>
#include <QVector>

// MythTV includes
#include "satiprtsp.h"
#include "mythlogging.h"
#include "mythsocket.h"
#include "rtppacketbuffer.h"
#include <rtptsdatapacket.h>
#include "satipstreamhandler.h"
#include "rtcpdatapacket.h"
#include "satiprtcppacket.h"

#define LOC QString("SatIPRTSP(%1): ").arg(m_request_url.toString())

SatIPRTSP::SatIPRTSP(const SatIPStreamHandler* handler)
    : m_request_url(QUrl("")),
      m_buffer(NULL),
      m_streamhandler(handler),
      m_cseq(0),
      m_sessionid(""),
      m_streamid(""),
      m_timer(0),
      m_timeout(0)
{
    connect(this, SIGNAL(startKeepalive(int)), this, SLOT(startKeepaliveRequested(int)));
    connect(this, SIGNAL(stopKeepalive()), this, SLOT(stopKeepaliveRequested()));

    m_buffer = new RTPPacketBuffer(0);
    m_readhelper = new SatIPRTSPReadHelper(this);
    m_writehelper = new SatIPRTSPWriteHelper(this, handler);

    if (!m_readhelper->m_socket->bind(QHostAddress::AnyIPv4, 0,
                                      QAbstractSocket::DefaultForPlatform))
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + QString("Failed to bind RTP socket"));
    }

    uint port = m_readhelper->m_socket->localPort() + 1;

    m_rtcp_readhelper = new SatIPRTCPReadHelper(this);
    if (!m_rtcp_readhelper->m_socket->bind(QHostAddress::AnyIPv4,
                                           port,
                                           QAbstractSocket::DefaultForPlatform))
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + QString("Failed to bind RTCP socket to port %1").arg(port));
    }
}

SatIPRTSP::~SatIPRTSP()
{
    delete m_buffer;
    delete m_readhelper;
    delete m_writehelper;
    delete m_rtcp_readhelper;
}


bool SatIPRTSP::Setup(QUrl url)
{
    m_request_url = url;
    LOG(VB_RECORD, LOG_NOTICE, LOC + "Setup");

    if (url.port() != 554)
    {
        LOG(VB_GENERAL, LOG_WARNING, "SatIP implementation specifies port 554 to be used");
    }

    QStringList headers;
    headers.append(
        QString("Transport: RTP/AVP;unicast;client_port=%1-%2")
        .arg(m_readhelper->m_socket->localPort()).arg(m_readhelper->m_socket->localPort() + 1));

    if (!sendMessage(m_request_url, "SETUP", &headers))
    {
        LOG(VB_RECORD, LOG_ERR, LOC + "Failed to send SETUP message");
        return false;
    }

    if (m_headers.contains("COM.SES.STREAMID"))
    {
        m_streamid = m_headers["COM.SES.STREAMID"];
    } else
    {
        LOG(VB_RECORD, LOG_ERR, LOC + "Response did not contain the com.ses.streamID field");
        return false;
    }

    QRegExp sessionTimeoutRegex(
        "^([^\\r\\n]+);timeout=([0-9]+)?", Qt::CaseSensitive, QRegExp::RegExp2);

    if (m_headers.contains("SESSION"))
    {
        if (sessionTimeoutRegex.indexIn(m_headers["SESSION"]) == -1)
        {
            LOG(VB_RECORD, LOG_ERR, LOC + QString("Failed to extract session id from sesion header ('%1')").arg(m_headers["SESSION"]));
        }

        QStringList parts = sessionTimeoutRegex.capturedTexts();
        m_sessionid = parts.at(1);
        m_timeout = (parts.length() > 1 ? parts.at(2).toInt() / 2 : 30) * 1000;
    }

    LOG(VB_RECORD, LOG_INFO, LOC + QString("Setup completed, sessionID = %1, streamID = %2, timeout = %3s").arg(m_sessionid).arg(m_streamid).arg(m_timeout / 1000));
    emit(startKeepalive(m_timeout));

    return true;
}

bool SatIPRTSP::Play(QStringList &pids)
{
    LOG(VB_RECORD, LOG_NOTICE, LOC + QString("Play, pids: %1").arg(pids.join(",")));

    QUrl url = QUrl(m_request_url);
    url.setQuery("");
    url.setPath(QString("/stream=%1").arg(m_streamid));

    url.setQuery(QString("pids=%1").arg(pids.size() > 0 ? pids.join(",") : "none"));

    if (!sendMessage(url, "PLAY"))
    {
        LOG(VB_RECORD, LOG_ERR, LOC + "Failed to send PLAY message");
        return false;
    }

    return true;
}

bool SatIPRTSP::Teardown(void)
{
    LOG(VB_RECORD, LOG_NOTICE, LOC + "Teardown");
    emit(stopKeepalive());

    QUrl url = QUrl(m_request_url);
    url.setQuery("");
    url.setPath(QString("/stream=%1").arg(m_streamid));

    bool result = sendMessage(url, "TEARDOWN");

    if (result)
    {
        m_sessionid = "";
        m_streamid = "";
    }

    return result;
}

bool SatIPRTSP::HasLock()
{
    QMutexLocker locker(&m_sigmon_lock);
    return m_hasLock;
}

int SatIPRTSP::GetSignalStrength()
{
    QMutexLocker locker(&m_sigmon_lock);
    return m_signalStrength;
}


void SatIPRTSP::SetSigmonValues(bool hasLock, int signalStrength)
{
    QMutexLocker locker(&m_sigmon_lock);

    m_hasLock = hasLock;
    m_signalStrength = signalStrength;
}


bool SatIPRTSP::sendMessage(QUrl url, QString msg, QStringList *additionalheaders)
{
    QMutexLocker locker(&m_ctrlsocket_lock);

    QTcpSocket ctrl_socket;
    ctrl_socket.connectToHost(url.host(), url.port());

    bool ok = ctrl_socket.waitForConnected();
    if (!ok)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + QString("Could not connect to server %1:%2").arg(url.host()).arg(url.port()));
        return false;
    }

    QStringList headers;
    headers.append(QString("%1 %2 RTSP/1.0").arg(msg).arg(url.toString()));
    headers.append(QString("User-Agent: MythTV Sat>IP client"));
    headers.append(QString("CSeq: %1").arg(++m_cseq));

    if (m_sessionid.length() > 0)
    {
        headers.append(QString("Session: %1").arg(m_sessionid));
    }

    if (additionalheaders != NULL)
    {
        for (int i = 0; i < additionalheaders->count(); i++)
        {
            headers.append(additionalheaders->at(i));
        }
    }

    headers.append("\r\n");

    QString request = headers.join("\r\n");
    LOG(VB_RECORD, LOG_DEBUG, LOC + QString("write: %1").arg(request));

    ctrl_socket.write(request.toLatin1());

    QRegExp firstLineRegex(
        "^RTSP/1.0 (\\d+) ([^\r\n]+)", Qt::CaseSensitive, QRegExp::RegExp2);
    QRegExp headerRegex(
        "^([^:]+):\\s*([^\\r\\n]+)", Qt::CaseSensitive, QRegExp::RegExp2);
    QRegExp blankLineRegex(
        "^[\\r\\n]*$", Qt::CaseSensitive, QRegExp::RegExp2);

    bool firstLine = true;
    while (true)
    {
        if (!ctrl_socket.canReadLine())
        {
            bool ready = ctrl_socket.waitForReadyRead(30 * 1000);
            if (!ready)
            {
                LOG(VB_RECORD, LOG_ERR, LOC + "RTSP server did not respond after 30s");
                return false;
            }
            continue;
        }

        QString line = ctrl_socket.readLine();
        LOG(VB_RECORD, LOG_DEBUG, LOC + QString("read: %1").arg(line));

        if (firstLine)
        {
            if (firstLineRegex.indexIn(line) == -1)
            {
                return false;
            }

            QStringList parts = firstLineRegex.capturedTexts();
            int responseCode = parts.at(1).toInt();
            QString responseMsg = parts.at(2);

            if (responseCode != 200)
            {
                return false;
            }
            firstLine = false;
            continue;
        }

        if (blankLineRegex.indexIn(line) != -1) break;

        if (headerRegex.indexIn(line) == -1)
        {
            return false;
        }
        QStringList parts = headerRegex.capturedTexts();
        m_headers.insert(parts.at(1).toUpper(), parts.at(2));
    }

    QString cSeq;

    if (m_headers.contains("CSEQ"))
    {
        cSeq = m_headers["CSEQ"];
    }

    if (cSeq != QString("%1").arg(m_cseq))
    {
        LOG(VB_RECORD, LOG_WARNING, LOC + QString("Expected CSeq of %1 but got %2").arg(m_cseq).arg(cSeq));
    }

    ctrl_socket.disconnectFromHost();
    if (ctrl_socket.state() != QAbstractSocket::UnconnectedState)
    {
        ctrl_socket.waitForDisconnected();
    }

    return true;
}

void SatIPRTSP::startKeepaliveRequested(int timeout)
{
    m_timer = startTimer(timeout);
}

void SatIPRTSP::stopKeepaliveRequested()
{
    killTimer(m_timer);
}

void SatIPRTSP::timerEvent(QTimerEvent* timerEvent)
{
    LOG(VB_RECORD, LOG_INFO, LOC + "Sending KeepAlive");

    QUrl url = QUrl(m_request_url);
    url.setPath("/");
    url.setQuery("");

    sendMessage(url, "OPTIONS");
}

#define LOC_RH QString("SatIPRTSP_RH(%1): ").arg(m_parent->m_request_url.toString())

SatIPRTSPReadHelper::SatIPRTSPReadHelper(SatIPRTSP* p)
    : QObject(p),
      m_socket(new QUdpSocket(this)),
      m_parent(p)
{
    LOG(VB_RECORD, LOG_INFO, LOC_RH +
        QString("Starting read helper for UDP (RTP) socket on port %1")
            .arg(m_socket->localPort()));

    connect(m_socket, SIGNAL(readyRead()), this, SLOT(ReadPending()));
}

SatIPRTSPReadHelper::~SatIPRTSPReadHelper()
{
    delete m_socket;
}

void SatIPRTSPReadHelper::ReadPending()
{
    while (m_socket->hasPendingDatagrams())
    {
        QHostAddress sender;
        quint16 senderPort;

        UDPPacket packet(m_parent->m_buffer->GetEmptyPacket());
        QByteArray &data = packet.GetDataReference();
        data.resize(m_socket->pendingDatagramSize());
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        m_parent->m_buffer->PushDataPacket(packet);
    }
}

#define LOC_RTCP QString("SatIPRTSP_RTCP(%1): ").arg(m_parent->m_request_url.toString())

SatIPRTCPReadHelper::SatIPRTCPReadHelper(SatIPRTSP* p)
    : QObject(p),
      m_socket(new QUdpSocket(this)),
      m_parent(p)
{
    LOG(VB_RECORD, LOG_INFO, LOC_RTCP +
        QString("Starting read helper for UDP (RTCP) socket on port %1")
            .arg(m_socket->localPort()));

    connect(m_socket, SIGNAL(readyRead()), this, SLOT(ReadPending()));
}

SatIPRTCPReadHelper::~SatIPRTCPReadHelper()
{
    delete m_socket;
}

void SatIPRTCPReadHelper::ReadPending()
{
    while (m_socket->hasPendingDatagrams())
    {
        QHostAddress sender;
        quint16 senderPort;

        QByteArray buf = QByteArray(m_socket->pendingDatagramSize(), Qt::Uninitialized);
        m_socket->readDatagram(buf.data(), buf.size(), &sender, &senderPort);

        SatIPRTCPPacket pkt = SatIPRTCPPacket(buf);
        if (!pkt.IsValid())
        {
            LOG(VB_GENERAL, LOG_ERR, LOC_RTCP + "Invalid RTCP packet received");
            continue;
        }

        QStringList data = pkt.Data().split(";");
        bool found = false;
        int i = 0;

        while (!found && i < data.length())
        {
            QString item = data.at(i);
            if (item.startsWith("tuner="))
            {
                found = true;
                QStringList tuner = item.split(",");

                if (tuner.length() > 2)
                {
                    bool hasLock = tuner.at(2).toInt() != 0;
                    int strength = tuner.at(1).toInt();

                    m_parent->SetSigmonValues(hasLock, strength);
                }
            }

            i++;
        }
    }
}

#define LOC_WH QString("SatIPRTSP_WH(%1): ").arg(m_parent->m_request_url.toString())

SatIPRTSPWriteHelper::SatIPRTSPWriteHelper(SatIPRTSP* parent, const SatIPStreamHandler* handler)
    : m_parent(parent),
      m_streamhandler(handler)
{
    m_timer = startTimer(200);
}

void SatIPRTSPWriteHelper::timerEvent(QTimerEvent*)
{
    while (m_parent->m_buffer->HasAvailablePacket())
    {
        RTPDataPacket pkt(m_parent->m_buffer->PopDataPacket());

        if (!pkt.IsValid())
        {
            break;
        }

        if (pkt.GetPayloadType() == RTPDataPacket::kPayLoadTypeTS)
        {
            RTPTSDataPacket ts_packet(pkt);

            if (!ts_packet.IsValid())
            {
                m_parent->m_buffer->FreePacket(pkt);
                continue;
            }

            uint exp_seq_num = m_last_sequence_number + 1;
            uint seq_num = ts_packet.GetSequenceNumber();
            if (m_last_sequence_number &&
                ((exp_seq_num & 0xFFFF) != (seq_num & 0xFFFF)))
            {
                LOG(VB_RECORD, LOG_INFO, LOC_WH +
                    QString("Sequence number mismatch %1!=%2")
                    .arg(seq_num).arg(exp_seq_num));
                if (seq_num > exp_seq_num)
                {
                    m_lost_interval = seq_num - exp_seq_num;
                    m_lost += m_lost_interval;
                }
            }
            m_last_sequence_number = seq_num;
            m_last_timestamp = ts_packet.GetTimeStamp();

#if 0
            LOG(VB_RECORD, LOG_DEBUG,
                QString("Processing RTP packet(seq:%1 ts:%2, ts_data_size:%3)")
                .arg(m_last_sequence_number).arg(m_last_timestamp).arg(ts_packet.GetTSDataSize()));
#endif

            m_streamhandler->_listener_lock.lock();

            int remainder = 0;
            SatIPStreamHandler::StreamDataList::const_iterator sit;
            sit = m_streamhandler->_stream_data_list.begin();
            for (; sit != m_streamhandler->_stream_data_list.end(); ++sit)
            {
                remainder = sit.key()->ProcessData(
                    ts_packet.GetTSData(), ts_packet.GetTSDataSize());
            }

            m_streamhandler->_listener_lock.unlock();

            if (remainder != 0)
            {
                LOG(VB_RECORD, LOG_INFO, LOC_WH +
                    QString("data_length = %1 remainder = %2")
                    .arg(ts_packet.GetTSDataSize()).arg(remainder));
            }
        }
        m_parent->m_buffer->FreePacket(pkt);
    }
}
