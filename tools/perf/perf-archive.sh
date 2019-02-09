#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# perf archive
# Arnaldo Carvalho de Melo <acme@redhat.com>

PERF_DATA=perf.data
if [ $# -ne 0 ] ; then
	PERF_DATA=$1
fi

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
NOBUILDID=0000000000000000000000000000000000000000

perf buildid-list -i $PERF_DATA --with-hits | grep -v "^$NOBUILDID " > $BUILDIDS
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

tar cjf $PERF_DATA.tar.bz2 -C $PERF_BUILDID_DIR -T $MANIFEST
rm $MANIFEST $BUILDIDS || true
echo -e "Now please run:\n"
echo -e "$ tar xvf $PERF_DATA.tar.bz2 -C ~/.debug\n"
echo "wherever you need to run 'perf report' on."
exit 0
