#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright 2021-2022 NXP

REQUIRE_ISOCHRON=${REQUIRE_ISOCHRON:=yes}
REQUIRE_LINUXPTP=${REQUIRE_LINUXPTP:=yes}

# Tunables
UTC_TAI_OFFSET=37
ISOCHRON_CPU=1

if [[ "$REQUIRE_ISOCHRON" = "yes" ]]; then
	# https://github.com/vladimiroltean/tsn-scripts
	# WARNING: isochron versions pre-1.0 are unstable,
	# always use the latest version
	require_command isochron
fi
if [[ "$REQUIRE_LINUXPTP" = "yes" ]]; then
	require_command phc2sys
	require_command ptp4l
fi

phc2sys_start()
{
	local uds_address=$1
	local extra_args=""

	if ! [ -z "${uds_address}" ]; then
		extra_args="${extra_args} -z ${uds_address}"
	fi

	phc2sys_log="$(mktemp)"

	chrt -f 10 phc2sys -m \
		-a -rr \
		--step_threshold 0.00002 \
		--first_step_threshold 0.00002 \
		${extra_args} \
		> "${phc2sys_log}" 2>&1 &
	phc2sys_pid=$!

	echo "phc2sys logs to ${phc2sys_log} and has pid ${phc2sys_pid}"

	sleep 1
}

phc2sys_stop()
{
	{ kill ${phc2sys_pid} && wait ${phc2sys_pid}; } 2> /dev/null
	rm "${phc2sys_log}" 2> /dev/null
}

# Replace space separators from interface list with underscores
if_names_to_label()
{
	local if_name_list="$1"

	echo "${if_name_list/ /_}"
}

ptp4l_start()
{
	local if_names="$1"
	local slave_only=$2
	local uds_address=$3
	local log="ptp4l_log_$(if_names_to_label ${if_names})"
	local pid="ptp4l_pid_$(if_names_to_label ${if_names})"
	local extra_args=""

	for if_name in ${if_names}; do
		extra_args="${extra_args} -i ${if_name}"
	done

	if [ "${slave_only}" = true ]; then
		extra_args="${extra_args} -s"
	fi

	# declare dynamic variables ptp4l_log_${if_name} and ptp4l_pid_${if_name}
	# as global, so that they can be referenced later
	declare -g "${log}=$(mktemp)"

	chrt -f 10 ptp4l -m -2 -P \
		--step_threshold 0.00002 \
		--first_step_threshold 0.00002 \
		--tx_timestamp_timeout 100 \
		--uds_address="${uds_address}" \
		${extra_args} \
		> "${!log}" 2>&1 &
	declare -g "${pid}=$!"

	echo "ptp4l for interfaces ${if_names} logs to ${!log} and has pid ${!pid}"

	sleep 1
}

ptp4l_stop()
{
	local if_names="$1"
	local log="ptp4l_log_$(if_names_to_label ${if_names})"
	local pid="ptp4l_pid_$(if_names_to_label ${if_names})"

	{ kill ${!pid} && wait ${!pid}; } 2> /dev/null
	rm "${!log}" 2> /dev/null
}

cpufreq_max()
{
	local cpu=$1
	local freq="cpu${cpu}_freq"
	local governor="cpu${cpu}_governor"

	# Kernel may be compiled with CONFIG_CPU_FREQ disabled
	if ! [ -d /sys/bus/cpu/devices/cpu${cpu}/cpufreq ]; then
		return
	fi

	# declare dynamic variables cpu${cpu}_freq and cpu${cpu}_governor as
	# global, so they can be referenced later
	declare -g "${freq}=$(cat /sys/bus/cpu/devices/cpu${cpu}/cpufreq/scaling_min_freq)"
	declare -g "${governor}=$(cat /sys/bus/cpu/devices/cpu${cpu}/cpufreq/scaling_governor)"

	cat /sys/bus/cpu/devices/cpu${cpu}/cpufreq/scaling_max_freq > \
		/sys/bus/cpu/devices/cpu${cpu}/cpufreq/scaling_min_freq
	echo -n "performance" > \
		/sys/bus/cpu/devices/cpu${cpu}/cpufreq/scaling_governor
}

