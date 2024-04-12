#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0-only
#
# (c) 2017 Tobin C. Harding <me@tobin.cc>
#
# leaking_addresses.pl: Scan the kernel for potential leaking addresses.
#  - Scans dmesg output.
#  - Walks directory tree and parses each file (for each directory in @DIRS).
#
# Use --debug to output path before parsing, this is useful to find files that
# cause the script to choke.

#
# When the system is idle it is likely that most files under /proc/PID will be
# identical for various processes.  Scanning _all_ the PIDs under /proc is
# unnecessary and implies that we are thoroughly scanning /proc.  This is _not_
# the case because there may be ways userspace can trigger creation of /proc
# files that leak addresses but were not present during a scan.  For these two
# reasons we exclude all PID directories under /proc except '1/'

use warnings;
use strict;
use POSIX;
use File::Basename;
use File::Spec;
use File::Temp qw/tempfile/;
use Cwd 'abs_path';
use Term::ANSIColor qw(:constants);
use Getopt::Long qw(:config no_auto_abbrev);
use Config;
use bigint qw/hex/;
use feature 'state';

my $P = $0;

# Directories to scan.
my @DIRS = ('/proc', '/sys');

# Timer for parsing each file, in seconds.
my $TIMEOUT = 10;

# Kernel addresses vary by architecture.  We can only auto-detect the following
# architectures (using `uname -m`).  (flag --32-bit overrides auto-detection.)
my @SUPPORTED_ARCHITECTURES = ('x86_64', 'ppc64', 'x86');

# Command line options.
my $help = 0;
my $debug = 0;
my $raw = 0;
my $output_raw = "";	# Write raw results to file.
my $input_raw = "";	# Read raw results from file instead of scanning.
my $suppress_dmesg = 0;		# Don't show dmesg in output.
my $squash_by_path = 0;		# Summary report grouped by absolute path.
my $squash_by_filename = 0;	# Summary report grouped by filename.
my $kallsyms_file = "";		# Kernel symbols file.
my $kernel_config_file = "";	# Kernel configuration file.
my $opt_32bit = 0;		# Scan 32-bit kernel.
my $page_offset_32bit = 0;	# Page offset for 32-bit kernel.

my @kallsyms = ();

# Skip these absolute paths.
my @skip_abs = (
	'/proc/kmsg',
	'/proc/device-tree',
	'/proc/1/syscall',
	'/sys/firmware/devicetree',
	'/sys/kernel/tracing/trace_pipe',
	'/sys/kernel/debug/tracing/trace_pipe',
	'/sys/kernel/security/apparmor/revision');

# Skip these under any subdirectory.
my @skip_any = (
	'pagemap',
	'events',
	'access',
	'registers',
	'snapshot_raw',
	'trace_pipe_raw',
	'ptmx',
	'trace_pipe',
	'fd',
	'usbmon');

sub help
{
	my ($exitcode) = @_;

	print << "EOM";

Usage: $P [OPTIONS]

Options:

	-o, --output-raw=<file>		Save results for future processing.
	-i, --input-raw=<file>		Read results from file instead of scanning.
	      --raw			Show raw results (default).
	      --suppress-dmesg		Do not show dmesg results.
	      --squash-by-path		Show one result per unique path.
	      --squash-by-filename	Show one result per unique filename.
	--kernel-config-file=<file>     Kernel configuration file (e.g /boot/config)
	--kallsyms=<file>		Read kernel symbol addresses from file (for
						scanning binary files).
	--32-bit			Scan 32-bit kernel.
	--page-offset-32-bit=o		Page offset (for 32-bit kernel 0xABCD1234).
	-d, --debug			Display debugging output.
	-h, --help			Display this help and exit.

Scans the running kernel for potential leaking addresses.

EOM
	exit($exitcode);
}

