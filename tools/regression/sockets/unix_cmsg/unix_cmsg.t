#!/bin/sh
# $FreeBSD$

cd `dirname $0`
cmd="./`basename $0 .t`"

make ${cmd} >/dev/null 2>&1

IFS=
n=0

run()
{
	result=`${cmd} -t $2 $3 ${5%% *} 2>&1`
	if [ $? -ne 0 ]; then
		echo -n "not "
	fi
	echo "ok $1 - $4 ${5#* }"
	echo ${result} | grep -E "SERVER|CLIENT" | while read line; do
		echo "# ${line}"
	done
}

echo "1..47"

for t1 in \
	"1 Sending, receiving cmsgcred" \
	"4 Sending cmsgcred, receiving sockcred" \
	"5 Sending, receiving timeval" \
	"6 Sending, receiving bintime" \
	"7 Check cmsghdr.cmsg_len"
do
	for t2 in \
		"0 " \
		"1 (no data)" \
		"2 (no array)" \
		"3 (no data, array)"
	do
		n=$((n + 1))
		run ${n} stream "-z ${t2%% *}" STREAM "${t1} ${t2#* }"
	done
done

for t1 in \
	"2 Receiving sockcred (listening socket)" \
	"3 Receiving sockcred (accepted socket)"
do
	for t2 in \
		"0 " \
		"1 (no data)"
	do
		n=$((n + 1))
		run ${n} stream "-z ${t2%% *}" STREAM "${t1} ${t2#* }"
	done
done

n=$((n + 1))
run ${n} stream "-z 0" STREAM "8 Check LOCAL_PEERCRED socket option"

for t1 in \
	"1 Sending, receiving cmsgcred" \
	"3 Sending cmsgcred, receiving sockcred" \
	"4 Sending, receiving timeval" \
	"5 Sending, receiving bintime" \
	"6 Check cmsghdr.cmsg_len"
do
	for t2 in \
		"0 " \
		"1 (no data)" \
		"2 (no array)" \
		"3 (no data, array)"
	do
		n=$((n + 1))
		run ${n} dgram "-z ${t2%% *}" DGRAM "${t1} ${t2#* }"
	done
done

for t1 in \
	"2 Receiving sockcred"
do
	for t2 in \
		"0 " \
		"1 (no data)"
	do
		n=$((n + 1))
		run ${n} dgram "-z ${t2%% *}" DGRAM "${t1} ${t2#* }"
	done
done
