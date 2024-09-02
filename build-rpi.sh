#!/bin/bash
set -eux

#crossdev -t arm-linux-gnueabihf
#rsync -vR --progress -rl --delete-after --safe-links pi@raspberry:/{usr,lib} cross-rootfs

HOST="$1"
PROG="$2"
TARGET="build-rpi/rel/$PROG"

export CC="arm-linux-gnueabihf-gcc"
export CFLAGS="-I./cross-rootfs-sel/usr/include"
#export LIBS="-Wl,-rpath-link,./cross-rootfs-sel/usr/lib/arm-linux-gnueabihf -L./cross-rootfs-sel/usr/lib/arm-linux-gnueabihf --sysroot=./cross-rootfs-sel -B./cross-rootfs-sel/usr/lib/arm-linux-gnueabihf"
export LIBS="-Wl,-rpath-link,./cross-rootfs-sel/usr/lib/arm-linux-gnueabihf --sysroot=./cross-rootfs-sel -B./cross-rootfs-sel/usr/lib/arm-linux-gnueabihf"
export BUILDDIR="build-rpi"

make "$TARGET" && scp "$TARGET" "$HOST":~/tmp/ && ssh "$HOST" ~/tmp/"$PROG"
