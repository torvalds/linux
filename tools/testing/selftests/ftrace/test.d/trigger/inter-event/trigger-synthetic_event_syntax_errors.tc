#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test synthetic_events syntax parser errors
# requires: synthetic_events error_log "' >> synthetic_events":README

check_error() { # command-with-error-pos-by-^
    ftrace_errlog_check 'synthetic_events' "$1" 'synthetic_events'
}

check_dyn_error() { # command-with-error-pos-by-^
    ftrace_errlog_check 'synthetic_events' "$1" 'dynamic_events'
}

check_error 'myevent ^chr arg'			# INVALID_TYPE
check_error 'myevent ^unsigned arg'		# INCOMPLETE_TYPE

check_error 'myevent char ^str]; int v'		# BAD_NAME
check_error '^mye-vent char str[]'		# BAD_NAME
check_error 'myevent char ^st-r[]'		# BAD_NAME

check_error 'myevent char str;^[]'		# INVALID_FIELD
check_error 'myevent char str; ^int'		# INVALID_FIELD

check_error 'myevent char ^str[; int v'		# INVALID_ARRAY_SPEC
check_error 'myevent char ^str[kdjdk]'		# INVALID_ARRAY_SPEC
check_error 'myevent char ^str[257]'		# INVALID_ARRAY_SPEC

check_error '^mye;vent char str[]'		# INVALID_CMD
check_error '^myevent ; char str[]'		# INVALID_CMD
check_error '^myevent; char str[]'		# INVALID_CMD
check_error '^myevent ;char str[]'		# INVALID_CMD
check_error '^; char str[]'			# INVALID_CMD
check_error '^;myevent char str[]'		# INVALID_CMD
check_error '^myevent'				# INVALID_CMD

check_dyn_error '^s:junk/myevent char str['	# INVALID_DYN_CMD

exit 0
