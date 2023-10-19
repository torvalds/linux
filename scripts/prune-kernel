#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# because I use CONFIG_LOCALVERSION_AUTO, not the same version again and
# again, /boot and /lib/modules/ eventually fill up.
# Dumb script to purge that stuff:

for f in "$@"
do
        if rpm -qf "/lib/modules/$f" >/dev/null; then
                echo "keeping $f (installed from rpm)"
        elif [ $(uname -r) = "$f" ]; then
                echo "keeping $f (running kernel) "
        else
                echo "removing $f"
                rm -f "/boot/initramfs-$f.img" "/boot/System.map-$f"
                rm -f "/boot/vmlinuz-$f"   "/boot/config-$f"
                rm -rf "/lib/modules/$f"
                if [ -x "$(command -v new-kernel-pkg)" ]; then
                        new-kernel-pkg --remove $f
                elif [ -x "$(command -v kernel-install)" ]; then
                        kernel-install remove $f
                fi
        fi
done
