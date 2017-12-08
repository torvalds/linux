#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# Generates a linker script that specifies the correct initcall order.
#
# Copyright (C) 2019 Google LLC

use strict;
use warnings;
use IO::Handle;

my $nm = $ENV{'LLVM_NM'} || "llvm-nm";
my $ar = $ENV{'AR'}	 || "llvm-ar";
my $objtree = $ENV{'objtree'} || ".";

## list of all object files to process, in link order
my @objects;
## currently active child processes
my $jobs = {};		# child process pid -> file handle
## results from child processes
my $results = {};	# object index -> { level, function }

## reads _NPROCESSORS_ONLN to determine the number of processes to start
sub get_online_processors {
	open(my $fh, "getconf _NPROCESSORS_ONLN 2>/dev/null |")
		or die "$0: failed to execute getconf: $!";
	my $procs = <$fh>;
	close($fh);

	if (!($procs =~ /^\d+$/)) {
		return 1;
	}

	return int($procs);
}

## finds initcalls defined in an object file, parses level and function name,
## and prints it out to the parent process
sub find_initcalls {
	my ($object) = @_;

	die "$0: object file $object doesn't exist?" if (! -f $object);

	open(my $fh, "\"$nm\" -just-symbol-name -defined-only \"$object\" 2>/dev/null |")
		or die "$0: failed to execute \"$nm\": $!";

	my $initcalls = {};

	while (<$fh>) {
		chomp;

		my ($counter, $line, $symbol) = $_ =~ /^__initcall_(\d+)_(\d+)_(.*)$/;

		if (!defined($counter) || !defined($line) || !defined($symbol)) {
			next;
		}

		my ($function, $level) = $symbol =~
			/^(.*)((early|rootfs|con|security|[0-9])s?)$/;

		die "$0: duplicate initcall counter value in object $object: $_"
			if exists($initcalls->{$counter});

		$initcalls->{$counter} = {
			'level'    => $level,
			'line'     => $line,
			'function' => $function
		};
	}

	close($fh);

	# sort initcalls in each object file numerically by the counter value
	# to ensure they are in the order they were defined
	foreach my $counter (sort { $a <=> $b } keys(%{$initcalls})) {
		print $initcalls->{$counter}->{"level"} . " " .
		      $counter . " " .
		      $initcalls->{$counter}->{"line"} . " " .
		      $initcalls->{$counter}->{"function"} . "\n";
	}
}

## waits for any child process to complete, reads the results, and adds them to
## the $results array for later processing
sub wait_for_results {
	my $pid = wait();
	if ($pid > 0) {
		my $fh = $jobs->{$pid};

		# the child process prints out results in the following format:
		#  line 1:    <object file index>
		#  line 2..n: <level> <counter> <line> <function>

		my $index = <$fh>;
		chomp($index);

		if (!($index =~ /^\d+$/)) {
			die "$0: child $pid returned an invalid index: $index";
		}
		$index = int($index);

		while (<$fh>) {
			chomp;
			my ($level, $counter, $line, $function) = $_ =~
				/^([^\ ]+)\ (\d+)\ (\d+)\ (.*)$/;

			if (!defined($level) ||
				!defined($counter) ||
				!defined($line) ||
				!defined($function)) {
				die "$0: child $pid returned invalid data";
			}

			if (!exists($results->{$index})) {
				$results->{$index} = [];
			}

			push (@{$results->{$index}}, {
				'level'    => $level,
				'counter'  => $counter,
				'line'     => $line,
				'function' => $function
			});
		}

		close($fh);
		delete($jobs->{$pid});
	}
}

## launches child processes to find initcalls from the object files, waits for
## each process to complete and collects the results
sub process_objects {
	my $index = 0;	# link order index of the object file
	my $njobs = get_online_processors();

	while (scalar(@objects) > 0) {
		my $object = shift(@objects);

		# fork a child process and read it's stdout
		my $pid = open(my $fh, '-|');

		if (!defined($pid)) {
			die "$0: failed to fork: $!";
		} elsif ($pid) {
			# save the child process pid and the file handle
			$jobs->{$pid} = $fh;
		} else {
			STDOUT->autoflush(1);
			print "$index\n";
			find_initcalls("$objtree/$object");
			exit;
		}

		$index++;

		# if we reached the maximum number of processes, wait for one
		# to complete before launching new ones
		if (scalar(keys(%{$jobs})) >= $njobs && scalar(@objects) > 0) {
			wait_for_results();
		}
	}

	# wait for the remaining children to complete
	while (scalar(keys(%{$jobs})) > 0) {
		wait_for_results();
	}
}

## gets a list of actual object files from thin archives, and adds them to
## @objects in link order
sub find_objects {
	while (my $file = shift(@ARGV)) {
		my $pid = open (my $fh, "\"$ar\" t \"$file\" 2>/dev/null |")
			or die "$0: failed to execute $ar: $!";

		my @output;

		while (<$fh>) {
			chomp;
			push(@output, $_);
		}

		close($fh);

		# if $ar failed, assume we have an object file
		if ($? != 0) {
			push(@objects, $file);
			next;
		}

		# if $ar succeeded, read the list of object files
		foreach (@output) {
			push(@objects, $_);
		}
	}
}

## START
find_objects();
process_objects();

## process results and add them to $sections in the correct order
my $sections = {};

foreach my $index (sort { $a <=> $b } keys(%{$results})) {
	foreach my $result (@{$results->{$index}}) {
		my $level = $result->{'level'};

		if (!exists($sections->{$level})) {
			$sections->{$level} = [];
		}

		my $fsname = $result->{'counter'} . '_' .
			     $result->{'line'}    . '_' .
			     $result->{'function'};

		push(@{$sections->{$level}}, $fsname);
	}
}

if (!keys(%{$sections})) {
	exit(0); # no initcalls...?
}

## print out a linker script that defines the order of initcalls for each
## level
print "SECTIONS {\n";

foreach my $level (sort(keys(%{$sections}))) {
	my $section;

	if ($level eq 'con') {
		$section = '.con_initcall.init';
	} elsif ($level eq 'security') {
		$section = '.security_initcall.init';
	} else {
		$section = ".initcall${level}.init";
	}

	print "\t${section} : {\n";

	foreach my $fsname (@{$sections->{$level}}) {
		print "\t\t*(${section}..${fsname}) ;\n"
	}

	print "\t}\n";
}

print "}\n";
