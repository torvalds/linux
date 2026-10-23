#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later OR copyleft-next-0.3.1
# Copyright (C) 2017 Luis R. Rodriguez <mcgrof@kernel.org>

# Reduce noise by disabling "unreachable" check.
# shellcheck disable=SC2317

# This performs a series of tests against the proc sysctl interface.

DIR="$(dirname "$(readlink -f "$0")")"
source "${DIR}"/../kselftest/ktap_helpers.sh

TEST_DRIVER="test_sysctl"

assert_write_rejected() # <file> <value>
{
	local file=$1 val=$2
	if printf '%s' "$val" > "$file" 2>/dev/null; then
		ktap_print_msg "$file: write of '$val' succeeded, expected rejection"
		return 1
	fi
	return 0
}

assert_content() # <file> <want-file>
{
	local file=$1 want_file=$2 got
	got=$(cat "$file")
	if [ "$got" == "$want_file" ]; then
		return 0
	fi
	ktap_print_msg "$file: got '$got', want '$want_file'"
	return 1
}

# compare ignoring whitespaces
assert_content_loose() # <file> <want-file>
{
	local file=$1 want_file=$2
	if echo "$want_file" | diff -q -w -u - "$file" > /dev/null; then
		return 0
	fi
	ktap_print_msg "$file: content does not match '$want_file' (whitespace-insensitive)"
	return 1
}

# For checks where writes must NOT take effect
assert_content_rejected() # <file> <bad>
{
	local file=$1 bad=$2
	if echo "$bad" | diff -q -w -u - "$file" > /dev/null; then
		ktap_print_msg "$file: unexpectedly now holds rejected value '$bad'"
		return 1
	fi
	return 0
}

# proc files get read a page at a time, which can confuse diff resulting in
# incorrect results. Use a temp file to diff.
assert_diff_proc_file() # <file> <want-file>
{
	local file=$1 want_file=$2 tmp
	tmp=$(mktemp)
	cat "$file" > "$tmp"

	if diff -w -q "$tmp" "$want_file" > /dev/null; then
		rm -f "$tmp"
		return 0
	fi
	ktap_print_msg "$file: content does not match expected file $want_file"
	rm -f "$tmp"
	return 1
}

# Verify that an erroneous update fails and does not change the baseline.
assert_no_partial_update() # <file> <baseline> <bad-value>
{
	local file=$1 baseline=$2 bad=$3

	printf '%s' "$baseline" > "$file"
	assert_write_rejected "$file" "$bad" || return 1
	assert_content_loose "$file" "$baseline"
}

assert_grep() # <file> <pattern> - <pattern> must appear in <file>
{
	local file=$1 pattern=$2
	if grep -q "$pattern" "$file"; then
		return 0
	fi
	ktap_print_msg "$file: pattern '$pattern' not found"
	return 1
}

assert_grep_absent() # <file> <pattern> - <pattern> must NOT appear
{
	local file=$1 pattern=$2
	if ! grep -q "$pattern" "$file"; then
		return 0
	fi
	ktap_print_msg "$file: pattern '$pattern' unexpectedly found"
	return 1
}

assert_dmesg_count() # <pattern> <want-count>
{
	local pattern=$1 want=$2 got
	got=$(dmesg | grep -c "$pattern")
	if [ "$got" -eq "$want" ]; then
		return 0
	fi
	ktap_print_msg "dmesg: pattern '$pattern' seen $got times, want $want"
	return 1
}

# write <str> through dd in <bs>-sized chunks
write_in_chunks()  # <file> <str> <bs>
{
	local file=$1 str=$2 bs=$3
	printf '%s' "$str" | dd of="$file" bs="$bs" 2>/dev/null
}

# write <str> through dd in <bs>-sized chunks. seeking <seek> blocks of size
# <bs> into the output. skipping <skip> blocks of size <bs> from the input.
# Unsyncrhonized when skip is not passed.
write_at_offset()  # <file> <str> <bs> <seek> [skip]
{
	local file=$1 str=$2 bs=$3 seek=$4 skip=${5:-}
	if [ -n "$skip" ]; then
		printf '%s' "$str" | dd of="$file" bs="$bs" seek="$seek" skip="$skip" 2>/dev/null
	else
		printf '%s' "$str" | dd of="$file" bs="$bs" seek="$seek" 2>/dev/null
	fi
}

