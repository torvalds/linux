# probe libc's inet_pton & backtrace it with ping

# Installs a probe on libc's inet_pton function, that will use uprobes,
# then use 'perf trace' on a ping to localhost asking for just one packet
# with the a backtrace 3 levels deep, check that it is what we expect.
# This needs no debuginfo package, all is done using the libc ELF symtab
# and the CFI info in the binaries.

# Arnaldo Carvalho de Melo <acme@kernel.org>, 2017

. $(dirname $0)/lib/probe.sh

libc=$(grep -w libc /proc/self/maps | head -1 | sed -r 's/.*[[:space:]](\/.*)/\1/g')
nm -g $libc 2>/dev/null | fgrep -q inet_pton || exit 254

trace_libc_inet_pton_backtrace() {
	idx=0
	expected[0]="PING.*bytes"
	expected[1]="64 bytes from ::1.*"
	expected[2]=".*ping statistics.*"
	expected[3]=".*packets transmitted.*"
	expected[4]="rtt min.*"
	expected[5]="[0-9]+\.[0-9]+[[:space:]]+probe_libc:inet_pton:\([[:xdigit:]]+\)"
	expected[6]=".*inet_pton[[:space:]]\($libc|inlined\)$"
	case "$(uname -m)" in
	s390x)
		eventattr='call-graph=dwarf'
		expected[7]="gaih_inet.*[[:space:]]\($libc|inlined\)$"
		expected[8]="__GI_getaddrinfo[[:space:]]\($libc|inlined\)$"
		expected[9]="main[[:space:]]\(.*/bin/ping.*\)$"
		expected[10]="__libc_start_main[[:space:]]\($libc\)$"
		expected[11]="_start[[:space:]]\(.*/bin/ping.*\)$"
		;;
	*)
		eventattr='max-stack=3'
		expected[7]="getaddrinfo[[:space:]]\($libc\)$"
		expected[8]=".*\(.*/bin/ping.*\)$"
		;;
	esac

	perf trace --no-syscalls -e probe_libc:inet_pton/$eventattr/ ping -6 -c 1 ::1 2>&1 | grep -v ^$ | while read line ; do
		echo $line
		echo "$line" | egrep -q "${expected[$idx]}"
		if [ $? -ne 0 ] ; then
			printf "FAIL: expected backtrace entry %d \"%s\" got \"%s\"\n" $idx "${expected[$idx]}" "$line"
			exit 1
		fi
		let idx+=1
		[ -z "${expected[$idx]}" ] && break
	done
}

# Check for IPv6 interface existence
ip a sh lo | fgrep -q inet6 || exit 2

skip_if_no_perf_probe && \
perf probe -q $libc inet_pton && \
trace_libc_inet_pton_backtrace
err=$?
rm -f ${file}
perf probe -q -d probe_libc:inet_pton
exit $err
