#include "timer-state.hpp"
#include <cstdlib>
#include <iostream>

static int checks = 0;
static void check(bool ok, const char *why) {
    ++checks;
    if (!ok) { std::cerr << "FAILED: " << why << '\n'; std::exit(1); }
}
int main() {
    TimerState s;
    s.reset(600000);
    check(s.remaining(99999) == 600000, "Ready does not consume time");
    s.start(1000);
    check(s.story == 1, "First start is story one");
    check(s.remaining(571000) == 30000, "Countdown reaches 30-second boundary");
    check(!s.warning(570999, 30), "Warning does not start early");
    check(s.warning(571000, 30), "Warning starts at threshold");
    check(!s.warning(571000, 0), "Warning can be disabled");
    s.pause(571250);
    check(s.elapsed(900000) == 570250, "Pause preserves fractional elapsed time");
    s.start(1000000);
    check(s.elapsed(1001000) == 571250, "Resume excludes paused interval");
    s.stop(1002000);
    check(s.elapsed(9999999) == 572250, "Stop freezes final reading");
    s.start(2000000);
    check(s.elapsed(2001000) == 573250, "Stopped timer can continue");
    s.nextStory(3000000, 10000);
    check(s.story == 2 && s.elapsed(3000000) == 0, "Next Story advances and clears timer");
    check(s.status == TimerState::Status::Running, "Next Story starts immediately");
    check(!s.overtime(3009999), "No early expiry");
    check(s.overtime(3010000), "Expiry at exact target");
    check(s.remaining(3011555) == -1555, "Overtime keeps fractional accuracy");
    s.takeBreak(4000000, 300000);
    check(s.story == 2 && s.segment == TimerState::Segment::Break, "Break preserves story number");
    check(s.status == TimerState::Status::Running && s.remaining(4000000) == 300000, "Break immediately starts its separate duration");
    s.nextStory(5000000, 600000);
    check(s.story == 3 && s.segment == TimerState::Segment::Story && s.target == 600000, "Next Story leaves break and restores story preset");
    s.reset(120000);
    check(s.status == TimerState::Status::Ready && s.elapsed(9000000) == 0 && s.target == 120000, "Reset prepares without starting");
    check(!s.warning(9000000, 999) && !s.overtime(9000000), "Ready has no alerts");
    s.nextStory(10000000, 600000);
    s.start(10005000);
    check(s.elapsed(10010000) == 10000, "Repeated start cannot lose elapsed time");
    check(s.elapsed(20000000) == 10000000, "Delayed UI updates do not accumulate drift");
    TimerState b; b.takeBreak(0, 1000); b.nextStory(1000, 10000);
    check(b.story == 1, "Break before first story still starts at story one");
    s.selectStory(9, 120000); s.start(1000); s.pause(31000);
    s.restartFromBeginning(600000);
    check(s.story == 1 && s.segment == TimerState::Segment::Story, "Restart from beginning returns to story one");
    check(s.elapsed(999999) == 0 && s.target == 600000 && s.status == TimerState::Status::Ready, "Restart clears paused time and readies story preset");
    s.start(1000000); s.nextStory(1005000, 600000);
    check(s.story == 2, "Next Story after starting a restarted session advances to two");
    s.pause(1010000); s.reset(600000);
    check(s.story == 2 && s.elapsed(5000000) == 0, "Reset redoes story two without advancing its number");
    s.selectStory(9, 600000); s.start(1000); s.previousStory(600000);
    check(s.story == 8 && s.status == TimerState::Status::Ready && s.elapsed(99999) == 0, "Previous Story selects prior number and resets without running");
    s.selectStory(1, 600000); s.previousStory(600000); s.previousStory(600000);
    check(s.story == 1, "Previous Story cannot go below one");
    s.takeBreak(1000, 300000); s.selectStory(2, 600000);
    check(s.story == 2 && s.segment == TimerState::Segment::Story && s.target == 600000, "Selecting a story leaves break and uses story preset");
    s.takeBreak(2000, 300000); s.restartFromBeginning(600000);
    check(s.story == 1 && s.segment == TimerState::Segment::Story && s.status == TimerState::Status::Ready, "Restart session during break prepares story one");
    s.selectStory(0, 600000);
    check(s.story == 1, "Invalid story selection clamps to one");
    std::cout << checks << " timer checks passed\n";
}
