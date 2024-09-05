#!/bin/bash
set -eux

#crossdev -t arm-linux-gnueabihf
#rsync -vR --progress -rl --delete-after --safe-links pi@raspberry:/{usr,lib} cross-rootfs

HOST="$1"
PROG="$2"
TARGET="build-rpi/rel/$PROG"

ROOTFS="$HOME/opt/rpi4-rootfs"

export CC="arm-linux-gnueabihf-gcc"
export CFLAGS="-I${ROOTFS}/usr/include -DL_tmpnam=20"
#export LIBS="-Wl,-rpath-link,${ROOTFS}/usr/lib/arm-linux-gnueabihf -L${ROOTFS}/usr/lib/arm-linux-gnueabihf --sysroot=${ROOTFS} -B${ROOTFS}/usr/lib/arm-linux-gnueabihf"
export LIBS="-Wl,-rpath-link,${ROOTFS}/usr/lib/arm-linux-gnueabihf --sysroot=${ROOTFS} -B${ROOTFS}/usr/lib/arm-linux-gnueabihf"
export BUILDDIR="build-rpi"

make "$TARGET" && scp "$TARGET" "$HOST":~/tmp/ && ssh "$HOST" ~/tmp/"$PROG"
