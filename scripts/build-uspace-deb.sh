#!/bin/bash
#
# Build linuxcnc-ethercat-uspace_*.deb: the userspace IgH EtherCAT master and
# lcec_uspace, installed in /opt/linuxcnc-ethercat-uspace, apart from the
# system and from an installed kernel master and lcec. See INSTALL-USPACE.md.
#
# Usage: scripts/build-uspace-deb.sh [-m <master source>] [-b <branch>] [-o <dir>]
#
#   -m  the userspace master (MetalMusings/ethercat): a git URL, or a local git
#       tree, whose committed HEAD is used. Default: the GitHub repository.
#   -b  branch, for a git URL. Default: uspace-tool-ipc-fixes.
#   -o  directory for the .deb. Default: the current directory.
#
# Both trees are built from fresh clones of their committed state, so
# uncommitted changes are not packaged. The package is built for the Debian
# release and LinuxCNC that this machine has.
#
# Build dependencies (Debian 12/13 with LinuxCNC 2.9 from linuxcnc.org):
#   build-essential git autoconf automake libtool pkg-config libxdp-dev
#   libbpf-dev libexpat1-dev linuxcnc-uspace-dev dpkg-dev

set -euo pipefail

PKG=linuxcnc-ethercat-uspace
PREFIX=/opt/$PKG
BASE_VERSION=0.6

MASTER_SRC=https://github.com/MetalMusings/ethercat
MASTER_BRANCH=uspace-tool-ipc-fixes
OUTDIR=$PWD

while getopts "m:b:o:h" opt; do
    case $opt in
        m) MASTER_SRC=$OPTARG ;;
        b) MASTER_BRANCH=$OPTARG ;;
        o) OUTDIR=$(realpath "$OPTARG") ;;
        *) sed -n '2,/^$/s/^# \{0,1\}//p' "$0"; exit 1 ;;
    esac
done

