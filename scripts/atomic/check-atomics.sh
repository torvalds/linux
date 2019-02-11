#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Check if atomic headers are up-to-date

ATOMICDIR=$(dirname $0)
ATOMICTBL=${ATOMICDIR}/atomics.tbl
LINUXDIR=${ATOMICDIR}/../..

cat <<EOF |
gen-atomic-instrumented.sh      asm-generic/atomic-instrumented.h
gen-atomic-long.sh              asm-generic/atomic-long.h
gen-atomic-fallback.sh          linux/atomic-fallback.h
EOF
while read script header; do
	if ! (${ATOMICDIR}/${script} ${ATOMICTBL} | diff - ${LINUXDIR}/include/${header} > /dev/null); then
		printf "warning: include/${header} is out-of-date.\n"
	fi
done
