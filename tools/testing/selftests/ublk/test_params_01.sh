#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

run_set_params_success()
{
	local name=$1

	shift

	echo "$name"
	if ! "$UBLK_PROG" set_params -q 1 -d 2 "$@"; then
		echo "$name: SET_PARAMS check failed"
		return 1
	fi
}

run_set_params_failure()
{
	local name=$1

	shift

	echo "$name"
	if "$UBLK_PROG" set_params -q 1 -d 2 "$@"; then
		echo "$name: SET_PARAMS succeeded unexpectedly"
		return 1
	fi
}

run_zoned_set_params_success()
{
	local name=$1

	shift

	echo "$name"
	if ! "$UBLK_PROG" set_params -q 1 -d 2 -u --zoned "$@"; then
		echo "$name: SET_PARAMS check failed"
		return 1
	fi
}

run_zoned_set_params_failure()
{
	local name=$1

	shift

	echo "$name"
	if "$UBLK_PROG" set_params -q 1 -d 2 -u --zoned "$@"; then
		echo "$name: SET_PARAMS succeeded unexpectedly"
		return 1
	fi
}

_prep_test "params" "SET_PARAMS validation"

if [ ! -c /dev/ublk-control ]; then
	_cleanup_test
	_show_result $TID $UBLK_SKIP_CODE
fi

run_set_params_success "valid basic params" ||
	ERR_CODE=1

run_set_params_failure "missing basic params" \
	--param_types none ||
	ERR_CODE=1

run_set_params_failure "logical block larger than physical block" \
	--logical_bs_shift 12 --physical_bs_shift 9 ||
	ERR_CODE=1

run_set_params_failure "too large max sectors" \
	--max_sectors 2049 ||
	ERR_CODE=1

if _have_feature "ZONED" && _have_feature "USER_COPY"; then
	run_zoned_set_params_success "valid zoned params" \
		--param_types basic,zoned ||
		ERR_CODE=1

	run_zoned_set_params_failure "missing zoned params" ||
		ERR_CODE=1

	run_zoned_set_params_failure "non-power-of-2 zone size" \
		--param_types basic,zoned \
		--chunk_sectors 96 --dev_sectors $((96 * 16)) ||
		ERR_CODE=1

	run_zoned_set_params_failure "zero max zone append" \
		--param_types basic,zoned \
		--max_zone_append_sectors 0 ||
		ERR_CODE=1

	run_zoned_set_params_failure "too many open zones" \
		--param_types basic,zoned \
		--dev_sectors $((128 * 4)) --max_open_zones 5 ||
		ERR_CODE=1

	run_zoned_set_params_failure "too many active zones" \
		--param_types basic,zoned \
		--dev_sectors $((128 * 4)) --max_active_zones 5 ||
		ERR_CODE=1
else
	echo "zoned ublk feature unavailable, skip zoned SET_PARAMS cases"
fi

_cleanup_test
_show_result $TID $ERR_CODE
