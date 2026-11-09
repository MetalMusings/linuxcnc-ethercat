# Performance of lcec_uspace

Measurements of `lcec_uspace`, the driver built against the userspace
IgH master (see [README](../README.md#userspace-ethercat-master-lcec_uspace)),
compared with `lcec` and the kernel master, and of the system tuning
that matters for it. The tests were made in November 2026 on one
machine, and those of section 1 also on a second; each case was run
once.

## Summary

- **Transport.** With the `xdp-native` transport the userspace master
  has the shortest and most even cycle times. On a 500 µs servo thread
  under load the longest cycle was 63 µs, against 313 µs with the
  default `raw` transport and 664 µs with the kernel master. The long
  cycles of `raw` and the kernel master are in the receive
  (`lcec.read-all`). On a second machine, which had no such long cycles
  with any master, `xdp-native` still had the shortest mean and p99,
  20–60 % below the others.
- **CPU governor and NIC interrupt placement** made no clear
  difference.
- **`isolcpus`** for the CPU that runs the servo thread matters once a
  full LinuxCNC (GUI, motion, a running program) and other load share
  the machine. Without it the longest servo cycle was 511 µs, longer
  than the 500 µs period, and wake-up jitter was larger. With it the
  longest cycle was 218 µs. The mean execution time is about the same
  either way.
- **The `ethercat` tool** can be used while LinuxCNC runs: about 165
  tool calls per second did not lengthen the servo cycle.
- **Load raises execution time about threefold** whatever the tuning:
  the N100's cores share the L2 cache and the memory bus, so a
  memory-heavy load on the other cores slows the realtime core as well.

## Test machine and method

- Intel N100 (4 cores, no SMT), Debian 13, PREEMPT_RT 6.12.69,
  LinuxCNC 2.9.4 (uspace).
- Kernel command line (always): `intel_idle.max_cstate=1
  i915.enable_rc6=0 pcie_aspm.policy=performance pcie_port_pm=off`, plus
  `isolcpus=3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2` where stated.
- EtherCAT on an Intel i226 (`igc`), interrupt coalescing
  (`ethtool -C enp2s0 rx-usecs 0 tx-usecs 0`) and EEE off.
- Bus: three ECT60V202 CiA 402 stepper drives with distributed clocks
  and one I/O slave (EaserCAT 7000). The drives were not enabled.
- LinuxCNC's realtime thread runs on the last CPU, CPU 3.
- HAL's `sampler` recorded every servo cycle:
  - `servo-thread.time`, the execution time of the thread;
  - `lcec.read-all.time` and `lcec.write-all.time`;
  - `lcec.0.pll-err`, the DC synchronization error;
  - `lcec.0.all-op`;
  - in the later tests, the wake-up jitter: `timedelta` first in the
    thread gives the time between invocations, and the jitter is its
    difference from the period.
- Times are converted from TSC clocks (806.4 MHz).
- "Load" is `stress-ng`: CPU, memory (`--vm`), cache, disk I/O and, in
  the LinuxCNC tests, also socket and UDP traffic over loopback.

## 1. Kernel master, `raw` and `xdp-native`

A HAL file with only `lcec.read-all` and `lcec.write-all` in the servo
thread, and the same XML file for all three. The load was
`stress-ng --cpu 4 --vm 2 --vm-bytes 20% --cache 2 --io 1` on all
cores, without `isolcpus`.

**500 µs**, 180 s per run (360 000 cycles). Execution time of the servo
thread, µs:

| | mean | p99 | p99.9 | max | cycles > 100 µs |
|---|---|---|---|---|---|
| kernel master (`lcec`), idle | 7.0 | 9.3 | 16.3 | 177 | 20 |
| userspace, `raw`, idle | 5.8 | 12.5 | 23.4 | 226 | 14 |
| userspace, `xdp-native`, idle | 4.1 | 5.6 | 9.5 | 42 | 0 |
| kernel master (`lcec`), with load | 16.8 | 35.2 | 45.2 | 664 | 65 |
| userspace, `raw`, with load | 15.0 | 32.0 | 41.9 | 313 | 57 |
| userspace, `xdp-native`, with load | 12.4 | 27.2 | 34.1 | 63 | 0 |

**1 ms, idle**, 180 s per run (180 000 cycles). Longest cycle: 265 µs
(kernel), 242 µs (`raw`), 70 µs (`xdp-native`).

- The long cycles are in `lcec.read-all`. Under load the receive took up
  to 625 µs with the kernel master, 297 µs with `raw` and 32 µs with
  `xdp-native`. With the kernel master under load, the longest cycle
  was longer than the 500 µs period.
- The DC synchronization was the same with all three: `pll-err` was
  0.5 µs on average idle and about 2 µs under load, at most 31–37 µs.
- All slaves stayed in OP in every cycle of every run.

### A second machine

The same tests on another machine and bus, to see whether `xdp-native`
is ahead there too:

- Intel Core i3-7100 (2 cores, no SMT), Debian 13, PREEMPT_RT 6.12.74,
  LinuxCNC 2.9.4 (uspace).
- Kernel command line: `acpi_mask_gpe=0x6F i915.enable_dc=0
  irqaffinity=0`. Without the first two, an ACPI GPE interrupt storm and
  the i915 display power states gave cycles of about 300 µs on this
  machine, with any master. No `isolcpus`; the servo thread runs on
  CPU 1.
- EtherCAT on an Intel I210 (`igb`), with the driver's default interrupt
  coalescing. The kernel master uses the `ec_generic` driver.
- Bus: EK1100, EL1008, EL2008, EL5101 and two ECT60V202 drives with
  distributed clocks; the drives were not enabled.
- Times from TSC clocks at 3912 MHz. The same load command as above.

**500 µs**, 180 s per run. Execution time of the servo thread, µs:

| | mean | p99 | p99.9 | max | cycles > 100 µs |
|---|---|---|---|---|---|
| kernel master (`lcec`), idle | 7.2 | 9.2 | 15.0 | 77 | 0 |
| userspace, `raw`, idle | 4.8 | 6.7 | 12.5 | 37 | 0 |
| userspace, `xdp-native`, idle | 2.9 | 4.4 | 10.0 | 33 | 0 |
| kernel master (`lcec`), with load | 13.2 | 26.8 | 32.0 | 52 | 0 |
| userspace, `raw`, with load | 10.4 | 23.3 | 28.8 | 46 | 0 |
| userspace, `xdp-native`, with load | 7.6 | 18.3 | 23.7 | 44 | 0 |

**1 ms, idle**, 180 s per run. Mean, p99.9 and longest cycle: 7.7, 17.1
and 36 µs (kernel); 5.3, 19.5 and 71 µs (`raw`); 3.1, 11.5 and 28 µs
(`xdp-native`).

- `xdp-native` again has the shortest cycles. Its mean is 60 % below
  the kernel master's and 40 % below `raw`'s idle, and 42 % and 27 % below
  under load. The longest receive (`lcec.read-all`) was 6 µs idle and 14 µs
  under load with `xdp-native`, against 12 and 21 µs with `raw` and 17
  and 22 µs with the kernel master.
- This machine had none of the long cycles that the first machine had
  with `raw` and the kernel master: no cycle over 100 µs with any master.
  The difference in the longest cycle is therefore small here.
- `pll-err` was 0.5 µs on average idle and 1.5 µs under load with all
  three masters, at most 15–68 µs.
- All slaves stayed in OP in every cycle of every run.
- `lcec_uspace`'s DC loop only locks when the XML file's
  `appTimePeriod` is the servo thread's period; with 1 ms in the file
  and a 500 µs thread, `pll-err` wandered over the whole period. The
  kernel master's `lcec` locked either way. The file was set to the
  thread's period for all runs.

## 2. CPU governor and NIC interrupts

`xdp-native` at 500 µs, the same HAL file and load as in 1, 180 s per
run, no `isolcpus`. The CPU governor (`scaling_governor`) was
`powersave`, the default, or `performance`, and the NIC interrupts
(`/proc/irq/*/smp_affinity_list`) went to CPUs 0–2, the default, or to
CPU 3, the realtime CPU ("IRQ 3"). Each of the four combinations was run
once idle and once with load. Wake-up jitter is |interval − period|.

| | load | thread mean / p99.9 / max, µs | jitter mean / p99.9 / max, µs |
|---|---|---|---|
| powersave, IRQ 0–2 | idle | 4.3 / 13.3 / 39 | 0.55 / 4.7 / 17 |
| performance, IRQ 0–2 | idle | 4.2 / 10.4 / 23 | 0.53 / 3.8 / 19 |
| performance, IRQ 3 | idle | 4.1 / 9.8 / 287 | 0.53 / 3.7 / 14 |
| powersave, IRQ 3 | idle | 4.1 / 9.0 / 26 | 0.53 / 3.5 / 14 |
| powersave, IRQ 0–2 | load | 12.5 / 34.0 / 71 | 2.81 / 18.1 / 37 |
| performance, IRQ 0–2 | load | 12.3 / 34.5 / 57 | 2.75 / 17.4 / 36 |
| performance, IRQ 3 | load | 12.4 / 34.2 / 54 | 2.36 / 15.6 / 32 |
| powersave, IRQ 3 | load | 12.7 / 35.2 / 100 | 2.28 / 15.8 / 37 |

Neither the governor nor the interrupt placement changed the execution
time. The single long cycles (287 µs with `performance` and IRQ 3 idle,
100 µs with `powersave` and IRQ 3 under load) did not repeat in the
other runs. With the NIC interrupts on CPU 3, the jitter under
load was somewhat lower.

`rtapi_app` cannot pin the NIC interrupts itself, because it accesses
files with the real user id and gets `EACCES` on `/proc/irq`. Pin them
from a start script with `sudo` if wanted.

## 3. `isolcpus`, EtherCAT only

The same as in 2 with the `powersave` governor, after a reboot with
`isolcpus=3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2`, with the NIC
interrupts on CPUs 0–2 or on CPU 3. For comparison, the rows "no
isolcpus" are the `powersave`, IRQ 0–2 runs from 2.

| | load | thread mean / p99.9 / max, µs | jitter mean / p99.9 / max, µs |
|---|---|---|---|
| no isolcpus, IRQ 0–2 | idle | 4.3 / 13.3 / 39 | 0.55 / 4.7 / 17 |
| isolcpus, IRQ 0–2 | idle | 4.3 / 10.6 / 55 | 0.52 / 2.7 / 20 |
| isolcpus, IRQ 3 | idle | 4.2 / 10.5 / 22 | 0.53 / 2.4 / 17 |
| no isolcpus, IRQ 0–2 | load | 12.5 / 34.0 / 71 | 2.81 / 18.1 / 37 |
| isolcpus, IRQ 0–2 | load | 13.3 / 35.8 / 57 | 2.52 / 17.2 / 36 |
| isolcpus, IRQ 3 | load | 12.9 / 33.6 / 59 | 1.57 / 14.4 / 38 |

With only EtherCAT in the realtime thread, `isolcpus` hardly changed
the execution time, and lowered the jitter mostly together with the
NIC interrupts on CPU 3. The load here was not pinned, so with
`isolcpus` it stayed off CPU 3, except for the `--cache` workers, which
set their own CPU affinity.

## 4. `isolcpus` with a full LinuxCNC

The realistic case: the qtplasmac-sim configuration at a 500 µs servo
period, with motion, the plasmac component and `lcec_uspace`
(`xdp-native`, the same bus) in the servo thread. The QtPlasmaC GUI ran
a program over and over: a 10 × 10 grid of circles and lines at
6000 mm/min, without the torch, so that the preview was redrawn all
the time.

The load was 11 `stress-ng` workers:

```
stress-ng --cpu 4 --vm 2 --vm-bytes 15% --cache 2 --cache-no-affinity --io 1 --sock 1 --udp 1
```

The NIC interrupts were on CPUs 0–2, and each run lasted 1 h
(7 200 000 cycles):

- **S1b:** `isolcpus` on CPU 3. LinuxCNC (GUI) and the load on CPUs 0–2;
  CPU 3 had only the realtime thread.
- **S2:** `isolcpus` on CPU 3, with a quarter of the load pinned to
  CPU 3 (the scheduler does not move tasks onto an isolated CPU).
- **S3:** booted without `isolcpus`, nothing pinned. The scheduler put
  about half of the `stress-ng` threads, and at times the GUI, on CPU 3.

| 500 µs, with load | S1b: isolcpus | S2: isolcpus, load on CPU 3 | S3: no isolcpus |
|---|---|---|---|
| servo-thread mean / p99.99 / max, µs | 29.1 / 79 / 218 | 22.3 / 70 / 280 | 25.7 / 85 / **511** |
| servo-thread cycles > 200 / > 300 µs | 1 / 0 | 3 / 0 | 14 / 5 |
| `lcec.write-all` p99 / max, µs | 16 / 183 | 14 / 258 | 26 / 476 |
| `lcec.read-all` max, µs | 46 | 40 | 42 |
| wake-up jitter mean / p99.9 / max, µs | **1.9 / 13.2 / 44** | 2.9 / 18.1 / 60 | 3.1 / 16.9 / 49 |
| cycles with jitter > 10 µs | **22 132** | 132 247 | 132 971 |
| cycles with jitter > 30 / > 50 µs | 82 / 0 | 449 / 9 | 174 / 0 |
| \|`pll-err`\| p99 / max, µs | 6.7 / 173 | 7.2 / 249 | 16.4 / 248 |

- Without `isolcpus` (S3), the longest servo cycle was 511 µs, longer
  than the period, and five cycles were over 300 µs. The long cycles
  were in `lcec.write-all`. LinuxCNC reported no overrun and the slaves
  stayed in OP, but the margin was gone in those cycles, and the DC
  synchronization was twice as noisy at p99.
- With other work on CPU 3 (S2, S3), cycles with more than 10 µs wake-up
  jitter were six times as common as with CPU 3 isolated (S1b).
- The mean execution time did not depend on isolation. The higher mean
  in S1b comes from the load: there all cache workers ran on CPUs 0–2
  all the time, which loads the shared L2 cache more. In an earlier run
  (S1) without `--cache-no-affinity` the cache workers moved between
  CPUs, also onto CPU 3, and the mean was 22.8 µs, with the same jitter
  as S1b.

## 5. The `ethercat` tool while LinuxCNC runs

`lcec_uspace` serves the `ethercat` tool through a socket, from a
thread outside the servo thread (see
[README](../README.md#the-ethercat-tool)). To see whether the tool
disturbs the realtime path, QtPlasmaC ran with a 1 ms servo thread,
`xdp-native` and `isolcpus`, idle, without `stress-ng`. Four runs of
60 s were made in turn: two without the tool, and two with a loop that
ran `slaves -v`, `domains -v`, `data`, `upload`, `sdos`, `reg_read`,
`sii_read` and `master` back to back, about 165 tool calls per second.
`servo-thread.tmax` was reset at the start of each run, and
`lcec.0.pll-err` was sampled with `halcmd` meanwhile.

| | longest servo cycle (µs) | largest \|pll-err\| sampled (µs) |
|---|---|---|
| without the tool | 103, 124 | 26, 30 |
| with the tool | 97, 107 | 33, 36 |

The tool did not lengthen the servo cycle. The sampled DC error was a
little higher with it, but the loop also started about 165 processes per
second on CPUs 0–2, and two runs are too few to tell. No frames were
lost, the working counter stayed complete, and all slaves stayed in OP.

## Recommendations

For `lcec_uspace` on a machine like this:

1. Use `export LCEC_ETHERCAT_TRANSPORT=xdp-native` when the network
   driver supports XDP.
2. Isolate the realtime CPU (the last one):
   `isolcpus=3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2` on a 4-core CPU,
   in `GRUB_CMDLINE_LINUX_DEFAULT` in `/etc/default/grub`, then
   `sudo update-grub` and a reboot.
3. Turn off interrupt coalescing and EEE on the EtherCAT interface.
   Moving its interrupts to the realtime CPU is optional; it lowered
   the jitter a little.

## Limits of these tests

- One run per case, and one machine except for section 1. Single long
  cycles (such as the 287 µs with `performance` and IRQ 3 in 2) may come
  from rare events that a second run would not show.
- The drives were not enabled, so no motor current or real motion.
- `stress-ng` is a harsher load than a typical plasma cutter's PC sees,
  but a GUI with a large preview, a web browser or a file copy loads
  the memory bus in a similar way.
- `servo-thread.time` is the execution time of the thread, and the
  wake-up jitter is measured from inside it with `timedelta`. Neither
  includes the time the network card takes to send.
