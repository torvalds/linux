#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source "$(dirname $(realpath $0))/../../kselftest/ktap_helpers.sh"

readonly ipv4_args=('--ip_version=ipv4 '
		    '--local_ip=192.168.0.1 '
		    '--gateway_ip=192.168.0.1 '
		    '--netmask_ip=255.255.0.0 '
		    '--remote_ip=192.0.2.1 '
		    '-D CMSG_LEVEL_IP=SOL_IP '
		    '-D CMSG_TYPE_RECVERR=IP_RECVERR ')

readonly ipv6_args=('--ip_version=ipv6 '
		    '--mtu=1520 '
		    '--local_ip=fd3d:0a0b:17d6::1 '
		    '--gateway_ip=fd3d:0a0b:17d6:8888::1 '
		    '--remote_ip=fd3d:fa7b:d17d::1 '
		    '-D CMSG_LEVEL_IP=SOL_IPV6 '
		    '-D CMSG_TYPE_RECVERR=IPV6_RECVERR ')

if [ $# -ne 1 ]; then
	ktap_exit_fail_msg "usage: $0 <script>"
	exit "$KSFT_FAIL"
fi
script="$1"

if [ -z "$(which packetdrill)" ]; then
	ktap_skip_all "packetdrill not found in PATH"
	exit "$KSFT_SKIP"
fi

declare -a optargs
if [[ -n "${KSFT_MACHINE_SLOW}" ]]; then
	optargs+=('--tolerance_usecs=14000')
fi

ktap_print_header
ktap_set_plan 2

unshare -n packetdrill ${ipv4_args[@]} ${optargs[@]} $(basename $script) > /dev/null \
	&& ktap_test_pass "ipv4" || ktap_test_fail "ipv4"
unshare -n packetdrill ${ipv6_args[@]} ${optargs[@]} $(basename $script) > /dev/null \
	&& ktap_test_pass "ipv6" || ktap_test_fail "ipv6"

ktap_finished
