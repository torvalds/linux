# failed system call counts
# (c) 2010, Tom Zanussi <tzanussi@gmail.com>
# Licensed under the terms of the GNU GPL License version 2
#
# Displays system-wide failed system call totals
# If a [comm] arg is specified, only syscalls called by [comm] are displayed.

use lib "$ENV{'PERF_EXEC_PATH'}/scripts/perl/Perf-Trace-Util/lib";
use lib "./Perf-Trace-Util/lib";
use Perf::Trace::Core;
use Perf::Trace::Context;
use Perf::Trace::Util;

my %failed_syscalls;

sub raw_syscalls::sys_exit
{
	my ($event_name, $context, $common_cpu, $common_secs, $common_nsecs,
	    $common_pid, $common_comm,
	    $id, $ret) = @_;

	if ($ret < 0) {
	    $failed_syscalls{$common_comm}++;
	}
}

sub trace_end
{
    printf("\nfailed syscalls by comm:\n\n");

    printf("%-20s  %10s\n", "comm", "# errors");
    printf("%-20s  %6s  %10s\n", "--------------------", "----------");

    foreach my $comm (sort {$failed_syscalls{$b} <=> $failed_syscalls{$a}}
		      keys %failed_syscalls) {
	    printf("%-20s  %10s\n", $comm, $failed_syscalls{$comm});
    }
}
