#!/bin/bash

TR=/sys/kernel/debug/tracing/
clear_trace() { # reset trace output
    echo > $TR/trace
}

disable_tracing() { # stop trace recording
    echo 0 > $TR/tracing_on
}

enable_tracing() { # start trace recording
    echo 1 > $TR/tracing_on
}

reset_tracer() { # reset the current tracer
    echo nop > $TR/current_tracer
}

disable_tracing
clear_trace

echo "" > $TR/set_ftrace_filter
echo '*printk* *console* *wake* *serial* *lock*' > $TR/set_ftrace_notrace

echo "bpf_prog_test*" > $TR/set_graph_function
echo "" > $TR/set_graph_notrace

echo function_graph > $TR/current_tracer

enable_tracing
./test_progs -t fentry
./test_progs -t fexit
disable_tracing
clear_trace

reset_tracer

exit 0
