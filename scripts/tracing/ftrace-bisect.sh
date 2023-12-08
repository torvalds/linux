#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
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
#  # cd /sys/kernel/tracing
#  # echo schedule > set_ftrace_filter
#  # echo function > current_tracer
#
# If this works, then we know that something is being traced that shouldn't be.
#
#  # echo nop > current_tracer
#
# Starting with v5.1 this can be done with numbers, making it much faster:
#
# The old (slow) way, for kernels before v5.1.
#
# [old-way] # cat available_filter_functions > ~/full-file
#
# [old-way] *** Note ***  this process will take several minutes to update the
# [old-way] filters. Setting multiple functions is an O(n^2) operation, and we
# [old-way] are dealing with thousands of functions. So go have coffee, talk
# [old-way] with your coworkers, read facebook. And eventually, this operation
# [old-way] will end.
#
# The new way (using numbers) is an O(n) operation, and usually takes less than a second.
#
# seq `wc -l available_filter_functions | cut -d' ' -f1` > ~/full-file
#
# This will create a sequence of numbers that match the functions in
# available_filter_functions, and when echoing in a number into the
# set_ftrace_filter file, it will enable the corresponding function in
# O(1) time. Making enabling all functions O(n) where n is the number of
# functions to enable.
#
# For either the new or old way, the rest of the operations remain the same.
#
#  # ftrace-bisect ~/full-file ~/test-file ~/non-test-file
#  # cat ~/test-file > set_ftrace_filter
#
#  # echo function > current_tracer
#
# If it crashes, we know that ~/test-file has a bad function.
#
#   Reboot back to test kernel.
#
#     # cd /sys/kernel/tracing
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
