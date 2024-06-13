#!/bin/sh
# perf pipe recording and injection test
# SPDX-License-Identifier: GPL-2.0

# skip if there's no compiler
if ! [ -x "$(command -v cc)" ]; then
	echo "failed: no compiler, install gcc"
	exit 2
fi

file=$(mktemp /tmp/test.file.XXXXXX)
data=$(mktemp /tmp/perf.data.XXXXXX)

cat <<EOF | cc -o ${file} -x c -
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

volatile int done;

void sigalrm(int sig) {
	done = 1;
}

__attribute__((noinline)) void noploop(void) {
	while (!done)
		continue;
}

int main(int argc, char *argv[]) {
	int sec = 1;

	if (argc > 1)
		sec = atoi(argv[1]);

	signal(SIGALRM, sigalrm);
	alarm(sec);

	noploop();
	return 0;
}
EOF


if ! perf record -e task-clock:u -o - ${file} | perf report -i - --task | grep test.file; then
	echo "cannot find the test file in the perf report"
	exit 1
fi

if ! perf record -e task-clock:u -o - ${file} | perf inject -b | perf report -i - | grep noploop; then
	echo "cannot find noploop function in pipe #1"
	exit 1
fi

perf record -e task-clock:u -o - ${file} | perf inject -b -o ${data}
if ! perf report -i ${data} | grep noploop; then
	echo "cannot find noploop function in pipe #2"
	exit 1
fi

perf record -e task-clock:u -o ${data} ${file}
if ! perf inject -b -i ${data} | perf report -i - | grep noploop; then
	echo "cannot find noploop function in pipe #3"
	exit 1
fi


rm -f ${file} ${data} ${data}.old
exit 0
