# SPDX-License-Identifier: GPL-2.0-only

clear_trace() { # reset trace output
    echo > trace
}

disable_tracing() { # stop trace recording
    echo 0 > tracing_on
}

enable_tracing() { # start trace recording
    echo 1 > tracing_on
}

reset_tracer() { # reset the current tracer
    echo nop > current_tracer
}

reset_trigger_file() {
    # remove action triggers first
    grep -H ':on[^:]*(' $@ |
    while read line; do
        cmd=`echo $line | cut -f2- -d: | cut -f1 -d"["`
	file=`echo $line | cut -f1 -d:`
	echo "!$cmd" >> $file
    done
    grep -Hv ^# $@ |
    while read line; do
        cmd=`echo $line | cut -f2- -d: | cut -f1 -d"["`
	file=`echo $line | cut -f1 -d:`
	echo "!$cmd" > $file
    done
}

reset_trigger() { # reset all current setting triggers
    if [ -d events/synthetic ]; then
        reset_trigger_file events/synthetic/*/trigger
    fi
    reset_trigger_file events/*/*/trigger
}

reset_events_filter() { # reset all current setting filters
    grep -v ^none events/*/*/filter |
    while read line; do
	echo 0 > `echo $line | cut -f1 -d:`
    done
}

reset_ftrace_filter() { # reset all triggers in set_ftrace_filter
    if [ ! -f set_ftrace_filter ]; then
      return 0
    fi
    echo > set_ftrace_filter
    grep -v '^#' set_ftrace_filter | while read t; do
	tr=`echo $t | cut -d: -f2`
	if [ "$tr" = "" ]; then
	    continue
	fi
	if ! grep -q "$t" set_ftrace_filter; then
		continue;
	fi
	name=`echo $t | cut -d: -f1 | cut -d' ' -f1`
	if [ $tr = "enable_event" -o $tr = "disable_event" ]; then
	    tr=`echo $t | cut -d: -f2-4`
	    limit=`echo $t | cut -d: -f5`
	else
	    tr=`echo $t | cut -d: -f2`
	    limit=`echo $t | cut -d: -f3`
	fi
	if [ "$limit" != "unlimited" ]; then
	    tr="$tr:$limit"
	fi
	echo "!$name:$tr" > set_ftrace_filter
    done
}

disable_events() {
    echo 0 > events/enable
}

clear_synthetic_events() { # reset all current synthetic events
    grep -v ^# synthetic_events |
    while read line; do
        echo "!$line" >> synthetic_events
    done
}

initialize_ftrace() { # Reset ftrace to initial-state
# As the initial state, ftrace will be set to nop tracer,
# no events, no triggers, no filters, no function filters,
# no probes, and tracing on.
    disable_tracing
    reset_tracer
    reset_trigger
    reset_events_filter
    reset_ftrace_filter
    disable_events
    [ -f set_event_pid ] && echo > set_event_pid
    [ -f set_ftrace_pid ] && echo > set_ftrace_pid
    [ -f set_ftrace_notrace ] && echo > set_ftrace_notrace
    [ -f set_graph_function ] && echo | tee set_graph_*
    [ -f stack_trace_filter ] && echo > stack_trace_filter
    [ -f kprobe_events ] && echo > kprobe_events
    [ -f uprobe_events ] && echo > uprobe_events
    [ -f synthetic_events ] && echo > synthetic_events
    [ -f snapshot ] && echo 0 > snapshot
    clear_trace
    enable_tracing
}
