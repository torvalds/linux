#!/bin/bash
#
# Here's how to use this:
#
# This script is used to help find functions that are being traced by function
# tracer or function graph tracing that causes the machine to reboot, hang, or
# crash. Here's the steps to take.
#
# First, determine if function tracing is working with a single function:
#
#   (note, if this is a problem with function_graph tracing, then simply
#    replace "function" with "function_graph" in the following steps).
#
#  # cd /sys/kernel/debug/tracing
#  # echo schedule > set_ftrace_filter
#  # echo function > current_tracer
#
# If this works, then we know that something is being traced that shouldn't be.
#
#  # echo nop > current_tracer
#
#  # cat available_filter_functions > ~/full-file
#  # ftrace-bisect ~/full-file ~/test-file ~/non-test-file
#  # cat ~/test-file > set_ftrace_filter
#
# *** Note *** this will take several minutes. Setting multiple functions is
# an O(n^2) operation, and we are dealing with thousands of functions. So go
# have  coffee, talk with your coworkers, read facebook. And eventually, this
# operation will end.
#
#  # echo function > current_tracer
#
# If it crashes, we know that ~/test-file has a bad function.
#
#   Reboot back to test kernel.
#
#     # cd /sys/kernel/debug/tracing
#     # mv ~/test-file ~/full-file
#
# If it didn't crash.
#
#     # echo nop > current_tracer
#     # mv ~/non-test-file ~/full-file
#
# Get rid of the other test file from previous run (or save them off somewhere).
#  # rm -f ~/test-file ~/non-test-file
#
# And start again:
#
#  # ftrace-bisect ~/full-file ~/test-file ~/non-test-file
#
# The good thing is, because this cuts the number of functions in ~/test-file
# by half, the cat of it into set_ftrace_filter takes half as long each
# iteration, so don't talk so much at the water cooler the second time.
#
# Eventually, if you did this correctly, you will get down to the problem
# function, and all we need to do is to notrace it.
#
# The way to figure out if the problem function is bad, just do:
#
#  # echo <problem-function> > set_ftrace_notrace
#  # echo > set_ftrace_filter
#  # echo function > current_tracer
#
# And if it doesn't crash, we are done.
#
# If it does crash, do this again (there's more than one problem function)
# but you need to echo the problem function(s) into set_ftrace_notrace before
# enabling function tracing in the above steps. Or if you can compile the
# kernel, annotate the problem functions with "notrace" and start again.
#


if [ $# -ne 3 ]; then
  echo 'usage: ftrace-bisect full-file test-file  non-test-file'
  exit
fi

full=$1
test=$2
nontest=$3

x=`cat $full | wc -l`
if [ $x -eq 1 ]; then
	echo "There's only one function left, must be the bad one"
	cat $full
	exit 0
fi

let x=$x/2
let y=$x+1

if [ ! -f $full ]; then
	echo "$full does not exist"
	exit 1
fi

if [ -f $test ]; then
	echo -n "$test exists, delete it? [y/N]"
	read a
	if [ "$a" != "y" -a "$a" != "Y" ]; then
		exit 1
	fi
fi

if [ -f $nontest ]; then
	echo -n "$nontest exists, delete it? [y/N]"
	read a
	if [ "$a" != "y" -a "$a" != "Y" ]; then
		exit 1
	fi
fi

sed -ne "1,${x}p" $full > $test
sed -ne "$y,\$p" $full > $nontest