cpufreq_restore()
{
	local cpu=$1
	local freq="cpu${cpu}_freq"
	local governor="cpu${cpu}_governor"

	if ! [ -d /sys/bus/cpu/devices/cpu${cpu}/cpufreq ]; then
		return
	fi

	echo "${!freq}" > /sys/bus/cpu/devices/cpu${cpu}/cpufreq/scaling_min_freq
	echo -n "${!governor}" > \
		/sys/bus/cpu/devices/cpu${cpu}/cpufreq/scaling_governor
}

isochron_recv_start()
{
	local if_name=$1
	local uds=$2
	local stats_port=$3
	local extra_args=$4
	local pid="isochron_pid_${stats_port}"

	if ! [ -z "${uds}" ]; then
		extra_args="${extra_args} --unix-domain-socket ${uds}"
	fi

	isochron rcv \
		--interface ${if_name} \
		--sched-priority 98 \
		--sched-fifo \
		--utc-tai-offset ${UTC_TAI_OFFSET} \
		--stats-port ${stats_port} \
		--quiet \
		${extra_args} & \
	declare -g "${pid}=$!"

	sleep 1
}

isochron_recv_stop()
{
	local stats_port=$1
	local pid="isochron_pid_${stats_port}"

	{ kill ${!pid} && wait ${!pid}; } 2> /dev/null
}

isochron_do()
{
	local sender_if_name=$1; shift
	local receiver_if_name=$1; shift
	local sender_uds=$1; shift
	local receiver_uds=$1; shift
	local base_time=$1; shift
	local cycle_time=$1; shift
	local shift_time=$1; shift
	local num_pkts=$1; shift
	local vid=$1; shift
	local priority=$1; shift
	local dst_ip=$1; shift
	local isochron_dat=$1; shift
	local extra_args=""
	local receiver_extra_args=""
	local vrf="$(master_name_get ${sender_if_name})"
	local use_l2="true"

	if ! [ -z "${dst_ip}" ]; then
		use_l2="false"
	fi

	if ! [ -z "${vrf}" ]; then
		dst_ip="${dst_ip}%${vrf}"
	fi

	if ! [ -z "${vid}" ]; then
		vid="--vid=${vid}"
	fi

	if [ -z "${receiver_uds}" ]; then
		extra_args="${extra_args} --omit-remote-sync"
	fi

	if ! [ -z "${shift_time}" ]; then
		extra_args="${extra_args} --shift-time=${shift_time}"
	fi

	if [ "${use_l2}" = "true" ]; then
		extra_args="${extra_args} --l2 --etype=0xdead ${vid}"
		receiver_extra_args="--l2 --etype=0xdead"
	else
		extra_args="${extra_args} --l4 --ip-destination=${dst_ip}"
		receiver_extra_args="--l4"
	fi

	cpufreq_max ${ISOCHRON_CPU}

	isochron_recv_start "${h2}" "${receiver_uds}" 5000 "${receiver_extra_args}"

	isochron send \
		--interface ${sender_if_name} \
		--unix-domain-socket ${sender_uds} \
		--priority ${priority} \
		--base-time ${base_time} \
		--cycle-time ${cycle_time} \
		--num-frames ${num_pkts} \
		--frame-size 64 \
		--txtime \
		--utc-tai-offset ${UTC_TAI_OFFSET} \
		--cpu-mask $((1 << ${ISOCHRON_CPU})) \
		--sched-fifo \
		--sched-priority 98 \
		--client 127.0.0.1 \
		--sync-threshold 5000 \
		--output-file ${isochron_dat} \
		${extra_args} \
		--quiet

	isochron_recv_stop 5000

	cpufreq_restore ${ISOCHRON_CPU}
}
