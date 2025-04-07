#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

HOSTS=("127.0.0.1" "::1")
PORT=1234
TOTAL_TESTS=0
FAILED_TESTS=0

declare -A TESTS=(
	["SO_RCVPRIORITY"]="-P 2"
	["SO_RCVMARK"]="-M 3"
)

check_result() {
	((TOTAL_TESTS++))
	if [ "$1" -ne 0 ]; then
		((FAILED_TESTS++))
	fi
}

cleanup()
{
	cleanup_ns $NS
}

trap cleanup EXIT

setup_ns NS

for HOST in "${HOSTS[@]}"; do
	PROTOCOL="IPv4"
	if [[ "$HOST" == "::1" ]]; then
		PROTOCOL="IPv6"
	fi
	for test_name in "${!TESTS[@]}"; do
		echo "Running $test_name test, $PROTOCOL"
		arg=${TESTS[$test_name]}

		ip netns exec $NS ./so_rcv_listener $arg $HOST $PORT &
		LISTENER_PID=$!

		sleep 0.5

		if ! ip netns exec $NS ./cmsg_sender $arg $HOST $PORT; then
			echo "Sender failed for $test_name, $PROTOCOL"
			kill "$LISTENER_PID" 2>/dev/null
			wait "$LISTENER_PID"
			check_result 1
			continue
		fi

		wait "$LISTENER_PID"
		LISTENER_EXIT_CODE=$?

		if [ "$LISTENER_EXIT_CODE" -eq 0 ]; then
			echo "Rcv test OK for $test_name, $PROTOCOL"
			check_result 0
		else
			echo "Rcv test FAILED for $test_name, $PROTOCOL"
			check_result 1
		fi
	done
done

if [ "$FAILED_TESTS" -ne 0 ]; then
	echo "FAIL - $FAILED_TESTS/$TOTAL_TESTS tests failed"
	exit ${KSFT_FAIL}
else
	echo "OK - All $TOTAL_TESTS tests passed"
	exit ${KSFT_PASS}
fi
