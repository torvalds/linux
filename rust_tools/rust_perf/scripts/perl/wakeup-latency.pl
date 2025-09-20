#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-only
# (c) 2009, Tom Zanussi <tzanussi@gmail.com>

# Display avg/min/max wakeup latency

# The common_* event handler fields are the most useful fields common to
# all events.  They don't necessarily correspond to the 'common_*' fields
# in the status files.  Those fields not available as handler params can
# be retrieved via script functions of the form get_common_*().

use 5.010000;
use strict;
use warnings;

use lib "$ENV{'PERF_EXEC_PATH'}/scripts/perl/Perf-Trace-Util/lib";
use lib "./Perf-Trace-Util/lib";
use Perf::Trace::Core;
use Perf::Trace::Util;

my %last_wakeup;

my $max_wakeup_latency;
my $min_wakeup_latency;
my $total_wakeup_latency = 0;
my $total_wakeups = 0;

sub sched::sched_switch
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm, $common_callchain,
	$prev_comm, $prev_pid, $prev_prio, $prev_state, $next_comm, $next_pid,
	$next_prio) = @_;

    my $wakeup_ts = $last_wakeup{$common_cpu}{ts};
    if ($wakeup_ts) {
	my $switch_ts = nsecs($common_secs, $common_nsecs);
	my $wakeup_latency = $switch_ts - $wakeup_ts;
	if ($wakeup_latency > $max_wakeup_latency) {
	    $max_wakeup_latency = $wakeup_latency;
	}
	if ($wakeup_latency < $min_wakeup_latency) {
	    $min_wakeup_latency = $wakeup_latency;
	}
	$total_wakeup_latency += $wakeup_latency;
	$total_wakeups++;
    }
    $last_wakeup{$common_cpu}{ts} = 0;
}

sub sched::sched_wakeup
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm, $common_callchain,
	$comm, $pid, $prio, $success, $target_cpu) = @_;

    $last_wakeup{$target_cpu}{ts} = nsecs($common_secs, $common_nsecs);
}

sub trace_begin
{
    $min_wakeup_latency = 1000000000;
    $max_wakeup_latency = 0;
}

sub trace_end
{
    printf("wakeup_latency stats:\n\n");
    print "total_wakeups: $total_wakeups\n";
    if ($total_wakeups) {
	printf("avg_wakeup_latency (ns): %u\n",
	       avg($total_wakeup_latency, $total_wakeups));
    } else {
	printf("avg_wakeup_latency (ns): N/A\n");
    }
    printf("min_wakeup_latency (ns): %u\n", $min_wakeup_latency);
    printf("max_wakeup_latency (ns): %u\n", $max_wakeup_latency);

    print_unhandled();
}

my %unhandled;

sub print_unhandled
{
    if ((scalar keys %unhandled) == 0) {
	return;
    }

    print "\nunhandled events:\n\n";

    printf("%-40s  %10s\n", "event", "count");
    printf("%-40s  %10s\n", "----------------------------------------",
	   "-----------");

    foreach my $event_name (keys %unhandled) {
	printf("%-40s  %10d\n", $event_name, $unhandled{$event_name});
    }
}

sub trace_unhandled
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm, $common_callchain) = @_;

    $unhandled{$event_name}++;
}
