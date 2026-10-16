#pragma once
#include <algorithm>
#include <cstdint>

// Times are monotonic milliseconds. Supplying time explicitly makes pause,
// delayed UI ticks and exact warning boundaries deterministic in tests.
struct TimerState {
    enum class Segment { Story, Break };
    enum class Status { Ready, Running, Paused, Stopped };
    Segment segment = Segment::Story;
    Status status = Status::Ready;
    std::int64_t target = 600000;
    int story = 0;

    std::int64_t elapsed(std::int64_t now) const {
        return held + (status == Status::Running ? std::max<std::int64_t>(0, now - anchor) : 0);
    }
    std::int64_t remaining(std::int64_t now) const { return target - elapsed(now); }
    bool warning(std::int64_t now, int seconds) const {
        return seconds > 0 && status != Status::Ready && remaining(now) > 0 &&
               remaining(now) <= std::int64_t(seconds) * 1000;
    }
    bool overtime(std::int64_t now) const { return status != Status::Ready && remaining(now) <= 0; }
    void start(std::int64_t now) {
        if (status == Status::Running) return;
        if (segment == Segment::Story && story == 0) story = 1;
        anchor = now;
        status = Status::Running;
    }
    void pause(std::int64_t now) {
        if (status != Status::Running) return;
        held = elapsed(now); status = Status::Paused;
    }
    void stop(std::int64_t now) { held = elapsed(now); status = Status::Stopped; }
    void reset(std::int64_t duration) {
        target = std::max<std::int64_t>(1000, duration);
        held = 0; anchor = 0; status = Status::Ready;
    }
    void selectStory(int number, std::int64_t duration) {
        story = std::max(1, number);
        segment = Segment::Story;
        reset(duration);
    }
    void previousStory(std::int64_t duration) { selectStory(story - 1, duration); }
    void restartFromBeginning(std::int64_t duration) { selectStory(1, duration); }
    void nextStory(std::int64_t now, std::int64_t duration) {
        ++story; segment = Segment::Story; reset(duration); start(now);
    }
    void takeBreak(std::int64_t now, std::int64_t duration) {
        segment = Segment::Break; reset(duration); start(now);
    }
private:
    std::int64_t held = 0;
    std::int64_t anchor = 0;
};
