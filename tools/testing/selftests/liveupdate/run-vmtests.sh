#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

SCRIPT_DIR=$(dirname "$(realpath "$0")")
TEST_RUNNER="$SCRIPT_DIR/vmtest.sh"

TARGETS=("x86_64" "aarch64")

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

passed=0
failed=0
skipped=0

TEST_NAMES=(
	"luo_kexec_simple"
	"luo_multi_session"
	"luo_stress_files"
	"luo_stress_sessions"
)

function usage() {
	cat <<EOF
$0 [-k] [-o output_dir] [-h]
Options:
	-k)	keep logs
	-o)	specify output directory
	-h)	display this help
EOF
}

function pass() {
	echo -e "${GREEN}PASS${NC}"
	((passed++))
}

function skip() {
	echo -e "${YELLOW}SKIP${NC}"
	((skipped++))
}

function fail() {
	echo -e "${RED}FAIL${NC}"
	((failed++))
}

function main() {
	while getopts 'hko:' opt; do
		case $opt in
		k) keep_logs=1 ;;
		o) output_dir=$OPTARG ;;
		h) usage; exit 0 ;;
		*) echo Unknown argument "$opt"
		   usage; exit 1 ;;
		esac
	done

	if [ -n "$keep_logs" ]; then
		if [ -z "$output_dir" ]; then
			output_dir="$SCRIPT_DIR/results_$(date +%Y%m%d_%H%M%S)"
		fi;
		mkdir -p "$output_dir"
	else
		output_dir=$(mktemp -d /tmp/luo.XXXXXXXX)
		trap 'rm -fr "$output_dir"' EXIT
	fi

	for arch in "${TARGETS[@]}"; do
		for test_name in "${TEST_NAMES[@]}"; do
			log="$output_dir/${arch}_${test_name}.log"

			printf "  -> %-8s %-24s ... " "$arch" "$test_name"

			"$TEST_RUNNER" -t "$arch" -T "$test_name" &> "$log"
			exit_code=$?

			case $exit_code in
			0) pass;;
			4) skip;;
			*) fail;;
			esac
		done
		echo ""
	done

	echo "SUMMARY: PASS=$passed SKIP=$skipped FAIL=$failed"
	if [ -n "$keep_logs" ]; then
		echo "Logs: $output_dir"
	fi

	exit $((failed != 0))
}

main "$@"
