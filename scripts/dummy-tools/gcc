#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Staring v4.18, Kconfig evaluates compiler capabilities, and hides CONFIG
# options your compiler does not support. This works well if you configure and
# build the kernel on the same host machine.
#
# It is inconvenient if you prepare the .config that is carried to a different
# build environment (typically this happens when you package the kernel for
# distros) because using a different compiler potentially produces different
# CONFIG options than the real build environment. So, you probably want to make
# as many options visible as possible. In other words, you need to create a
# super-set of CONFIG options that cover any build environment. If some of the
# CONFIG options turned out to be unsupported on the build machine, they are
# automatically disabled by the nature of Kconfig.
#
# However, it is not feasible to get a full-featured compiler for every arch.
# Hence these dummy toolchains to make all compiler tests pass.
#
# Usage:
#
# From the top directory of the source tree, run
#
#   $ make CROSS_COMPILE=scripts/dummy-tools/ oldconfig
#
# Most of compiler features are tested by cc-option, which simply checks the
# exit code of $(CC). This script does nothing and just exits with 0 in most
# cases. So, $(cc-option, ...) is evaluated as 'y'.
#
# This scripts caters to more checks; handle --version and pre-process __GNUC__
# etc. to pretend to be GCC, and also do right things to satisfy some scripts.

# Check if the first parameter appears in the rest. Succeeds if found.
# This helper is useful if a particular option was passed to this script.
# Typically used like this:
#   arg_contain <word-you-are-searching-for> "$@"
arg_contain ()
{
	search="$1"
	shift

	while [ $# -gt 0 ]
	do
		if [ "$search" = "$1" ]; then
			return 0
		fi
		shift
	done

	return 1
}

# To set CONFIG_CC_IS_GCC=y
if arg_contain --version "$@"; then
	echo "gcc (scripts/dummy-tools/gcc)"
	exit 0
fi

if arg_contain -E "$@"; then
	# For scripts/cc-version.sh; This emulates GCC 20.0.0
	if arg_contain - "$@"; then
		sed -n '/^GCC/{s/__GNUC__/20/; s/__GNUC_MINOR__/0/; s/__GNUC_PATCHLEVEL__/0/; p;}; s/__LONG_DOUBLE_128__/1/ p'
		exit 0
	else
		echo "no input files" >&2
		exit 1
	fi
fi

# To set CONFIG_AS_IS_GNU
if arg_contain -Wa,--version "$@"; then
	echo "GNU assembler (scripts/dummy-tools) 2.50"
	exit 0
fi

if arg_contain -S "$@"; then
	# For scripts/gcc-x86-*-has-stack-protector.sh
	if arg_contain -fstack-protector "$@"; then
		if arg_contain -mstack-protector-guard-reg=fs "$@"; then
			echo "%fs"
		else
			echo "%gs"
		fi
		exit 0
	fi

	# For arch/powerpc/tools/gcc-check-mprofile-kernel.sh
	if arg_contain -m64 "$@" && arg_contain -mprofile-kernel "$@"; then
		if ! test -t 0 && ! grep -q notrace; then
			echo "_mcount"
		fi
		exit 0
	fi

	# For arch/powerpc/tools/gcc-check-fpatchable-function-entry.sh
	if arg_contain -m64 "$@" && arg_contain -fpatchable-function-entry=2 "$@"; then
		echo "func:"
		echo ".section __patchable_function_entries"
		echo ".localentry"
		echo "  nop"
		echo "  nop"
		exit 0
	fi
fi

# To set GCC_PLUGINS
if arg_contain -print-file-name=plugin "$@"; then
	# Use $0 to find the in-tree dummy directory
	echo "$(dirname "$(readlink -f "$0")")/dummy-plugin-dir"
	exit 0
fi

# inverted return value
if arg_contain -D__SIZEOF_INT128__=0 "$@"; then
	exit 1
fi
