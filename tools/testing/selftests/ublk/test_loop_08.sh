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

TID=loop_08

_prep_test "loop" "end-to-end integrity"

_create_backfile 0 256M
_create_backfile 1 32M # 256M * (64 integrity bytes / 512 data bytes)
integrity_params="--integrity_capable --integrity_reftag
                  --metadata_size 64 --pi_offset 56 --csum_type t10dif"
dev_id=$(_add_ublk_dev -t loop -u $integrity_params "${UBLK_BACKFILES[@]}")
_check_add_dev $TID $?

# 1M * (64 integrity bytes / 512 data bytes) = 128K
fio_args="--ioengine io_uring --direct 1 --bsrange 512-1M --iodepth 32
          --md_per_io_size 128K --pi_act 0 --pi_chk GUARD,REFTAG,APPTAG
          --filename /dev/ublkb$dev_id"
fio --name fill --rw randwrite $fio_args > /dev/null
err=$?
if [ $err != 0 ]; then
	echo "fio fill failed"
	_show_result $TID $err
fi

fio --name verify --rw randread $fio_args > /dev/null
err=$?
if [ $err != 0 ]; then
	echo "fio verify failed"
	_show_result $TID $err
fi

fio_err=$(mktemp fio_err_XXXXX)

# Overwrite 4-byte reftag at offset 56 + 4 = 60
dd_reftag_args="bs=1 seek=60 count=4 oflag=dsync conv=notrunc status=none"
dd if=/dev/urandom "of=${UBLK_BACKFILES[1]}" $dd_reftag_args
err=$?
if [ $err != 0 ]; then
	echo "dd corrupted_reftag failed"
	rm -f "$fio_err"
	_show_result $TID $err
fi
if fio --name corrupted_reftag --rw randread $fio_args > /dev/null 2> "$fio_err"; then
	echo "fio corrupted_reftag unexpectedly succeeded"
	rm -f "$fio_err"
	_show_result $TID 255
fi
expected_err="REFTAG compare error: LBA: 0 Expected=0, Actual="
if ! grep -q "$expected_err" "$fio_err"; then
	echo "fio corrupted_reftag message not found: $expected_err"
	rm -f "$fio_err"
	_show_result $TID 255
fi
# Reset to 0
dd if=/dev/zero "of=${UBLK_BACKFILES[1]}" $dd_reftag_args
err=$?
if [ $err != 0 ]; then
	echo "dd restore corrupted_reftag failed"
	rm -f "$fio_err"
	_show_result $TID $err
fi

dd_data_args="bs=512 count=1 oflag=direct,dsync conv=notrunc status=none"
dd if=/dev/zero "of=${UBLK_BACKFILES[0]}" $dd_data_args
err=$?
if [ $err != 0 ]; then
	echo "dd corrupted_data failed"
	rm -f "$fio_err"
	_show_result $TID $err
fi
if fio --name corrupted_data --rw randread $fio_args > /dev/null 2> "$fio_err"; then
	echo "fio corrupted_data unexpectedly succeeded"
	rm -f "$fio_err"
	_show_result $TID 255
fi
expected_err="Guard compare error: LBA: 0 Expected=0, Actual="
if ! grep -q "$expected_err" "$fio_err"; then
	echo "fio corrupted_data message not found: $expected_err"
	rm -f "$fio_err"
	_show_result $TID 255
fi

if fio --name bad_apptag --rw randread $fio_args --apptag 0x4321 > /dev/null 2> "$fio_err"; then
	echo "fio bad_apptag unexpectedly succeeded"
	rm -f "$fio_err"
	_show_result $TID 255
fi
expected_err="APPTAG compare error: LBA: [0-9]* Expected=4321, Actual=1234"
if ! grep -q "$expected_err" "$fio_err"; then
	echo "fio bad_apptag message not found: $expected_err"
	rm -f "$fio_err"
	_show_result $TID 255
fi

rm -f "$fio_err"

_cleanup_test
_show_result $TID 0
