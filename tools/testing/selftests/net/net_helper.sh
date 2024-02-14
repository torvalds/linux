#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Helper functions

wait_local_port_listen()
{
	local listener_ns="${1}"
	local port="${2}"
	local protocol="${3}"
	local port_hex
	local i

	port_hex="$(printf "%04X" "${port}")"
	for i in $(seq 10); do
		if ip netns exec "${listener_ns}" cat /proc/net/"${protocol}"* | \
		   grep -q "${port_hex}"; then
			break
		fi
		sleep 0.1
	done
}
