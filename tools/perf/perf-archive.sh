#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# perf archive
# Arnaldo Carvalho de Melo <acme@redhat.com>

PERF_DATA=perf.data
PERF_SYMBOLS=perf.symbols
PERF_ALL=perf.all
ALL=0

while [ $# -gt 0 ] ; do
	if [ $1 == "--all" ]; then
		ALL=1
		shift
	else
		PERF_DATA=$1
		shift
	fi
done

#
# PERF_BUILDID_DIR environment variable set by perf
# path to buildid directory, default to $HOME/.debug
#
if [ -z $PERF_BUILDID_DIR ]; then
	PERF_BUILDID_DIR=~/.debug/
else
        # append / to make substitutions work
        PERF_BUILDID_DIR=$PERF_BUILDID_DIR/
fi

BUILDIDS=$(mktemp /tmp/perf-archive-buildids.XXXXXX)

perf buildid-list -i $PERF_DATA --with-hits | grep -v "^ " > $BUILDIDS
if [ ! -s $BUILDIDS ] ; then
	echo "perf archive: no build-ids found"
	rm $BUILDIDS || true
	exit 1
fi

MANIFEST=$(mktemp /tmp/perf-archive-manifest.XXXXXX)
PERF_BUILDID_LINKDIR=$(readlink -f $PERF_BUILDID_DIR)/

cut -d ' ' -f 1 $BUILDIDS | \
while read build_id ; do
	linkname=$PERF_BUILDID_DIR.build-id/${build_id:0:2}/${build_id:2}
	filename=$(readlink -f $linkname)
	echo ${linkname#$PERF_BUILDID_DIR} >> $MANIFEST
	echo ${filename#$PERF_BUILDID_LINKDIR} >> $MANIFEST
done

if [ $ALL -eq 1 ]; then						# pack perf.data file together with tar containing debug symbols
	HOSTNAME=$(hostname)
	DATE=$(date '+%Y%m%d-%H%M%S')
	tar cjf $PERF_SYMBOLS.tar.bz2 -C $PERF_BUILDID_DIR -T $MANIFEST
	tar cjf	$PERF_ALL-$HOSTNAME-$DATE.tar.bz2 $PERF_DATA $PERF_SYMBOLS.tar.bz2
	rm $PERF_SYMBOLS.tar.bz2 $MANIFEST $BUILDIDS || true

	echo -e "Now please run:\n"
	echo -e "$ tar xvf $PERF_ALL-$HOSTNAME-$DATE.tar.bz2 && tar xvf $PERF_SYMBOLS.tar.bz2 -C ~/.debug\n"
	echo "wherever you need to run 'perf report' on."
else										# pack only the debug symbols
	tar cjf $PERF_DATA.tar.bz2 -C $PERF_BUILDID_DIR -T $MANIFEST
	rm $MANIFEST $BUILDIDS || true

	echo -e "Now please run:\n"
	echo -e "$ tar xvf $PERF_DATA.tar.bz2 -C ~/.debug\n"
	echo "wherever you need to run 'perf report' on."
fi

exit 0
