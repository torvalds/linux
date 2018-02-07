#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

if [ $# -lt 2 ]
then
	echo "Usage: headers_install.sh OUTDIR SRCDIR [FILES...]"
	echo
	echo "Prepares kernel header files for use by user space, by removing"
	echo "all compiler.h definitions and #includes, removing any"
	echo "#ifdef __KERNEL__ sections, and putting __underscores__ around"
	echo "asm/inline/volatile keywords."
	echo
	echo "OUTDIR: directory to write each userspace header FILE to."
	echo "SRCDIR: source directory where files are picked."
	echo "FILES:  list of header files to operate on."

	exit 1
fi

# Grab arguments

OUTDIR="$1"
shift
SRCDIR="$1"
shift

# Iterate through files listed on command line

FILE=
trap 'rm -f "$OUTDIR/$FILE" "$OUTDIR/$FILE.sed"' EXIT
for i in "$@"
do
	FILE="$(basename "$i")"
	sed -E \
		-e 's/([[:space:](])(__user|__force|__iomem)[[:space:]]/\1/g' \
		-e 's/__attribute_const__([[:space:]]|$)/\1/g' \
		-e 's@^#include <linux/compiler(|_types).h>@@' \
		-e 's/(^|[^a-zA-Z0-9])__packed([^a-zA-Z0-9_]|$)/\1__attribute__((packed))\2/g' \
		-e 's/(^|[[:space:](])(inline|asm|volatile)([[:space:](]|$)/\1__\2__\3/g' \
		-e 's@#(ifndef|define|endif[[:space:]]*/[*])[[:space:]]*_UAPI@#\1 @' \
		"$SRCDIR/$i" > "$OUTDIR/$FILE.sed" || exit 1
	scripts/unifdef -U__KERNEL__ -D__EXPORTED_HEADERS__ "$OUTDIR/$FILE.sed" \
		> "$OUTDIR/$FILE"
	[ $? -gt 1 ] && exit 1
	rm -f "$OUTDIR/$FILE.sed"
done
trap - EXIT
