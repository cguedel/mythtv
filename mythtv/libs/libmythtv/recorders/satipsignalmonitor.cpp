// -*- Mode: c++ -*-
// Copyright (c) 2006, Daniel Thor Kristjansson

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/select.h>
#endif

#include "mythlogging.h"
#include "mythdbcon.h"
#include "satipsignalmonitor.h"
#include "mpegtables.h"
#include "atsctables.h"

#include "satipchannel.h"
#include "satipstreamhandler.h"

#define LOC QString("SATIPSigMon[%1](%2): ") \
            .arg(capturecardnum).arg(channel->GetDevice())

SatIPSignalMonitor::SatIPSignalMonitor(
    int db_cardnum, SatIPChannel* _channel, uint64_t _flags) :
    DTVSignalMonitor(db_cardnum, _channel, _flags),
    streamHandlerStarted(false), streamHandler(NULL)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + "ctor");

    signalStrength.SetRange(0, 255);

    AddFlags(kSigMon_WaitForSig);

    streamHandler = SatIPStreamHandler::Get(_channel->GetDevice());
}

SatIPSignalMonitor::~SatIPSignalMonitor()
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + "dtor");
    Stop();
    SatIPStreamHandler::Return(streamHandler);
}

void SatIPSignalMonitor::Stop(void)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + "Stop() -- begin");
    SignalMonitor::Stop();
    if (GetStreamData())
        streamHandler->RemoveListener(GetStreamData());
    streamHandlerStarted = false;

    LOG(VB_CHANNEL, LOG_INFO, LOC + "Stop() -- end");
}

SatIPChannel *SatIPSignalMonitor::GetSatIPChannel(void)
{
    return dynamic_cast<SatIPChannel*>(channel);
}

void SatIPSignalMonitor::UpdateValues(void)
{
    if (!running || exit)
        return;

    SatIPStreamHandler* sh = GetSatIPChannel()->GetStreamHandler();

    if (streamHandlerStarted)
    {
        EmitStatus();
        if (IsAllGood())
            SendMessageAllGood();

        signalStrength.SetValue(sh->m_rtsp->GetSignalStrength());
        //signalStrength.SetValue(1);

        update_done = true;
        return;
    }
    
    // Set SignalMonitorValues from info from card.
    bool isLocked = false;
    {
        QMutexLocker locker(&statusLock);
        signalLock.SetValue(sh->m_rtsp->HasLock());
        //signalLock.SetValue(1);
        signalStrength.SetValue(sh->m_rtsp->GetSignalStrength());
        isLocked = signalLock.IsGood();
    }

    EmitStatus();
    if (IsAllGood())
        SendMessageAllGood();

    // Start table monitoring if we are waiting on any table
    // and we have a lock.
    if (isLocked && GetStreamData() &&
        HasAnyFlag(kDTVSigMon_WaitForPAT | kDTVSigMon_WaitForPMT |
            kDTVSigMon_WaitForMGT | kDTVSigMon_WaitForVCT |
            kDTVSigMon_WaitForNIT | kDTVSigMon_WaitForSDT))
    {
        streamHandler->AddListener(GetStreamData());
        streamHandlerStarted = true;
    }

    update_done = true;
}
