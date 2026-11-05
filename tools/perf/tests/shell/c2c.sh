#!/bin/bash
# perf c2c tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_c2c_test.perf.data.XXXXX)
funcdata=$(mktemp /tmp/__perf_c2c_function_test.perf.data.XXXXX)
perfout=$(mktemp /tmp/__perf_c2c_test.output.XXXXX)

cleanup() {
	rm -f "${perfdata}"
	rm -f "${perfdata}".old
	rm -f "${funcdata}"
	rm -f "${funcdata}".old
	rm -f "${perfout}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

check_c2c_support() {
	# Check if perf c2c record works.
	if ! perf c2c record -o "${perfdata}" -- true > /dev/null 2>&1 ; then
		return 1
	fi
	return 0
}

test_c2c_record_report() {
	echo "c2c record and report test"
	if ! check_c2c_support ; then
		echo "c2c record and report test [Skipped: perf c2c record failed (possibly missing hardware support)]"
		err=2
		return
	fi

	# Run a workload that does some memory operations.
	if ! perf c2c record -o "${perfdata}" -- perf test -w datasym 1 > /dev/null 2>&1 ; then
		echo "c2c record and report test [Skipped: perf c2c record failed during workload]"
		err=2
		return
	fi

	if ! perf c2c report -i "${perfdata}" --stdio > /dev/null 2>&1 ; then
		echo "c2c record and report test [Failed: report failed]"
		err=1
		return
	fi

	if ! perf c2c report -i "${perfdata}" -N > /dev/null 2>&1 ; then
		echo "c2c record and report test [Failed: report -N failed]"
		err=1
		return
	fi

	echo "c2c record and report test [Success]"
}

test_c2c_function_report() {
	echo "c2c function stdio report test"

	if perf c2c report -i "${perfdata}" --function -c pid > "${perfout}" 2>&1 ; then
		echo "c2c function stdio report test [Failed: report accepted missing iaddr]"
		err=1
		return
	fi
	if ! grep -Fq "The function view requires iaddr in --coalesce." "${perfout}" ; then
		echo "c2c function stdio report test [Failed: missing iaddr diagnostic]"
		cat "${perfout}"
		err=1
		return
	fi
	if grep -Fq "Shared Data Functions Table" "${perfout}" ; then
		echo "c2c function stdio report test [Failed: partial report on missing iaddr]"
		cat "${perfout}"
		err=1
		return
	fi

	if perf c2c report -i "${perfdata}" --function --stats > "${perfout}" 2>&1 ; then
		echo "c2c function stdio report test [Failed: accepted conflicting options]"
		err=1
		return
	fi
	if ! grep -Fq -- "--stats and --function cannot be used together." "${perfout}" ; then
		echo "c2c function stdio report test [Failed: missing conflict diagnostic]"
		cat "${perfout}"
		err=1
		return
	fi

	# Exercise contended futex hash-bucket locks so the function view can get
	# reader, writer, and cacheline rows without changing the original test.
	if ! perf c2c record -o "${funcdata}" -- \
		perf bench futex hash -t 4 -r 1 -s > /dev/null 2>&1 ; then
		echo "c2c function stdio report test [Skipped: recording failed]"
		err=2
		return
	fi

	if ! perf c2c report -i "${funcdata}" --function > "${perfout}" 2>&1 ; then
		echo "c2c function stdio report test [Failed: report failed]"
		cat "${perfout}"
		err=1
		return
	fi

	for expected in "Shared Data Functions Table" \
		"#    Cycles    Store" \
		"#         %    count  Function / Contending function / Cacheline" \
		"# .........  ......." ; do
		if ! grep -Fq "${expected}" "${perfout}" ; then
			echo "c2c function stdio report test [Failed: missing '${expected}']"
			cat "${perfout}"
			err=1
			return
		fi
	done
	for unexpected in "Shared Data Cache Line Table" \
		"Shared Cache Line Distribution Pareto" ; do
		if grep -Fq "${unexpected}" "${perfout}" ; then
			echo "c2c function stdio report test [Failed: found '${unexpected}']"
			cat "${perfout}"
			err=1
			return
		fi
	done

	if ! awk '/^# \.+/ { body = 1; next }
		body && !/^#/ && NF { found = 1 }
		END { exit !found }' "${perfout}" ; then
		echo "c2c function stdio report test [Skipped: no contended samples]"
		err=2
		return
	fi

	# The spaces intentionally verify per-level indentation and expanded
	# fold-sign placement without depending on symbol names.
	for expected in \
		'^  - +[0-9]+\.[0-9]+% +[0-9]+  - ' \
		'^ +[0-9]+    - ' \
		'^ +[0-9]+        0x[[:xdigit:]]+$' ; do
		if ! grep -Eq "${expected}" "${perfout}" ; then
			echo "c2c function stdio report test [Failed: missing hierarchy row]"
			echo "  ${expected}"
			cat "${perfout}"
			err=1
			return
		fi
	done

	if awk '/^# \.+/ { body = 1; next }
		body && /[[:blank:]]$/ { found = 1 }
		END { exit !found }' "${perfout}" ; then
		echo "c2c function stdio report test [Failed: trailing whitespace in table body]"
		cat "${perfout}"
		err=1
		return
	fi

	echo "c2c function stdio report test [Success]"
}

test_c2c_record_report
if [ "${err}" -eq 0 ]; then
	test_c2c_function_report
fi
cleanup
exit $err
