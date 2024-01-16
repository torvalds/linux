#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-only
# (c) 2010, Tom Zanussi <tzanussi@gmail.com>

# read/write top
#
# Periodically displays system-wide r/w call activity, broken down by
# pid.  If an [interval] arg is specified, the display will be
# refreshed every [interval] seconds.  The default interval is 3
# seconds.

use 5.010000;
use strict;
use warnings;

use lib "$ENV{'PERF_EXEC_PATH'}/scripts/perl/Perf-Trace-Util/lib";
use lib "./Perf-Trace-Util/lib";
use Perf::Trace::Core;
use Perf::Trace::Util;
use POSIX qw/SIGALRM SA_RESTART/;

my $default_interval = 3;
my $nlines = 20;
my $print_thread;
my $print_pending = 0;

my %reads;
my %writes;

my $interval = shift;
if (!$interval) {
    $interval = $default_interval;
}

sub syscalls::sys_exit_read
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm, $common_callchain,
	$nr, $ret) = @_;

    print_check();

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
	$common_pid, $common_comm, $common_callchain,
	$nr, $fd, $buf, $count) = @_;

    print_check();

    $reads{$common_pid}{bytes_requested} += $count;
    $reads{$common_pid}{total_reads}++;
    $reads{$common_pid}{comm} = $common_comm;
}

sub syscalls::sys_exit_write
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm, $common_callchain,
	$nr, $ret) = @_;

    print_check();

    if ($ret <= 0) {
	$writes{$common_pid}{errors}{$ret}++;
    }
}

sub syscalls::sys_enter_write
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm, $common_callchain,
	$nr, $fd, $buf, $count) = @_;

    print_check();

    $writes{$common_pid}{bytes_written} += $count;
    $writes{$common_pid}{total_writes}++;
    $writes{$common_pid}{comm} = $common_comm;
}

sub trace_begin
{
    my $sa = POSIX::SigAction->new(\&set_print_pending);
    $sa->flags(SA_RESTART);
    $sa->safe(1);
    POSIX::sigaction(SIGALRM, $sa) or die "Can't set SIGALRM handler: $!\n";
    alarm 1;
}

sub trace_end
{
    print_unhandled();
    print_totals();
}

sub print_check()
{
    if ($print_pending == 1) {
	$print_pending = 0;
	print_totals();
    }
}

sub set_print_pending()
{
    $print_pending = 1;
    alarm $interval;
}

sub print_totals
{
    my $count;

    $count = 0;

    clear_term();

    printf("\nread counts by pid:\n\n");

    printf("%6s  %20s  %10s  %10s  %10s\n", "pid", "comm",
	   "# reads", "bytes_req", "bytes_read");
    printf("%6s  %-20s  %10s  %10s  %10s\n", "------", "--------------------",
	   "----------", "----------", "----------");

    foreach my $pid (sort { ($reads{$b}{bytes_read} || 0) <=>
			       ($reads{$a}{bytes_read} || 0) } keys %reads) {
	my $comm = $reads{$pid}{comm} || "";
	my $total_reads = $reads{$pid}{total_reads} || 0;
	my $bytes_requested = $reads{$pid}{bytes_requested} || 0;
	my $bytes_read = $reads{$pid}{bytes_read} || 0;

	printf("%6s  %-20s  %10s  %10s  %10s\n", $pid, $comm,
	       $total_reads, $bytes_requested, $bytes_read);

	if (++$count == $nlines) {
	    last;
	}
    }

    $count = 0;

    printf("\nwrite counts by pid:\n\n");

    printf("%6s  %20s  %10s  %13s\n", "pid", "comm",
	   "# writes", "bytes_written");
    printf("%6s  %-20s  %10s  %13s\n", "------", "--------------------",
	   "----------", "-------------");

    foreach my $pid (sort { ($writes{$b}{bytes_written} || 0) <=>
			($writes{$a}{bytes_written} || 0)} keys %writes) {
	my $comm = $writes{$pid}{comm} || "";
	my $total_writes = $writes{$pid}{total_writes} || 0;
	my $bytes_written = $writes{$pid}{bytes_written} || 0;

	printf("%6s  %-20s  %10s  %13s\n", $pid, $comm,
	       $total_writes, $bytes_written);

	if (++$count == $nlines) {
	    last;
	}
    }

    %reads = ();
    %writes = ();
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
