#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

if [ $# -ne 2 ]
then
	echo "Usage: headers_install.sh INFILE OUTFILE"
	echo
	echo "Prepares kernel header files for use by user space, by removing"
	echo "all compiler.h definitions and #includes, removing any"
	echo "#ifdef __KERNEL__ sections, and putting __underscores__ around"
	echo "asm/inline/volatile keywords."
	echo
	echo "INFILE: header file to operate on"
	echo "OUTFILE: output file which the processed header is written to"

	exit 1
fi

# Grab arguments
INFILE=$1
OUTFILE=$2
TMPFILE=$OUTFILE.tmp

trap 'rm -f $OUTFILE $TMPFILE' EXIT

# SPDX-License-Identifier with GPL variants must have "WITH Linux-syscall-note"
if [ -n "$(sed -n -e "/SPDX-License-Identifier:.*GPL-/{/WITH Linux-syscall-note/!p}" $INFILE)" ]; then
	echo "error: $INFILE: missing \"WITH Linux-syscall-note\" for SPDX-License-Identifier" >&2
	exit 1
fi

sed -E -e '
	s/([[:space:](])(__user|__force|__iomem)[[:space:]]/\1/g
	s/__attribute_const__([[:space:]]|$)/\1/g
	s@^#include <linux/compiler(|_types).h>@@
	s/(^|[^a-zA-Z0-9])__packed([^a-zA-Z0-9_]|$)/\1__attribute__((packed))\2/g
	s/(^|[[:space:](])(inline|asm|volatile)([[:space:](]|$)/\1__\2__\3/g
	s@#(ifndef|define|endif[[:space:]]*/[*])[[:space:]]*_UAPI@#\1 @
' $INFILE > $TMPFILE || exit 1

scripts/unifdef -U__KERNEL__ -D__EXPORTED_HEADERS__ $TMPFILE > $OUTFILE
[ $? -gt 1 ] && exit 1

# Remove /* ... */ style comments, and find CONFIG_ references in code
configs=$(sed -e '
:comment
	s:/\*[^*][^*]*:/*:
	s:/\*\*\**\([^/]\):/*\1:
	t comment
	s:/\*\*/: :
	t comment
	/\/\*/! b check
	N
	b comment
:print
	P
	D
:check
	s:^\(CONFIG_[[:alnum:]_]*\):\1\n:
	t print
	s:^[[:alnum:]_][[:alnum:]_]*::
	s:^[^[:alnum:]_][^[:alnum:]_]*::
	t check
	d
' $OUTFILE)

# The entries in the following list do not result in an error.
# Please do not add a new entry. This list is only for existing ones.
# The list will be reduced gradually, and deleted eventually. (hopefully)
#
# The format is <file-name>:<CONFIG-option> in each line.
config_leak_ignores="
arch/alpha/include/uapi/asm/setup.h:CONFIG_ALPHA_LEGACY_START_ADDRESS
arch/arc/include/uapi/asm/page.h:CONFIG_ARC_PAGE_SIZE_16K
arch/arc/include/uapi/asm/page.h:CONFIG_ARC_PAGE_SIZE_4K
arch/arc/include/uapi/asm/swab.h:CONFIG_ARC_HAS_SWAPE
arch/arm/include/uapi/asm/ptrace.h:CONFIG_CPU_ENDIAN_BE8
arch/hexagon/include/uapi/asm/ptrace.h:CONFIG_HEXAGON_ARCH_VERSION
arch/hexagon/include/uapi/asm/user.h:CONFIG_HEXAGON_ARCH_VERSION
arch/ia64/include/uapi/asm/cmpxchg.h:CONFIG_IA64_DEBUG_CMPXCHG
arch/m68k/include/uapi/asm/ptrace.h:CONFIG_COLDFIRE
arch/nios2/include/uapi/asm/swab.h:CONFIG_NIOS2_CI_SWAB_NO
arch/nios2/include/uapi/asm/swab.h:CONFIG_NIOS2_CI_SWAB_SUPPORT
arch/x86/include/uapi/asm/auxvec.h:CONFIG_IA32_EMULATION
arch/x86/include/uapi/asm/auxvec.h:CONFIG_X86_64
arch/x86/include/uapi/asm/mman.h:CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
include/uapi/asm-generic/fcntl.h:CONFIG_64BIT
include/uapi/linux/atmdev.h:CONFIG_COMPAT
include/uapi/linux/elfcore.h:CONFIG_BINFMT_ELF_FDPIC
include/uapi/linux/eventpoll.h:CONFIG_PM_SLEEP
include/uapi/linux/hw_breakpoint.h:CONFIG_HAVE_MIXED_BREAKPOINTS_REGS
include/uapi/linux/pktcdvd.h:CONFIG_CDROM_PKTCDVD_WCACHE
include/uapi/linux/raw.h:CONFIG_MAX_RAW_DEVS
"

for c in $configs
do
	leak_error=1

	for ignore in $config_leak_ignores
	do
		if echo "$INFILE:$c" | grep -q "$ignore$"; then
			leak_error=
			break
		fi
	done

	if [ "$leak_error" = 1 ]; then
		echo "error: $INFILE: leak $c to user-space" >&2
		exit 1
	fi
done

rm -f $TMPFILE
trap - EXIT
