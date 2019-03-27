#!/bin/sh

LIBVER_MAJOR_SCRIPT=`sed -n '/define ZSTD_VERSION_MAJOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < ../../lib/zstd.h`
LIBVER_MINOR_SCRIPT=`sed -n '/define ZSTD_VERSION_MINOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < ../../lib/zstd.h`
LIBVER_PATCH_SCRIPT=`sed -n '/define ZSTD_VERSION_RELEASE/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < ../../lib/zstd.h`
LIBVER_SCRIPT=$LIBVER_MAJOR_SCRIPT.$LIBVER_MINOR_SCRIPT.$LIBVER_PATCH_SCRIPT

echo ZSTD_VERSION=$LIBVER_SCRIPT
./gen_html $LIBVER_SCRIPT ../../lib/zstd.h ./zstd_manual.html
