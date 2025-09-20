#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A simple program for generating traffic for the toeplitz test.
#
# This program sends packets periodically for, conservatively, 20 seconds. The
# intent is for the calling program to kill this program once it is no longer
# needed, rather than waiting for the 20 second expiration.

send_traffic() {
	expiration=$((SECONDS+20))
	while [[ "${SECONDS}" -lt "${expiration}" ]]
	do
		if [[ "${PROTO}" == "-u" ]]; then
			echo "msg $i" | nc "${IPVER}" -u -w 0 "${ADDR}" "${PORT}"
		else
			echo "msg $i" | nc "${IPVER}" -w 0 "${ADDR}" "${PORT}"
		fi
		sleep 0.001
	done
}

PROTO=$1
IPVER=$2
ADDR=$3
PORT=$4

send_traffic
