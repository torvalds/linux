#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Translate the bits making up a GFP mask
# (c) 2009, Mel Gorman <mel@csn.ul.ie>
SOURCE=
GFPMASK=none

# Helper function to report failures and exit
die() {
	echo ERROR: $@
	if [ "$TMPFILE" != "" ]; then
		rm -f $TMPFILE
	fi
	exit -1
}

usage() {
	echo "usage: gfp-translate [-h] [ --source DIRECTORY ] gfpmask"
	exit 0
}

# Parse command-line arguments
while [ $# -gt 0 ]; do
	case $1 in
		--source)
			SOURCE=$2
			shift 2
			;;
		-h)
			usage
			;;
		--help)
			usage
			;;
		*)
			GFPMASK=$1
			shift
			;;
	esac
done

# Guess the kernel source directory if it's not set. Preference is in order of
# o current directory
# o /usr/src/linux
if [ "$SOURCE" = "" ]; then
	if [ -r "/usr/src/linux/Makefile" ]; then
		SOURCE=/usr/src/linux
	fi
	if [ -r "`pwd`/Makefile" ]; then
		SOURCE=`pwd`
	fi
fi

# Confirm that a source directory exists
if [ ! -r "$SOURCE/Makefile" ]; then
	die "Could not locate kernel source directory or it is invalid"
fi

# Confirm that a GFP mask has been specified
if [ "$GFPMASK" = "none" ]; then
	usage
fi

# Extract GFP flags from the kernel source
TMPFILE=`mktemp -t gfptranslate-XXXXXX.c` || exit 1

echo Source: $SOURCE
echo Parsing: $GFPMASK

(
    cat <<EOF
#include <stdint.h>
#include <stdio.h>

// Try to fool compiler.h into not including extra stuff
#define __ASSEMBLY__	1

#include <generated/autoconf.h>
#include <linux/gfp_types.h>

static const char *masks[] = {
EOF

    sed -nEe 's/^[[:space:]]+(___GFP_.*)_BIT,.*$/\1/p' $SOURCE/include/linux/gfp_types.h |
	while read b; do
	    cat <<EOF
#if defined($b) && ($b > 0)
	[${b}_BIT]	= "$b",
#endif
EOF
	done

    cat <<EOF
};

int main(int argc, char *argv[])
{
	unsigned long long mask = $GFPMASK;

	for (int i = 0; i < sizeof(mask) * 8; i++) {
		unsigned long long bit = 1ULL << i;
		if (mask & bit)
			printf("\t%-25s0x%llx\n",
			       (i < ___GFP_LAST_BIT && masks[i]) ?
					masks[i] : "*** INVALID ***",
			       bit);
	}

	return 0;
}
EOF
) > $TMPFILE

${CC:-gcc} -Wall -o ${TMPFILE}.bin -I $SOURCE/include $TMPFILE && ${TMPFILE}.bin

rm -f $TMPFILE ${TMPFILE}.bin

exit 0
