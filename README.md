# linuxcnc-ethercat

This is a set of [LinuxCNC](https://linuxcnc.org/) drivers for
EtherCAT devices, intended to be used to help build a [CNC
machine](https://en.wikipedia.org/wiki/Numerical_control).

[EtherCAT](https://en.wikipedia.org/wiki/EtherCAT) is a standard for
connecting industrial control equipment to PCs using Ethernet.
EtherCAT uses dedicated Ethernet networks and achieves consistently
low latency without requiring special hardware.  A number of
manufacturers produce EtherCAT equipment for driving servos, steppers,
digital I/O, and reading sensors.

This tree was forked from
[sittner/linuxcnc-ethercat](https://github.com/sittner/linuxcnc-ethercat)
in 2023, and is the new home for most LinuxCNC EtherCAT development.

> **Development has moved to [ecat](https://github.com/MetalMusings/ecat)
> (2026-11-10).** In this fork (MetalMusings/linuxcnc-ethercat), the
> userspace work continues in ecat: a HAL component and a setup program
> on the same userspace master, without XML files (lcec XML files can be
> imported). `lcec_uspace` here is frozen. The userspace master
> ([MetalMusings/ethercat](https://github.com/MetalMusings/ethercat)) is
> still developed, and its package `linuxcnc-ethercat-uspace` is built
> there from 1.1 on, without `lcec_uspace`. The `uspace-v*` releases here
> (up to 1.0) are the last with `lcec_uspace`.

This version lets you choose between two EtherCAT masters: IgH's
kernel-mode master, loaded as `lcec` as before, or a userspace master
that runs inside LinuxCNC without a kernel module, loaded as
`lcec_uspace`. You can install either or both, and each machine
configuration chooses which one to use. See [Userspace EtherCAT master
(lcec_uspace)](#userspace-ethercat-master-lcec_uspace). In
[performance tests](#performance) on a 500 µs servo thread, the
userspace master with the `xdp-native` transport had the shortest and
most even cycle times of the three ways of running: its longest cycle
under load was 63 µs, against 313 µs with the userspace master's
default `raw` transport and 664 µs with the kernel master.

## Installing

The recommended way to install this driver is to use the `.deb` apt
repository managed by the Etherlab folks.  It should contain
everything that you need to install Ethercat support for LinuxCNC with
minimal manual work.

These instructions install the kernel-mode master and `lcec`. The
userspace master and `lcec_uspace` are not packaged; to use them
instead, or in addition, build them as described in
[Building](#building) under [Userspace EtherCAT master
(lcec_uspace)](#userspace-ethercat-master-lcec_uspace). To try the
userspace master from a package, see [INSTALL-USPACE.md](INSTALL-USPACE.md).

### Initial setup

First, you need to tell `apt` how to find the Etherlab repository,
hosted at https://build.opensuse.org/project/show/science:EtherLab.  This
is the preferred mechanism from the [LinuxCNC
forum](https://forum.linuxcnc.org/ethercat/45336-ethercat-installation-from-repositories-how-to-step-by-step):


```
sudo mkdir -p /usr/local/share/keyrings/
wget -O- https://build.opensuse.org/projects/science:EtherLab/signing_keys/download?kind=gpg | gpg --dearmor | sudo dd of=/etc/apt/trusted.gpg.d/science_EtherLab.gpg
sudo tee -a /etc/apt/sources.list.d/ighvh.sources > /dev/null <<EOT
Types: deb
Signed-By: /etc/apt/trusted.gpg.d/science_EtherLab.gpg
Suites: ./
URIs: http://download.opensuse.org/repositories/science:/EtherLab/Debian_12/
EOT
sudo apt update
sudo apt install -y linux-headers-$(uname -r) ethercat-master linuxcnc-ethercat
```

**Note:** If using the official linuxcnc 2.9.x ISO, The Ethercat repository is already installed so only the last two lines above are required.

(These directions are for Debian 12.  Debian 11 should be very similar,
just change `Debian_12` to `Debian_11`.)

You will then need to do a bit of setup for Ethercat; at a minimum
you'll need to edit `/etc/ethercat.conf` to tell it which interface it
should use.  See the forum link (above) for additional details.

You can verify that Ethercat is working when `ethercat slaves` shows
the devices attached to your system.  See the forum link above for
additional helpful steps.

### Updates

Ongoing updates should be easy and *mostly* handled automatically by
`apt`.  Just run `sudo apt update` followed by `sudo apt upgrade` and
things will mostly work, with one possible exception.  If the kernel
is upgraded, then you *may* need to re-run this command in order to
get Ethercat working again:

```
sudo apt install -y linux-headers-$(uname -r)
```

This is because the real-time kernel that LinuxCNC prefers doesn't get
its headers installed by default, and this breaks compiling the
Ethercat modules for the new kernel.  Just run this `apt` command and
then either reboot or run `sudo systemctl start ethercat`.

### Manual Installation

If you decide that you want to install this manually and not use a
package manager, then first you'll need to make sure that you have
LinuxCNC (with its development tools) and at least one EtherCAT master
installed: the kernel-mode [Ethercat
Master](https://gitlab.com/etherlab.org/ethercat), the userspace master
(see [Userspace EtherCAT master
(lcec_uspace)](#userspace-ethercat-master-lcec_uspace) below), or both.
Then download linuxcnc-ethercat and run `make install`. It builds
`lcec` for the kernel-mode master and `lcec_uspace` for the userspace
master, for whichever of them is installed.


## Userspace EtherCAT master (lcec_uspace)

**Frozen (2026-11-10).** `lcec_uspace` gets no further changes. Its
successor is [ecat](https://github.com/MetalMusings/ecat), a HAL component
and a setup program built on the same userspace master, which imports
lcec XML files. The userspace master itself is still developed, in
[MetalMusings/ethercat](https://github.com/MetalMusings/ethercat), and its
package is built there from 1.1 on, without `lcec_uspace`. To run
`lcec_uspace`, use the `uspace-v1.0` (or older) package from this
repository's releases; installing 1.1 over it removes `lcec_uspace`.

`lcec_uspace` is this driver built against a userspace build of the
IgH EtherCAT master,
[stratuMAK/ethercat](https://github.com/stratuMAK/ethercat), a fork of
IgH 1.6.8. There the whole master (state machines, CoE, distributed
clocks, domains) runs as a library inside LinuxCNC's `rtapi_app`:

- no kernel module, so nothing to rebuild with DKMS when the kernel is
  upgraded, and no `ethercat` service or `/etc/ethercat.conf`;
- the same driver code, device drivers, XML files and HAL pins as
  `lcec`.

It is **experimental**. stratuMAK's userspace master is new and has not
been through a realtime qualification on hardware, and `lcec_uspace`
has so far been tested on one machine: LinuxCNC 2.9.4 on a PREEMPT_RT
6.12 kernel, an Intel i226 network card, three CiA 402 stepper drives
with distributed clocks and one I/O slave. Try it with the machine
unable to do harm, and if you can, keep the kernel master installed as
well, to switch back to.

Which master is used is chosen per machine configuration, by loading
`lcec` or `lcec_uspace` in its HAL file. With both masters installed,
`lcec.so` and `lcec_uspace.so` are built and installed side by side,
and you can switch between them (see [Running and switching between the
masters](#running-and-switching-between-the-masters)). Neither master is
required: with only one installed, only its module is built (see
[Building](#building)).

### Building

These steps install the userspace master and `lcec_uspace` on a
machine that already runs LinuxCNC 2.9 (uspace, preferably on a
PREEMPT_RT kernel). They were tested on Debian 13 with LinuxCNC 2.9.4.
The kernel master can stay installed; see [Running and switching
between the masters](#running-and-switching-between-the-masters).

1. Install the build tools and libraries:

   ```
   sudo apt install build-essential git autoconf automake libtool pkg-config \
       libxdp-dev libbpf-dev libexpat1-dev golang-go linuxcnc-uspace-dev ethtool
   ```

   `linuxcnc-uspace-dev` provides `halcompile` and LinuxCNC's headers
   (with a LinuxCNC built from source, use that build instead).
   `libxdp-dev` is needed for the XDP transports, which have the lowest
   latency (see [Performance](#performance)); without it only the raw
   socket transport is built.

2. Build and install the userspace master in a prefix of its own, so
   that its `ecrt.h` does not replace the kernel master's
   `/usr/include/ecrt.h` if that is installed too:

   ```
   git clone -b uspace-tool-ipc-fixes https://github.com/MetalMusings/ethercat ethercat-uspace
   cd ethercat-uspace
   ./bootstrap
   ./configure --enable-uspace-master --prefix=/opt/ethercat-uspace
   make
   make check          # bus simulator tests, no hardware needed
   sudo make install
   ```

   This is [stratuMAK/ethercat](https://github.com/stratuMAK/ethercat)
   with changes that are not yet in stratuMAK's version: what the
   `ethercat` tool needs (see [The ethercat tool](#the-ethercat-tool)),
   corrected DC system time offsets and a reported failed DC sync check,
   and `ecrt_master_request_al_state()` for an orderly stop (see [When
   LinuxCNC stops](#when-linuxcnc-stops)). With stratuMAK's version
   `lcec_uspace` builds and runs, but `ethercat domains` and `ethercat
   data` fail, the tool has no `start` or `stop`, and the slaves stop
   with an error when LinuxCNC stops.

   Check that the XDP transports were built:

   ```
   $ /opt/ethercat-uspace/bin/ec_master -h 2>&1 | grep transports
   Available transports: raw xdp-skb xdp-native ccat sim
   ```

   If `xdp-skb` and `xdp-native` are missing, `libxdp-dev` was not
   installed when `configure` ran. Install it, then run `./configure`
   again and `make clean` before `make`: the new compiler flag does not
   make `make` rebuild the files already compiled.

3. Make the userspace tool easy to reach. `/usr/bin/ethercat` is the
   kernel master's tool (if installed), so give the userspace one its
   own name. `/usr/local/bin` is also on `sudo`'s path:

   ```
   sudo ln -s /opt/ethercat-uspace/bin/ethercat /usr/local/bin/ethercat-uspace
   ```

   On a machine without the kernel master, the link can be called
   `/usr/local/bin/ethercat` instead; the commands below are then
   `ethercat ...`. The daemon `ec_master` needs no link: the tool starts
   it from `/opt/ethercat-uspace/bin`.

4. Tell `ethercat-uspace start` which network interface the EtherCAT bus is
   on (find it with `ip link`), and which transport to use (see
   [Configuring](#configuring)):

   ```
   sudo mkdir -p /opt/ethercat-uspace/etc
   sudo tee /opt/ethercat-uspace/etc/ethercat-uspace.conf <<EOF
   INTERFACE=enp2s0
   TRANSPORT=xdp-native
   EOF
   ```

   This file is only for `ethercat-uspace start`; LinuxCNC takes the
   interface from the XML file.

5. Check that the master finds the slaves. If the kernel master is
   installed, stop it first (`sudo systemctl stop ethercat`):

   ```
   $ sudo ethercat-uspace start
   ec_master started on enp2s0 (pid 12345), 4 slave(s).
   It runs until LinuxCNC starts or 'sudo ethercat-uspace stop'.
   $ ethercat-uspace slaves
   0  0:0  PREOP  +  ECT60V202(COE)
   ...
   $ sudo ethercat-uspace stop
   ```

   If `start` reports 0 slaves, check the cable and the interface name.
   If it fails with `xdp-native`, try `TRANSPORT=raw`: the network
   driver may lack XDP support.

6. Build and install linuxcnc-ethercat from this repository (the
   `.deb` packages and the linuxcnc-ethercat upstream repository do not
   have `lcec_uspace`):

   ```
   git clone https://github.com/MetalMusings/linuxcnc-ethercat
   cd linuxcnc-ethercat
   make
   sudo make install
   ```

   `lcec_uspace.so` is built whenever
   `/opt/ethercat-uspace/include/ecrt.h` exists; for another prefix, run
   `make USPACE_ETHERCAT=/path/to/prefix`. The same sources are compiled
   a second time, as `*.uspace.o`, against that `ecrt.h`. If the
   `linuxcnc-ethercat` package is installed, `make install` replaces its
   files, and a later upgrade of the package replaces them back; remove
   the package or run `sudo make install` again after an upgrade.

   Either master is optional. Each module is built when its master is
   installed:

   | Installed | Built |
   |---|---|
   | IgH kernel master only | `lcec.so` |
   | userspace master only | `lcec_uspace.so` |
   | both | `lcec.so` and `lcec_uspace.so` |

   The kernel master is detected by `ecrt.h` in `/usr/include` or
   `/usr/local/include`. Without it, `lcec_conf` and the other tools are
   linked against the userspace master's `libethercat.so.2`. Override
   the detection with `make BUILD_KERNEL=no` or `make BUILD_USPACE=no`
   (or `yes`). Run `make clean` when changing these in a tree that has
   already been built.

7. Check what was built. `lcec.so` should use the kernel master's
   library and `lcec_uspace.so` the userspace one (only the modules
   that were built are installed):

   ```
   $ readelf -d /usr/lib/linuxcnc/modules/lcec.so | grep 'NEEDED.*ethercat'
    0x0000000000000001 (NEEDED)   Shared library: [libethercat.so.1]
   $ readelf -d /usr/lib/linuxcnc/modules/lcec_uspace.so | grep 'NEEDED.*ethercat'
    0x0000000000000001 (NEEDED)   Shared library: [libethercat.so.2]
   ```

8. Optionally, tune the machine for the realtime thread: turn off
   interrupt coalescing and EEE on the EtherCAT interface (in the start
   script, see [Running and switching between the
   masters](#running-and-switching-between-the-masters)), and isolate
   the realtime CPU (see [Performance](#performance)).

Then move a machine configuration to the userspace master as described
next.

### Configuring

To move a configuration to the userspace master, start from a copy of
a working `lcec` configuration, so that you can switch back by starting
the original, and change three things (and optionally a fourth, the transport):

- In the HAL file, load `lcec_uspace` instead of `lcec`:

  ```
  loadusr -W lcec_conf ethercat-conf.xml
  loadrt lcec_uspace
  ```

  The HAL component is called `lcec_uspace`, because `halcmd` requires
  the module's name, but pins and functions keep their `lcec.` names
  (`lcec.read-all`, `lcec.0.X.actual-position`, ...), so the rest of the
  HAL files stays as it is.

- In the XML file, give the network interface on the master. The
  kernel master takes it from `/etc/ethercat.conf`; the userspace master
  has no such file:

  ```XML
  <master idx="0" interface="enp2s0" appTimePeriod="1000000" refClockSyncCycles="-1000">
  ```

  The environment variable `LCEC_ETHERCAT_INTERFACE` is used when the
  attribute is missing. Only master 0 is supported. `lcec` ignores the
  attribute.

- Optionally, choose the transport on the master as well:

  ```XML
  <master idx="0" interface="enp2s0" transport="xdp-native" appTimePeriod="1000000" refClockSyncCycles="-1000">
  ```

  The environment variable `LCEC_ETHERCAT_TRANSPORT` is used when the
  attribute is missing, for example from the start script (see below).

  | Transport | |
  |---|---|
  | `raw` (default) | raw socket (AF_PACKET), works with any network card |
  | `xdp-native` | AF_XDP in the network driver; needs a network driver with XDP support, such as `igc` for the i225/i226. Removes the long receive times of `raw` (see [Performance](#performance)) |
  | `xdp-skb` | AF_XDP in the kernel's generic path, works with any network card, but gains little over `raw`; untested |

  The XDP transports need the userspace master built with libxdp (see
  [Building](#building)). `xdp-native` has been tested on an i226 with
  the `igc` driver. When it starts, libxdp may print
  `Can't use dispatcher without a working bpffs` and
  `Falling back to loading single prog without dispatcher`, and on exit
  `Error occurred when decrementing xsk XDP prog refcount`. These are
  harmless: the dispatcher is only needed for several XDP programs on
  the same interface, and the transport detaches its program itself.

- Move anything that was done with the `ethercat` tool before starting
  LinuxCNC into the XML file. LinuxCNC starts a master of its own, which
  scans and configures the slaves at every start, and the XML file is
  applied every time. (A master started with `ethercat-uspace start`
  can be used before LinuxCNC starts, but it stops when LinuxCNC
  starts.) For example, `ethercat -p 0 download 0x2010 0 1` for a
  16-bit object becomes, in that slave's section:

  ```XML
  <sdoConfig idx="2010" subIdx="0"><sdoDataRaw data="01 00"/></sdoConfig>
  ```

### Running and switching between the masters

A start script per machine configuration keeps the settings for the
userspace master together and can be started from a desktop launcher.
With the kernel master installed as well, it also stops the kernel
master, which must not use the network interface at the same time, and
starts it again when LinuxCNC exits:

```bash
#!/bin/bash
# start-uspace.sh, next to my-machine.ini
cd "$(dirname "$0")"

# Only with the kernel master installed: stop it while LinuxCNC runs.
if systemctl is-active --quiet ethercat; then
    trap 'sudo systemctl start ethercat' EXIT
    sudo systemctl stop ethercat
fi

# Lower latency on the EtherCAT interface: no interrupt coalescing, no EEE.
sudo ethtool -C enp2s0 rx-usecs 0 tx-usecs 0
sudo ethtool --set-eee enp2s0 eee off

export LCEC_ETHERCAT_TRANSPORT=xdp-native
linuxcnc my-machine.ini
```

A launcher on the desktop, for example `~/Desktop/my-machine.desktop`
(made executable with `chmod +x`):

```
[Desktop Entry]
Type=Application
Name=My machine (userspace master)
Exec=/home/cnc/linuxcnc/configs/my-machine/start-uspace.sh
Path=/home/cnc/linuxcnc/configs/my-machine
Icon=linuxcncicon
Terminal=false
```

A launcher has no terminal to ask for the `sudo` password, so allow
these commands without one (`ethtool` needs `sudo apt install ethtool`).
Create the file with `sudo visudo -f /etc/sudoers.d/linuxcnc-ethercat`
and replace `cnc` with your user name:

```
cnc ALL=(root) NOPASSWD: /usr/bin/systemctl start ethercat, /usr/bin/systemctl stop ethercat, /usr/sbin/ethtool
```

The script for an `lcec` configuration can make sure the kernel master
runs before it starts LinuxCNC:

```bash
systemctl is-active --quiet ethercat || { sudo systemctl start ethercat; sleep 3; }
```

`lcec_uspace` stops a free-running userspace master that holds the
interface (see [The ethercat tool](#the-ethercat-tool)), but the kernel
master does not know about it. With both masters installed, a systemd
drop-in stops it whenever the kernel master's service starts, whoever
starts it:

```
# /etc/systemd/system/ethercat.service.d/stop-uspace-master.conf
[Service]
ExecStartPre=-/opt/ethercat-uspace/bin/ethercat stop
```

followed by `sudo systemctl daemon-reload`.

### When LinuxCNC stops

`lcec_uspace` stops the bus in order by itself: every output becomes
zero and the slaves go to PREOP while process data still flows. Without
that, the slaves would lose their process data in OP and stop with an
error (sync manager watchdog 0x001B, synchronization error 0x001A), and
CiA 402 drives would latch a communication fault (0x7500 on the
Rtelligent ECT60, after one or two missed cycles) that must be reset
before they run again. Nothing is needed in the HAL files or in
`[HAL]SHUTDOWN`.

LinuxCNC stops the servo thread (`halcmd stop`) and unloads the
components right after. A stop guard, a realtime task with the servo
period beside the servo thread, sends the process data unchanged when
`lcec.write-all` is more than a quarter period late; after 10 such
cycles in a row it sets every output to zero and takes the slaves to
PREOP, and unloading waits for it (at most a second).
`lcec.0.guard-state` shows it (0 the servo thread runs, 1 taking the
slaves down, 2 done), and `lcec.0.guard-cycles` counts the cycles it
sent; while the machine runs that stays 0. `lcec.take-down` does the
same on request while the thread runs; `lcec-take-down` sets it and
waits for PREOP.

This needs the master fork's `ecrt_master_request_al_state()`
(`linuxcnc-ethercat-uspace` 0.7 or newer; the guard itself 0.8). After
`halcmd stop` and `start` by hand, DC slaves stop in SAFEOP with a
synchronization error, as they did before: restart LinuxCNC instead.

### The ethercat tool

The userspace master has its own build of IgH's `ethercat` tool,
`/opt/ethercat-uspace/bin/ethercat`, with the same commands and output.
It reaches the master through the Unix socket `/run/ethercat.sock`
instead of `/dev/EtherCAT0`. With both masters installed,
`/usr/bin/ethercat` is the kernel master's tool, which cannot reach the
userspace master; step 3 of [Building](#building) links the userspace
one as `ethercat-uspace`, and the examples here use that name.

A userspace master runs either inside LinuxCNC or, when LinuxCNC is not
running, as the daemon `ec_master`, which `ethercat-uspace start`
starts. The tool talks to whichever runs; other commands than `start`
never start a master.

#### While LinuxCNC runs

`lcec_uspace` opens the socket when it starts its master. The socket
belongs to the user that runs LinuxCNC, so the tool needs no `sudo`:

```
$ ethercat-uspace slaves
0  0:0  OP  +  ECT60V202(COE)
1  0:1  OP  +  ECT60V202(COE)
2  0:2  OP  +  ECT60V202(COE)
3  0:3  OP  +  MetalMusings EaserCAT 7000
```

The commands are served by a thread outside the servo thread. About 165
tool calls per second (`slaves -v`, `domains -v`, `data`, `upload`,
`sdos`, `reg_read`, `sii_read`, `master`) did not lengthen the servo
thread's longest cycle (see
[documentation/uspace-performance.md](documentation/uspace-performance.md#5-the-ethercat-tool-while-linuxcnc-runs)).
`upload` and `download` work on slaves in OP, so mind what a `download`
does to a running machine.

Tested: `master`, `slaves`, `domains`, `data`, `config`, `pdos`, `sdos`,
`upload`, `download`, `sii_read`, `reg_read`, `crc`, `graph`, `xml`,
`cstruct`, `debug`. Not yet tested: `states`, `sii_write`, `foe_*`,
`soe_*`, `alias`, `rescan`.

| Environment variable | |
|---|---|
| `LCEC_ETHERCAT_SOCKET` | socket path (default `/run/ethercat.sock`); `none` turns the socket off |
| `LCEC_ETHERCAT_SOCKET_GROUP` | an extra group that may use the socket |

#### Without LinuxCNC: ethercat start

`ethercat-uspace start` runs the userspace master in the background, as
the daemon `ec_master`, the way the `ethercat` service runs the kernel
master:

```
sudo ethercat-uspace start      # start it and wait for the bus scan
ethercat-uspace slaves          # no sudo needed from here on
ethercat-uspace master          # its state, or "No userspace master is running"
sudo ethercat-uspace stop       # stop it and wait until the interface is free
```

Its settings are in `/opt/ethercat-uspace/etc/ethercat-uspace.conf`:

```
INTERFACE=enp2s0
TRANSPORT=xdp-native
```

`GROUP`, `CPU` and `IRQ_CPU` are optional. Arguments after `--` are
given to `ec_master` instead of the file's settings, for example
`sudo ethercat-uspace start -- -i enp2s0 -t raw` (see
`/opt/ethercat-uspace/bin/ec_master -h`).

- `start` returns once the slaves are in PREOP, a few seconds. If the
  master already runs, it does nothing.
- The socket is given to the group in `GROUP`, else to the group of the
  user that ran `sudo`, so the tool works without `sudo` afterwards.
- The master runs until `stop` or until LinuxCNC starts. It also keeps
  running after the terminal is closed.
- `start` and `stop` only handle the free-running `ec_master`, never a
  master inside LinuxCNC: `stop` leaves it alone, and `start` refuses
  an interface that LinuxCNC uses.
- States set with `ethercat states` stay between commands, as with the
  kernel master. Since `start` and `stop` exist, `states` cannot be
  abbreviated to `st` or `sta` in the userspace tool.
- Without LinuxCNC there is no application configuration, so `domains`,
  `data` and `config` are empty, as with the kernel master on its own.
  The frame counters of `ethercat master` start from zero at every
  start.

Whoever uses an interface holds the lock
`/run/ethercat-uspace.<interface>.lock` with its PID in it. When
LinuxCNC starts, `lcec_uspace` takes it: an `ec_master` holding it is
sent SIGTERM, and LinuxCNC waits until the interface is free. Any other
holder, such as a second LinuxCNC, stops the start with an error.
`start` refuses to run while the kernel master is loaded
(`/dev/EtherCAT0` exists); for the other direction, see the drop-in in
[Running and switching between the
masters](#running-and-switching-between-the-masters).

### Differences from lcec

- At every start, the master scans the bus and configures the slaves
  (the kernel master keeps them scanned between runs). Expect the slaves
  in OP a few seconds after LinuxCNC has started.
- The master is activated in a helper thread instead of in the first
  servo thread cycle: in the userspace master, activation blocks for
  10–15 ms (see
  [stratuMAK/ethercat#7](https://github.com/stratuMAK/ethercat/issues/7)).
  The first, period-anchored application time is still set in the servo
  thread, so the DC SYNC0 phase is the same as with `lcec`.
- On exit, the master is deactivated and the driver waits up to 2 s for
  the slaves to reach PREOP, which the kernel master's idle thread
  otherwise does after LinuxCNC has gone. DC slaves may still report a
  synchronization error between the servo thread stopping and the
  unload; the kernel master shows the same in `dmesg`.
- The `ethercat` command line tool is the userspace build,
  `/opt/ethercat-uspace/bin/ethercat`, not `/usr/bin/ethercat` (see
  [The ethercat tool](#the-ethercat-tool)). The master's own messages go
  to LinuxCNC's standard error, not to `dmesg`.

### Performance

Measured on the test machine (Intel N100, PREEMPT_RT 6.12, i226 with
`igc`, three CiA 402 drives with DC and one I/O slave), 500 µs servo
thread with only `lcec.read-all` and `lcec.write-all`, `stress-ng` load
on all cores. Execution time of the servo thread, µs:

| with load | mean | p99.9 | max |
|---|---|---|---|
| kernel master (`lcec`) | 16.8 | 45.2 | 664 |
| userspace, `raw` | 15.0 | 41.9 | 313 |
| userspace, `xdp-native` | 12.4 | 34.1 | 63 |

- The long cycles of the kernel master and `raw` are in the receive
  (`lcec.read-all`); `xdp-native` removes them. The DC synchronization
  was the same with all three, and all slaves stayed in OP.
- With a full LinuxCNC (QtPlasmaC running a program, motion) and load,
  isolating the realtime CPU matters: without `isolcpus` the longest
  servo cycle was 511 µs, longer than the period; with
  `isolcpus=3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2` (on a 4-core CPU)
  it was 218 µs, and wake-up jitter over 10 µs was six times rarer.
- The CPU governor and the placement of the NIC interrupts made no clear
  difference.

All tests, the method and the full tables are in
[documentation/uspace-performance.md](documentation/uspace-performance.md).

### Troubleshooting

- *userspace master: cannot start master on enp2s0*: the kernel master
  still owns the interface (`systemctl is-active ethercat`), or the
  interface name is wrong.
- *userspace master: enp2s0 is in use by process ...*: another program
  than the `ec_master` daemon holds the interface lock, such as a second
  LinuxCNC (`cat /run/ethercat-uspace.enp2s0.lock` gives its PID).
- *Failed to connect to master socket /var/run/ethercat.sock: No such
  file or directory. No userspace master is running.*: neither LinuxCNC
  nor `ec_master` runs. Start one with
  `sudo ethercat-uspace start`, or use `/usr/bin/ethercat` if the
  kernel master runs.
- *Failed to connect to master socket ...: Permission denied*: the user
  is not in the socket's group (`ls -l /run/ethercat.sock`). Set
  `LCEC_ETHERCAT_SOCKET_GROUP` for LinuxCNC, or `GROUP` in
  `ethercat-uspace.conf`.
- *userspace master: give the EtherCAT network interface ...*: the
  `interface` attribute is missing from `<master>`.
- *userspace master: cannot create transport xdp-native on enp2s0*: the
  userspace master was built without libxdp (check with
  `/opt/ethercat-uspace/bin/ec_master -h`, see [Building](#building)),
  the transport name is misspelled, or the network driver lacks XDP
  support (try `xdp-skb` or `raw`).
- After a crash with an XDP transport, the XDP program may stay attached
  to the interface. `ip link show enp2s0` then shows `xdp` (or
  `xdpgeneric`); remove it with `sudo ip link set enp2s0 xdp off`.
- *Can't find module 'lcec_uspace'*: linuxcnc-ethercat was built without
  the userspace master in `/opt/ethercat-uspace`; install it and rebuild.
- To check which driver a running LinuxCNC uses: `halcmd show comp | grep lcec`
  lists `lcec` or `lcec_uspace` (next to `lcec_conf`).


## Configuring

At a minimum, you will need two files.  First, you'll need an XML file
(commonly called `ethercat.xml`) that describes your hardware.  Then
you'll need a LinuxCNC HAL file that loads the LinuxCNC-Ethercat
driver and tells LinuxCNC about your CNC.

There are two ways to use EtherCAT hardware with this driver.  First,
many devices have dedicated drivers which know about all of the
details of devices.  For instance, you can tell it to use a [Beckhoff
EL7041 Stepper controller](http://beckhoff.com/EL7041) as `x-axis` by
saying

```XML
   <slave idx="3" type="EL7041" name="x-axis"/>
```

This will create a number of LinuxCNC
[`pins`](https://linuxcnc.org/docs/html/hal/intro.html) that talk
directly to the EL7041 and control the stepper connected to it.  You
will still need to tell LinuxCNC what to do with the new hardware, but
the low-level details will be handled automatically.

The second way to use EtherCAT hardware is with the "generic" driver.
You can tell LinuxCNC-Ethercat about your hardware entirely in XML,
and it will let you send EtherCAT messages to any hardware, even if
we've never seen it before.  This is easier than writing a new driver,
but more difficult than using a pre-written driver.

A [reference guide to LinuxCNC-Ethercat's XML
configuration](documentation/configuration-reference.md) file is
available.

Several examples are available in the [`examples/`](examples/)
directory, but they're somewhat dated.  The [LinuxCNC
Forum](https://forum.linuxcnc.org/ethercat) is a better place to
start.

There is also a new, experimental tool included called
`lcec_configgen` that will attempt to automatically create an XML
configuration for you by examining the EtherCAT devices attached to
the system.  It should recognize all devices with pre-compiled
drivers, and will attempt to create generic drivers for other devices.
It's not always perfect, but it's usually an OK starting point.  The
configgen tool will not overwrite any files, so it should be safe to
run.

## Devices Supported

See [the device documentation](documentation/DEVICES.md) for a partial
list of Ethercat devices supported by this project.  Not all devices
are equally supported.  If you have any problems, please [file a
bug](https://github.com/linuxcnc-ethercat/linuxcnc-ethercat/issues/new/choose).

## Breaking Changes

We try not to deliberately break working systems while we're working
on LinuxCNC-Ethercat, but there are times when it's simply
unavoidable.  Sometimes this happens due to the nature of the bug, and
there's no safe way *not* to break things.  Sometimes it happens
because the existing behavior is so broken that it's not reasonable to
leave it in place, and other times it happens because we believe that
there are no impacted users.

See [the changes file](documentation/changes.md) for a list of
potentially-breaking changes.  In general, we try to communicate
potentially breaking changes via the LinuxCNC forums.

## Contributing

See [the contributing documentation](CONTRIBUTING.md) for details.  If
you have any issues with the contributing process, *please* file an
issue here.  Everything is new, and it may be broken.

[API
Documentation](https://linuxcnc-ethercat.github.io/linuxcnc-ethercat/doxygen/)
via Doxygen is available, but incomplete.
