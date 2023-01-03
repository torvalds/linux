#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Probe for libraries and create header files to record the results. Both C
# header files and Makefile include fragments are created.

OUTPUT_H_FILE=local_config.h
OUTPUT_MKFILE=local_config.mk

tmpname=$(mktemp)
tmpfile_c=${tmpname}.c
tmpfile_o=${tmpname}.o

# liburing
echo "#include <sys/types.h>"        > $tmpfile_c
echo "#include <liburing.h>"        >> $tmpfile_c
echo "int func(void) { return 0; }" >> $tmpfile_c

CC=${1:?"Usage: $0 <compiler> # example compiler: gcc"}
$CC -c $tmpfile_c -o $tmpfile_o >/dev/null 2>&1

if [ -f $tmpfile_o ]; then
    echo "#define LOCAL_CONFIG_HAVE_LIBURING 1"  > $OUTPUT_H_FILE
    echo "COW_EXTRA_LIBS = -luring"              > $OUTPUT_MKFILE
else
    echo "// No liburing support found"          > $OUTPUT_H_FILE
    echo "# No liburing support found, so:"      > $OUTPUT_MKFILE
    echo "COW_EXTRA_LIBS = "                    >> $OUTPUT_MKFILE
fi

rm ${tmpname}.*
