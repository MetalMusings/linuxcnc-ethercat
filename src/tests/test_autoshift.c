#include <stdio.h>

#include "../lcec_autoshift.h"
#include "tests.h"

TESTGLOBALSETUP;

/// Runs n cycles with send offsets cycling through the given values; returns
/// how many of them chose a new shift.
static int feed(lcec_autoshift_t *a, int ready, const int32_t *offsets, int count, int n) {
  int i, changes = 0;
  for (i = 0; i < n; i++) changes += lcec_autoshift_update(a, ready, offsets[i % count]);
  return changes;
}

/// Measured for the full time with send offsets lo..hi.
static void measure(lcec_autoshift_t *a, uint32_t period, int32_t lo, int32_t hi) {
  int32_t offs[] = {hi, (lo + hi) / 2, lo};  // the first is not the smallest
  feed(a, 1, offs, 3, (int)(LCEC_AUTOSHIFT_MEASURE_NS / period) + 1);
  (void)period;
}

TESTFUNC(test_autoshift_waits_for_the_bus) {
  TESTSETUP;
  lcec_autoshift_t a;
  int32_t offs[] = {60000};

  lcec_autoshift_init(&a, 1000000, 500000);
  TESTINT(feed(&a, 0, offs, 1, 10000), 0);
  TESTINT(a.state, LCEC_AUTOSHIFT_WAIT);
  TESTINT((int)a.shift, 500000);
  // ready: measures 2 s, then chooses once
  TESTINT(feed(&a, 1, offs, 1, 1999), 0);
  TESTINT(a.state, LCEC_AUTOSHIFT_MEASURE);
  TESTINT(feed(&a, 1, offs, 1, 1), 1);
  TESTINT(a.state, LCEC_AUTOSHIFT_TWO);
  TESTINT(feed(&a, 1, offs, 1, 10000), 0);

  TESTRESULTS;
}

TESTFUNC(test_autoshift_two_cycles_in_the_middle) {
  TESTSETUP;
  lcec_autoshift_t a;

  // 1 ms, frames 50..80 us: window 130 us .. 1000 + 50 - 50 us
  lcec_autoshift_init(&a, 1000000, 500000);
  measure(&a, 1000000, 50000, 80000);
  TESTINT(a.state, LCEC_AUTOSHIFT_TWO);
  TESTINT((int)a.send_min, 50000);
  TESTINT((int)a.send_max, 80000);
  TESTINT((int)a.shift, 565000);
  TESTINT(a.fits, 1);

  // 500 us, frames 40..60 us: 110 .. 490 us (the first is not the largest)
  lcec_autoshift_init(&a, 500000, 250000);
  {
    int32_t offs[] = {40000, 50000, 60000};
    feed(&a, 1, offs, 3, 4001);
  }
  TESTINT(a.state, LCEC_AUTOSHIFT_TWO);
  TESTINT((int)a.shift, 300000);

  TESTRESULTS;
}

TESTFUNC(test_autoshift_wraps_into_the_period) {
  TESTSETUP;
  lcec_autoshift_t a;

  // late frames, 300..320 us of 500: two cycles at 370 .. 750 us, the
  // middle 560 us is 60 us into the next period
  lcec_autoshift_init(&a, 500000, 250000);
  measure(&a, 500000, 300000, 320000);
  TESTINT(a.state, LCEC_AUTOSHIFT_TWO);
  TESTINT((int)a.pos, 560000);
  TESTINT((int)a.shift, 60000);

  TESTRESULTS;
}

TESTFUNC(test_autoshift_three_cycles_when_two_do_not_fit) {
  TESTSETUP;
  lcec_autoshift_t a;

  // 250 us with a slow input: two cycles need 210 .. 200 us (empty),
  // three fit at -40 .. 0 us: the middle, -20 us, is 230 us
  lcec_autoshift_init(&a, 250000, 125000);
  a.input_margin = 100000;
  measure(&a, 250000, 50000, 160000);
  TESTINT(a.state, LCEC_AUTOSHIFT_THREE);
  TESTINT(a.fits, 1);
  TESTINT((int)a.pos, -20000);
  TESTINT((int)a.shift, 230000);

  // just within the window for two
  lcec_autoshift_init(&a, 250000, 125000);
  a.input_margin = 100000;
  measure(&a, 250000, 50000, 150000);
  TESTINT(a.state, LCEC_AUTOSHIFT_TWO);
  TESTINT((int)a.shift, 200000);

  TESTRESULTS;
}

TESTFUNC(test_autoshift_nothing_fits) {
  TESTSETUP;
  lcec_autoshift_t a;
  int32_t far[] = {0, 249000};

  // frames all over the period: three cycles as well as can be, and no
  // more changes, as nothing would be better
  lcec_autoshift_init(&a, 250000, 125000);
  measure(&a, 250000, 20000, 230000);
  TESTINT(a.state, LCEC_AUTOSHIFT_THREE);
  TESTINT(a.fits, 0);
  TESTINT(feed(&a, 1, far, 2, 100), 0);

  TESTRESULTS;
}

