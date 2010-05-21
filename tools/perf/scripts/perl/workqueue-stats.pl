#!/usr/bin/perl -w
# (c) 2009, Tom Zanussi <tzanussi@gmail.com>
# Licensed under the terms of the GNU GPL License version 2

# Displays workqueue stats
#
# Usage:
#
#   perf record -c 1 -f -a -R -e workqueue:workqueue_creation -e
#     workqueue:workqueue_destruction -e workqueue:workqueue_execution
#     -e workqueue:workqueue_insertion
#
#   perf trace -p -s tools/perf/scripts/perl/workqueue-stats.pl

use 5.010000;
use strict;
use warnings;

use lib "$ENV{'PERF_EXEC_PATH'}/scripts/perl/Perf-Trace-Util/lib";
use lib "./Perf-Trace-Util/lib";
use Perf::Trace::Core;
use Perf::Trace::Util;

my @cpus;

sub workqueue::workqueue_destruction
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm,
	$thread_comm, $thread_pid) = @_;

    $cpus[$common_cpu]{$thread_pid}{destroyed}++;
    $cpus[$common_cpu]{$thread_pid}{comm} = $thread_comm;
}

sub workqueue::workqueue_creation
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm,
	$thread_comm, $thread_pid, $cpu) = @_;

    $cpus[$common_cpu]{$thread_pid}{created}++;
    $cpus[$common_cpu]{$thread_pid}{comm} = $thread_comm;
}

sub workqueue::workqueue_execution
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm,
	$thread_comm, $thread_pid, $func) = @_;

    $cpus[$common_cpu]{$thread_pid}{executed}++;
    $cpus[$common_cpu]{$thread_pid}{comm} = $thread_comm;
}

sub workqueue::workqueue_insertion
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm,
	$thread_comm, $thread_pid, $func) = @_;

    $cpus[$common_cpu]{$thread_pid}{inserted}++;
    $cpus[$common_cpu]{$thread_pid}{comm} = $thread_comm;
}

sub trace_end
{
    print "workqueue work stats:\n\n";
    my $cpu = 0;
    printf("%3s %6s %6s\t%-20s\n", "cpu", "ins", "exec", "name");
    printf("%3s %6s %6s\t%-20s\n", "---", "---", "----", "----");
    foreach my $pidhash (@cpus) {
	while ((my $pid, my $wqhash) = each %$pidhash) {
	    my $ins = $$wqhash{'inserted'} || 0;
	    my $exe = $$wqhash{'executed'} || 0;
	    my $comm = $$wqhash{'comm'} || "";
	    if ($ins || $exe) {
		printf("%3u %6u %6u\t%-20s\n", $cpu, $ins, $exe, $comm);
	    }
	}
	$cpu++;
    }

    $cpu = 0;
    print "\nworkqueue lifecycle stats:\n\n";
    printf("%3s %6s %6s\t%-20s\n", "cpu", "created", "destroyed", "name");
    printf("%3s %6s %6s\t%-20s\n", "---", "-------", "---------", "----");
    foreach my $pidhash (@cpus) {
	while ((my $pid, my $wqhash) = each %$pidhash) {
	    my $created = $$wqhash{'created'} || 0;
	    my $destroyed = $$wqhash{'destroyed'} || 0;
	    my $comm = $$wqhash{'comm'} || "";
	    if ($created || $destroyed) {
		printf("%3u %6u %6u\t%-20s\n", $cpu, $created, $destroyed,
		       $comm);
	    }
	}
	$cpu++;
    }

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
	$common_pid, $common_comm) = @_;

    $unhandled{$event_name}++;
}
