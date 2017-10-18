#!/bin/bash

#
# print Intel PT Power Events and PTWRITE. The intel_pt PMU event needs
# to be specified with appropriate config terms.
#
if ! echo "$@" | grep -q intel_pt ; then
	echo "Options must include the Intel PT event e.g. -e intel_pt/pwr_evt,ptw/"
	echo "and for power events it probably needs to be system wide i.e. -a option"
	echo "For example: -a -e intel_pt/pwr_evt,branch=0/ sleep 1"
	exit 1
fi
perf record $@