LCEC_SRC=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
JOBS=$(nproc)
WORK=$(mktemp -d -t $PKG.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

MASTER_ROOT=$WORK/master-root  # the master's "make install"
ROOT=$WORK/pkg                 # the package's file tree

step() { echo; echo "=== $*"; }

# A fresh clone of a source's committed state.
clone() {  # <source> <dir> [branch]
    if [ -d "$1" ]; then
        if [ -n "$(git -C "$1" status --porcelain --untracked-files=no)" ]; then
            echo "Note: $1 has uncommitted changes; they are not packaged."
        fi
        git clone -q "$1" "$2"
        git -C "$2" checkout -q "$(git -C "$1" rev-parse HEAD)"
    else
        git clone -q --depth 1 ${3:+-b "$3"} "$1" "$2"
    fi
}

step "Sources"
clone "$MASTER_SRC" "$WORK/ethercat" "$MASTER_BRANCH"
clone "$LCEC_SRC" "$WORK/lcec"
MASTER_REV=$(git -C "$WORK/ethercat" rev-parse --short=8 HEAD)
LCEC_REV=$(git -C "$WORK/lcec" rev-parse --short=8 HEAD)
echo "master $MASTER_REV, linuxcnc-ethercat $LCEC_REV"

step "Userspace master"
(
    cd "$WORK/ethercat"
    ./bootstrap >/dev/null
    ./configure -q --enable-uspace-master --prefix=$PREFIX
    make -s -j"$JOBS"
    make -s install DESTDIR="$MASTER_ROOT"
) >"$WORK/master.log" 2>&1 || { tail -30 "$WORK/master.log"; exit 1; }
if ! LD_LIBRARY_PATH="$MASTER_ROOT$PREFIX/lib" "$MASTER_ROOT$PREFIX/bin/ec_master" -h 2>&1 \
        | grep -q xdp-native; then
    echo "The master was built without the XDP transports: install libxdp-dev." >&2
    exit 1
fi

step "lcec_uspace"
make -s -C "$WORK/lcec" install-uspace USPACE_ETHERCAT=$PREFIX \
    USPACE_SYSROOT="$MASTER_ROOT" DESTDIR="$ROOT" \
    >"$WORK/lcec.log" 2>&1 || { tail -30 "$WORK/lcec.log"; exit 1; }

step "Package tree"
install -d "$ROOT$PREFIX/bin" "$ROOT$PREFIX/lib" "$ROOT$PREFIX/etc" "$ROOT$PREFIX/include" \
    "$ROOT$PREFIX/share/doc" "$ROOT/usr/bin" "$ROOT/DEBIAN"
install -m 755 "$MASTER_ROOT$PREFIX/bin/ec_master" \
    "$MASTER_ROOT$PREFIX/bin/ethercat" "$ROOT$PREFIX/bin/"
cp -a "$MASTER_ROOT$PREFIX"/lib/libethercat.so.2* "$ROOT$PREFIX/lib/"
# The master's headers, so that other HAL components (such as ecat) can be
# built against the package: they link libethercat.so.2 by its full name.
install -m 644 "$MASTER_ROOT$PREFIX"/include/*.h "$ROOT$PREFIX/include/"
strip --strip-unneeded "$ROOT$PREFIX"/bin/* "$ROOT$PREFIX"/lib/libethercat.so.2.* \
    "$ROOT$PREFIX"/lib/linuxcnc/modules/lcec_uspace.so
ln -s $PREFIX/bin/ethercat "$ROOT/usr/bin/ethercat-uspace"

cat >"$ROOT$PREFIX/etc/ethercat-uspace.conf" <<EOF
# Settings for "sudo ethercat-uspace start", the free-running master that the
# ethercat-uspace tool can use before LinuxCNC starts. LinuxCNC (lcec_uspace)
# takes its settings from the XML file instead.
# Arguments after "--" replace these: sudo ethercat-uspace start -- -i eth0
#
# The network interface of the EtherCAT bus (see "ip link"):
#INTERFACE=enp2s0
# raw (any network card), xdp-native (driver with XDP support, such as igc):
#TRANSPORT=raw
EOF

install -m 644 "$WORK/lcec/INSTALL-USPACE.md" "$WORK/lcec/README.md" \
    "$ROOT$PREFIX/share/doc/"
cat >"$ROOT$PREFIX/share/doc/VERSIONS" <<EOF
userspace EtherCAT master: $MASTER_SRC $(git -C "$WORK/ethercat" rev-parse HEAD)
linuxcnc-ethercat: $(git -C "$WORK/lcec" rev-parse HEAD)
EOF
install -m 644 "$WORK/ethercat/COPYING" "$ROOT$PREFIX/share/doc/COPYING.ethercat"
install -m 644 "$WORK/ethercat/COPYING.LESSER" "$ROOT$PREFIX/share/doc/COPYING.LESSER.ethercat"
install -m 644 "$WORK/lcec/LICENSE" "$ROOT$PREFIX/share/doc/LICENSE.linuxcnc-ethercat"

step "Control file"
. /etc/os-release
ARCH=$(dpkg --print-architecture)
# The date is UTC, as on the CI machines, so that a local build late in the
# evening does not get a later date than a release built at the same time.
VERSION=$BASE_VERSION+git$(date -u +%Y%m%d).$LCEC_REV.$MASTER_REV~$ID$VERSION_ID
MAINTAINER="$(git -C "$WORK/lcec" log -1 --format='%an <%ae>')"

# Library dependencies of the binaries, without libethercat.so.2 (in the
# package). liblinuxcnchal.so.0 gives linuxcnc-uspace of at least the version
# built against; it is added by hand if LinuxCNC's package has no shlibs file.
mkdir -p "$WORK/shlibs/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' $PKG $PKG \
    >"$WORK/shlibs/debian/control"
SHLIBS=$(cd "$WORK/shlibs" && dpkg-shlibdeps -O --ignore-missing-info \
    -l"$ROOT$PREFIX/lib" -l/usr/lib \
    "$ROOT$PREFIX"/bin/* "$ROOT$PREFIX"/lib/libethercat.so.2.* \
    "$ROOT$PREFIX"/lib/linuxcnc/modules/lcec_uspace.so 2>/dev/null \
    | sed -n 's/^shlibs:Depends=//p')
# Only the upstream version of LinuxCNC counts: the HAL interface is the same
# in Debian's own revisions (1:2.9.4-2+deb13u1) and linuxcnc.org's (1:2.9.4).
SHLIBS=$(echo "$SHLIBS" | sed -E 's/(linuxcnc-uspace \(>= [^)-]*)-[^)]*\)/\1)/')
case "$SHLIBS" in
    *linuxcnc-uspace*) ;;
    *) SHLIBS="linuxcnc-uspace (>= 1:2.9), $SHLIBS" ;;
esac

cat >"$ROOT/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Architecture: $ARCH
Maintainer: $MAINTAINER
Depends: $SHLIBS
Section: misc
Priority: optional
Homepage: https://github.com/MetalMusings/linuxcnc-ethercat
Description: Userspace EtherCAT master for LinuxCNC (lcec_uspace), experimental
 The IgH EtherCAT master built as a library, without a kernel module, and
 lcec_uspace, the LinuxCNC EtherCAT driver built against it. Installed in
 $PREFIX, apart from an installed kernel master and lcec.
 See $PREFIX/share/doc/INSTALL-USPACE.md.
EOF
echo "$PREFIX/etc/ethercat-uspace.conf" >"$ROOT/DEBIAN/conffiles"

step "Package"
mkdir -p "$OUTDIR"
DEB=$OUTDIR/${PKG}_${VERSION}_$ARCH.deb
dpkg-deb --root-owner-group -Zxz --build "$ROOT" "$DEB" >/dev/null
dpkg-deb --info "$DEB" | sed -n '/Package:/,$p'
dpkg-deb --contents "$DEB" | awk '{print $6, $7, $8}'
echo
echo "Built $DEB"
