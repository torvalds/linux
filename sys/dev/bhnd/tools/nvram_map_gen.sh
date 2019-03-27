#!/bin/sh

# Use C locale to ensure AWK string comparisons always produce
# a stable sort order.

# $FreeBSD$

BHND_TOOLDIR="$(dirname $0)/"

LC_ALL=C; export LC_ALL

"${AWK:-/usr/bin/awk}" -f "$BHND_TOOLDIR/nvram_map_gen.awk" $@
