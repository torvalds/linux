#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later

if [[ ! -w /dev/crypto/nx-gzip ]]; then
	echo "Can't access /dev/crypto/nx-gzip, skipping"
	echo "skip: $0"
	exit 4
fi

set -e

function cleanup
{
	rm -f nx-tempfile*
}

trap cleanup EXIT

function test_sizes
{
	local n=$1
	local fname="nx-tempfile.$n"

	for size in 4K 64K 1M 64M
	do
		echo "Testing $size ($n) ..."
		dd if=/dev/urandom of=$fname bs=$size count=1
		./gzfht_test $fname
		./gunz_test ${fname}.nx.gz
	done
}

echo "Doing basic test of different sizes ..."
test_sizes 0

echo "Running tests in parallel ..."
for i in {1..16}
do
	test_sizes $i &
done

wait

echo "OK"

exit 0
