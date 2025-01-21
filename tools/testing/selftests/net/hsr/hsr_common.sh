# SPDX-License-Identifier: GPL-2.0
# Common code for HSR testing scripts

source ../lib.sh
ret=0
ksft_skip=4

# $1: IP address
is_v6()
{
	[ -z "${1##*:*}" ]
}

do_ping()
{
	local netns="$1"
	local connect_addr="$2"
	local ping_args="-q -c 2 -i 0.1"

	if is_v6 "${connect_addr}"; then
		$ipv6 || return 0
		ping_args="${ping_args} -6"
	fi

	ip netns exec ${netns} ping ${ping_args} $connect_addr >/dev/null
	if [ $? -ne 0 ] ; then
		echo "$netns -> $connect_addr connectivity [ FAIL ]" 1>&2
		ret=1
		return 1
	fi

	return 0
}

do_ping_long()
{
	local netns="$1"
	local connect_addr="$2"
	local ping_args="-q -c 10 -i 0.1"

	if is_v6 "${connect_addr}"; then
		$ipv6 || return 0
		ping_args="${ping_args} -6"
	fi

	OUT="$(LANG=C ip netns exec ${netns} ping ${ping_args} $connect_addr | grep received)"
	if [ $? -ne 0 ] ; then
		echo "$netns -> $connect_addr ping [ FAIL ]" 1>&2
		ret=1
		return 1
	fi

	VAL="$(echo $OUT | cut -d' ' -f1-8)"
	SED_VAL="$(echo ${VAL} | sed -r -e 's/([0-9]{2}).*([0-9]{2}).*[[:space:]]([0-9]+%).*/\1 transmitted \2 received \3 loss/')"
	if [ "${SED_VAL}" != "10 transmitted 10 received 0% loss" ]
	then
		echo "$netns -> $connect_addr ping TEST [ FAIL ]"
		echo "Expect to send and receive 10 packets and no duplicates."
		echo "Full message: ${OUT}."
		ret=1
		return 1
	fi

	return 0
}

stop_if_error()
{
	local msg="$1"

	if [ ${ret} -ne 0 ]; then
		echo "FAIL: ${msg}" 1>&2
		exit ${ret}
	fi
}

check_prerequisites()
{
	ip -Version > /dev/null 2>&1
	if [ $? -ne 0 ];then
		echo "SKIP: Could not run test without ip tool"
		exit $ksft_skip
	fi
}
