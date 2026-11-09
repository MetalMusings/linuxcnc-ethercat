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
/// @brief Automatic globalShiftTime (globalShiftTime="auto").
///
/// The turn-around time, from an output written in one servo cycle to the
/// slave's answer read back, is two cycles when SYNC0 falls after the
/// cycle's frame has reached the slaves, and three when SYNC0 falls before
/// it (the outputs then wait for the next SYNC0). When the frame is sent
/// close to SYNC0, the count varies from cycle to cycle, and that is what
/// upsets drives and loops.
///
/// SYNC0 sits globalShiftTime after the nominal start of the servo thread
/// period, and the frame is sent send-offset after it (both plus the same
/// transmit delay). This code watches send-offset and places SYNC0 where
/// the turn-around is always two cycles:
///
///   send_max + margin <= shift <= period + send_min - input_margin
///
/// (after every frame, and early enough that the slaves' inputs are ready
/// before the next frame passes). When that window is empty it places
/// SYNC0 where it is always three cycles, before every frame:
///
///   send_max + margin - period <= shift <= send_min - margin
///
/// In both it takes the middle of the window, as far as possible from
/// both edges. It measures for a while after the bus is up, then keeps
/// watching: a send-offset outside what was measured that leaves less
/// than half the margins makes it choose again.
///
/// This file has no HAL or RTAPI dependencies, so that it can be unit
/// tested.

#ifndef _LCEC_AUTOSHIFT_H_
#define _LCEC_AUTOSHIFT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCEC_AUTOSHIFT_MARGIN_DEFAULT 50000         ///< ns between a frame and SYNC0
#define LCEC_AUTOSHIFT_INPUT_MARGIN_DEFAULT 50000   ///< ns from SYNC0 until the inputs are ready
#define LCEC_AUTOSHIFT_MEASURE_NS 2000000000LL      ///< time to measure before the first choice

/// States, also the value of the turnaround-cycles pin.
enum {
  LCEC_AUTOSHIFT_WAIT = 0,  ///< waiting for the bus (all slaves in OP, DC phased)
  LCEC_AUTOSHIFT_MEASURE = 1,
  LCEC_AUTOSHIFT_TWO = 2,    ///< always two cycles
  LCEC_AUTOSHIFT_THREE = 3,  ///< always three cycles
};

typedef struct {
  int64_t period;      ///< servo thread period (ns)
  int64_t margin;      ///< ns kept between a frame and SYNC0
  int64_t input_margin;///< ns kept between SYNC0 and the next frame (two cycles only)
  int64_t send_min;    ///< smallest send-offset seen (ns)
  int64_t send_max;    ///< largest send-offset seen (ns)
  long long measured;  ///< ns measured so far
  int state;           ///< LCEC_AUTOSHIFT_*
  int fits;            ///< the chosen window was not empty
  int64_t pos;         ///< SYNC0 from the nominal thread start, may be outside 0..period
  uint32_t shift;      ///< pos as a globalShiftTime, 0 <= shift < period
} lcec_autoshift_t;

/// Start: shift is the globalShiftTime to use until the first choice.
void lcec_autoshift_init(lcec_autoshift_t *a, uint32_t period, uint32_t shift);

/// One servo cycle. ready: all slaves in OP and the DC phase settled.
/// Returns 1 when a new shift was chosen (a->shift, a->state).
int lcec_autoshift_update(lcec_autoshift_t *a, int ready, int32_t send_offset);

#ifdef __cplusplus
}
#endif

#endif