# pad (prefix) <str> with <pad> 'A' characters, then write it all to <file>
# with dd block size <bs> (defaults to <pad>).
write_overlong()  # <file> <pad> <str> [bs]
{
	local file=$1 pad=$2 str=$3 bs=$4 p
	: "${bs:=$pad}"

	# save a string with $pad spaces in $p
	printf -v p '%*s' "$pad" ''
	# replace " " with "A"
	printf '%s%s' "${p// /A}" "$str" | dd of="$file" bs="$bs" 2>/dev/null
}

# pad (prefix) <str> with <pad> ASCII spaces, then write with default block size.
write_padded_digits()  # <file> <pad> <val>
{
	local file=$1 pad=$2 val=$3 p
	printf -v p '%*s' "$pad" ''
	printf '%s%s' "$p" "$val" | dd of="$file" 2>/dev/null
}

test_int_array_writes()
{
	local t="${SYSCTL}/int_0003"
	local limit

	[[ -f "$t" ]] || { RET=$KSFT_SKIP; return; }

	printf '%s' "4 3 2 1" > "$t"
	assert_content_loose "$t" "4 3 2 1" || RET=$KSFT_FAIL

	# Skipping trailing elements leaves them intact
	printf '%s' "100 101" > "$t"
	assert_content_loose "$t" "100 101 2 1" || RET=$KSFT_FAIL

	# Even for an int array, a single write is still capped at MAX_DIGITS
	# (PAGE_SIZE/8) bytes; check right at, and just past, that boundary.
	# Carries on the state from the writes above.
	limit=$((MAX_DIGITS - 1))
	write_padded_digits "$t" "$limit" "9"
	assert_content_loose "$t" "9 101 2 1" || RET=$KSFT_FAIL

	limit=$((MAX_DIGITS))
	write_padded_digits "$t" "$limit" "7"
	assert_content_rejected "$t" "7 101 2 1" || RET=$KSFT_FAIL
}

test_int_array_no_partial_update()
{
	local t="${SYSCTL}/int_0003"

	[[ -f "$t" ]] || { RET=$KSFT_SKIP; return; }

	# bad 3rd element (not a number)
	assert_no_partial_update "$t" "1 2 3 4" "10 20 abc 40" || RET=$KSFT_FAIL

	# bad 3rd element (overflows int)
	assert_no_partial_update "$t" "1 2 3 4" "10 20 $((INT_MAX + 1)) 40" || RET=$KSFT_FAIL

	# bad 1st element
	assert_no_partial_update "$t" "1 2 3 4" "abc 20 30 40" || RET=$KSFT_FAIL
}

# Exercise the write patterns
assert_write_patterns()  # <file> <baseline> <val>
{
	local file=$1 baseline=$2 val=$3 status=0

	printf '%s' "$val" > "$file"
	assert_content "$file" "$val" || status=1
	printf '%s' "$baseline" > "$file"
	assert_content "$file" "$baseline" || status=1

	printf '%s' "$baseline" > "$file"
	write_in_chunks "$file" "$val" 4096
	assert_content "$file" "$val" || status=1

	printf '%s' "$val" > "$file"
	write_at_offset "$file" "$val" 1 1 1
	assert_content "$file" "$val" || status=1

	printf '%s' "$baseline" > "$file"
	write_at_offset "$file" "$val" 20 2
	assert_content_rejected "$file" "$val" || status=1

	printf '%s' "$baseline" > "$file"
	write_overlong "$file" 50 "$val"
	assert_content_rejected "$file" "$val" || status=1

	return "$status"
}

# None of these fit in 32 bits, so writing them to an int/uint sysctl must fail
assert_wideint_writes_rejected()  # <file> <baseline>
{
	local file=$1 baseline=$2 sign mag status=0
	local magnitudes=(
		0x0000000100000000
		0x0000000100000001
		0x00000001ffffffff
		0x0000000180000000
		0x000000017fffffff
		0xffffffff00000000
		0xffffffff00000001
		0xffffffffffffffff
		0xffffffff80000000
		0xffffffff7fffffff
	)

	for sign in '' '-'; do
		for mag in "${magnitudes[@]}"; do
			assert_no_partial_update "$file" "$baseline" "${sign}${mag}" || status=1
		done
	done
	return "$status"
}

