//
//    Copyright (C) 2026 MetalMusings
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

/// @file
/// @brief Automatic globalShiftTime, see lcec_autoshift.h.

#include "lcec_autoshift.h"

void lcec_autoshift_init(lcec_autoshift_t *a, uint32_t period, uint32_t shift) {
  a->period = period;
  a->margin = LCEC_AUTOSHIFT_MARGIN_DEFAULT;
  a->input_margin = LCEC_AUTOSHIFT_INPUT_MARGIN_DEFAULT;
  a->send_min = 0;
  a->send_max = 0;
  a->measured = 0;
  a->state = LCEC_AUTOSHIFT_WAIT;
  a->fits = 0;
  a->pos = shift;
  a->shift = shift;
}

/// The window for two cycles: SYNC0 after every frame, and early enough
/// for the inputs before the next one.
static void window_two(const lcec_autoshift_t *a, int64_t *lo, int64_t *hi) {
  *lo = a->send_max + a->margin;
  *hi = a->period + a->send_min - a->input_margin;
}

/// The window for three cycles: SYNC0 before every frame, and every frame
/// before the next SYNC0.
static void window_three(const lcec_autoshift_t *a, int64_t *lo, int64_t *hi) {
  *lo = a->send_max + a->margin - a->period;
  *hi = a->send_min - a->margin;
}

static void choose(lcec_autoshift_t *a) {
  int64_t lo, hi, s;

  window_two(a, &lo, &hi);
  if (lo <= hi) {
    a->state = LCEC_AUTOSHIFT_TWO;
    a->fits = 1;
  } else {
    window_three(a, &lo, &hi);
    a->state = LCEC_AUTOSHIFT_THREE;
    // when even this is empty, the frames spread over (nearly) the whole
    // period: the middle is still the best that can be done
    a->fits = lo <= hi;
  }
  a->pos = lo + (hi - lo) / 2;
  s = a->pos % a->period;
  if (s < 0) s += a->period;
  a->shift = (uint32_t)s;
}

/// Whether the chosen position still keeps half the margins to the frames
/// seen so far.
static int still_good(const lcec_autoshift_t *a) {
  int64_t lo, hi;

  if (!a->fits) return 1;  // nothing better to choose until the spread shrinks
  if (a->state == LCEC_AUTOSHIFT_TWO) {
    window_two(a, &lo, &hi);
    lo -= a->margin / 2;
    hi += a->input_margin / 2;
  } else {
    window_three(a, &lo, &hi);
    lo -= a->margin / 2;
    hi += a->margin / 2;
  }
  return a->pos >= lo && a->pos <= hi;
}

int lcec_autoshift_update(lcec_autoshift_t *a, int ready, int32_t send_offset) {
  int64_t s = send_offset;

  switch (a->state) {
    case LCEC_AUTOSHIFT_WAIT:
      if (ready) {
        a->state = LCEC_AUTOSHIFT_MEASURE;
        a->send_min = s;
        a->send_max = s;
        a->measured = a->period;
      }
      return 0;
    case LCEC_AUTOSHIFT_MEASURE:
      if (s < a->send_min) a->send_min = s;
      if (s > a->send_max) a->send_max = s;
      a->measured += a->period;
      if (a->measured < LCEC_AUTOSHIFT_MEASURE_NS) return 0;
      choose(a);
      return 1;
    default:
      if (s >= a->send_min && s <= a->send_max) return 0;
      if (s < a->send_min) a->send_min = s;
      if (s > a->send_max) a->send_max = s;
      if (still_good(a)) return 0;
      choose(a);
      return 1;
  }
}
