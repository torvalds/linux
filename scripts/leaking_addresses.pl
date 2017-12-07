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

use warnings;
use strict;
use POSIX;
use File::Basename;
use File::Spec;
use Cwd 'abs_path';
use Term::ANSIColor qw(:constants);
use Getopt::Long qw(:config no_auto_abbrev);
use Config;
use bigint qw/hex/;

my $P = $0;
my $V = '0.01';

# Directories to scan.
my @DIRS = ('/proc', '/sys');

# Timer for parsing each file, in seconds.
my $TIMEOUT = 10;

# Script can only grep for kernel addresses on the following architectures. If
# your architecture is not listed here and has a grep'able kernel address please
# consider submitting a patch.
my @SUPPORTED_ARCHITECTURES = ('x86_64', 'ppc64');

# Command line options.
my $help = 0;
my $debug = 0;
my $raw = 0;
my $output_raw = "";	# Write raw results to file.
my $input_raw = "";	# Read raw results from file instead of scanning.

my $suppress_dmesg = 0;		# Don't show dmesg in output.
my $squash_by_path = 0;		# Summary report grouped by absolute path.
my $squash_by_filename = 0;	# Summary report grouped by filename.

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

	-o, --output-raw=<file>		Save results for future processing.
	-i, --input-raw=<file>		Read results from file instead of scanning.
	      --raw			Show raw results (default).
	      --suppress-dmesg		Do not show dmesg results.
	      --squash-by-path		Show one result per unique path.
	      --squash-by-filename	Show one result per unique filename.
	-d, --debug			Display debugging output.
	-h, --help, --version		Display this help and exit.

Scans the running (64 bit) kernel for potential leaking addresses.

EOM
	exit($exitcode);
}

GetOptions(
	'd|debug'		=> \$debug,
	'h|help'		=> \$help,
	'version'		=> \$help,
	'o|output-raw=s'        => \$output_raw,
	'i|input-raw=s'         => \$input_raw,
	'suppress-dmesg'        => \$suppress_dmesg,
	'squash-by-path'        => \$squash_by_path,
	'squash-by-filename'    => \$squash_by_filename,
	'raw'                   => \$raw,
) or help(1);

help(0) if ($help);

if ($input_raw) {
	format_output($input_raw);
	exit(0);
}

if (!$input_raw and ($squash_by_path or $squash_by_filename)) {
	printf "\nSummary reporting only available with --input-raw=<file>\n";
	printf "(First run scan with --output-raw=<file>.)\n";
	exit(128);
}

if (!is_supported_architecture()) {
	printf "\nScript does not support your architecture, sorry.\n";
	printf "\nCurrently we support: \n\n";
	foreach(@SUPPORTED_ARCHITECTURES) {
		printf "\t%s\n", $_;
	}

	my $archname = $Config{archname};
	printf "\n\$ perl -MConfig -e \'print \"\$Config{archname}\\n\"\'\n";
	printf "%s\n", $archname;

	exit(129);
}

if ($output_raw) {
	open my $fh, '>', $output_raw or die "$0: $output_raw: $!\n";
	select $fh;
}

parse_dmesg();
walk(@DIRS);

exit 0;

sub dprint
{
	printf(STDERR @_) if $debug;
}

sub is_supported_architecture
{
	return (is_x86_64() or is_ppc64());
}

sub is_x86_64
{
	my $archname = $Config{archname};

	if ($archname =~ m/x86_64/) {
		return 1;
	}
	return 0;
}

sub is_ppc64
{
	my $archname = $Config{archname};

	if ($archname =~ m/powerpc/ and $archname =~ m/64/) {
		return 1;
	}
	return 0;
}

sub is_false_positive
{
	my ($match) = @_;

	if ($match =~ '\b(0x)?(f|F){16}\b' or
	    $match =~ '\b(0x)?0{16}\b') {
		return 1;
	}

	if (is_x86_64() and is_in_vsyscall_memory_region($match)) {
		return 1;
	}

	return 0;
}

sub is_in_vsyscall_memory_region
{
	my ($match) = @_;

	my $hex = hex($match);
	my $region_min = hex("0xffffffffff600000");
	my $region_max = hex("0xffffffffff601000");

	return ($hex >= $region_min and $hex <= $region_max);
}

