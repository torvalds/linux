#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Event probe event parser error log check
# requires: dynamic_events events/syscalls/sys_enter_openat "<attached-group>.<attached-event> [<args>]":README error_log

check_error() { # command-with-error-pos-by-^
    ftrace_errlog_check 'event_probe' "$1" 'dynamic_events'
}

check_error 'e ^a.'			# NO_EVENT_INFO
check_error 'e ^.b'			# NO_EVENT_INFO
check_error 'e ^a.b'			# BAD_ATTACH_EVENT
check_error 'e syscalls/sys_enter_openat ^foo'	# BAD_ATTACH_ARG
check_error 'e:^/bar syscalls/sys_enter_openat'	# NO_GROUP_NAME
check_error 'e:^12345678901234567890123456789012345678901234567890123456789012345/bar syscalls/sys_enter_openat'	# GROUP_TOO_LONG

check_error 'e:^foo.1/bar syscalls/sys_enter_openat'	# BAD_GROUP_NAME
check_error 'e:^ syscalls/sys_enter_openat'		# NO_EVENT_NAME
check_error 'e:foo/^12345678901234567890123456789012345678901234567890123456789012345 syscalls/sys_enter_openat'	# EVENT_TOO_LONG
check_error 'e:foo/^bar.1 syscalls/sys_enter_openat'	# BAD_EVENT_NAME

check_error 'e:foo/bar syscalls/sys_enter_openat arg=^dfd'	# BAD_FETCH_ARG
check_error 'e:foo/bar syscalls/sys_enter_openat ^arg=$foo'	# BAD_ATTACH_ARG

if grep -q '<attached-group>\.<attached-event>.*\[if <filter>\]' README; then
  check_error 'e:foo/bar syscalls/sys_enter_openat if ^'	# NO_EP_FILTER
fi

exit 0