# A single write is capped at MAX_DIGITS (PAGE_SIZE/8) bytes; check that
# leading whitespace up to that limit is ignored, and that PAGE_SIZE of
# leading whitespace pushes the value past the limit and fails.
assert_digit_limit()  # <file> <baseline>
{
	local file=$1 baseline=$2 status=0

	printf '%s' "$baseline" > "$file"
	write_padded_digits "$file" "$((MAX_DIGITS - 1))" "3"
	assert_content "$file" "3" || status=1

	printf '%s' "$baseline" > "$file"
	write_padded_digits "$file" "$MAX_DIGITS" "4"
	assert_content_rejected "$file" "4" || status=1

	return "$status"
}

# proc_dointvec()-specific range checks: INT_MAX must be accepted, INT_MAX+1
# must be rejected, and negative values must be accepted.
assert_int_range()  # <file> <baseline>
{
	local file=$1 baseline=$2 status=0

	printf '%s' "$baseline" > "$file"
	printf '%s' "$INT_MAX" > "$file"
	assert_content "$file" "$INT_MAX" || status=1

	assert_no_partial_update "$file" "$baseline" "$((INT_MAX + 1))" || status=1

	printf '%s' "$baseline" > "$file"
	printf '%s' "-3" > "$file" 2>/dev/null
	assert_content "$file" "-3" || status=1

	return "$status"
}

# proc_douintvec()-specific range checks: UINT_MAX must be accepted,
# UINT_MAX+1 must be rejected, and negative values must be rejected too.
assert_uint_range()  # <file> <baseline>
{
	local file=$1 baseline=$2 status=0

	printf '%s' "$baseline" > "$file"
	printf '%s' "$UINT_MAX" > "$file"
	assert_content "$file" "$UINT_MAX" || status=1

	assert_no_partial_update "$file" "$baseline" "$((UINT_MAX + 1))" || status=1
	assert_no_partial_update "$file" "$baseline" "-3" || status=1

	return "$status"
}

test_int_minmax()
{
	local t="${SYSCTL}/int_0001"
	local baseline="60" val="61"

	[[ -f "$t" ]] || { RET=$KSFT_SKIP; return; }

	assert_write_patterns "$t" "$baseline" "$val" || RET=$KSFT_FAIL
	assert_wideint_writes_rejected "$t" "$baseline" || RET=$KSFT_FAIL
	assert_digit_limit "$t" "$baseline" || RET=$KSFT_FAIL
}

test_int_plain()
{
	local t="${SYSCTL}/int_0002"
	local baseline="1" val="2"

	[[ -f "$t" ]] || { RET=$KSFT_SKIP; return; }

	assert_write_patterns "$t" "$baseline" "$val" || RET=$KSFT_FAIL
	assert_wideint_writes_rejected "$t" "$baseline" || RET=$KSFT_FAIL
	assert_digit_limit "$t" "$baseline" || RET=$KSFT_FAIL
	assert_int_range "$t" "$baseline" || RET=$KSFT_FAIL
}

# The jiffies converters report the sign separately from the magnitude, so a
# negative value must survive the round trip through the converter.  Each
# value below is chosen to convert exactly at every CONFIG_HZ.
test_int_jiffies_negative()
{
	local t="${SYSCTL}/int_jiffies"
	local m="${SYSCTL}/int_ms_jiffies"
	local u="${SYSCTL}/int_userhz_jiffies"

	[[ -f "$t" && -f "$m" && -f "$u" ]] || { RET=$KSFT_SKIP; return; }

	printf '%s' "-1" > "$t"
	assert_content "$t" "-1" || RET=$KSFT_FAIL
	printf '%s' "-1000" > "$m"
	assert_content "$m" "-1000" || RET=$KSFT_FAIL
	printf '%s' "-100" > "$u"
	assert_content "$u" "-100" || RET=$KSFT_FAIL

	# The positive path was never broken; keep it covered.
	printf '%s' "1" > "$t"
	assert_content "$t" "1" || RET=$KSFT_FAIL
}

test_uint_plain()
{
	local t="${SYSCTL}/uint_0001"
	local baseline="314" val="315"

	[[ -f "$t" ]] || { RET=$KSFT_SKIP; return; }

	assert_write_patterns "$t" "$baseline" "$val" || RET=$KSFT_FAIL
	assert_wideint_writes_rejected "$t" "$baseline" || RET=$KSFT_FAIL
	assert_digit_limit "$t" "$baseline" || RET=$KSFT_FAIL
	assert_uint_range "$t" "$baseline" || RET=$KSFT_FAIL
}

