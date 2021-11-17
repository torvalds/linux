#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

readonly SERVER_MAC="aa:00:00:00:00:02"
readonly CLIENT_MAC="aa:00:00:00:00:01"
readonly TESTS=("data" "ack" "flags" "tcp" "ip" "large")
readonly PROTOS=("ipv4" "ipv6")
dev=""
test="all"
proto="ipv4"

run_test() {
  local server_pid=0
  local exit_code=0
  local protocol=$1
  local test=$2
  local ARGS=( "--${protocol}" "--dmac" "${SERVER_MAC}" \
  "--smac" "${CLIENT_MAC}" "--test" "${test}" "--verbose" )

  setup_ns
  # Each test is run 3 times to deflake, because given the receive timing,
  # not all packets that should coalesce will be considered in the same flow
  # on every try.
  for tries in {1..3}; do
    # Actual test starts here
    ip netns exec server_ns ./gro "${ARGS[@]}" "--rx" "--iface" "server" \
      1>>log.txt &
    server_pid=$!
    sleep 0.5  # to allow for socket init
    ip netns exec client_ns ./gro "${ARGS[@]}" "--iface" "client" \
      1>>log.txt
    wait "${server_pid}"
    exit_code=$?
    if [[ "${exit_code}" -eq 0 ]]; then
        break;
    fi
  done
  cleanup_ns
  echo ${exit_code}
}

run_all_tests() {
  local failed_tests=()
  for proto in "${PROTOS[@]}"; do
    for test in "${TESTS[@]}"; do
      echo "running test ${proto} ${test}" >&2
      exit_code=$(run_test $proto $test)
      if [[ "${exit_code}" -ne 0 ]]; then
        failed_tests+=("${proto}_${test}")
      fi;
    done;
  done
  if [[ ${#failed_tests[@]} -ne 0 ]]; then
    echo "failed tests: ${failed_tests[*]}. \
    Please see log.txt for more logs"
    exit 1
  else
    echo "All Tests Succeeded!"
  fi;
}

usage() {
  echo "Usage: $0 \
  [-i <DEV>] \
  [-t data|ack|flags|tcp|ip|large] \
  [-p <ipv4|ipv6>]" 1>&2;
  exit 1;
}

while getopts "i:t:p:" opt; do
  case "${opt}" in
    i)
      dev="${OPTARG}"
      ;;
    t)
      test="${OPTARG}"
      ;;
    p)
      proto="${OPTARG}"
      ;;
    *)
      usage
      ;;
  esac
done

if [ -n "$dev" ]; then
	source setup_loopback.sh
else
	source setup_veth.sh
fi

setup
trap cleanup EXIT
if [[ "${test}" == "all" ]]; then
  run_all_tests
else
  run_test "${proto}" "${test}"
fi;
