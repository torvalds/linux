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
	s@^#include <linux/compiler.h>@@
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

for c in $configs
do
	echo "error: $INFILE: leak $c to user-space" >&2
	exit 1
done

rm -f $TMPFILE
trap - EXIT
