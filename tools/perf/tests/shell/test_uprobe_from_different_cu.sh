#!/bin/bash
# test perf probe of function from different CU
# SPDX-License-Identifier: GPL-2.0

set -e

# skip if there's no gcc
if ! [ -x "$(command -v gcc)" ]; then
        echo "failed: no gcc compiler"
        exit 2
fi

temp_dir=$(mktemp -d /tmp/perf-uprobe-different-cu-sh.XXXXXXXXXX)

cleanup()
{
	trap - EXIT TERM INT
	if [[ "${temp_dir}" =~ ^/tmp/perf-uprobe-different-cu-sh.*$ ]]; then
		echo "--- Cleaning up ---"
		perf probe -x ${temp_dir}/testfile -d foo || true
		rm -f "${temp_dir}/"*
		rmdir "${temp_dir}"
	fi
}

trap_cleanup()
{
        cleanup
        exit 1
}

trap trap_cleanup EXIT TERM INT

cat > ${temp_dir}/testfile-foo.h << EOF
struct t
{
  int *p;
  int c;
};

extern int foo (int i, struct t *t);
EOF

cat > ${temp_dir}/testfile-foo.c << EOF
#include "testfile-foo.h"

int
foo (int i, struct t *t)
{
  int j, res = 0;
  for (j = 0; j < i && j < t->c; j++)
    res += t->p[j];

  return res;
}
EOF

cat > ${temp_dir}/testfile-main.c << EOF
#include "testfile-foo.h"

static struct t g;

int
main (int argc, char **argv)
{
  int i;
  int j[argc];
  g.c = argc;
  g.p = j;
  for (i = 0; i < argc; i++)
    j[i] = (int) argv[i][0];
  return foo (3, &g);
}
EOF

gcc -g -Og -flto -c ${temp_dir}/testfile-foo.c -o ${temp_dir}/testfile-foo.o
gcc -g -Og -c ${temp_dir}/testfile-main.c -o ${temp_dir}/testfile-main.o
gcc -g -Og -o ${temp_dir}/testfile ${temp_dir}/testfile-foo.o ${temp_dir}/testfile-main.o

perf probe -x ${temp_dir}/testfile --funcs foo | grep "foo"
perf probe -x ${temp_dir}/testfile foo

cleanup
