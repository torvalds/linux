#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

MPTCP_LIB_KSFT_TEST="$(basename "${0}" .sh)" \
	"$(dirname "${0}")/mptcp_connect.sh" -m mmap "${@}"
