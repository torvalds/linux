#!/bin/sh
#
# A depmod wrapper used by the toplevel Makefile

if test $# -ne 3; then
	echo "Usage: $0 /sbin/depmod <kernelrelease> <symbolprefix>" >&2
	exit 1
fi
DEPMOD=$1
KERNELRELEASE=$2
SYMBOL_PREFIX=$3

if ! test -r System.map ; then
	exit 0
fi

if [ -z $(command -v $DEPMOD) ]; then
	echo "'make modules_install' requires $DEPMOD. Please install it." >&2
	echo "This is probably in the kmod package." >&2
	exit 1
fi

# older versions of depmod don't support -P <symbol-prefix>
# support was added in module-init-tools 3.13
if test -n "$SYMBOL_PREFIX"; then
	release=$("$DEPMOD" --version)
	package=$(echo "$release" | cut -d' ' -f 1)
	if test "$package" = "module-init-tools"; then
		version=$(echo "$release" | cut -d' ' -f 2)
		later=$(printf '%s\n' "$version" "3.13" | sort -V | tail -n 1)
		if test "$later" != "$version"; then
			# module-init-tools < 3.13, drop the symbol prefix
			SYMBOL_PREFIX=""
		fi
	fi
	if test -n "$SYMBOL_PREFIX"; then
		SYMBOL_PREFIX="-P $SYMBOL_PREFIX"
	fi
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
"$DEPMOD" "$@" "$KERNELRELEASE" $SYMBOL_PREFIX
ret=$?

if $depmod_hack_needed; then
	rm -f "$symlink"
fi

exit $ret
