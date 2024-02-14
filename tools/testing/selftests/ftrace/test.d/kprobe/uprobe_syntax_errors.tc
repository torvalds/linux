#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Uprobe event parser error log check
# requires: uprobe_events error_log

check_error() { # command-with-error-pos-by-^
    ftrace_errlog_check 'trace_uprobe' "$1" 'uprobe_events'
}

check_error 'p ^/non_exist_file:100'	# FILE_NOT_FOUND
check_error 'p ^/sys:100'		# NO_REGULAR_FILE
check_error 'p /bin/sh:^10a'		# BAD_UPROBE_OFFS
check_error 'p /bin/sh:10(^1a)'		# BAD_REFCNT
check_error 'p /bin/sh:10(10^'		# REFCNT_OPEN_BRACE
check_error 'p /bin/sh:10(10)^a'	# BAD_REFCNT_SUFFIX

check_error 'p /bin/sh:10 ^@+ab'	# BAD_FILE_OFFS
check_error 'p /bin/sh:10 ^@symbol'	# SYM_ON_UPROBE

# %return suffix error
if grep -q "place (uprobe): .*%return.*" README; then
check_error 'p /bin/sh:10^%hoge'	# BAD_ADDR_SUFFIX
check_error 'p /bin/sh:10(10)^%return'	# BAD_REFCNT_SUFFIX
fi

exit 0