# proc_dostring()-specific write patterns
assert_string_write_patterns()  # <file> <baseline> <val> <maxlen>
{
	local file=$1 baseline=$2 val=$3 maxlen=$4 status=0

	printf '%s' "$baseline" > "$file"
	write_in_chunks "$file" "$val" 1
	assert_content "$file" "$val" || status=1

	printf '%s' "$val" > "$file"
	write_at_offset "$file" "$val" 1 1
	assert_content_rejected "$file" "$val" || status=1

	# sysctl maxlen is at least $maxlen
	printf '%s' "$baseline" > "$file"
	write_overlong "$file" "$((maxlen - 2))" "B" "$maxlen"
	assert_grep "$file" "B" || status=1

	# keep original string on overflow append
	printf '%s' "$baseline" > "$file"
	write_overlong "$file" "$((maxlen - 1))" "B" "$((maxlen - 1))"
	assert_grep_absent "$file" "B" || status=1

	# sysctl stays NULL terminated on write
	printf '%s' "$baseline" > "$file"
	write_overlong "$file" "$((maxlen - 1))" "B" "$maxlen"
	assert_grep_absent "$file" "B" || status=1

	# sysctl stays NULL terminated on overwrite
	printf '%s' "$baseline" > "$file"
	write_overlong "$file" "$((maxlen - 1))" "BB" "$((maxlen + 1))"
	assert_grep_absent "$file" "B" || status=1

	return "$status"
}

test_string_dostring()
{
	local t="${SYSCTL}/string_0001"
	local baseline="(none)" val="Testing sysctl" maxlen=65

	[[ -f "$t" ]] || { RET=$KSFT_SKIP; return; }

	assert_write_patterns "$t" "$baseline" "$val" || RET=$KSFT_FAIL
	assert_string_write_patterns "$t" "$baseline" "$val" "$maxlen" || RET=$KSFT_FAIL
}

# Emit a random sysctl bit map
random_bitmap_spec()
{
	local length=$((RANDOM % 65000))
	local bit=$((RANDOM % 1024))
	local spec=$bit
	local range_end

	while [ "${#spec}" -le "$length" ]; do
		# Keep entries discontiguous, skip ahead by at least 2.
		bit=$((bit + 2 + RANDOM % 10))
		spec="${spec},${bit}"

		if [ "$((RANDOM % 2))" -eq 1 ]; then
			range_end=$((bit + 1 + RANDOM % 10))
			spec="${spec}-${range_end}"
			bit=$range_end
		fi
	done

	echo -n "$spec"
}

# Do *not* prefix with "test_". All autogenerate bitmap tests call this helper
bitmap_range_check_once()  # <iter>
{
	local t="${SYSCTL}/bitmap_0001"
	local spec_file seed

	[[ -f "$t" ]] || { RET=$KSFT_SKIP; return; }

	seed=$((BITMAP_SEED + $1))
	RANDOM=$seed

	spec_file=$(mktemp)
	random_bitmap_spec > "$spec_file"

	if ! cat "$spec_file" > "$t" 2>/dev/null; then
		ktap_print_msg "$t: write of random bitmap spec rejected (seed $seed)"
		RET=$KSFT_FAIL
	elif ! assert_diff_proc_file "$t" "$spec_file"; then
		ktap_print_msg "$t: readback mismatch (seed $seed)"
		RET=$KSFT_FAIL
	fi

	rm -f "$spec_file"
}

test_unregister_removes_dir()
{
	local t="${SYSCTL}/unregister_error"

	if [[ -d "$t" ]]; then
		ktap_print_msg "$t: directory still exists, expected it to be unregistered"
		RET=$KSFT_FAIL
	fi
}

test_mount_point_error()
{
	local t="${SYSCTL}/mnt/mnt_error"

	if [[ -d "$t" ]]; then
		ktap_print_msg "$t: directory unexpectedly created"
		RET=$KSFT_FAIL
	fi
}

test_empty_dir_registration()
{
	local t="${SYSCTL}/empty_add"

	if [[ ! -d "$t" ]]; then
		ktap_print_msg "$t: directory was not created"
		RET=$KSFT_FAIL
		return
	fi
	if [[ ! -d "$t/empty" ]]; then
		ktap_print_msg "$t/empty: directory was not created"
		RET=$KSFT_FAIL
	fi
}

