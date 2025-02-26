#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Test the "inc" interpreter.
#
# See include/uapi/linux/securebits.h, include/uapi/linux/fcntl.h and
# samples/check-exec/inc.c
#
# Copyright © 2024 Microsoft Corporation

set -u -e -o pipefail

EXPECTED_OUTPUT="1"
exec 2>/dev/null

DIR="$(dirname $(readlink -f "$0"))"
source "${DIR}"/../kselftest/ktap_helpers.sh

exec_direct() {
	local expect="$1"
	local script="$2"
	shift 2
	local ret=0
	local out

	# Updates PATH for `env` to execute the `inc` interpreter.
	out="$(PATH="." "$@" "${script}")" || ret=$?

	if [[ ${ret} -ne ${expect} ]]; then
		echo "ERROR: Wrong expectation for direct file execution: ${ret}"
		return 1
	fi
	if [[ ${ret} -eq 0 && "${out}" != "${EXPECTED_OUTPUT}" ]]; then
		echo "ERROR: Wrong output for direct file execution: ${out}"
		return 1
	fi
}

exec_indirect() {
	local expect="$1"
	local script="$2"
	shift 2
	local ret=0
	local out

	# Script passed as argument.
	out="$("$@" ./inc "${script}")" || ret=$?

	if [[ ${ret} -ne ${expect} ]]; then
		echo "ERROR: Wrong expectation for indirect file execution: ${ret}"
		return 1
	fi
	if [[ ${ret} -eq 0 && "${out}" != "${EXPECTED_OUTPUT}" ]]; then
		echo "ERROR: Wrong output for indirect file execution: ${out}"
		return 1
	fi
}

exec_stdin_reg() {
	local expect="$1"
	local script="$2"
	shift 2
	local ret=0
	local out

	# Executing stdin must be allowed if the related file is executable.
	out="$("$@" ./inc -i < "${script}")" || ret=$?

	if [[ ${ret} -ne ${expect} ]]; then
		echo "ERROR: Wrong expectation for stdin regular file execution: ${ret}"
		return 1
	fi
	if [[ ${ret} -eq 0 && "${out}" != "${EXPECTED_OUTPUT}" ]]; then
		echo "ERROR: Wrong output for stdin regular file execution: ${out}"
		return 1
	fi
}

exec_stdin_pipe() {
	local expect="$1"
	shift
	local ret=0
	local out

	# A pipe is not executable.
	out="$(cat script-exec.inc | "$@" ./inc -i)" || ret=$?

	if [[ ${ret} -ne ${expect} ]]; then
		echo "ERROR: Wrong expectation for stdin pipe execution: ${ret}"
		return 1
	fi
}

exec_argument() {
	local expect="$1"
	local ret=0
	shift
	local out

	# Script not coming from a file must not be executed.
	out="$("$@" ./inc -c "$(< script-exec.inc)")" || ret=$?

	if [[ ${ret} -ne ${expect} ]]; then
		echo "ERROR: Wrong expectation for arbitrary argument execution: ${ret}"
		return 1
	fi
	if [[ ${ret} -eq 0 && "${out}" != "${EXPECTED_OUTPUT}" ]]; then
		echo "ERROR: Wrong output for arbitrary argument execution: ${out}"
		return 1
	fi
}

exec_interactive() {
	exec_stdin_pipe "$@"
	exec_argument "$@"
}

ktap_test() {
	ktap_test_result "$*" "$@"
}

ktap_print_header
ktap_set_plan 28

# Without secbit configuration, nothing is changed.

ktap_print_msg "By default, executable scripts are allowed to be interpreted and executed."
ktap_test exec_direct 0 script-exec.inc
ktap_test exec_indirect 0 script-exec.inc

ktap_print_msg "By default, executable stdin is allowed to be interpreted."
ktap_test exec_stdin_reg 0 script-exec.inc

ktap_print_msg "By default, non-executable scripts are allowed to be interpreted, but not directly executed."
# We get 126 because of direct execution by Bash.
ktap_test exec_direct 126 script-noexec.inc
ktap_test exec_indirect 0 script-noexec.inc

ktap_print_msg "By default, non-executable stdin is allowed to be interpreted."
ktap_test exec_stdin_reg 0 script-noexec.inc

ktap_print_msg "By default, interactive commands are allowed to be interpreted."
ktap_test exec_interactive 0

# With only file restriction: protect non-malicious users from inadvertent errors (e.g. python ~/Downloads/*.py).

ktap_print_msg "With -f, executable scripts are allowed to be interpreted and executed."
ktap_test exec_direct 0 script-exec.inc ./set-exec -f --
ktap_test exec_indirect 0 script-exec.inc ./set-exec -f --

ktap_print_msg "With -f, executable stdin is allowed to be interpreted."
ktap_test exec_stdin_reg 0 script-exec.inc ./set-exec -f --

ktap_print_msg "With -f, non-executable scripts are not allowed to be executed nor interpreted."
# Direct execution of non-executable script is alwayse denied by the kernel.
ktap_test exec_direct 1 script-noexec.inc ./set-exec -f --
ktap_test exec_indirect 1 script-noexec.inc ./set-exec -f --

ktap_print_msg "With -f, non-executable stdin is allowed to be interpreted."
ktap_test exec_stdin_reg 0 script-noexec.inc ./set-exec -f --

ktap_print_msg "With -f, interactive commands are allowed to be interpreted."
ktap_test exec_interactive 0 ./set-exec -f --

# With only denied interactive commands: check or monitor script content (e.g. with LSM).

ktap_print_msg "With -i, executable scripts are allowed to be interpreted and executed."
ktap_test exec_direct 0 script-exec.inc ./set-exec -i --
ktap_test exec_indirect 0 script-exec.inc ./set-exec -i --

ktap_print_msg "With -i, executable stdin is allowed to be interpreted."
ktap_test exec_stdin_reg 0 script-exec.inc ./set-exec -i --

ktap_print_msg "With -i, non-executable scripts are allowed to be interpreted, but not directly executed."
# Direct execution of non-executable script is alwayse denied by the kernel.
ktap_test exec_direct 1 script-noexec.inc ./set-exec -i --
ktap_test exec_indirect 0 script-noexec.inc ./set-exec -i --

ktap_print_msg "With -i, non-executable stdin is not allowed to be interpreted."
ktap_test exec_stdin_reg 1 script-noexec.inc ./set-exec -i --

ktap_print_msg "With -i, interactive commands are not allowed to be interpreted."
ktap_test exec_interactive 1 ./set-exec -i --

# With both file restriction and denied interactive commands: only allow executable scripts.

ktap_print_msg "With -fi, executable scripts are allowed to be interpreted and executed."
ktap_test exec_direct 0 script-exec.inc ./set-exec -fi --
ktap_test exec_indirect 0 script-exec.inc ./set-exec -fi --

ktap_print_msg "With -fi, executable stdin is allowed to be interpreted."
ktap_test exec_stdin_reg 0 script-exec.inc ./set-exec -fi --

ktap_print_msg "With -fi, non-executable scripts are not allowed to be interpreted nor executed."
# Direct execution of non-executable script is alwayse denied by the kernel.
ktap_test exec_direct 1 script-noexec.inc ./set-exec -fi --
ktap_test exec_indirect 1 script-noexec.inc ./set-exec -fi --

ktap_print_msg "With -fi, non-executable stdin is not allowed to be interpreted."
ktap_test exec_stdin_reg 1 script-noexec.inc ./set-exec -fi --

ktap_print_msg "With -fi, interactive commands are not allowed to be interpreted."
ktap_test exec_interactive 1 ./set-exec -fi --

ktap_finished
