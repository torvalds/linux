#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

if ! _have_program fio; then
	exit $UBLK_SKIP_CODE
fi

fio_version=$(fio --version)
if [[ "$fio_version" =~ fio-[0-9]+\.[0-9]+$ ]]; then
	echo "Requires development fio version with https://github.com/axboe/fio/pull/1992"
	exit $UBLK_SKIP_CODE
fi

ERR_CODE=0

# Global variables set during device setup
dev_id=""
fio_args=""
fio_err=""

_setup_device() {
	_create_backfile 0 256M
	_create_backfile 1 32M # 256M * (64 integrity bytes / 512 data bytes)

	local integrity_params="--integrity_capable --integrity_reftag
		--metadata_size 64 --pi_offset 56 --csum_type t10dif"
	dev_id=$(_add_ublk_dev -t loop -u $integrity_params "${UBLK_BACKFILES[@]}")
	_check_add_dev "$TID" $?

	# 1M * (64 integrity bytes / 512 data bytes) = 128K
	fio_args="--ioengine io_uring --direct 1 --bsrange 512-1M --iodepth 32
		--md_per_io_size 128K --pi_act 0 --pi_chk GUARD,REFTAG,APPTAG
		--filename /dev/ublkb$dev_id"

	fio_err=$(mktemp "${UBLK_TEST_DIR}"/fio_err_XXXXX)
}

_test_fill_and_verify() {
	fio --name fill --rw randwrite $fio_args > /dev/null
	if [ $? != 0 ]; then
		echo "fio fill failed"
		ERR_CODE=255
		return 1
	fi

	fio --name verify --rw randread $fio_args > /dev/null
	if [ $? != 0 ]; then
		echo "fio verify failed"
		ERR_CODE=255
		return 1
	fi
}

_test_corrupted_reftag() {
	local dd_reftag_args="bs=1 seek=60 count=4 oflag=dsync conv=notrunc status=none"
	local expected_err="REFTAG compare error: LBA: 0 Expected=0, Actual="

	# Overwrite 4-byte reftag at offset 56 + 4 = 60
	dd if=/dev/urandom "of=${UBLK_BACKFILES[1]}" $dd_reftag_args
	if [ $? != 0 ]; then
		echo "dd corrupted_reftag failed"
		ERR_CODE=255
		return 1
	fi

	if fio --name corrupted_reftag --rw randread $fio_args > /dev/null 2> "$fio_err"; then
		echo "fio corrupted_reftag unexpectedly succeeded"
		ERR_CODE=255
		return 1
	fi

	if ! grep -q "$expected_err" "$fio_err"; then
		echo "fio corrupted_reftag message not found: $expected_err"
		ERR_CODE=255
		return 1
	fi

	# Reset to 0
	dd if=/dev/zero "of=${UBLK_BACKFILES[1]}" $dd_reftag_args
	if [ $? != 0 ]; then
		echo "dd restore corrupted_reftag failed"
		ERR_CODE=255
		return 1
	fi
}

_test_corrupted_data() {
	local dd_data_args="bs=512 count=1 oflag=direct,dsync conv=notrunc status=none"
	local expected_err="Guard compare error: LBA: 0 Expected=0, Actual="

	dd if=/dev/zero "of=${UBLK_BACKFILES[0]}" $dd_data_args
	if [ $? != 0 ]; then
		echo "dd corrupted_data failed"
		ERR_CODE=255
		return 1
	fi

	if fio --name corrupted_data --rw randread $fio_args > /dev/null 2> "$fio_err"; then
		echo "fio corrupted_data unexpectedly succeeded"
		ERR_CODE=255
		return 1
	fi

	if ! grep -q "$expected_err" "$fio_err"; then
		echo "fio corrupted_data message not found: $expected_err"
		ERR_CODE=255
		return 1
	fi
}

_test_bad_apptag() {
	local expected_err="APPTAG compare error: LBA: [0-9]* Expected=4321, Actual=1234"

	if fio --name bad_apptag --rw randread $fio_args --apptag 0x4321 > /dev/null 2> "$fio_err"; then
		echo "fio bad_apptag unexpectedly succeeded"
		ERR_CODE=255
		return 1
	fi

	if ! grep -q "$expected_err" "$fio_err"; then
		echo "fio bad_apptag message not found: $expected_err"
		ERR_CODE=255
		return 1
	fi
}

_prep_test "loop" "end-to-end integrity"

_setup_device

_test_fill_and_verify && \
_test_corrupted_reftag && \
_test_corrupted_data && \
_test_bad_apptag

rm -f "$fio_err"

_cleanup_test
_show_result "$TID" $ERR_CODE
