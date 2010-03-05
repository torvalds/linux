#!/bin/bash
# perf archive
# Arnaldo Carvalho de Melo <acme@redhat.com>

PERF_DATA=perf.data
if [ $# -ne 0 ] ; then
	PERF_DATA=$1
fi

DEBUGDIR=~/.debug/
BUILDIDS=$(mktemp /tmp/perf-archive-buildids.XXXXXX)

perf buildid-list -i $PERF_DATA --with-hits > $BUILDIDS
if [ ! -s $BUILDIDS ] ; then
	echo "perf archive: no build-ids found"
	rm -f $BUILDIDS
	exit 1
fi

MANIFEST=$(mktemp /tmp/perf-archive-manifest.XXXXXX)

cut -d ' ' -f 1 $BUILDIDS | \
while read build_id ; do
	linkname=$DEBUGDIR.build-id/${build_id:0:2}/${build_id:2}
	filename=$(readlink -f $linkname)
	echo ${linkname#$DEBUGDIR} >> $MANIFEST
	echo ${filename#$DEBUGDIR} >> $MANIFEST
done

tar cfj $PERF_DATA.tar.bz2 -C $DEBUGDIR -T $MANIFEST
rm -f $MANIFEST $BUILDIDS
exit 0