TESTFUNC(test_autoshift_follows_later_frames) {
  TESTSETUP;
  lcec_autoshift_t a;
  int32_t a_bit_later[] = {300000};
  int32_t much_later[] = {560000};
  int32_t early[] = {10000};

  lcec_autoshift_init(&a, 1000000, 500000);
  measure(&a, 1000000, 50000, 80000);
  TESTINT((int)a.shift, 565000);
  // later frames that keep half the margin: no change
  TESTINT(feed(&a, 1, a_bit_later, 1, 10), 0);
  TESTINT((int)a.send_max, 300000);
  TESTINT((int)a.shift, 565000);
  // one too close to SYNC0: a new choice, once
  TESTINT(feed(&a, 1, much_later, 1, 10), 1);
  TESTINT(a.state, LCEC_AUTOSHIFT_TWO);
  // 610 .. 1000 us
  TESTINT((int)a.shift, 805000);
  // an early frame that leaves the inputs too little time
  TESTINT(feed(&a, 1, early, 1, 10), 0);  // 805 <= 1000 + 10 - 50 + 25
  TESTINT((int)a.send_min, 10000);

  TESTRESULTS;
}

TESTFUNC(test_autoshift_input_side) {
  TESTSETUP;
  lcec_autoshift_t a;
  int32_t early[] = {0};

  // frames 400..420 us: 470 .. 1350 us, SYNC0 at 910 us
  lcec_autoshift_init(&a, 1000000, 500000);
  measure(&a, 1000000, 400000, 420000);
  TESTINT((int)a.shift, 910000);
  a.input_margin = 200000;
  // a frame at 0 us leaves the inputs 90 us, they need 200: 910 > 1000 + 0
  // - 200 + 100, a new choice
  TESTINT(feed(&a, 1, early, 1, 1), 1);
  TESTINT(a.state, LCEC_AUTOSHIFT_TWO);
  // 470 .. 800 us
  TESTINT((int)a.shift, 635000);

  TESTRESULTS;
}

TESTFUNC(test_autoshift_two_to_three) {
  TESTSETUP;
  lcec_autoshift_t a;
  int32_t late[] = {160000};

  // 250 us with a slow input: two cycles at 150 .. 200 us
  lcec_autoshift_init(&a, 250000, 125000);
  a.input_margin = 100000;
  measure(&a, 250000, 50000, 100000);
  TESTINT(a.state, LCEC_AUTOSHIFT_TWO);
  TESTINT((int)a.shift, 175000);
  // the frames now spread 50..160 us: two cycles no longer fit, three at
  // -40 .. 0 us
  TESTINT(feed(&a, 1, late, 1, 1), 1);
  TESTINT(a.state, LCEC_AUTOSHIFT_THREE);
  TESTINT(a.fits, 1);
  TESTINT((int)a.shift, 230000);

  TESTRESULTS;
}

TESTFUNC(test_autoshift_keeps_half_the_margins) {
  TESTSETUP;
  lcec_autoshift_t a;
  int32_t later[] = {530000};
  int32_t too_late[] = {545000};
  int32_t early[] = {0};
  int32_t three_late[] = {185000};
  int32_t three_early[] = {25000};

  // two cycles at 565 us; a frame at 530 us leaves 35 us, more than half
  // the margin: kept, so that SYNC0 does not wander with every new frame
  lcec_autoshift_init(&a, 1000000, 500000);
  measure(&a, 1000000, 50000, 80000);
  TESTINT(feed(&a, 1, later, 1, 1), 0);
  TESTINT((int)a.shift, 565000);
  TESTINT(feed(&a, 1, too_late, 1, 1), 1);  // 20 us left

  // the input side: SYNC0 at 910 us, inputs need 100 us; a frame at 0 us
  // leaves them 90 us, more than half: kept
  lcec_autoshift_init(&a, 1000000, 500000);
  measure(&a, 1000000, 400000, 420000);
  a.input_margin = 100000;
  TESTINT(feed(&a, 1, early, 1, 1), 0);
  TESTINT((int)a.shift, 910000);

  // three cycles at -20 us (-40 .. 0 us): frames at 185 and 25 us leave
  // SYNC0 inside the window widened by half the margin
  lcec_autoshift_init(&a, 250000, 125000);
  a.input_margin = 100000;
  measure(&a, 250000, 50000, 160000);
  TESTINT(a.state, LCEC_AUTOSHIFT_THREE);
  TESTINT(feed(&a, 1, three_late, 1, 1), 0);
  TESTINT(feed(&a, 1, three_early, 1, 1), 0);
  TESTINT((int)a.shift, 230000);

  TESTRESULTS;
}

TESTMAIN
