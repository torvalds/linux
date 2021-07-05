#!/bin/bash
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
run = -e cpu-clock -m 1 sleep 10

[session-time]
run = -e task-clock -m 1 sleep 10
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
	check_line_other "${line}" size "-e cpu-clock -m 1 sleep 10" ${base}/session-size \
			 ${base}/session-size/output ${base}/session-size/control \
			 ${base}/session-size/ack "0"

	# check 2nd session
	# pid:time:-e task-clock:base/time:base/time/output:base/time/control:base/time/ack:0
	local line=`perf daemon --config ${config} -x: | head -3 | tail -1`
	check_line_other "${line}" time "-e task-clock -m 1 sleep 10" ${base}/session-time \
			 ${base}/session-time/output ${base}/session-time/control \
			 ${base}/session-time/ack "0"

	# stop daemon
	daemon_exit ${base} ${config}

	rm -rf ${base}
	rm -f ${config}
}

test_reconfig()
{
	echo "test daemon reconfig"

	local config=$(mktemp /tmp/perf.daemon.config.XXX)
	local base=$(mktemp -d /tmp/perf.daemon.base.XXX)

	# prepare config
	cat <<EOF > ${config}
[daemon]
base=BASE

[session-size]
run = -e cpu-clock -m 1 sleep 10

[session-time]
run = -e task-clock -m 1 sleep 10
EOF

	sed -i -e "s|BASE|${base}|" ${config}

	# start daemon
	daemon_start ${config} size

	# check 2nd session
	# pid:time:-e task-clock:base/time:base/time/output:base/time/control:base/time/ack:0
	local line=`perf daemon --config ${config} -x: | head -3 | tail -1`
	check_line_other "${line}" time "-e task-clock -m 1 sleep 10" ${base}/session-time \
			 ${base}/session-time/output ${base}/session-time/control ${base}/session-time/ack "0"
	local pid=`echo "${line}" | awk 'BEGIN { FS = ":" } ; { print $1 }'`

	# prepare new config
	local config_new=${config}.new
	cat <<EOF > ${config_new}
[daemon]
base=BASE

[session-size]
run = -e cpu-clock -m 1 sleep 10

[session-time]
run = -e cpu-clock -m 1 sleep 10
EOF

	# TEST 1 - change config

	sed -i -e "s|BASE|${base}|" ${config_new}
	cp ${config_new} ${config}

	# wait for old session to finish
	tail --pid=${pid} -f /dev/null

	# wait for new one to start
	local state="FAIL"
	while [ "${state}" != "OK" ]; do
		state=`perf daemon ping --config ${config} --session time | awk '{ print $1 }'`
	done

	# check reconfigured 2nd session
	# pid:time:-e task-clock:base/time:base/time/output:base/time/control:base/time/ack:0
	local line=`perf daemon --config ${config} -x: | head -3 | tail -1`
	check_line_other "${line}" time "-e cpu-clock -m 1 sleep 10" ${base}/session-time \
			 ${base}/session-time/output ${base}/session-time/control ${base}/session-time/ack "0"

	# TEST 2 - empty config

	local config_empty=${config}.empty
	cat <<EOF > ${config_empty}
[daemon]
base=BASE
EOF

	# change config
	sed -i -e "s|BASE|${base}|" ${config_empty}
	cp ${config_empty} ${config}

	# wait for sessions to finish
	local state="OK"
	while [ "${state}" != "FAIL" ]; do
		state=`perf daemon ping --config ${config} --session time | awk '{ print $1 }'`
	done

	local state="OK"
	while [ "${state}" != "FAIL" ]; do
		state=`perf daemon ping --config ${config} --session size | awk '{ print $1 }'`
	done

	local one=`perf daemon --config ${config} -x: | wc -l`

	if [ ${one} -ne "1" ]; then
		echo "FAILED: wrong list output"
		error=1
	fi

	# TEST 3 - config again

	cp ${config_new} ${config}

	# wait for size to start
	local state="FAIL"
	while [ "${state}" != "OK" ]; do
		state=`perf daemon ping --config ${config} --session size | awk '{ print $1 }'`
	done

	# wait for time to start
	local state="FAIL"
	while [ "${state}" != "OK" ]; do
		state=`perf daemon ping --config ${config} --session time | awk '{ print $1 }'`
	done

	# stop daemon
	daemon_exit ${base} ${config}

	rm -rf ${base}
	rm -f ${config}
	rm -f ${config_new}
	rm -f ${config_empty}
}