# True if argument potentially contains a kernel address.
sub may_leak_address
{
	my ($line) = @_;
	my $address_re;

	# Signal masks.
	if ($line =~ '^SigBlk:' or
	    $line =~ '^SigIgn:' or
	    $line =~ '^SigCgt:') {
		return 0;
	}

	if ($line =~ '\bKEY=[[:xdigit:]]{14} [[:xdigit:]]{16} [[:xdigit:]]{16}\b' or
	    $line =~ '\b[[:xdigit:]]{14} [[:xdigit:]]{16} [[:xdigit:]]{16}\b') {
		return 0;
	}

	# One of these is guaranteed to be true.
	if (is_x86_64()) {
		$address_re = '\b(0x)?ffff[[:xdigit:]]{12}\b';
	} elsif (is_ppc64()) {
		$address_re = '\b(0x)?[89abcdef]00[[:xdigit:]]{13}\b';
	}

	while (/($address_re)/g) {
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

sub timed_parse_file
{
	my ($file) = @_;

	eval {
		local $SIG{ALRM} = sub { die "alarm\n" }; # NB: \n required.
		alarm $TIMEOUT;
		parse_file($file);
		alarm 0;
	};

	if ($@) {
		die unless $@ eq "alarm\n";	# Propagate unexpected errors.
		printf STDERR "timed out parsing: %s\n", $file;
	}
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
				timed_parse_file($path);
			}
		}
	}
}

sub format_output
{
	my ($file) = @_;

	# Default is to show raw results.
	if ($raw or (!$squash_by_path and !$squash_by_filename)) {
		dump_raw_output($file);
		return;
	}

	my ($total, $dmesg, $paths, $files) = parse_raw_file($file);

	printf "\nTotal number of results from scan (incl dmesg): %d\n", $total;

	if (!$suppress_dmesg) {
		print_dmesg($dmesg);
	}

	if ($squash_by_filename) {
		squash_by($files, 'filename');
	}

	if ($squash_by_path) {
		squash_by($paths, 'path');
	}
}

sub dump_raw_output
{
	my ($file) = @_;

	open (my $fh, '<', $file) or die "$0: $file: $!\n";
	while (<$fh>) {
		if ($suppress_dmesg) {
			if ("dmesg:" eq substr($_, 0, 6)) {
				next;
			}
		}
		print $_;
	}
	close $fh;
}

sub parse_raw_file
{
	my ($file) = @_;

	my $total = 0;          # Total number of lines parsed.
	my @dmesg;              # dmesg output.
	my %files;              # Unique filenames containing leaks.
	my %paths;              # Unique paths containing leaks.

	open (my $fh, '<', $file) or die "$0: $file: $!\n";
	while (my $line = <$fh>) {
		$total++;

		if ("dmesg:" eq substr($line, 0, 6)) {
			push @dmesg, $line;
			next;
		}

		cache_path(\%paths, $line);
		cache_filename(\%files, $line);
	}

	return $total, \@dmesg, \%paths, \%files;
}

sub print_dmesg
{
	my ($dmesg) = @_;

	print "\ndmesg output:\n";

	if (@$dmesg == 0) {
		print "<no results>\n";
		return;
	}

	foreach(@$dmesg) {
		my $index = index($_, ': ');
		$index += 2;    # skid ': '
		print substr($_, $index);
	}
}

sub squash_by
{
	my ($ref, $desc) = @_;

	print "\nResults squashed by $desc (excl dmesg). ";
	print "Displaying [<number of results> <$desc>], <example result>\n";

	if (keys %$ref == 0) {
		print "<no results>\n";
		return;
	}

	foreach(keys %$ref) {
		my $lines = $ref->{$_};
		my $length = @$lines;
		printf "[%d %s] %s", $length, $_, @$lines[0];
	}
}

sub cache_path
{
	my ($paths, $line) = @_;

	my $index = index($line, ': ');
	my $path = substr($line, 0, $index);

	$index += 2;            # skip ': '
	add_to_cache($paths, $path, substr($line, $index));
}

sub cache_filename
{
	my ($files, $line) = @_;

	my $index = index($line, ': ');
	my $path = substr($line, 0, $index);
	my $filename = basename($path);

	$index += 2;            # skip ': '
	add_to_cache($files, $filename, substr($line, $index));
}

sub add_to_cache
{
	my ($cache, $key, $value) = @_;

	if (!$cache->{$key}) {
		$cache->{$key} = ();
	}
	push @{$cache->{$key}}, $value;
}
