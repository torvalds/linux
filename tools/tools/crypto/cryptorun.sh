#!/bin/sh
#
# A simple test runner for cryptotest
#
# Althought cryptotest itself has a -z mode to test all algorithms at
# a variety of sizes, this script allows us to be more selective.
# Threads and buffer sizes move in powers of two from 1, for threads,
# and 256 for buffer sizes.
#
# e.g.  cryptorun.sh aes 4 512
#
# Test aes with 1, 2 and 4 processes, and at sizes of 256 and 512 bytes.
#
# $FreeBSD$
#

threads=1
size=256
iterations=1000000
crypto="/tank/users/gnn/Repos/svn/FreeBSD.HEAD/tools/tools/crypto/cryptotest"
max_threads=$2
max_size=$3 

while [ "$threads" -le "$max_threads" ]; do
	echo "Testing with $threads processes."
	while [ "$size" -le "$max_size" ]; do
		$crypto -t $threads -a $1 $iterations $size
		size=$(($size * 2))
	done
	size=256
	threads=$(($threads * 2))
done
