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
servo thread time to the distributed clock time.

```xml
 <master idx="0" appTimePeriod="2000000" refClockSyncCycles="1000" syncToRefClock="true">
  ....
 </master>

```

### `syncToRefClock`

This option enables or disables synchronization of LinuxCNC's servo thread
to the DC reference clock. If this option is set to "true", the two clocks
are kept synchronized, "false" disables the feature. If the option is not
specified, a negative `refClockSyncCycles` value enables this feature for
backward compatibility.

### SYNC0 phase relative to the servo thread

lcec anchors the DC SYNC0 grid to the *nominal* start of the servo
thread period. The first application time given to the EtherCAT master
is the thread's nominal start time, and the EtherCAT master places SYNC0 at
`app_start_time + n * sync0Cycle + sync0Shift`. As a result, SYNC0 fires
at a fixed time after the nominal start of each servo thread period:

```
SYNC0 = thread period start + sync0Shift + transmit delay
```

The transmit delay is the time from when lcec samples the
application time in `write` until the frame reaches the reference
slave. It is typically a few tens of microseconds and roughly constant.

The process data frame is sent when lcec's `write` function runs. So
outputs reach the slaves `sync0Shift - (time from thread start to lcec write)`
before SYNC0. Choose `sync0Shift` larger than the worst case time from
thread start to the end of lcec's `write`, plus the slave's own
requirement (object `0x1C32:06`, "Calc and copy time", if the slave
provides it). `sync0Cycle` must be a multiple or a divisor of
`appTimePeriod`.

- With `syncToRefClock="false"` the DC reference clock follows the master
  clock, so the phase is fixed from the start. `pll-err` is only calculated
  for monitoring.
- With `syncToRefClock="true"` the DC reference clock is the time source, and
  the servo thread period is adjusted (+/- `pll-step` per cycle) so that
  its nominal start keeps the phase described above. Large initial phase
  errors are pulled in at `pll-step` per cycle. With the default settings,
  that is at most about 500 cycles.

Synchronization is done with a bang-bang controller. Three hal parameters
and four hal pins are available.

Hal parameters
- `pll-step="<n>" RW`. The adjustment step in nanoseconds. Default 0.1% of appTimePeriod.
- `pll-max-err="<n>" RW`. Max allowed change per cycle of the offset
  between the DC reference clock and the master clock in nanoseconds.
  Larger changes (for example when the master writes the slaves' system
  time offsets during start-up) are counted in `pll-reset-count`, and
  that cycle is not used for control. Default one appTimePeriod.
- `dc-phase-max-err="<n>" RW`. Limit for `dc-phased`. Default 10% of appTimePeriod.

Hal pins
- `pll-err="<n>" OUT`. Phase error in nanoseconds between the nominal
  start of the servo thread and the SYNC0 grid (minus `sync0Shift`), wrapped
  to +/- half a period. Positive means the thread starts too early.
- `pll-out="<n>" OUT`. Current output correction, will always be +/-pll-step
  when `syncToRefClock="true"`, otherwise 0.
- `pll-reset-count="<n>" OUT`. Number of times the DC time has jumped by
  more than `pll-max-err`.
- `dc-phased` OUT. True when `|pll-err| < dc-phase-max-err`.

`pll-err` varies up and down. `pll-reset-count` should only increase during
start-up. If `pll-err` never settles, the difference in speed between
the clocks is large. Increasing `pll-step` might help. `pll-step` is limited
to 1% of appTimePeriod.


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

These shift the Sync0 and Sync1 interrupts by a fixed number of ns.
`sync0Shift` is measured from the nominal start of the servo thread
period. See "SYNC0 phase relative to the servo thread" above. With
`sync0Shift="0"`, SYNC0 fires right at the start of the thread period. Process
data from lcec's `write` then arrives almost a full period before
SYNC0, so it is one period old when the slave uses it.

## Drivers and DC Clocks

Some devices (like RTelligent stepper drives) *only* seem to work in
DC mode.  To make configuring them less complex, the driver in
`lcec_rtec.c` automatically enables DC mode if `<dcConf/>` isn't
provided.  Since the DC parameters are largely just derived from
information in each device's ESI file, this should be able to be done
safely.
