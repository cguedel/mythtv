// MythTV includes
#include "satipstreamhandler.h"
#include "satiprecorder.h"
#include "satipchannel.h"
#include "ringbuffer.h"
#include "tv_rec.h"
#include "mythlogging.h"

#define LOC QString("SatIPRec[%1]: ") \
            .arg(tvrec ? tvrec->GetInputId() : -1)

SatIPRecorder::SatIPRecorder(TVRec *rec, SatIPChannel *channel)
    : DTVRecorder(rec), _channel(channel), _stream_handler(NULL)
{
}

bool SatIPRecorder::Open(void)
{
    if (IsOpen())
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC + "Card already open");
        return true;
    }

    ResetForNewFile();

    _stream_handler = SatIPStreamHandler::Get(_channel->GetDevice());

    LOG(VB_RECORD, LOG_INFO, LOC + "SatIP opened successfully");

    return true;
}

void SatIPRecorder::Close(void)
{
    LOG(VB_RECORD, LOG_INFO, LOC + "Close() -- begin");

    if (IsOpen())
        SatIPStreamHandler::Return(_stream_handler);

    LOG(VB_RECORD, LOG_INFO, LOC + "Close() -- end");
}

void SatIPRecorder::StartNewFile(void)
{
    if (_record_mpts)
        _stream_handler->AddNamedOutputFile(ringBuffer->GetFilename());

    // Make sure the first things in the file are a PAT & PMT
    HandleSingleProgramPAT(_stream_data->PATSingleProgram(), true);
    HandleSingleProgramPMT(_stream_data->PMTSingleProgram(), true);
}

void SatIPRecorder::run(void)
{
    LOG(VB_RECORD, LOG_INFO, LOC + "run -- begin");

    /* Create video socket. */
    if (!Open())
    {
        _error = "Failed to open SatIPRecorder device";
        LOG(VB_GENERAL, LOG_ERR, LOC + _error);
        return;
    }

    {
        QMutexLocker locker(&pauseLock);
        request_recording = true;
        recording = true;
        recordingWait.wakeAll();
    }

    StartNewFile();

    _stream_data->AddAVListener(this);
    _stream_data->AddWritingListener(this);
    _stream_handler->AddListener(_stream_data, false, false,
                         (_record_mpts) ? ringBuffer->GetFilename() : QString());

    while (IsRecordingRequested() && !IsErrored())
    {
        if (PauseAndWait())
            continue;

        if (!IsRecordingRequested())
            break;

        {   // sleep 100 milliseconds unless StopRecording() or Unpause()
            // is called, just to avoid running this too often.
            QMutexLocker locker(&pauseLock);
            if (!request_recording || request_pause)
                continue;
            unpauseWait.wait(&pauseLock, 100);
        }

        if (!_input_pmt)
        {
            LOG(VB_GENERAL, LOG_WARNING, LOC +
                    "Recording will not commence until a PMT is set.");
            usleep(5000);
            continue;
        }

        if (!_stream_handler->IsRunning())
        {
            _error = "Stream handler died unexpectedly.";
            LOG(VB_GENERAL, LOG_ERR, LOC + _error);
        }
    }

    LOG(VB_RECORD, LOG_INFO, LOC + "run -- ending...");

    _stream_handler->RemoveListener(_stream_data);
    _stream_data->RemoveWritingListener(this);
    _stream_data->RemoveAVListener(this);

    Close();

    FinishRecording();

    QMutexLocker locker(&pauseLock);
    recording = false;
    recordingWait.wakeAll();

    LOG(VB_RECORD, LOG_INFO, LOC + "run -- end");
}

bool SatIPRecorder::PauseAndWait(int timeout)
{
    QMutexLocker locker(&pauseLock);
    if (request_pause)
    {
        if (!IsPaused(true))
        {
            _stream_handler->RemoveListener(_stream_data);

            paused = true;
            pauseWait.wakeAll();
            if (tvrec)
                tvrec->RecorderPaused();
        }

        unpauseWait.wait(&pauseLock, timeout);
    }

    if (!request_pause && IsPaused(true))
    {
        paused = false;
        _stream_handler->AddListener(_stream_data);
        unpauseWait.wakeAll();
    }

    return IsPaused(true);
}

QString SatIPRecorder::GetSIStandard(void) const
{
    return _channel->GetSIStandard();
}