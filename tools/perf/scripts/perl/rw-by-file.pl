#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-only
# (c) 2009, Tom Zanussi <tzanussi@gmail.com>

# Display r/w activity for files read/written to for a given program

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

my $usage = "perf script -s rw-by-file.pl <comm>\n";

my $for_comm = shift or die $usage;

my %reads;
my %writes;

sub syscalls::sys_enter_read
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm, $nr, $fd, $buf, $count) = @_;

    if ($common_comm eq $for_comm) {
	$reads{$fd}{bytes_requested} += $count;
	$reads{$fd}{total_reads}++;
    }
}

sub syscalls::sys_enter_write
{
    my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	$common_pid, $common_comm, $nr, $fd, $buf, $count) = @_;

    if ($common_comm eq $for_comm) {
	$writes{$fd}{bytes_written} += $count;
	$writes{$fd}{total_writes}++;
    }
}

sub trace_end
{
    printf("file read counts for $for_comm:\n\n");

    printf("%6s  %10s  %10s\n", "fd", "# reads", "bytes_requested");
    printf("%6s  %10s  %10s\n", "------", "----------", "-----------");

    foreach my $fd (sort {$reads{$b}{bytes_requested} <=>
			      $reads{$a}{bytes_requested}} keys %reads) {
	my $total_reads = $reads{$fd}{total_reads};
	my $bytes_requested = $reads{$fd}{bytes_requested};
	printf("%6u  %10u  %10u\n", $fd, $total_reads, $bytes_requested);
    }

    printf("\nfile write counts for $for_comm:\n\n");

    printf("%6s  %10s  %10s\n", "fd", "# writes", "bytes_written");
    printf("%6s  %10s  %10s\n", "------", "----------", "-----------");

    foreach my $fd (sort {$writes{$b}{bytes_written} <=>
			      $writes{$a}{bytes_written}} keys %writes) {
	my $total_writes = $writes{$fd}{total_writes};
	my $bytes_written = $writes{$fd}{bytes_written};
	printf("%6u  %10u  %10u\n", $fd, $total_writes, $bytes_written);
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


