#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source _debugfs_common.sh

# Test duplicated context creation
# ================================

if ! echo foo > "$DBGFS/mk_contexts"
then
	echo "context creation failed"
	exit 1
fi

if echo foo > "$DBGFS/mk_contexts"
then
	echo "duplicate context creation success"
	exit 1
fi

if ! echo foo > "$DBGFS/rm_contexts"
then
	echo "context deletion failed"
	exit 1
fi

exit 0