test_u8_range_check()
{
	local t="${SYSCTL}/u8_valid"

	if [[ ! -f "$t" ]]; then
		ktap_print_msg "$t: file was not created"
		RET=$KSFT_FAIL
		return
	fi

	assert_dmesg_count "u8_over range value" 1 || RET=$KSFT_FAIL
	assert_dmesg_count "u8_under range value" 1 || RET=$KSFT_FAIL
}

test_boot_param_int()
{
	local t="${SYSCTL}/boot_int"
	local orig found

	[[ -f "$t" ]] || { RET=$KSFT_SKIP; return; }

	# Boot cmdline params only apply when test_sysctl is built in, not
	# loaded as a module.
	if [[ -d "$MODULE_DIR" ]]; then
		ktap_print_msg "$t: test only possible if test_sysctl is built-in, not a module"
		RET=$KSFT_SKIP
		return
	fi

	orig=$(cat "$t")
	if [[ "$orig" == "1" ]]; then
		return
	fi

	if [[ ! -f /proc/cmdline ]]; then
		ktap_print_msg "$t: no /proc/cmdline to check for the boot parameter"
		RET=$KSFT_SKIP
		return
	fi

	found=$(grep -c "sysctl[./]debug[./]test_sysctl[./]boot_int=1" /proc/cmdline)
	if [[ "$found" == "1" ]]; then
		ktap_print_msg "$t: kernel param found on cmdline but value is not 1"
		RET=$KSFT_FAIL
		return
	fi

	ktap_print_msg "$t: kernel not booted with sysctl.debug.test_sysctl.boot_int=1"
	RET=$KSFT_SKIP
}

test_sysctl_macro_match()
{
	local t="${SYSCTL}/match_int"

	[[ -f "$t" ]] || { RET=$KSFT_SKIP; return; }

	assert_content "$t" "1" || RET=$KSFT_FAIL
}

run_one()
{
	local name=$1

	# Subshell: a test cannot leak vars/cwd/traps into the next one, and an
	# accidental `exit` inside a test kills only its own subshell.
	( RET=$KSFT_PASS; "$name"; exit $RET )

	case $? in
	"$KSFT_PASS") ktap_test_pass "$name" ;;
	"$KSFT_SKIP") ktap_test_skip "$name" ;;
	*)          ktap_test_fail "$name" ;;
	esac
}

usage()
{
	cat <<-EOF
	Usage: $0 [-r N] [-l] [-t T] [-s N] [-h|--help]

	Runs every auto-discovered test_* function. Setting SYSCTL_TESTS will
	override the default and passing -t will override both env var and the
	default.

	    -r N          repeat selected tests N times (default 1)
	    -l            list discovered tests
	    -t T          space separated tests list. For example "test_1 test_2"
	    -s N          base seed for the randomized bitmap iterations.
	    -h, --help    help

	    SYSCTL_TESTS='test_1 test_2' $0  run only the named tests
	EOF
}

list_tests()
{
	echo "Discovered tests:"
	local t
	for t in $ALL_TESTS; do
		echo "  $t"
	done
}

check_reqs()
{
	local uid
	uid=$(id -u)

	if [ "$uid" -ne 0 ]; then
		ktap_skip_all "must be run as root"
		exit "$KSFT_SKIP"
	fi

	if ! which getconf > /dev/null 2>&1; then
		ktap_skip_all "$0: You need getconf installed"
		exit "$KSFT_SKIP"
	fi

	if ! which diff > /dev/null 2>&1; then
		ktap_skip_all "$0: You need diff installed"
		exit "$KSFT_SKIP"
	fi
}

check_args()  # <repeat> <selected> <seed>
{
	local repeat_arg="$1"
	local selected_arg="$2"
	local seed_arg="$3"
	local t

	if [ -z "$selected_arg" ]; then
		ktap_exit_fail_msg "no test selected"
	fi

	if ! [[ "$repeat_arg" =~ ^[0-9]+$ ]]; then
		ktap_exit_fail_msg "repeat argument '$repeat_arg' is not a number"
	fi

	if ! [[ "$seed_arg" =~ ^[0-9]+$ ]]; then
		ktap_exit_fail_msg "seed argument '$seed_arg' is not a number"
	fi

	for t in $selected_arg; do
		if ! declare -F "$t" > /dev/null; then
			ktap_exit_fail_msg "unknown selected test '$t'"
		fi
	done
}

