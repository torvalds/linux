#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

outfile=""
now=`date +%s`

while [ $# -gt 0 ]
do
    case "$1" in
        -o)
	    outfile="$2"
	    shift 2;;
	-h)
	    echo "usage: $0 [-o outfile] <make options/args>"
	    exit 0;;
	*)  break;;
    esac
done

if [ -z "$outfile" ]
then
    outfile=`mktemp --tmpdir stackusage.$$.XXXX`
fi

KCFLAGS="${KCFLAGS} -fstack-usage" make "$@"

# Prepend directory name to file names, remove column information,
# make file:line/function/size/type properly tab-separated.
find . -name '*.su' -newermt "@${now}" -print |                     \
    xargs perl -MFile::Basename -pe                                 \
        '$d = dirname($ARGV); s#([^:]+:[0-9]+):[0-9]+:#$d/$1\t#;' | \
    sort -k3,3nr > "${outfile}"

echo "$0: output written to ${outfile}"
