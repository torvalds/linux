#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source "$(dirname $(realpath $0))/../../kselftest/ktap_helpers.sh"

declare -A ip_args=(
	[ipv4]="--ip_version=ipv4
		--local_ip=192.168.0.1
		--gateway_ip=192.168.0.1
		--netmask_ip=255.255.0.0
		--remote_ip=192.0.2.1
		-D TFO_COOKIE=3021b9d889017eeb
		-D TFO_COOKIE_ZERO=b7c12350a90dc8f5
		-D CMSG_LEVEL_IP=SOL_IP
		-D CMSG_TYPE_RECVERR=IP_RECVERR"
	[ipv6]="--ip_version=ipv6
		--mtu=1520
		--local_ip=fd3d:0a0b:17d6::1
		--gateway_ip=fd3d:0a0b:17d6:8888::1
		--remote_ip=fd3d:fa7b:d17d::1
		-D TFO_COOKIE=c1d1e9742a47a9bc
		-D TFO_COOKIE_ZERO=82af1a8f9a205c34
		-D CMSG_LEVEL_IP=SOL_IPV6
		-D CMSG_TYPE_RECVERR=IPV6_RECVERR"
)

if [ $# -ne 1 ]; then
	ktap_exit_fail_msg "usage: $0 <script>"
	exit "$KSFT_FAIL"
fi
script="$(basename $1)"

if [ -z "$(which packetdrill)" ]; then
	ktap_skip_all "packetdrill not found in PATH"
	exit "$KSFT_SKIP"
fi

declare -a optargs
failfunc=ktap_test_fail

if [[ -n "${KSFT_MACHINE_SLOW}" ]]; then
	optargs+=('--tolerance_usecs=14000')
	failfunc=ktap_test_xfail
fi

ip_versions=$(grep -E '^--ip_version=' $script | cut -d '=' -f 2)
if [[ -z $ip_versions ]]; then
	ip_versions="ipv4 ipv6"
elif [[ ! "$ip_versions" =~ ^ipv[46]$ ]]; then
	ktap_exit_fail_msg "Too many or unsupported --ip_version: $ip_versions"
	exit "$KSFT_FAIL"
fi

ktap_print_header
ktap_set_plan $(echo $ip_versions | wc -w)

for ip_version in $ip_versions; do
	unshare -n packetdrill ${ip_args[$ip_version]} ${optargs[@]} $script > /dev/null \
		&& ktap_test_pass $ip_version || $failfunc $ip_version
done

ktap_finished
