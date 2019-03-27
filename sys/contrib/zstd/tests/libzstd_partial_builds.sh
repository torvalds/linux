#!/bin/sh -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

ECHO=echo
RM="rm -f"
GREP="grep"
INTOVOID="/dev/null"

die() {
    $ECHO "$@" 1>&2
    exit 1
}

isPresent() {
    $GREP $@ tmplog || die "$@" "should be present"
}

mustBeAbsent() {
    $GREP $@ tmplog && die "$@ should not be there !!"
    $ECHO "$@ correctly not present"  # for some reason, this $ECHO must exist, otherwise mustBeAbsent() always fails (??)
}

# default compilation : all features enabled
make clean > /dev/null
$ECHO "testing default library compilation"
CFLAGS= make -C $DIR/../lib libzstd.a > $INTOVOID
nm $DIR/../lib/libzstd.a | $GREP "\.o" > tmplog
isPresent "zstd_compress.o"
isPresent "zstd_decompress.o"
isPresent "zdict.o"
isPresent "zstd_v07.o"
isPresent "zbuff_compress.o"
$RM $DIR/../lib/libzstd.a tmplog

# compression disabled => also disable zdict and zbuff
$ECHO "testing with compression disabled"
ZSTD_LIB_COMPRESSION=0 CFLAGS= make -C $DIR/../lib libzstd.a > $INTOVOID
nm $DIR/../lib/libzstd.a | $GREP "\.o" > tmplog
mustBeAbsent "zstd_compress.o"
isPresent "zstd_decompress.o"
mustBeAbsent "zdict.o"
isPresent "zstd_v07.o"
mustBeAbsent "zbuff_compress.o"
$RM $DIR/../lib/libzstd.a tmplog

# decompression disabled => also disable legacy and zbuff
$ECHO "testing with decompression disabled"
ZSTD_LIB_DECOMPRESSION=0 CFLAGS= make -C $DIR/../lib libzstd.a > $INTOVOID
nm $DIR/../lib/libzstd.a | $GREP "\.o" > tmplog
isPresent "zstd_compress.o"
mustBeAbsent "zstd_decompress.o"
isPresent "zdict.o"
mustBeAbsent "zstd_v07.o"
mustBeAbsent "zbuff_compress.o"
$RM $DIR/../lib/libzstd.a tmplog

# deprecated function disabled => only remove zbuff
$ECHO "testing with deprecated functions disabled"
ZSTD_LIB_DEPRECATED=0 CFLAGS= make -C $DIR/../lib libzstd.a > $INTOVOID
nm $DIR/../lib/libzstd.a | $GREP "\.o" > tmplog
isPresent "zstd_compress.o"
isPresent "zstd_decompress.o"
isPresent "zdict.o"
isPresent "zstd_v07.o"
mustBeAbsent "zbuff_compress.o"
$RM $DIR/../lib/libzstd.a tmplog

# dictionary builder disabled => only remove zdict
$ECHO "testing with dictionary builder disabled"
ZSTD_LIB_DICTBUILDER=0 CFLAGS= make -C $DIR/../lib libzstd.a > $INTOVOID
nm $DIR/../lib/libzstd.a | $GREP "\.o" > tmplog
isPresent "zstd_compress.o"
isPresent "zstd_decompress.o"
mustBeAbsent "zdict.o"
isPresent "zstd_v07.o"
isPresent "zbuff_compress.o"
$RM $DIR/../lib/libzstd.a tmplog

# both decompression and dictionary builder disabled => only compression remains
$ECHO "testing with both decompression and dictionary builder disabled (only compression remains)"
ZSTD_LIB_DECOMPRESSION=0 ZSTD_LIB_DICTBUILDER=0 CFLAGS= make -C $DIR/../lib libzstd.a > $INTOVOID
nm $DIR/../lib/libzstd.a | $GREP "\.o" > tmplog
isPresent "zstd_compress.o"
mustBeAbsent "zstd_decompress.o"
mustBeAbsent "zdict.o"
mustBeAbsent "zstd_v07.o"
mustBeAbsent "zbuff_compress.o"
$RM $DIR/../lib/libzstd.a tmplog
