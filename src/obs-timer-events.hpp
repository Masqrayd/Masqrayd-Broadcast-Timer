#pragma once
#include "timer-window.hpp"
#include <obs-frontend-api.h>

// Shared with the frontend regression test so the actual OBS event mapping is
// tested, including STARTED rather than STARTING and unrelated-event no-ops.
inline void forwardTimerOutputEvent(TimerWindow &timer, obs_frontend_event event) {
    using Output = TimerWindow::Output;
    using Phase = TimerWindow::OutputPhase;
    switch (event) {
    case OBS_FRONTEND_EVENT_RECORDING_STARTING: timer.outputEvent(Output::Recording, Phase::Starting); break;
    case OBS_FRONTEND_EVENT_RECORDING_STARTED: timer.outputEvent(Output::Recording, Phase::Started); break;
    case OBS_FRONTEND_EVENT_RECORDING_STOPPING: timer.outputEvent(Output::Recording, Phase::Stopping); break;
    case OBS_FRONTEND_EVENT_RECORDING_STOPPED: timer.outputEvent(Output::Recording, Phase::Stopped); break;
    case OBS_FRONTEND_EVENT_STREAMING_STARTING: timer.outputEvent(Output::Streaming, Phase::Starting); break;
    case OBS_FRONTEND_EVENT_STREAMING_STARTED: timer.outputEvent(Output::Streaming, Phase::Started); break;
    case OBS_FRONTEND_EVENT_STREAMING_STOPPING: timer.outputEvent(Output::Streaming, Phase::Stopping); break;
    case OBS_FRONTEND_EVENT_STREAMING_STOPPED: timer.outputEvent(Output::Streaming, Phase::Stopped); break;
    default: break;
    }
}
