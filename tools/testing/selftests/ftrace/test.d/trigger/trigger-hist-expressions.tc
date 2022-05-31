#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test histogram expression parsing
# requires: set_event events/sched/sched_process_fork/trigger events/sched/sched_process_fork/hist error_log "<var1>=<field|var_ref|numeric_literal>":README


fail() { #msg
    echo $1
    exit_fail
}

test_hist_expr() { # test_name expression expected_val
    trigger="events/sched/sched_process_fork/trigger"

    reset_trigger_file $trigger

    echo "Test hist trigger expressions - $1"

    echo "hist:keys=common_pid:x=$2" > $trigger

    for i in `seq 1 10` ; do ( echo "forked" > /dev/null); done

    actual=`grep -o 'x=[[:digit:]]*' $trigger | awk -F= '{ print $2 }'`

    if [ $actual != $3 ]; then
        fail "Failed hist trigger expression evaluation: Expression: $2 Expected: $3, Actual: $actual"
    fi

    reset_trigger_file $trigger
}

check_error() { # test_name command-with-error-pos-by-^
    trigger="events/sched/sched_process_fork/trigger"

    echo "Test hist trigger expressions - $1"
    ftrace_errlog_check 'hist:sched:sched_process_fork' "$2" $trigger
}

test_hist_expr "Variable assignment" "123" "123"

test_hist_expr "Subtraction not associative" "16-8-4-2" "2"

test_hist_expr "Division not associative" "64/8/4/2" "1"

test_hist_expr "Same precedence operators (+,-) evaluated left to right" "16-8+4+2" "14"

test_hist_expr "Same precedence operators (*,/) evaluated left to right" "4*3/2*2" "12"

test_hist_expr "Multiplication evaluated before addition/subtraction" "4+3*2-2" "8"

test_hist_expr "Division evaluated before addition/subtraction" "4+6/2-2" "5"

# err pos for "too many subexpressions" is dependent on where
# the last subexpression was detected. This can vary depending
# on how the expression tree was generated.
check_error "Too many subexpressions" 'hist:keys=common_pid:x=32+^10*3/20-4'
check_error "Too many subexpressions" 'hist:keys=common_pid:x=^1+2+3+4+5'

check_error "Unary minus not supported in subexpression" 'hist:keys=common_pid:x=-(^1)+2'

check_error "Division by zero" 'hist:keys=common_pid:x=3/^0'

exit 0
