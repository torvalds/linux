#!/bin/sh
# daemon operations
# SPDX-License-Identifier: GPL-2.0

check_line_first()
{
	local line=$1
	local name=$2
	local base=$3
	local output=$4
	local lock=$5
	local up=$6

	local line_name=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $2 }'`
	local line_base=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $3 }'`
	local line_output=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $4 }'`
	local line_lock=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $5 }'`
	local line_up=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $6 }'`

	if [ "${name}" != "${line_name}" ]; then
		echo "FAILED: wrong name"
		error=1
	fi

	if [ "${base}" != "${line_base}" ]; then
		echo "FAILED: wrong base"
		error=1
	fi

	if [ "${output}" != "${line_output}" ]; then
		echo "FAILED: wrong output"
		error=1
	fi

	if [ "${lock}" != "${line_lock}" ]; then
		echo "FAILED: wrong lock"
		error=1
	fi

	if [ "${up}" != "${line_up}" ]; then
		echo "FAILED: wrong up"
		error=1
	fi
}

check_line_other()
{
	local line=$1
	local name=$2
	local run=$3
	local base=$4
	local output=$5
	local control=$6
	local ack=$7
	local up=$8

	local line_name=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $2 }'`
	local line_run=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $3 }'`
	local line_base=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $4 }'`
	local line_output=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $5 }'`
	local line_control=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $6 }'`
	local line_ack=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $7 }'`
	local line_up=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $8 }'`

	if [ "${name}" != "${line_name}" ]; then
		echo "FAILED: wrong name"
		error=1
	fi

	if [ "${run}" != "${line_run}" ]; then
		echo "FAILED: wrong run"
		error=1
	fi

	if [ "${base}" != "${line_base}" ]; then
		echo "FAILED: wrong base"
		error=1
	fi

	if [ "${output}" != "${line_output}" ]; then
		echo "FAILED: wrong output"
		error=1
	fi

	if [ "${control}" != "${line_control}" ]; then
		echo "FAILED: wrong control"
		error=1
	fi

	if [ "${ack}" != "${line_ack}" ]; then
		echo "FAILED: wrong ack"
		error=1
	fi

	if [ "${up}" != "${line_up}" ]; then
		echo "FAILED: wrong up"
		error=1
	fi
}

daemon_start()
{
	local config=$1
	local session=$2

	perf daemon start --config ${config}

	# wait for the session to ping
	local state="FAIL"
	while [ "${state}" != "OK" ]; do
		state=`perf daemon ping --config ${config} --session ${session} | awk '{ print $1 }'`
		sleep 0.05
	done
}

daemon_exit()
{
	local base=$1
	local config=$2

	local line=`perf daemon --config ${config} -x: | head -1`
	local pid=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $1 }'`

	# stop daemon
	perf daemon stop --config ${config}

	# ... and wait for the pid to go away
	tail --pid=${pid} -f /dev/null
}

test_list()
{
	echo "test daemon list"

	local config=$(mktemp /tmp/perf.daemon.config.XXX)
	local base=$(mktemp -d /tmp/perf.daemon.base.XXX)

	cat <<EOF > ${config}
[daemon]
base=BASE

[session-size]
run = -e cpu-clock

[session-time]
run = -e task-clock
EOF

	sed -i -e "s|BASE|${base}|" ${config}

	# start daemon
	daemon_start ${config} size

	# check first line
	# pid:daemon:base:base/output:base/lock
	local line=`perf daemon --config ${config} -x: | head -1`
	check_line_first ${line} daemon ${base} ${base}/output ${base}/lock "0"

	# check 1st session
	# pid:size:-e cpu-clock:base/size:base/size/output:base/size/control:base/size/ack:0
	local line=`perf daemon --config ${config} -x: | head -2 | tail -1`
	check_line_other "${line}" size "-e cpu-clock" ${base}/session-size \
			 ${base}/session-size/output ${base}/session-size/control \
			 ${base}/session-size/ack "0"

	# check 2nd session
	# pid:time:-e task-clock:base/time:base/time/output:base/time/control:base/time/ack:0
	local line=`perf daemon --config ${config} -x: | head -3 | tail -1`
	check_line_other "${line}" time "-e task-clock" ${base}/session-time \
			 ${base}/session-time/output ${base}/session-time/control \
			 ${base}/session-time/ack "0"

	# stop daemon
	daemon_exit ${base} ${config}

	rm -rf ${base}
	rm -f ${config}
}

error=0

test_list

exit ${error}
