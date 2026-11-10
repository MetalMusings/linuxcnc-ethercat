# Trying the userspace EtherCAT master (lcec_uspace)

**Frozen (2026-11-10).** `lcec_uspace` gets no further changes. Its
successor is [ecat](https://github.com/MetalMusings/ecat), a HAL component
and a setup program built on the same userspace master, which imports
lcec XML files. The userspace master itself is still developed, in
[MetalMusings/ethercat](https://github.com/MetalMusings/ethercat), and its
package is built there from 1.1 on, without `lcec_uspace`. To run
`lcec_uspace`, use the `uspace-v1.0` (or older) package from this
repository's releases; installing 1.1 over it removes `lcec_uspace`.

`lcec_uspace` runs the EtherCAT master inside LinuxCNC, without a kernel
module. It is experimental: try it with the machine unable to do harm.
More about it in [README.md](README.md#userspace-ethercat-master-lcec_uspace).

Requirements: LinuxCNC 2.9 (uspace) on Debian 12 or 13, preferably with a
PREEMPT_RT kernel.

## 1. Install

```
sudo apt install ./linuxcnc-ethercat-uspace_*.deb
```

Everything is installed in `/opt/linuxcnc-ethercat-uspace`, plus two
links: `/usr/bin/ethercat-uspace` and
`/usr/lib/linuxcnc/modules/lcec_uspace.so`. An installed kernel master and
`lcec` are left as they are.

## 2. Stop the kernel master, if installed

The two masters cannot use the network interface at the same time:

```
sudo systemctl stop ethercat
```

To start it again later: `sudo systemctl start ethercat`.

## 3. Check that the slaves are found

Replace `enp2s0` with your EtherCAT interface (see `ip link`):

```
sudo ethercat-uspace start -- -i enp2s0
ethercat-uspace slaves
sudo ethercat-uspace stop
```

## 4. Change a copy of your machine configuration

Copy a working `lcec` configuration, so that the original still works
with the kernel master. In the copy:

XML file, on the `<master>` line, add `interface` and `transport`:

```xml
<master idx="0" interface="enp2s0" transport="raw" appTimePeriod="1000000" refClockSyncCycles="-1000">
```

`raw` works with any network card. `xdp-native` gives shorter and more
even cycle times, but needs a driver with XDP support, such as `igc`
(Intel i225/i226).

HAL file, replace the two lines that load `lcec_conf` and `lcec`:

```
loadusr -W /opt/linuxcnc-ethercat-uspace/bin/lcec_conf ethercat-conf.xml
loadrt lcec_uspace
```

Nothing else changes: pins and functions keep their `lcec.` names.

When LinuxCNC stops, `lcec_uspace` stops the bus in order by itself:
every output becomes zero and the slaves go to PREOP while process data
still flows. Without that, the slaves would lose their process data in
OP and stop with an error (sync manager watchdog 0x001B, synchronization
error 0x001A), and CiA 402 drives would latch a communication fault (such
as 0x7500) that must be reset before they run again. A stop guard, a
realtime task beside the servo thread, sends the process data when
`lcec.write-all` is late, and takes the slaves down after 10 cycles
without the servo thread; `lcec.0.guard-state` and `lcec.0.guard-cycles`
show it. `lcec.take-down`, set by `lcec-take-down`, does the same on
request. Nothing is needed in `[HAL]SHUTDOWN`. More in
[README.md](README.md#when-linuxcnc-stops).

## 5. Run

Start LinuxCNC with the copy. The `ethercat-uspace` tool works while it
runs, for example `ethercat-uspace slaves`.

To go back, start the original configuration after
`sudo systemctl start ethercat`.

## Uninstall

```
sudo apt remove linuxcnc-ethercat-uspace
```