allow_user_defaults()
{
	if [ -z "${MODULE_DIR}" ]; then
		MODULE_DIR="/sys/module/test_sysctl/"
	fi
	if [ -z "${SYSCTL}" ]; then
		SYSCTL="/proc/sys/debug/test_sysctl"
	fi
	if [ -z "${PROD_SYSCTL}" ]; then
		PROD_SYSCTL="/proc/sys"
	fi
	if [ -z "${WRITES_STRICT}" ]; then
		WRITES_STRICT="${PROD_SYSCTL}/kernel/sysctl_writes_strict"
	fi
	if [ -z "${BITMAP_ITERATIONS}" ]; then
		BITMAP_ITERATIONS=50
	fi
	# Set seed for bitmap random tests
	if [ -z "${BITMAP_SEED}" ]; then
		BITMAP_SEED=$RANDOM
	fi
}

check_production_sysctl_writes_strict()
{
	if [ ! -e "${WRITES_STRICT}" ]; then
		ktap_print_msg "${WRITES_STRICT} missing, skipping strict write check (old kernel?)"
	else
		old_strict=$(cat "${WRITES_STRICT}")
		if [ "$old_strict" != "1" ]; then
			ktap_print_msg "forcing ${WRITES_STRICT} to 1 (was ${old_strict})"
			echo "1" > "${WRITES_STRICT}"
		fi
	fi

	if [ -z "${PAGE_SIZE}" ]; then
		PAGE_SIZE=$(getconf PAGESIZE)
	fi
	if [ -z "${MAX_DIGITS}" ]; then
		MAX_DIGITS=$((PAGE_SIZE / 8))
	fi
	if [ -z "${INT_MAX}" ]; then
		INT_MAX=$(getconf INT_MAX)
	fi
	if [ -z "${UINT_MAX}" ]; then
		UINT_MAX=$(getconf UINT_MAX)
	fi
}

load_req_mod()
{
	if [ ! -d "${SYSCTL}" ]; then
		if ! modprobe -q -n "${TEST_DRIVER}"; then
			ktap_skip_all "module ${TEST_DRIVER} not found. \
				You must set CONFIG_TEST_SYSCTL=m in your kernel"
			exit "$KSFT_SKIP"
		fi
		if ! modprobe "${TEST_DRIVER}"; then
			ktap_exit_fail_msg "modprobe ${TEST_DRIVER} failed"
		fi
	fi
}

# Cleanup only! Do not call exit here or it will silently override KTAP exit status
restore_sysctl_writes_strict()
{
	if [ -n "${old_strict}" ]; then
		echo "${old_strict}" > "${WRITES_STRICT}"
	fi
}

allow_user_defaults

# Each bitmap iteration is one function to generate one KTAP report per iteration
for i in $(seq 1 "$BITMAP_ITERATIONS"); do
	eval "test_bitmap_range_check_$(printf '%02d' "$i")() \
		{ \
			bitmap_range_check_once $i; \
		}"
done

# Discover test_* functions above this line
ALL_TESTS=$(declare -F | awk '{print $3}' | grep '^test_' | sort)

REPEAT=1
while [ $# -gt 0 ]; do
	case "$1" in
	-r )
		shift
		REPEAT=$1
		;;
	-l )
		list_tests
		exit 0
		;;
	-h | --help )
		usage
		exit 0
		;;
	-t )
		shift
		SYSCTL_TESTS="$1"
		;;
	-s )
		shift
		BITMAP_SEED="$1"
		;;
	* )
		usage
		exit 1
		;;
	esac
	shift
done

SELECTED=${SYSCTL_TESTS-$ALL_TESTS}

ktap_print_header
trap restore_sysctl_writes_strict EXIT
check_args "$REPEAT" "$SELECTED" "$BITMAP_SEED"
check_reqs
check_production_sysctl_writes_strict
load_req_mod

ktap_print_msg "bitmap base seed: $BITMAP_SEED (replay with -s $BITMAP_SEED)"

ktap_set_plan "$(( $(wc -w <<< "$SELECTED") * REPEAT ))"

for ((i = 0; i < REPEAT; i++)); do
	for t in $SELECTED; do
		run_one "$t"
	done
done

ktap_finished
