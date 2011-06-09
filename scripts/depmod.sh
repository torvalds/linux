#!/bin/sh
#
# A depmod wrapper used by the toplevel Makefile

if test $# -ne 2; then
	echo "Usage: $0 /sbin/depmod <kernelrelease>" >&2
	exit 1
fi
DEPMOD=$1
KERNELRELEASE=$2

if ! "$DEPMOD" -V 2>/dev/null | grep -q module-init-tools; then
	echo "Warning: you may need to install module-init-tools" >&2
	echo "See http://www.codemonkey.org.uk/docs/post-halloween-2.6.txt" >&2
	sleep 1
fi

if ! test -r System.map -a -x "$DEPMOD"; then
	exit 0
fi
set -- -ae -F System.map
if test -n "$INSTALL_MOD_PATH"; then
	set -- "$@" -b "$INSTALL_MOD_PATH"
fi
exec "$DEPMOD" "$@" "$KERNELRELEASE"
