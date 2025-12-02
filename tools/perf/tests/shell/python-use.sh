#!/bin/bash
# 'import perf' in python
# SPDX-License-Identifier: GPL-2.0
# Just test if we can load the python binding.
set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

MODULE_DIR=$(dirname "$(which perf)")/python

if [ -d "$MODULE_DIR" ]
then
    CMD=$(cat <<EOF
import sys
sys.path.insert(0, '$MODULE_DIR')
import perf
print('success!')
EOF
    )
else
    CMD=$(cat <<EOF
import perf
print('success!')
EOF
    )
fi

echo -e "Testing 'import perf' with:\n$CMD"

if ! echo "$CMD" | $PYTHON | grep -q "success!"
then
  exit 1
fi
exit 0
