#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Fprobe event parser error log check
# requires: dynamic_events "f[:[<group>/][<event>]] <func-name>[%return] [<args>]":README

check_error() { # command-with-error-pos-by-^
    ftrace_errlog_check 'trace_fprobe' "$1" 'dynamic_events'
}

case `uname -m` in
x86_64|i[3456]86)
  REG=%ax ;;
aarch64)
  REG=%x0 ;;
*)
  REG=%r0 ;;
esac

check_error 'f^100 vfs_read'		# MAXACT_NO_KPROBE
check_error 'f^1a111 vfs_read'		# BAD_MAXACT
check_error 'f^100000 vfs_read'		# MAXACT_TOO_BIG

check_error 'f ^non_exist_func'		# BAD_PROBE_ADDR (enoent)
check_error 'f ^vfs_read+10'		# BAD_PROBE_ADDR
check_error 'f:^/bar vfs_read'		# NO_GROUP_NAME
check_error 'f:^12345678901234567890123456789012345678901234567890123456789012345/bar vfs_read'	# GROUP_TOO_LONG

check_error 'f:^foo.1/bar vfs_read'	# BAD_GROUP_NAME
check_error 'f:^ vfs_read'		# NO_EVENT_NAME
check_error 'f:foo/^12345678901234567890123456789012345678901234567890123456789012345 vfs_read'	# EVENT_TOO_LONG
check_error 'f:foo/^bar.1 vfs_read'	# BAD_EVENT_NAME

check_error 'f vfs_read ^$stack10000'	# BAD_STACK_NUM

check_error 'f vfs_read ^$arg10000'	# BAD_ARG_NUM

if !grep -q 'kernel return probes support:' README; then
check_error 'f vfs_read $retval ^$arg1' # BAD_VAR
fi
check_error 'f vfs_read ^$none_var'	# BAD_VAR
check_error 'f vfs_read ^'$REG		# BAD_VAR

check_error 'f vfs_read ^@12345678abcde'	# BAD_MEM_ADDR
check_error 'f vfs_read ^@+10'		# FILE_ON_KPROBE

grep -q "imm-value" README && \
check_error 'f vfs_read arg1=\^x'	# BAD_IMM
grep -q "imm-string" README && \
check_error 'f vfs_read arg1=\"abcd^'	# IMMSTR_NO_CLOSE

check_error 'f vfs_read ^+0@0)'		# DEREF_NEED_BRACE
check_error 'f vfs_read ^+0ab1(@0)'	# BAD_DEREF_OFFS
check_error 'f vfs_read +0(+0(@0^)'	# DEREF_OPEN_BRACE

if grep -A1 "fetcharg:" README | grep -q '\$comm' ; then
check_error 'f vfs_read +0(^$comm)'	# COMM_CANT_DEREF
fi

check_error 'f vfs_read ^&1'		# BAD_FETCH_ARG


# We've introduced this limitation with array support
if grep -q ' <type>\\\[<array-size>\\\]' README; then
check_error 'f vfs_read +0(^+0(+0(+0(+0(+0(+0(+0(+0(+0(+0(+0(+0(+0(@0))))))))))))))'	# TOO_MANY_OPS?
check_error 'f vfs_read +0(@11):u8[10^'		# ARRAY_NO_CLOSE
check_error 'f vfs_read +0(@11):u8[10]^a'	# BAD_ARRAY_SUFFIX
check_error 'f vfs_read +0(@11):u8[^10a]'	# BAD_ARRAY_NUM
check_error 'f vfs_read +0(@11):u8[^256]'	# ARRAY_TOO_BIG
fi

check_error 'f vfs_read @11:^unknown_type'	# BAD_TYPE
check_error 'f vfs_read $stack0:^string'	# BAD_STRING
check_error 'f vfs_read @11:^b10@a/16'		# BAD_BITFIELD

check_error 'f vfs_read ^arg123456789012345678901234567890=@11'	# ARG_NAME_TOO_LOG
check_error 'f vfs_read ^=@11'			# NO_ARG_NAME
check_error 'f vfs_read ^var.1=@11'		# BAD_ARG_NAME
check_error 'f vfs_read var1=@11 ^var1=@12'	# USED_ARG_NAME
check_error 'f vfs_read ^+1234567(+1234567(+1234567(+1234567(+1234567(+1234567(@1234))))))'	# ARG_TOO_LONG
check_error 'f vfs_read arg1=^'			# NO_ARG_BODY


# multiprobe errors
if grep -q "Create/append/" README && grep -q "imm-value" README; then
echo "f:fprobes/testevent $FUNCTION_FORK" > dynamic_events
check_error '^f:fprobes/testevent do_exit%return'	# DIFF_PROBE_TYPE

# Explicitly use printf "%s" to not interpret \1
printf "%s" "f:fprobes/testevent $FUNCTION_FORK abcd=\\1" > dynamic_events
check_error "f:fprobes/testevent $FUNCTION_FORK ^bcd=\\1"	# DIFF_ARG_TYPE
check_error "f:fprobes/testevent $FUNCTION_FORK ^abcd=\\1:u8"	# DIFF_ARG_TYPE
check_error "f:fprobes/testevent $FUNCTION_FORK ^abcd=\\\"foo\"" # DIFF_ARG_TYPE
check_error "^f:fprobes/testevent $FUNCTION_FORK abcd=\\1"	# SAME_PROBE
fi

# %return suffix errors
check_error 'f vfs_read^%hoge'		# BAD_ADDR_SUFFIX

# BTF arguments errors
if grep -q "<argname>" README; then
check_error 'f vfs_read args=^$arg*'		# BAD_VAR_ARGS
check_error 'f vfs_read +0(^$arg*)'		# BAD_VAR_ARGS
check_error 'f vfs_read $arg* ^$arg*'		# DOUBLE_ARGS
if !grep -q 'kernel return probes support:' README; then
check_error 'f vfs_read%return ^$arg*'		# NOFENTRY_ARGS
fi
check_error 'f vfs_read ^hoge'			# NO_BTFARG
check_error 'f kfree ^$arg10'			# NO_BTFARG (exceed the number of parameters)
check_error 'f kfree%return ^$retval'		# NO_RETVAL

if grep -qF "<argname>[->field[->field|.field...]]" README ; then
check_error 'f vfs_read%return $retval->^foo'	# NO_PTR_STRCT
check_error 'f vfs_read file->^foo'		# NO_BTF_FIELD
check_error 'f vfs_read file^-.foo'		# BAD_HYPHEN
check_error 'f vfs_read ^file:string'		# BAD_TYPE4STR
fi

else
check_error 'f vfs_read ^$arg*'			# NOSUP_BTFARG
check_error 't kfree ^$arg*'			# NOSUP_BTFARG
fi

exit 0
