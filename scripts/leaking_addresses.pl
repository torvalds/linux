#!/usr/bin/env perl
#
# (c) 2017 Tobin C. Harding <me@tobin.cc>
# Licensed under the terms of the GNU GPL License version 2
#
# leaking_addresses.pl: Scan 64 bit kernel for potential leaking addresses.
#  - Scans dmesg output.
#  - Walks directory tree and parses each file (for each directory in @DIRS).
#
# Use --debug to output path before parsing, this is useful to find files that
# cause the script to choke.
#
# You may like to set kptr_restrict=2 before running script
# (see Documentation/sysctl/kernel.txt).

use warnings;
use strict;
use POSIX;
use File::Basename;
use File::Spec;
use Cwd 'abs_path';
use Term::ANSIColor qw(:constants);
use Getopt::Long qw(:config no_auto_abbrev);

my $P = $0;
my $V = '0.01';

# Directories to scan.
my @DIRS = ('/proc', '/sys');

# Command line options.
my $help = 0;
my $debug = 0;

# Do not parse these files (absolute path).
my @skip_parse_files_abs = ('/proc/kmsg',
			    '/proc/kcore',
			    '/proc/fs/ext4/sdb1/mb_groups',
			    '/proc/1/fd/3',
			    '/sys/firmware/devicetree',
			    '/proc/device-tree',
			    '/sys/kernel/debug/tracing/trace_pipe',
			    '/sys/kernel/security/apparmor/revision');

# Do not parse these files under any subdirectory.
my @skip_parse_files_any = ('0',
			    '1',
			    '2',
			    'pagemap',
			    'events',
			    'access',
			    'registers',
			    'snapshot_raw',
			    'trace_pipe_raw',
			    'ptmx',
			    'trace_pipe');

# Do not walk these directories (absolute path).
my @skip_walk_dirs_abs = ();

# Do not walk these directories under any subdirectory.
my @skip_walk_dirs_any = ('self',
			  'thread-self',
			  'cwd',
			  'fd',
			  'usbmon',
			  'stderr',
			  'stdin',
			  'stdout');

sub help
{
	my ($exitcode) = @_;

	print << "EOM";
Usage: $P [OPTIONS]
Version: $V

Options:

	-d, --debug                Display debugging output.
	-h, --help, --version      Display this help and exit.

Scans the running (64 bit) kernel for potential leaking addresses.

EOM
	exit($exitcode);
}

GetOptions(
	'd|debug'		=> \$debug,
	'h|help'		=> \$help,
	'version'		=> \$help
) or help(1);

help(0) if ($help);

parse_dmesg();
walk(@DIRS);

exit 0;

sub dprint
{
	printf(STDERR @_) if $debug;
}

sub is_false_positive
{
	my ($match) = @_;

	if ($match =~ '\b(0x)?(f|F){16}\b' or
	    $match =~ '\b(0x)?0{16}\b') {
		return 1;
	}


	if ($match =~ '\bf{10}600000\b' or# vsyscall memory region, we should probably check against a range here.
	    $match =~ '\bf{10}601000\b') {
		return 1;
	}

	return 0;
}

# True if argument potentially contains a kernel address.
sub may_leak_address
{
	my ($line) = @_;
	my $address = '\b(0x)?ffff[[:xdigit:]]{12}\b';

	# Signal masks.
	if ($line =~ '^SigBlk:' or
	    $line =~ '^SigCgt:') {
		return 0;
	}

	if ($line =~ '\bKEY=[[:xdigit:]]{14} [[:xdigit:]]{16} [[:xdigit:]]{16}\b' or
	    $line =~ '\b[[:xdigit:]]{14} [[:xdigit:]]{16} [[:xdigit:]]{16}\b') {
		return 0;
	}

	while (/($address)/g) {
		if (!is_false_positive($1)) {
			return 1;
		}
	}

	return 0;
}

sub parse_dmesg
{
	open my $cmd, '-|', 'dmesg';
	while (<$cmd>) {
		if (may_leak_address($_)) {
			print 'dmesg: ' . $_;
		}
	}
	close $cmd;
}

# True if we should skip this path.
sub skip
{
	my ($path, $paths_abs, $paths_any) = @_;

	foreach (@$paths_abs) {
		return 1 if (/^$path$/);
	}

	my($filename, $dirs, $suffix) = fileparse($path);
	foreach (@$paths_any) {
		return 1 if (/^$filename$/);
	}

	return 0;
}

sub skip_parse
{
	my ($path) = @_;
	return skip($path, \@skip_parse_files_abs, \@skip_parse_files_any);
}

sub parse_file
{
	my ($file) = @_;

	if (! -R $file) {
		return;
	}

	if (skip_parse($file)) {
		dprint "skipping file: $file\n";
		return;
	}
	dprint "parsing: $file\n";

	open my $fh, "<", $file or return;
	while ( <$fh> ) {
		if (may_leak_address($_)) {
			print $file . ': ' . $_;
		}
	}
	close $fh;
}


# True if we should skip walking this directory.
sub skip_walk
{
	my ($path) = @_;
	return skip($path, \@skip_walk_dirs_abs, \@skip_walk_dirs_any)
}

# Recursively walk directory tree.
sub walk
{
	my @dirs = @_;

	while (my $pwd = shift @dirs) {
		next if (skip_walk($pwd));
		next if (!opendir(DIR, $pwd));
		my @files = readdir(DIR);
		closedir(DIR);

		foreach my $file (@files) {
			next if ($file eq '.' or $file eq '..');

			my $path = "$pwd/$file";
			next if (-l $path);

			if (-d $path) {
				push @dirs, $path;
			} else {
				parse_file($path);
			}
		}
	}
}