test_stop()
{
	echo "test daemon stop"

	local config=$(mktemp /tmp/perf.daemon.config.XXX)
	local base=$(mktemp -d /tmp/perf.daemon.base.XXX)

	# prepare config
	cat <<EOF > ${config}
[daemon]
base=BASE

[session-size]
run = -e cpu-clock -m 1 sleep 10

[session-time]
run = -e task-clock -m 1 sleep 10
EOF

	sed -i -e "s|BASE|${base}|" ${config}

	# start daemon
	daemon_start ${config} size

	local pid_size=`perf daemon --config ${config} -x: | head -2 | tail -1 | awk 'BEGIN { FS = ":" } ; { print $1 }'`
	local pid_time=`perf daemon --config ${config} -x: | head -3 | tail -1 | awk 'BEGIN { FS = ":" } ; { print $1 }'`

	# check that sessions are running
	if [ ! -d "/proc/${pid_size}" ]; then
		echo "FAILED: session size not up"
	fi

	if [ ! -d "/proc/${pid_time}" ]; then
		echo "FAILED: session time not up"
	fi

	# stop daemon
	daemon_exit ${base} ${config}

	# check that sessions are gone
	if [ -d "/proc/${pid_size}" ]; then
		echo "FAILED: session size still up"
	fi

	if [ -d "/proc/${pid_time}" ]; then
		echo "FAILED: session time still up"
	fi

	rm -rf ${base}
	rm -f ${config}
}

test_signal()
{
	echo "test daemon signal"

	local config=$(mktemp /tmp/perf.daemon.config.XXX)
	local base=$(mktemp -d /tmp/perf.daemon.base.XXX)

	# prepare config
	cat <<EOF > ${config}
[daemon]
base=BASE

[session-test]
run = -e cpu-clock --switch-output -m 1 sleep 10
EOF

	sed -i -e "s|BASE|${base}|" ${config}

	# start daemon
	daemon_start ${config} test

	# send 2 signals
	perf daemon signal --config ${config} --session test
	perf daemon signal --config ${config}

	# stop daemon
	daemon_exit ${base} ${config}

	# count is 2 perf.data for signals and 1 for perf record finished
	count=`ls ${base}/session-test/ | grep perf.data | wc -l`
	if [ ${count} -ne 3 ]; then
		error=1
		echo "FAILED: perf data no generated"
	fi

	rm -rf ${base}
	rm -f ${config}
}

test_ping()
{
	echo "test daemon ping"

	local config=$(mktemp /tmp/perf.daemon.config.XXX)
	local base=$(mktemp -d /tmp/perf.daemon.base.XXX)

	# prepare config
	cat <<EOF > ${config}
[daemon]
base=BASE

[session-size]
run = -e cpu-clock -m 1 sleep 10

[session-time]
run = -e task-clock -m 1 sleep 10
EOF

	sed -i -e "s|BASE|${base}|" ${config}

	# start daemon
	daemon_start ${config} size

	size=`perf daemon ping --config ${config} --session size | awk '{ print $1 }'`
	type=`perf daemon ping --config ${config} --session time | awk '{ print $1 }'`

	if [ ${size} != "OK" -o ${type} != "OK" ]; then
		error=1
		echo "FAILED: daemon ping failed"
	fi

	# stop daemon
	daemon_exit ${base} ${config}

	rm -rf ${base}
	rm -f ${config}
}

test_lock()
{
	echo "test daemon lock"

	local config=$(mktemp /tmp/perf.daemon.config.XXX)
	local base=$(mktemp -d /tmp/perf.daemon.base.XXX)

	# prepare config
	cat <<EOF > ${config}
[daemon]
base=BASE

[session-size]
run = -e cpu-clock -m 1 sleep 10
EOF

	sed -i -e "s|BASE|${base}|" ${config}

	# start daemon
	daemon_start ${config} size

	# start second daemon over the same config/base
	failed=`perf daemon start --config ${config} 2>&1 | awk '{ print $1 }'`

	# check that we failed properly
	if [ ${failed} != "failed:" ]; then
		error=1
		echo "FAILED: daemon lock failed"
	fi

	# stop daemon
	daemon_exit ${base} ${config}

	rm -rf ${base}
	rm -f ${config}
}

error=0

test_list
test_reconfig
test_stop
test_signal
test_ping
test_lock

exit ${error}
