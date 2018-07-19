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

	expected=`mktemp -u /tmp/expected.XXX`

	echo "ping[][0-9 \.:]+probe_libc:inet_pton: \([[:xdigit:]]+\)" > $expected
	echo ".*inet_pton\+0x[[:xdigit:]]+[[:space:]]\($libc|inlined\)$" >> $expected
	case "$(uname -m)" in
	s390x)
		eventattr='call-graph=dwarf,max-stack=4'
		echo "gaih_inet.*\+0x[[:xdigit:]]+[[:space:]]\($libc|inlined\)$" >> $expected
		echo "(__GI_)?getaddrinfo\+0x[[:xdigit:]]+[[:space:]]\($libc|inlined\)$" >> $expected
		echo "main\+0x[[:xdigit:]]+[[:space:]]\(.*/bin/ping.*\)$" >> $expected
		;;
	*)
		eventattr='max-stack=3'
		echo "getaddrinfo\+0x[[:xdigit:]]+[[:space:]]\($libc\)$" >> $expected
		echo ".*\+0x[[:xdigit:]]+[[:space:]]\(.*/bin/ping.*\)$" >> $expected
		;;
	esac

	perf_data=`mktemp -u /tmp/perf.data.XXX`
	perf_script=`mktemp -u /tmp/perf.script.XXX`
	perf record -e probe_libc:inet_pton/$eventattr/ -o $perf_data ping -6 -c 1 ::1 > /dev/null 2>&1
	perf script -i $perf_data > $perf_script

	exec 3<$perf_script
	exec 4<$expected
	while read line <&3 && read -r pattern <&4; do
		[ -z "$pattern" ] && break
		echo $line
		echo "$line" | egrep -q "$pattern"
		if [ $? -ne 0 ] ; then
			printf "FAIL: expected backtrace entry \"%s\" got \"%s\"\n" "$pattern" "$line"
			exit 1
		fi
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
rm -f ${perf_data} ${perf_script} ${expected}
perf probe -q -d probe_libc:inet_pton
exit $err
