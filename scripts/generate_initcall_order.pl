#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# Generates a linker script that specifies the correct initcall order.
#
# Copyright (C) 2019 Google LLC

use strict;
use warnings;
use IO::Handle;
use IO::Select;
use POSIX ":sys_wait_h";

my $nm = $ENV{'NM'} || die "$0: ERROR: NM not set?";
my $objtree = $ENV{'objtree'} || '.';

## currently active child processes
my $jobs = {};		# child process pid -> file handle
## results from child processes
my $results = {};	# object index -> [ { level, secname }, ... ]

## reads _NPROCESSORS_ONLN to determine the maximum number of processes to
## start
sub get_online_processors {
	open(my $fh, "getconf _NPROCESSORS_ONLN 2>/dev/null |")
		or die "$0: ERROR: failed to execute getconf: $!";
	my $procs = <$fh>;
	close($fh);

	if (!($procs =~ /^\d+$/)) {
		return 1;
	}

	return int($procs);
}

## writes results to the parent process
## format: <file index> <initcall level> <base initcall section name>
sub write_results {
	my ($index, $initcalls) = @_;

	# sort by the counter value to ensure the order of initcalls within
	# each object file is correct
	foreach my $counter (sort { $a <=> $b } keys(%{$initcalls})) {
		my $level = $initcalls->{$counter}->{'level'};

		# section name for the initcall function
		my $secname = $initcalls->{$counter}->{'module'} . '__' .
			      $counter . '_' .
			      $initcalls->{$counter}->{'line'} . '_' .
			      $initcalls->{$counter}->{'function'};

		print "$index $level $secname\n";
	}
}

## reads a result line from a child process and adds it to the $results array
sub read_results{
	my ($fh) = @_;

	# each child prints out a full line w/ autoflush and exits after the
	# last line, so even if buffered I/O blocks here, it shouldn't block
	# very long
	my $data = <$fh>;

	if (!defined($data)) {
		return 0;
	}

	chomp($data);

	my ($index, $level, $secname) = $data =~
		/^(\d+)\ ([^\ ]+)\ (.*)$/;

	if (!defined($index) ||
		!defined($level) ||
		!defined($secname)) {
		die "$0: ERROR: child process returned invalid data: $data\n";
	}

	$index = int($index);

	if (!exists($results->{$index})) {
		$results->{$index} = [];
	}

	push (@{$results->{$index}}, {
		'level'   => $level,
		'secname' => $secname
	});

	return 1;
}

## finds initcalls from an object file or all object files in an archive, and
## writes results back to the parent process
sub find_initcalls {
	my ($index, $file) = @_;

	die "$0: ERROR: file $file doesn't exist?" if (! -f $file);

	open(my $fh, "\"$nm\" --defined-only \"$file\" 2>/dev/null |")
		or die "$0: ERROR: failed to execute \"$nm\": $!";

	my $initcalls = {};

	while (<$fh>) {
		chomp;

		# check for the start of a new object file (if processing an
		# archive)
		my ($path)= $_ =~ /^(.+)\:$/;

		if (defined($path)) {
			write_results($index, $initcalls);
			$initcalls = {};
			next;
		}

		# look for an initcall
		my ($module, $counter, $line, $symbol) = $_ =~
			/[a-z]\s+__initcall__(\S*)__(\d+)_(\d+)_(.*)$/;

		if (!defined($module)) {
			$module = ''
		}

		if (!defined($counter) ||
			!defined($line) ||
			!defined($symbol)) {
			next;
		}

		# parse initcall level
		my ($function, $level) = $symbol =~
			/^(.*)((early|rootfs|con|[0-9])s?)$/;

		die "$0: ERROR: invalid initcall name $symbol in $file($path)"
			if (!defined($function) || !defined($level));

		$initcalls->{$counter} = {
			'module'   => $module,
			'line'     => $line,
			'function' => $function,
			'level'    => $level,
		};
	}

	close($fh);
	write_results($index, $initcalls);
}

## waits for any child process to complete, reads the results, and adds them to
## the $results array for later processing
sub wait_for_results {
	my ($select) = @_;

	my $pid = 0;
	do {
		# unblock children that may have a full write buffer
		foreach my $fh ($select->can_read(0)) {
			read_results($fh);
		}

		# check for children that have exited, read the remaining data
		# from them, and clean up
		$pid = waitpid(-1, WNOHANG);
		if ($pid > 0) {
			if (!exists($jobs->{$pid})) {
				next;
			}

			my $fh = $jobs->{$pid};
			$select->remove($fh);

			while (read_results($fh)) {
				# until eof
			}

			close($fh);
			delete($jobs->{$pid});
		}
	} while ($pid > 0);
}

## forks a child to process each file passed in the command line and collects
## the results
sub process_files {
	my $index = 0;
	my $njobs = $ENV{'PARALLELISM'} || get_online_processors();
	my $select = IO::Select->new();

	while (my $file = shift(@ARGV)) {
		# fork a child process and read it's stdout
		my $pid = open(my $fh, '-|');

		if (!defined($pid)) {
			die "$0: ERROR: failed to fork: $!";
		} elsif ($pid) {
			# save the child process pid and the file handle
			$select->add($fh);
			$jobs->{$pid} = $fh;
		} else {
			# in the child process
			STDOUT->autoflush(1);
			find_initcalls($index, "$objtree/$file");
			exit;
		}

		$index++;

		# limit the number of children to $njobs
		if (scalar(keys(%{$jobs})) >= $njobs) {
			wait_for_results($select);
		}
	}

	# wait for the remaining children to complete
	while (scalar(keys(%{$jobs})) > 0) {
		wait_for_results($select);
	}
}

sub generate_initcall_lds() {
	process_files();

	my $sections = {};	# level -> [ secname, ...]

	# sort results to retain link order and split to sections per
	# initcall level
	foreach my $index (sort { $a <=> $b } keys(%{$results})) {
		foreach my $result (@{$results->{$index}}) {
			my $level = $result->{'level'};

			if (!exists($sections->{$level})) {
				$sections->{$level} = [];
			}

			push(@{$sections->{$level}}, $result->{'secname'});
		}
	}

	die "$0: ERROR: no initcalls?" if (!keys(%{$sections}));

	# print out a linker script that defines the order of initcalls for
	# each level
	print "SECTIONS {\n";

	foreach my $level (sort(keys(%{$sections}))) {
		my $section;

		if ($level eq 'con') {
			$section = '.con_initcall.init';
		} else {
			$section = ".initcall${level}.init";
		}

		print "\t${section} : {\n";

		foreach my $secname (@{$sections->{$level}}) {
			print "\t\t*(${section}..${secname}) ;\n";
		}

		print "\t}\n";
	}

	print "}\n";
}

generate_initcall_lds();
