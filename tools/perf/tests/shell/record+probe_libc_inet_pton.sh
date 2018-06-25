# probe libc's inet_pton & backtrace it with ping

# Installs a probe on libc's inet_pton function, that will use uprobes,
# then use 'perf trace' on a ping to localhost asking for just one packet
# with the a backtrace 3 levels deep, check that it is what we expect.
# This needs no debuginfo package, all is done using the libc ELF symtab
# and the CFI info in the binaries.

# Arnaldo Carvalho de Melo <acme@kernel.org>, 2017

. $(dirname $0)/lib/probe.sh

libc=$(grep -w libc /proc/self/maps | head -1 | sed -r 's/.*[[:space:]](\/.*)/\1/g')
nm -Dg $libc 2>/dev/null | fgrep -q inet_pton || exit 254

trace_libc_inet_pton_backtrace() {
	idx=0
	expected[0]="ping[][0-9 \.:]+probe_libc:inet_pton: \([[:xdigit:]]+\)"
	expected[1]=".*inet_pton\+0x[[:xdigit:]]+[[:space:]]\($libc|inlined\)$"
	case "$(uname -m)" in
	s390x)
		eventattr='call-graph=dwarf,max-stack=4'
		expected[2]="gaih_inet.*\+0x[[:xdigit:]]+[[:space:]]\($libc|inlined\)$"
		expected[3]="(__GI_)?getaddrinfo\+0x[[:xdigit:]]+[[:space:]]\($libc|inlined\)$"
		expected[4]="main\+0x[[:xdigit:]]+[[:space:]]\(.*/bin/ping.*\)$"
		;;
	*)
		eventattr='max-stack=3'
		expected[2]="getaddrinfo\+0x[[:xdigit:]]+[[:space:]]\($libc\)$"
		expected[3]=".*\+0x[[:xdigit:]]+[[:space:]]\(.*/bin/ping.*\)$"
		;;
	esac

	file=`mktemp -u /tmp/perf.data.XXX`

	perf record -e probe_libc:inet_pton/$eventattr/ -o $file ping -6 -c 1 ::1 > /dev/null 2>&1
	perf script -i $file | while read line ; do
		echo $line
		echo "$line" | egrep -q "${expected[$idx]}"
		if [ $? -ne 0 ] ; then
			printf "FAIL: expected backtrace entry %d \"%s\" got \"%s\"\n" $idx "${expected[$idx]}" "$line"
			exit 1
		fi
		let idx+=1
		[ -z "${expected[$idx]}" ] && break
	done

	# If any statements are executed from this point onwards,
	# the exit code of the last among these will be reflected
	# in err below. If the exit code is 0, the test will pass
	# even if the perf script output does not match.
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
