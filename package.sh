#!/usr/bin/env bash

SCR=$HOME/src/linux
BUILD=/usr/src/linux-5.15-SPDIO
LOCAL=-spd
JOBS=$(nproc)

#mkdir -p "$BUILD"
#cp "/boot/config-$(uname -r)" "$BUILD/.config"
#make O="$BUILD" olddefconfig

#scripts/config --file "$BUILD/.config" \
#  --set-str CONFIG_LOCALVERSION "$LOCAL"

make O="$BUILD" -j"$JOBS" bindeb-pkg

cd "$BUILD"/..
sudo dpkg -i linux-image-*${LOCAL}*.deb \
  linux-headers-*${LOCAL}*.deb

sudo reboot
