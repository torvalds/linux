#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Test for mishandling of splice() on pseudofilesystems, which should catch
# bugs like 11990a5bd7e5 ("module: Correctly truncate sysfs sections output")
#
# Since splice fallback was removed as part of the set_fs() rework, many of these
# tests expect to fail now. See https://lore.kernel.org/lkml/202009181443.C2179FB@keescook/
set -e

DIR=$(dirname "$0")

ret=0

expect_success()
{
	title="$1"
	shift

	echo "" >&2
	echo "$title ..." >&2

	set +e
	"$@"
	rc=$?
	set -e

	case "$rc" in
	0)
		echo "ok: $title succeeded" >&2
		;;
	1)
		echo "FAIL: $title should work" >&2
		ret=$(( ret + 1 ))
		;;
	*)
		echo "FAIL: something else went wrong" >&2
		ret=$(( ret + 1 ))
		;;
	esac
}

expect_failure()
{
	title="$1"
	shift

	echo "" >&2
	echo "$title ..." >&2

	set +e
	"$@"
	rc=$?
	set -e

	case "$rc" in
	0)
		echo "FAIL: $title unexpectedly worked" >&2
		ret=$(( ret + 1 ))
		;;
	1)
		echo "ok: $title correctly failed" >&2
		;;
	*)
		echo "FAIL: something else went wrong" >&2
		ret=$(( ret + 1 ))
		;;
	esac
}

do_splice()
{
	filename="$1"
	bytes="$2"
	expected="$3"
	report="$4"

	out=$("$DIR"/splice_read "$filename" "$bytes" | cat)
	if [ "$out" = "$expected" ] ; then
		echo "      matched $report" >&2
		return 0
	else
		echo "      no match: '$out' vs $report" >&2
		return 1
	fi
}

test_splice()
{
	filename="$1"

	echo "  checking $filename ..." >&2

	full=$(cat "$filename")
	rc=$?
	if [ $rc -ne 0 ] ; then
		return 2
	fi

	two=$(echo "$full" | grep -m1 . | cut -c-2)

	# Make sure full splice has the same contents as a standard read.
	echo "    splicing 4096 bytes ..." >&2
	if ! do_splice "$filename" 4096 "$full" "full read" ; then
		return 1
	fi

	# Make sure a partial splice see the first two characters.
	echo "    splicing 2 bytes ..." >&2
	if ! do_splice "$filename" 2 "$two" "'$two'" ; then
		return 1
	fi

	return 0
}

### /proc/$pid/ has no splice interface; these should all fail.
expect_failure "proc_single_open(), seq_read() splice" test_splice /proc/$$/limits
expect_failure "special open(), seq_read() splice" test_splice /proc/$$/comm

### /proc/sys/ has a splice interface; these should all succeed.
expect_success "proc_handler: proc_dointvec_minmax() splice" test_splice /proc/sys/fs/nr_open
expect_success "proc_handler: proc_dostring() splice" test_splice /proc/sys/kernel/modprobe
expect_success "proc_handler: special read splice" test_splice /proc/sys/kernel/version

### /sys/ has no splice interface; these should all fail.
if ! [ -d /sys/module/test_module/sections ] ; then
	expect_success "test_module kernel module load" modprobe test_module
fi
expect_failure "kernfs attr splice" test_splice /sys/module/test_module/coresize
expect_failure "kernfs binattr splice" test_splice /sys/module/test_module/sections/.init.text

exit $ret
