#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-only
# (c) 2009, Tom Zanussi <tzanussi@gmail.com>

# Display r/w activity for all processes

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

my %reads;
my %writes;

sub syscalls::sys_exit_read
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm,
	$nr, $ret) = @_;

    if ($ret > 0) {
	$reads{$common_pid}{bytes_read} += $ret;
    } else {
	if (!defined ($reads{$common_pid}{bytes_read})) {
	    $reads{$common_pid}{bytes_read} = 0;
	}
	$reads{$common_pid}{errors}{$ret}++;
    }
}

sub syscalls::sys_enter_read
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm,
	$nr, $fd, $buf, $count) = @_;

    $reads{$common_pid}{bytes_requested} += $count;
    $reads{$common_pid}{total_reads}++;
    $reads{$common_pid}{comm} = $common_comm;
}

sub syscalls::sys_exit_write
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm,
	$nr, $ret) = @_;

    if ($ret <= 0) {
	$writes{$common_pid}{errors}{$ret}++;
    }
}

sub syscalls::sys_enter_write
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm,
	$nr, $fd, $buf, $count) = @_;

    $writes{$common_pid}{bytes_written} += $count;
    $writes{$common_pid}{total_writes}++;
    $writes{$common_pid}{comm} = $common_comm;
}

sub trace_end
{
    printf("read counts by pid:\n\n");

    printf("%6s  %20s  %10s  %10s  %10s\n", "pid", "comm",
	   "# reads", "bytes_requested", "bytes_read");
    printf("%6s  %-20s  %10s  %10s  %10s\n", "------", "--------------------",
	   "-----------", "----------", "----------");

    foreach my $pid (sort { ($reads{$b}{bytes_read} || 0) <=>
				($reads{$a}{bytes_read} || 0) } keys %reads) {
	my $comm = $reads{$pid}{comm} || "";
	my $total_reads = $reads{$pid}{total_reads} || 0;
	my $bytes_requested = $reads{$pid}{bytes_requested} || 0;
	my $bytes_read = $reads{$pid}{bytes_read} || 0;

	printf("%6s  %-20s  %10s  %10s  %10s\n", $pid, $comm,
	       $total_reads, $bytes_requested, $bytes_read);
    }

    printf("\nfailed reads by pid:\n\n");

    printf("%6s  %20s  %6s  %10s\n", "pid", "comm", "error #", "# errors");
    printf("%6s  %20s  %6s  %10s\n", "------", "--------------------",
	   "------", "----------");

    my @errcounts = ();

    foreach my $pid (keys %reads) {
	foreach my $error (keys %{$reads{$pid}{errors}}) {
	    my $comm = $reads{$pid}{comm} || "";
	    my $errcount = $reads{$pid}{errors}{$error} || 0;
	    push @errcounts, [$pid, $comm, $error, $errcount];
	}
    }

    @errcounts = sort { $b->[3] <=> $a->[3] } @errcounts;

    for my $i (0 .. $#errcounts) {
	printf("%6d  %-20s  %6d  %10s\n", $errcounts[$i][0],
	       $errcounts[$i][1], $errcounts[$i][2], $errcounts[$i][3]);
    }

    printf("\nwrite counts by pid:\n\n");

    printf("%6s  %20s  %10s  %10s\n", "pid", "comm",
	   "# writes", "bytes_written");
    printf("%6s  %-20s  %10s  %10s\n", "------", "--------------------",
	   "-----------", "----------");

    foreach my $pid (sort { ($writes{$b}{bytes_written} || 0) <=>
			($writes{$a}{bytes_written} || 0)} keys %writes) {
	my $comm = $writes{$pid}{comm} || "";
	my $total_writes = $writes{$pid}{total_writes} || 0;
	my $bytes_written = $writes{$pid}{bytes_written} || 0;

	printf("%6s  %-20s  %10s  %10s\n", $pid, $comm,
	       $total_writes, $bytes_written);
    }

    printf("\nfailed writes by pid:\n\n");

    printf("%6s  %20s  %6s  %10s\n", "pid", "comm", "error #", "# errors");
    printf("%6s  %20s  %6s  %10s\n", "------", "--------------------",
	   "------", "----------");

    @errcounts = ();

    foreach my $pid (keys %writes) {
	foreach my $error (keys %{$writes{$pid}{errors}}) {
	    my $comm = $writes{$pid}{comm} || "";
	    my $errcount = $writes{$pid}{errors}{$error} || 0;
	    push @errcounts, [$pid, $comm, $error, $errcount];
	}
    }

    @errcounts = sort { $b->[3] <=> $a->[3] } @errcounts;

    for my $i (0 .. $#errcounts) {
	printf("%6d  %-20s  %6d  %10s\n", $errcounts[$i][0],
	       $errcounts[$i][1], $errcounts[$i][2], $errcounts[$i][3]);
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
