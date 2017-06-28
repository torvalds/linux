#!/bin/sh
n=`./default_file_splice_read </dev/null | wc -c`

test "$n" = 0 && exit 0

echo "default_file_splice_read broken: leaked $n"
exit 1
