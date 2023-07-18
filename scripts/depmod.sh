#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# A depmod wrapper used by the toplevel Makefile

if test $# -ne 2; then
	echo "Usage: $0 /sbin/depmod <kernelrelease>" >&2
	exit 1
fi
DEPMOD=$1
KERNELRELEASE=$2

if ! test -r System.map ; then
	echo "Warning: modules_install: missing 'System.map' file. Skipping depmod." >&2
	exit 0
fi

# legacy behavior: "depmod" in /sbin, no /sbin in PATH
PATH="$PATH:/sbin"
if [ -z $(command -v $DEPMOD) ]; then
	echo "Warning: 'make modules_install' requires $DEPMOD. Please install it." >&2
	echo "This is probably in the kmod package." >&2
	exit 0
fi

set -- -ae -F System.map
if test -n "$INSTALL_MOD_PATH"; then
	set -- "$@" -b "$INSTALL_MOD_PATH"
fi
exec "$DEPMOD" "$@" "$KERNELRELEASE"
