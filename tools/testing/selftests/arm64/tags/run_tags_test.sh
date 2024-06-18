#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

echo "--------------------"
echo "running tags test"
echo "--------------------"
./tags_test
if [ $? -ne 0 ]; then
	echo "[FAIL]"
else
	echo "[PASS]"
fi
