#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Test ftrace direct functions against tracers

rmmod ftrace-direct ||:
if ! modprobe ftrace-direct ; then
  echo "No ftrace-direct sample module - please make CONFIG_SAMPLE_FTRACE_DIRECT=m"
  exit_unresolved;
fi

echo "Let the module run a little"
sleep 1

grep -q "my_direct_func: waking up" trace

rmmod ftrace-direct

test_tracer() {
	tracer=$1

	# tracer -> direct -> no direct > no tracer
	echo $tracer > current_tracer
	modprobe ftrace-direct
	rmmod ftrace-direct
	echo nop > current_tracer

	# tracer -> direct -> no tracer > no direct
	echo $tracer > current_tracer
	modprobe ftrace-direct
	echo nop > current_tracer
	rmmod ftrace-direct

	# direct -> tracer -> no tracer > no direct
	modprobe ftrace-direct
	echo $tracer > current_tracer
	echo nop > current_tracer
	rmmod ftrace-direct

	# direct -> tracer -> no direct > no notracer
	modprobe ftrace-direct
	echo $tracer > current_tracer
	rmmod ftrace-direct
	echo nop > current_tracer
}

for t in `cat available_tracers`; do
	if [ "$t" != "nop" ]; then
		test_tracer $t
	fi
done

echo nop > current_tracer
rmmod ftrace-direct ||:

# Now do the same thing with another direct function registered
echo "Running with another ftrace direct function"

rmmod ftrace-direct-too ||:
modprobe ftrace-direct-too

for t in `cat available_tracers`; do
	if [ "$t" != "nop" ]; then
		test_tracer $t
	fi
done

echo nop > current_tracer
rmmod ftrace-direct ||:
rmmod ftrace-direct-too ||:
