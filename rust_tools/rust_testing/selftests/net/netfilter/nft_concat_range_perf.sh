#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#

source lib.sh

[ "$KSFT_MACHINE_SLOW" = yes ] && exit ${ksft_skip}

NFT_CONCAT_RANGE_TESTS="performance" exec ./nft_concat_range.sh
