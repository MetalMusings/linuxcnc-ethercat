# Distributed Clocks

EtherCAT has a feature called "[distributed
clocks](https://infosys.beckhoff.com/english.php?content=../content/1033/ethercatsystem/2469118347.html&id=)"
that seems to confuse many people on the LinuxCNC forum.  Distributed
clocks (when enabled) allow devices to stay synchronized to each other
with very small amounts of jitter (typically a few microseconds).
The EtherCAT master needs to be told to synchronize LinuxCNC's
servo thread to the distributed clock time.
This allows LinuxCNC's motion planner to tell EtherCAT servo and
stepper drives where to be *at a specific time* around 1 millisecond,
and have them almost exactly match the plan provided by the software.

## Background

EtherCAT is designed for low-latency, low-jitter control of industrial
equipment.  To accomplish this, it pays very close attention to timing
and delays across the network.  Running `ethercat -v slaves` will show
delay times between individual EtherCAT slaves.  On my system, typical
delays are around 150ns for EL* devices, 550ns for EP* devices, and
750ns for stepper and servo drives.

Individual devices can use a [few different
schemes](https://infosys.beckhoff.com/english.php?content=../content/1033/ethercatsystem/2469122443.html&id=)
for polling and communicating PDOs across the bus.  For devices like
digital inputs, exact timing may not matter, while for servo controls
precise timing is needed to manage speeds and motion with minimal
jitter.  At least some servo and stepper drives will not work without
distributed clocks.  This includes all of the Leadshine and RTelligent
drives that I've tested.

When running in CSP (cyclic synchronous position) mode, the motor
controller expects to receive a new target position every 1 milliscond
*precisely*.  LinuxCNC can then send each axis movement plans for 1ms
in the future and expect all motors to start their next segment
precisely on cue, all in sync with each other.

To close the time chain, the LinuxCNC servo thread must be
synchronized with the distributed clock time. It is inevitable
that the distributed clock and the LinuxCNC real-time clock
run at different speeds. The lcec module can adjust the
servo thread's time to the distributed clock reference time.
Unsynchronized distributed clock and servo thread clock are known to
lead to unpleasant noises from the drives. Users report "gravel noises",
"friction noises", and rapid peaks in torque and acceleration.
This only affects drives that use distributed clock synchronization.

## Master settings

The `syncToRefClock` option controls synchronization of LinuxCNC's
servo thread time to the distributed clock time. `globalShiftTime` sets
when SYNC0 fires within the servo thread period.

```xml
 <master idx="0" appTimePeriod="2000000" refClockSyncCycles="1000" syncToRefClock="true" globalShiftTime="400000">
  ....
 </master>

```

### `syncToRefClock`

This option enables or disables synchronization of LinuxCNC's servo thread
to the DC reference clock. If this option is set to "true", the two clocks
are kept synchronized, "false" disables the feature. If the option is not
specified, a negative `refClockSyncCycles` value enables this feature for
backward compatibility.

### `globalShiftTime`

The master's global shift time in nanoseconds (default 0). It is the time from
the nominal start of the servo thread period to SYNC0, and it applies
to all DC slaves on the master. It must be less than `appTimePeriod`. This
corresponds to the master's part of the "global shift time" in Beckhoff's
EtherCAT documentation, and to the shift of the master's local timer
against the DC Base Time in ETG.1020, 20.2.2. A slave's own `sync0Shift`
(the slave's user defined shift, normally 0) is added on top of it. See
"Shift times in the EtherCAT standards" below.

### SYNC0 phase relative to the servo thread

lcec anchors the DC SYNC0 grid to the *nominal* start of the servo
thread period. The first application time given to the EtherCAT master
is the thread's nominal start time plus `globalShiftTime`. The EtherCAT
master places SYNC0 at `app_start_time + n * sync0Cycle + sync0Shift`.
As a result, SYNC0 fires at a fixed time after the nominal start of each
servo thread period, and that time is the same on every start:

```
SYNC0 = thread period start + globalShiftTime + sync0Shift + transmit delay
```

The transmit delay is the time from when lcec samples the
application time in `write` until the frame reaches the reference
slave. It is typically a few tens of microseconds and roughly constant.

The absolute DC time of SYNC0 (the DC time modulo the cycle time) differs from
start to start. Only its phase relative to the servo thread is fixed.

The process data frame is sent when lcec's `write` function runs. So
outputs reach the slaves `globalShiftTime - (time from thread start to lcec write)`
before SYNC0. `sync0Cycle` must be a multiple or a divisor of
`appTimePeriod`.

To choose `globalShiftTime`, run the machine under realistic load (with a GUI,
during program execution, etc.) and read `send-offset-max`. A starting point is:

```
globalShiftTime = send-offset-max + transmit delay (~20-50 us) + slave margin (0x1C32:06, or ~50 us)
```

Round it up generously, or let lcec choose it (`globalShiftTime="auto"`,
below). With a 1 ms period, 250-400 us is
a reasonable range on a typical PREEMPT_RT PC. A larger value is safer but
delays the outputs: they take effect `globalShiftTime` after the start of the
servo thread period. With `globalShiftTime="0"`, SYNC0 fires at the start
of the thread period. The outputs written in that period are then used one period later.

- With `syncToRefClock="false"` the DC reference clock follows the master
  clock, so the phase is fixed from the start. `pll-err` is only calculated
  for monitoring.
- With `syncToRefClock="true"` the DC reference clock is the time source, and
  the servo thread period is adjusted (+/- `pll-step` per cycle) so that
  its nominal start keeps the phase described above. Large initial phase
  errors are pulled in at `pll-step` per cycle. With the default settings,
  that is at most about 500 cycles.

### `globalShiftTime="auto"`

The turn-around time is the time from an output written in one servo
cycle until the slave's answer to it is read back. It is two cycles when
SYNC0 falls after the cycle's frame has reached the slaves, and three
when SYNC0 falls before it, as the outputs then wait for the next
SYNC0. When the frame is sent close to SYNC0, the count changes from
cycle to cycle with the servo thread's latency, and that, not the delay
itself, upsets drives and control loops. On one test machine (I210 NIC,
1 ms), `globalShiftTime="0"` gave two cycles about 40% of the time and
three the rest; 100 us or more gave two cycles every time.

With `globalShiftTime="auto"` (and `syncToRefClock="true"`), lcec
places SYNC0 itself:

1. It starts with SYNC0 in the middle of the period.
2. When all slaves are in OP and `dc-phased` is true, it measures
   `send-offset` for 2 seconds.
3. It chooses where the turn-around is always two cycles: after every
   frame by at least `auto-shift-margin`, and at least
   `auto-shift-input-margin` before the next frame, so that the slaves'
   inputs are ready:

   ```
   send-offset max + margin  <=  shift  <=  period + send-offset min - input margin
   ```

   If that window is empty, it chooses where the turn-around is always
   three cycles, before every frame by at least `auto-shift-margin`:

   ```
   send-offset max + margin - period  <=  shift  <=  send-offset min - margin
   ```

   It takes the middle of the window, the farthest from both edges. The
   shift is the position within the period (modulo the period).
4. It keeps watching `send-offset`. When a frame comes later or earlier
   than any before, so that SYNC0 has less than half the margins left, it
   chooses again from all it has seen.

A new shift moves the servo thread against SYNC0 through the PLL,
`pll-step` per cycle, as at start-up: 100 us takes 100 cycles with the
default step. SYNC0 itself does not move in the slaves' time; only the
time at which the frames arrive does. The windows above use only the
send times, since the transmit delay is in both the frame and SYNC0.

If neither window fits (the send time varies by nearly a whole period),
lcec still takes the middle of the three-cycle window and logs an error.
Look at the servo thread's latency then.

The slaves' own `sync0Shift` is added on top, as for a fixed
`globalShiftTime`; keep it 0 with `auto`.

Pins and parameters for `auto`:

- `shift-time` OUT. The `globalShiftTime` in use, in ns.
- `turnaround-cycles` OUT. 2 or 3 once chosen; 0 while waiting for the
  bus or measuring.
- `auto-shift-margin` RW. Time kept between a frame and SYNC0, in ns.
  Default 50000. It covers the time the slaves need for the outputs
  before SYNC0 (0x1C32:06, calc and copy time), the frame's time along
  the bus, and the jitter of the transmit delay.
- `auto-shift-input-margin` RW. Time from SYNC0 until the slaves' inputs
  are ready for the next frame, in ns. Default 50000 (0x1C33:06, calc
  and copy time of the inputs).

Each choice is logged with the measured `send-offset` range, the shift
and the turn-around.

### PLL

Synchronization is done with a bang-bang controller. Four hal parameters
and five hal pins are available.

Hal parameters
- `pll-step="<n>" RW`. The adjustment step in nanoseconds. Default 0.1% of appTimePeriod.
- `pll-max-err="<n>" RW`. Max allowed change per cycle of the offset
  between the DC reference clock and the master clock in nanoseconds.
  Larger changes (for example when the master writes the slaves' system
  time offsets during start-up) are counted in `pll-reset-count`, and
  that cycle is not used for control. Default one appTimePeriod.
- `dc-phase-max-err="<n>" RW`. Limit for `dc-phased`. Default 10% of appTimePeriod.
- `send-offset-max="<n>" RW`. Largest `send-offset` seen while cyclic
  communication is running. Set to 0 to reset.

Hal pins
- `pll-err="<n>" OUT`. Phase error in nanoseconds between the nominal
  start of the servo thread and the SYNC0 grid (minus `globalShiftTime` and `sync0Shift`), wrapped
  to +/- half a period. Positive means the thread starts too early.
- `pll-out="<n>" OUT`. Current output correction, will always be +/-pll-step
  when `syncToRefClock="true"`, otherwise 0.
- `pll-reset-count="<n>" OUT`. Number of times the DC time has jumped by
  more than `pll-max-err`.
- `dc-phased` OUT. True when `|pll-err| < dc-phase-max-err`.
- `send-offset="<n>" OUT`. Time in nanoseconds from the nominal start of the
  servo thread period to when lcec samples the application time, just
  before sending the frame.

`pll-err` varies up and down. `pll-reset-count` should only increase during
start-up. If `pll-err` never settles, the difference in speed between
the clocks is large. Increasing `pll-step` might help. `pll-step` is limited
to 1% of appTimePeriod.


### DC sync check

When a DC slave is configured, before SYNC0 is started, the EtherCAT
master waits until the slave's clock is within 10 us of the reference
clock, for at most 5 seconds. If it is not, the master logs "Slave did
not sync after 5000 ms" and starts SYNC0 anyway, on a clock that is
still off. Drives then often leave OP with AL status 0x001A
"Synchronization error", or run with SYNC0 at the wrong time until
their clock has been pulled in.

lcec shows this in HAL (with the userspace master, `lcec_uspace`; with
the kernel master the pins stay false):

- `lcec.<master>.<slave>.slave-dc-sync-failed` OUT: this slave's last
  DC sync check timed out. Cleared when a later configuration passes it.
- `lcec.<master>.dc-sync-failed` OUT: any slave of this master.
- `lcec.dc-sync-failed` OUT: any slave of any master.

For example, keep the machine from being switched on:

```
net dc-failed lcec.dc-sync-failed => not.0.in
net dc-ok     not.0.out => and2.0.in1    # in the machine-on / E-stop chain
```

With the userspace master in package 0.5 or later, the check passes
within a fraction of a second; a timeout points to a real problem, such
as a cable, the reference clock, or a slave whose clock is not stable.

## Slave settings

Each slave in LCEC has its own (optional) distributed clock config,
which looks like this:

```xml
  <slave idx="27" type="generic" vid="00000a88" pid="0a880002" configPdos="true" name="rt-ect60">
    <dcConf assignActivate="300" sync0Cycle="*1" sync0Shift="0"/>
    ....
  </slave>

```

The `dcConf` XML tag controls distributed clock configs for this one
slave.  It takes 5 parameters, 3 of which are used in this example:

### `assignActivate`

`assignActivate` control which DC mode the device runs in, and should
generally be considered a device-specific setting.  For new devices,
look in the manufacturer's ESI file for `AssignActivate` and copy the
value from there.

Common values are `300` and `700`.  This is a 2 byte value, and the
higher-order byte (the `3` or `7` in these cases).

The low byte is written into `0x0980` and the high byte is written
into `0x0981`.  The best documentation for these that I've found so
far is
https://www.sanyodenki.com/global/america/file/SANMOTION_R_3E_EtherCAT_Servo_M0011697C.pdf

It says that `0x0981` has 3 defined bits:

0: Active cycle operation (when 1, DC is in use?)
1: SYNC0 is active
2: SYNC1 is active.

So, `assignActivate` values of `0x3xx` only enable SYNC0, while
`0x7xx` enable SYNC0 and SYNC1.

The low order byte (`0x0980`) also has 3 defined bits:

0: SYNC out unit control.  0: master, 1: slave.  Should be 0 for
EtherCAT?
4: Latch in Unit0 (0: master-controlled, 1: slave-controlled)
5: Latch in Unit1 (0: master-controlled, 1: slave-controlled).

The only values that I've seen used for `assignActivate` so far are 0
(no DC), 0x300, 0x320, 0x700, and 0x720.  Presumably 0x330 and 0x730
exist, and maybe 0x310 and 0x710.  Those are the only defined values
for this version of the spec.

### `sync0Cycle` and `sync1Cycle`

The EtherCAT distributed clock system has 2 different timing signals
available, `sync0` and `sync1`.  This controls the cycle time for each
signal, in units of 1ns (although EtherCAT itself may round this to
the nearest 10ns).  As a shortcut, LCEC will let you say `"*1"`
to set this to the current cycle time, or `"*X"`, where X is an
integer, to set this to an integer multiple of the current cycle time. 

I'm not sure if anything other than `sync0Cycle="*1"` ever makes sense
if we're using DC, but it's not unusual to have `sync1Cycle` unset,
set to `*1`, or set to a small multiple like `*3` to have Sync1 run
every 3rd cycle.  Exactly what this means depends on the hardware.

### `sync0Shift` and `sync1Shift`

`sync0Shift` shifts the SYNC0 signal of this one slave by a fixed number
of ns. It is the slave's own (user defined) shift and is added to the
master's `globalShiftTime`. See "SYNC0 phase relative to the servo
thread" above. It is normally 0. Use it only when a slave needs SYNC0
earlier (negative) or later (positive) than the other slaves, for example to
latch inputs before the frame arrives. Set the time the frame needs to
reach the slaves with `globalShiftTime`, not with `sync0Shift`.

Both values must be whole numbers of ns. lcec stops with an error on
anything else. At start-up lcec logs (at INFO level) where SYNC0 fires for
each slave with SYNC0 activated:

```
SYNC0 at globalShiftTime + sync0Shift = 300000 + 0 = 300000 ns after the servo period start
```

It warns if `globalShiftTime + sync0Shift` is negative or not less than
`sync0Cycle`. SYNC0 then still works, but falls in another cycle than
intended. This usually means that the same shift has been given in both
`globalShiftTime` and `sync0Shift`.

`sync1Shift` is passed to the EtherCAT master, but the IgH master used
here only stores it. It writes `sync1Cycle` unchanged to register
0x09A4 and does not use `sync1Shift` anywhere.

### Shift times in the EtherCAT standards

The EtherCAT documents use the word "shift" for three different things.
Only the first two are configured with `globalShiftTime` and `sync0Shift`.

| What | Standard | In lcec |
|------|----------|---------|
| The master's shift: application time, frame time, frame delay and user shift, the same for all slaves | ETG.1020, 20.2.2 "DC Mode, Master follows Reference Clock" | `globalShiftTime` |
| A slave's SYNC0 shift: offset of the SYNC0 start time (register 0x0990) relative to the global DC reference | ETG.2000, `Dc:OpMode:ShiftTimeSync0` (Table 48, attributes in Table 50) | `sync0Shift` |
| The slave's internal Shift Time: delay from SYNC0 to Outputs Valid or Input Latch, done by the slave's firmware | ETG.1020, 20.1.1.2.7 "Shift Time" and 20.1.1.5.1.2 "Shift of Outputs Valid and/or Input Latch", objects 0x1C32:03 and 0x1C33:03 | not set by lcec |

ETG.1020 20.2.2 says that "the Sync0 event of each slave may be shifted
to the DC Base Time by writing the Start Time of the cyclic Operation
(Register 0x990-0x993)". This is what `sync0Shift` does. `sync0Shift`
is **not** written to 0x1C32:03 or 0x1C33:03. If a slave supports a
writable Shift Time (see 0x1C32:04 bits 5-6), set it with `<sdoConfig>`
like any other SDO.

lcec does not read ESI files when it runs. The following parts of
`Dc:OpMode` in ETG.2000 (Table 48 and Table 50) are therefore not used,
and the values must be entered in `<dcConf>` by hand:

- `CycleTimeSync0`, `ShiftTimeSync0` and their `Factor` attributes.
- `ShiftTimeSync0@Input` (and `ShiftTimeSync1@Input`) is **not supported**.
  With `Input="1"` a configuration tool such as TwinCAT ("Based on input
  reference") moves SYNC0 of an input slave to shortly before the frame
  that collects the inputs, so that the slave has measured and copied its
  inputs to SyncManager 3 when the frame passes. lcec places SYNC0 of all
  slaves at `globalShiftTime + sync0Shift`. To latch the inputs of a slave
  earlier, give it a negative `sync0Shift`.
- `OutputDelayTime` and `InputDelayTime` (0x1C32:09 and 0x1C33:09).
  lcec does not use them to align the slaves' Outputs Valid as described in
  ETG.1020 20.2.2, Table 83.

References: ETG.1020 Protocol Enhancements, V1.2.0, chapter 20
"Synchronization". ETG.2000 EtherCAT Slave Information Specification,
V1.0.6, `DeviceType:Dc`. Both are available from
<https://www.ethercat.org/>.

## Drivers and DC Clocks

Some devices (like RTelligent stepper drives) *only* seem to work in
DC mode.  To make configuring them less complex, the driver in
`lcec_rtec.c` automatically enables DC mode if `<dcConf/>` isn't
provided.  Since the DC parameters are largely just derived from
information in each device's ESI file, this should be able to be done
safely.
