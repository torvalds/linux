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
# older versions of depmod require the version string to start with three
# numbers, so we cheat with a symlink here
depmod_hack_needed=true
tmp_dir=$(mktemp -d ${TMPDIR:-/tmp}/depmod.XXXXXX)
mkdir -p "$tmp_dir/lib/modules/$KERNELRELEASE"
if "$DEPMOD" -b "$tmp_dir" $KERNELRELEASE 2>/dev/null; then
	if test -e "$tmp_dir/lib/modules/$KERNELRELEASE/modules.dep" -o \
		-e "$tmp_dir/lib/modules/$KERNELRELEASE/modules.dep.bin"; then
		depmod_hack_needed=false
	fi
fi
rm -rf "$tmp_dir"
if $depmod_hack_needed; then
	symlink="$INSTALL_MOD_PATH/lib/modules/99.98.$KERNELRELEASE"
	ln -s "$KERNELRELEASE" "$symlink"
	KERNELRELEASE=99.98.$KERNELRELEASE
fi

set -- -ae -F System.map
if test -n "$INSTALL_MOD_PATH"; then
	set -- "$@" -b "$INSTALL_MOD_PATH"
fi
"$DEPMOD" "$@" "$KERNELRELEASE"
ret=$?

if $depmod_hack_needed; then
	rm -f "$symlink"
fi

exit $ret
