#!/bin/sh
# Check Arm64 callgraphs are complete in fp mode
# SPDX-License-Identifier: GPL-2.0

lscpu | grep -q "aarch64" || exit 2

if ! [ -x "$(command -v cc)" ]; then
	echo "failed: no compiler, install gcc"
	exit 2
fi

PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
TEST_PROGRAM_SOURCE=$(mktemp /tmp/test_program.XXXXX.c)
TEST_PROGRAM=$(mktemp /tmp/test_program.XXXXX)

cleanup_files()
{
	rm -f $PERF_DATA
	rm -f $TEST_PROGRAM_SOURCE
	rm -f $TEST_PROGRAM
}

trap cleanup_files exit term int

cat << EOF > $TEST_PROGRAM_SOURCE
int a = 0;
void leaf(void) {
  for (;;)
    a += a;
}
void parent(void) {
  leaf();
}
int main(void) {
  parent();
  return 0;
}
EOF

echo " + Compiling test program ($TEST_PROGRAM)..."

CFLAGS="-g -O0 -fno-inline -fno-omit-frame-pointer"
cc $CFLAGS $TEST_PROGRAM_SOURCE -o $TEST_PROGRAM || exit 1

# Add a 1 second delay to skip samples that are not in the leaf() function
perf record -o $PERF_DATA --call-graph fp -e cycles//u -D 1000 -- $TEST_PROGRAM 2> /dev/null &
PID=$!

echo " + Recording (PID=$PID)..."
sleep 2
echo " + Stopping perf-record..."

kill $PID
wait $PID

# expected perf-script output:
#
# program
# 	728 leaf
# 	753 parent
# 	76c main
# ...

perf script -i $PERF_DATA -F comm,ip,sym | head -n4
perf script -i $PERF_DATA -F comm,ip,sym | head -n4 | \
	awk '{ if ($2 != "") sym[i++] = $2 } END { if (sym[0] != "leaf" ||
						       sym[1] != "parent" ||
						       sym[2] != "main") exit 1 }'