GetOptions(
	'd|debug'		=> \$debug,
	'h|help'		=> \$help,
	'o|output-raw=s'        => \$output_raw,
	'i|input-raw=s'         => \$input_raw,
	'suppress-dmesg'        => \$suppress_dmesg,
	'squash-by-path'        => \$squash_by_path,
	'squash-by-filename'    => \$squash_by_filename,
	'raw'                   => \$raw,
	'kallsyms=s'            => \$kallsyms_file,
	'kernel-config-file=s'	=> \$kernel_config_file,
	'32-bit'		=> \$opt_32bit,
	'page-offset-32-bit=o'	=> \$page_offset_32bit,
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

if (!(is_supported_architecture() or $opt_32bit or $page_offset_32bit)) {
	printf "\nScript does not support your architecture, sorry.\n";
	printf "\nCurrently we support: \n\n";
	foreach(@SUPPORTED_ARCHITECTURES) {
		printf "\t%s\n", $_;
	}
	printf("\n");

	printf("If you are running a 32-bit architecture you may use:\n");
	printf("\n\t--32-bit or --page-offset-32-bit=<page offset>\n\n");

	my $archname = `uname -m`;
	printf("Machine hardware name (`uname -m`): %s\n", $archname);

	exit(129);
}

if ($output_raw) {
	open my $fh, '>', $output_raw or die "$0: $output_raw: $!\n";
	select $fh;
}

if ($kallsyms_file) {
	open my $fh, '<', $kallsyms_file or die "$0: $kallsyms_file: $!\n";
	while (<$fh>) {
		chomp;
		my @entry = split / /, $_;
		my $addr_text = $entry[0];
		if ($addr_text !~ /^0/) {
			# TODO: Why is hex() so impossibly slow?
			my $addr = hex($addr_text);
			my $symbol = $entry[2];
			# Only keep kernel text addresses.
			my $long = pack("J", $addr);
			my $entry = [$long, $symbol];
			push @kallsyms, $entry;
		}
	}
	close $fh;
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
	return (is_x86_64() or is_ppc64() or is_ix86_32());
}

sub is_32bit
{
	# Allow --32-bit or --page-offset-32-bit to override
	if ($opt_32bit or $page_offset_32bit) {
		return 1;
	}

	return is_ix86_32();
}

sub is_ix86_32
{
       state $arch = `uname -m`;

       chomp $arch;
       if ($arch =~ m/i[3456]86/) {
               return 1;
       }
       return 0;
}

sub is_arch
{
       my ($desc) = @_;
       my $arch = `uname -m`;

       chomp $arch;
       if ($arch eq $desc) {
               return 1;
       }
       return 0;
}

sub is_x86_64
{
	state $is = is_arch('x86_64');
	return $is;
}

sub is_ppc64
{
	state $is = is_arch('ppc64');
	return $is;
}

# Gets config option value from kernel config file.
# Returns "" on error or if config option not found.
sub get_kernel_config_option
{
	my ($option) = @_;
	my $value = "";
	my $tmp_fh;
	my $tmp_file = "";
	my @config_files;

	# Allow --kernel-config-file to override.
	if ($kernel_config_file ne "") {
		@config_files = ($kernel_config_file);
	} elsif (-R "/proc/config.gz") {
		($tmp_fh, $tmp_file) = tempfile("config.gz-XXXXXX",
						UNLINK => 1);

		if (system("gunzip < /proc/config.gz > $tmp_file")) {
			dprint("system(gunzip < /proc/config.gz) failed\n");
			return "";
		} else {
			@config_files = ($tmp_file);
		}
	} else {
		my $file = '/boot/config-' . `uname -r`;
		chomp $file;
		@config_files = ($file, '/boot/config');
	}

	foreach my $file (@config_files) {
		dprint("parsing config file: $file\n");
		$value = option_from_file($option, $file);
		if ($value ne "") {
			last;
		}
	}

	return $value;
}

# Parses $file and returns kernel configuration option value.
sub option_from_file
{
	my ($option, $file) = @_;
	my $str = "";
	my $val = "";

	open(my $fh, "<", $file) or return "";
	while (my $line = <$fh> ) {
		if ($line =~ /^$option/) {
			($str, $val) = split /=/, $line;
			chomp $val;
			last;
		}
	}

	close $fh;
	return $val;
}

sub is_false_positive
{
	my ($match) = @_;

	if (is_32bit()) {
		return is_false_positive_32bit($match);
	}

	# Ignore 64 bit false positives:
	# 0xfffffffffffffff[0-f]
	# 0x0000000000000000
	if ($match =~ '\b(0x)?(f|F){15}[0-9a-f]\b' or
	    $match =~ '\b(0x)?0{16}\b') {
		return 1;
	}

	if (is_x86_64() and is_in_vsyscall_memory_region($match)) {
		return 1;
	}

	return 0;
}

sub is_false_positive_32bit
{
       my ($match) = @_;
       state $page_offset = get_page_offset();

       if ($match =~ '\b(0x)?(f|F){7}[0-9a-f]\b') {
               return 1;
       }

       if (hex($match) < $page_offset) {
               return 1;
       }

       return 0;
}

# returns integer value
sub get_page_offset
{
       my $page_offset;
       my $default_offset = 0xc0000000;

       # Allow --page-offset-32bit to override.
       if ($page_offset_32bit != 0) {
               return $page_offset_32bit;
       }

       $page_offset = get_kernel_config_option('CONFIG_PAGE_OFFSET');
       if (!$page_offset) {
	       return $default_offset;
       }
       return $page_offset;
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
	my ($path, $line) = @_;
	my $address_re;

	# Ignore Signal masks.
	if ($line =~ '^SigBlk:' or
	    $line =~ '^SigIgn:' or
	    $line =~ '^SigCgt:') {
		return 0;
	}

	# Ignore input device reporting.
	# /proc/bus/input/devices: B: KEY=402000000 3803078f800d001 feffffdfffefffff fffffffffffffffe
	# /sys/devices/platform/i8042/serio0/input/input1/uevent: KEY=402000000 3803078f800d001 feffffdfffefffff fffffffffffffffe
	# /sys/devices/platform/i8042/serio0/input/input1/capabilities/key: 402000000 3803078f800d001 feffffdfffefffff fffffffffffffffe
	if ($line =~ '\bKEY=[[:xdigit:]]{9,14} [[:xdigit:]]{16} [[:xdigit:]]{16}\b' or
            ($path =~ '\bkey$' and
             $line =~ '\b[[:xdigit:]]{9,14} [[:xdigit:]]{16} [[:xdigit:]]{16}\b')) {
		return 0;
	}

	$address_re = get_address_re();
	while ($line =~ /($address_re)/g) {
		if (!is_false_positive($1)) {
			return 1;
		}
	}

	return 0;
}

sub get_address_re
{
	if (is_ppc64()) {
		return '\b(0x)?[89abcdef]00[[:xdigit:]]{13}\b';
	} elsif (is_32bit()) {
		return '\b(0x)?[[:xdigit:]]{8}\b';
	}

	return get_x86_64_re();
}

sub get_x86_64_re
{
	# We handle page table levels but only if explicitly configured using
	# CONFIG_PGTABLE_LEVELS.  If config file parsing fails or config option
	# is not found we default to using address regular expression suitable
	# for 4 page table levels.
	state $ptl = get_kernel_config_option('CONFIG_PGTABLE_LEVELS');

	if ($ptl == 5) {
		return '\b(0x)?ff[[:xdigit:]]{14}\b';
	}
	return '\b(0x)?ffff[[:xdigit:]]{12}\b';
}

sub parse_dmesg
{
	open my $cmd, '-|', 'dmesg';
	while (<$cmd>) {
		if (may_leak_address("dmesg", $_)) {
			print 'dmesg: ' . $_;
		}
	}
	close $cmd;
}

# True if we should skip this path.
sub skip
{
	my ($path) = @_;

	foreach (@skip_abs) {
		return 1 if (/^$path$/);
	}

	my($filename, $dirs, $suffix) = fileparse($path);
	foreach (@skip_any) {
		return 1 if (/^$filename$/);
	}

	return 0;
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

sub parse_binary
{
	my ($file) = @_;

	open my $fh, "<:raw", $file or return;
	local $/ = undef;
	my $bytes = <$fh>;
	close $fh;

	foreach my $entry (@kallsyms) {
		my $addr = $entry->[0];
		my $symbol = $entry->[1];
		my $offset = index($bytes, $addr);
		if ($offset != -1) {
			printf("$file: $symbol @ $offset\n");
		}
	}
}

sub parse_file
{
	my ($file) = @_;

	if (! -R $file) {
		return;
	}

	if (! -T $file) {
		if ($file =~ m|^/sys/kernel/btf/| or
		    $file =~ m|^/sys/devices/pci| or
		    $file =~ m|^/sys/firmware/efi/efivars/| or
		    $file =~ m|^/proc/bus/pci/|) {
			return;
		}
		if (scalar @kallsyms > 0) {
			parse_binary($file);
		}
		return;
	}

	open my $fh, "<", $file or return;
	while ( <$fh> ) {
		chomp;
		if (may_leak_address($file, $_)) {
			printf("$file: $_\n");
		}
	}
	close $fh;
}

# Checks if the actual path name is leaking a kernel address.
sub check_path_for_leaks
{
	my ($path) = @_;

	if (may_leak_address($path, $path)) {
		printf("Path name may contain address: $path\n");
	}
}

# Recursively walk directory tree.
sub walk
{
	my @dirs = @_;

	while (my $pwd = shift @dirs) {
		next if (!opendir(DIR, $pwd));
		my @files = readdir(DIR);
		closedir(DIR);

		foreach my $file (@files) {
			next if ($file eq '.' or $file eq '..');

			my $path = "$pwd/$file";
			next if (-l $path);

			# skip /proc/PID except /proc/1
			next if (($path =~ /^\/proc\/[0-9]+$/) &&
				 ($path !~ /^\/proc\/1$/));

			next if (skip($path));

			check_path_for_leaks($path);

			if (-d $path) {
				push @dirs, $path;
				next;
			}

			dprint("parsing: $path\n");
			timed_parse_file($path);
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
