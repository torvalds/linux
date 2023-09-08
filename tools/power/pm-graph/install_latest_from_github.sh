#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Script which clones and installs the latest pm-graph
# from http://github.com/intel/pm-graph.git

OUT=`mktemp -d 2>/dev/null`
if [ -z "$OUT" -o ! -e $OUT ]; then
	echo "ERROR: mktemp failed to create folder"
	exit
fi

cleanup() {
	if [ -e "$OUT" ]; then
		cd $OUT
		rm -rf pm-graph
		cd /tmp
		rmdir $OUT
	fi
}

git clone http://github.com/intel/pm-graph.git $OUT/pm-graph
if [ ! -e "$OUT/pm-graph/sleepgraph.py" ]; then
	echo "ERROR: pm-graph github repo failed to clone"
	cleanup
	exit
fi

cd $OUT/pm-graph
echo "INSTALLING PM-GRAPH"
sudo make install
if [ $? -eq 0 ]; then
	echo "INSTALL SUCCESS"
	sleepgraph -v
else
	echo "INSTALL FAILED"
fi
cleanup
