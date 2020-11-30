#!/usr/bin/perl
# SPDX-License-Identifier: GPL-2.0

use strict;
use Pod::Usage;
use Getopt::Long;
use File::Find;
use Fcntl ':mode';

my $help;
my $man;
my $debug;
my $arch;
my $feat;
my $prefix="Documentation/features";

GetOptions(
	"debug|d+" => \$debug,
	"dir=s" => \$prefix,
	'help|?' => \$help,
	'arch=s' => \$arch,
	'feat=s' => \$feat,
	man => \$man
) or pod2usage(2);

pod2usage(1) if $help;
pod2usage(-exitstatus => 0, -verbose => 2) if $man;

pod2usage(2) if (scalar @ARGV < 1 || @ARGV > 2);

my ($cmd, $arg) = @ARGV;

pod2usage(2) if ($cmd ne "current" && $cmd ne "rest" && $cmd ne "validate");

require Data::Dumper if ($debug);

my %data;
my %archs;

#
# Displays an error message, printing file name and line
#
sub parse_error($$$$) {
	my ($file, $ln, $msg, $data) = @_;

	$data =~ s/\s+$/\n/;

	print STDERR "Warning: file $file#$ln:\n\t$msg";

	if ($data ne "") {
		print STDERR ". Line\n\t\t$data";
	} else {
	    print STDERR "\n";
	}
}

#
# Parse a features file, storing its contents at %data
#

my $h_name = "Feature";
my $h_kconfig = "Kconfig";
my $h_description = "Description";
my $h_subsys = "Subsystem";
my $h_status = "Status";
my $h_arch = "Architecture";

my $max_size_name = length($h_name);
my $max_size_kconfig = length($h_kconfig);
my $max_size_description = length($h_description);
my $max_size_subsys = length($h_subsys);
my $max_size_status = length($h_status);
my $max_size_arch = length($h_arch);

sub parse_feat {
	my $file = $File::Find::name;

	my $mode = (stat($file))[2];
	return if ($mode & S_IFDIR);
	return if ($file =~ m,($prefix)/arch-support.txt,);
	return if (!($file =~ m,arch-support.txt$,));

	my $subsys = "";
	$subsys = $2 if ( m,.*($prefix)/([^/]+).*,);

	if (length($subsys) > $max_size_subsys) {
		$max_size_subsys = length($subsys);
	}

	my $name;
	my $kconfig;
	my $description;
	my $comments = "";
	my $last_status;
	my $ln;
	my %arch_table;

	print STDERR "Opening $file\n" if ($debug > 1);
	open IN, $file;

	while(<IN>) {
		$ln++;

		if (m/^\#\s+Feature\s+name:\s*(.*\S)/) {
			$name = $1;
			if (length($name) > $max_size_name) {
				$max_size_name = length($name);
			}
			next;
		}
		if (m/^\#\s+Kconfig:\s*(.*\S)/) {
			$kconfig = $1;
			if (length($kconfig) > $max_size_kconfig) {
				$max_size_kconfig = length($kconfig);
			}
			next;
		}
		if (m/^\#\s+description:\s*(.*\S)/) {
			$description = $1;
			if (length($description) > $max_size_description) {
				$max_size_description = length($description);
			}
			next;
		}
		next if (m/^\\s*$/);
		next if (m/^\s*\-+\s*$/);
		next if (m/^\s*\|\s*arch\s*\|\s*status\s*\|\s*$/);

		if (m/^\#\s*(.*)/) {
			$comments .= "$1\n";
			next;
		}
		if (m/^\s*\|\s*(\S+):\s*\|\s*(\S+)\s*\|\s*$/) {
			my $a = $1;
			my $status = $2;

			if (length($status) > $max_size_status) {
				$max_size_status = length($status);
			}
			if (length($a) > $max_size_arch) {
				$max_size_arch = length($a);
			}

			$status = "---" if ($status =~ m/^\.\.$/);

			$archs{$a} = 1;
			$arch_table{$a} = $status;
			next;
		}

		#Everything else is an error
		parse_error($file, $ln, "line is invalid", $_);
	}
	close IN;

	if (!$name) {
		parse_error($file, $ln, "Feature name not found", "");
		return;
	}

	parse_error($file, $ln, "Subsystem not found", "") if (!$subsys);
	parse_error($file, $ln, "Kconfig not found", "") if (!$kconfig);
	parse_error($file, $ln, "Description not found", "") if (!$description);

	if (!%arch_table) {
		parse_error($file, $ln, "Architecture table not found", "");
		return;
	}

	$data{$name}->{where} = $file;
	$data{$name}->{subsys} = $subsys;
	$data{$name}->{kconfig} = $kconfig;
	$data{$name}->{description} = $description;
	$data{$name}->{comments} = $comments;
	$data{$name}->{table} = \%arch_table;
}

#
# Output feature(s) for a given architecture
#
sub output_arch_table {
	my $title = "Feature status on $arch architecture";

	print "=" x length($title) . "\n";
	print "$title\n";
	print "=" x length($title) . "\n\n";

	print "=" x $max_size_subsys;
	print "  ";
	print "=" x $max_size_name;
	print "  ";
	print "=" x $max_size_kconfig;
	print "  ";
	print "=" x $max_size_status;
	print "  ";
	print "=" x $max_size_description;
	print "\n";
	printf "%-${max_size_subsys}s  ", $h_subsys;
	printf "%-${max_size_name}s  ", $h_name;
	printf "%-${max_size_kconfig}s  ", $h_kconfig;
	printf "%-${max_size_status}s  ", $h_status;
	printf "%-${max_size_description}s\n", $h_description;
	print "=" x $max_size_subsys;
	print "  ";
	print "=" x $max_size_name;
	print "  ";
	print "=" x $max_size_kconfig;
	print "  ";
	print "=" x $max_size_status;
	print "  ";
	print "=" x $max_size_description;
	print "\n";

	foreach my $name (sort {
				($data{$a}->{subsys} cmp $data{$b}->{subsys}) ||
				($data{$a}->{name} cmp $data{$b}->{name})
			       } keys %data) {
		next if ($feat && $name ne $feat);

		my %arch_table = %{$data{$name}->{table}};
		printf "%-${max_size_subsys}s  ", $data{$name}->{subsys};
		printf "%-${max_size_name}s  ", $name;
		printf "%-${max_size_kconfig}s  ", $data{$name}->{kconfig};
		printf "%-${max_size_status}s  ", $arch_table{$arch};
		printf "%-${max_size_description}s\n", $data{$name}->{description};
	}

	print "=" x $max_size_subsys;
	print "  ";
	print "=" x $max_size_name;
	print "  ";
	print "=" x $max_size_kconfig;
	print "  ";
	print "=" x $max_size_status;
	print "  ";
	print "=" x $max_size_description;
	print "\n";
}

#
# Output a feature on all architectures
#
sub output_feature {
	my $title = "Feature $feat";

	print "=" x length($title) . "\n";
	print "$title\n";
	print "=" x length($title) . "\n\n";

	print ":Subsystem: $data{$feat}->{subsys} \n" if ($data{$feat}->{subsys});
	print ":Kconfig: $data{$feat}->{kconfig} \n" if ($data{$feat}->{kconfig});

	my $desc = $data{$feat}->{description};
	$desc =~ s/^([a-z])/\U$1/;
	$desc =~ s/\.?\s*//;
	print "\n$desc.\n\n";

	my $com = $data{$feat}->{comments};
	$com =~ s/^\s+//;
	$com =~ s/\s+$//;
	if ($com) {
		print "Comments\n";
		print "--------\n\n";
		print "$com\n\n";
	}

	print "=" x $max_size_arch;
	print "  ";
	print "=" x $max_size_status;
	print "\n";

	printf "%-${max_size_arch}s  ", $h_arch;
	printf "%-${max_size_status}s", $h_status . "\n";

	print "=" x $max_size_arch;
	print "  ";
	print "=" x $max_size_status;
	print "\n";

	my %arch_table = %{$data{$feat}->{table}};
	foreach my $arch (sort keys %arch_table) {
		printf "%-${max_size_arch}s  ", $arch;
		printf "%-${max_size_status}s\n", $arch_table{$arch};
	}

	print "=" x $max_size_arch;
	print "  ";
	print "=" x $max_size_status;
	print "\n";
}

#
# Output all features for all architectures
#

sub matrix_lines {
	print "=" x $max_size_subsys;
	print "  ";
	print "=" x $max_size_name;
	print "  ";

	foreach my $arch (sort keys %archs) {
		my $len = $max_size_status;

		$len = length($arch) if ($len < length($arch));

		print "=" x $len;
		print "  ";
	}
	print "=" x $max_size_kconfig;
	print "  ";
	print "=" x $max_size_description;
	print "\n";
}

sub output_matrix {

	my $title = "Feature List (feature x architecture)";

	print "=" x length($title) . "\n";
	print "$title\n";
	print "=" x length($title) . "\n\n";

	matrix_lines;

	printf "%-${max_size_subsys}s  ", $h_subsys;
	printf "%-${max_size_name}s  ", $h_name;

	foreach my $arch (sort keys %archs) {
		printf "%-${max_size_status}s  ", $arch;
	}
	printf "%-${max_size_kconfig}s  ", $h_kconfig;
	printf "%-${max_size_description}s\n", $h_description;

	matrix_lines;

	foreach my $name (sort {
				($data{$a}->{subsys} cmp $data{$b}->{subsys}) ||
				($data{$a}->{name} cmp $data{$b}->{name})
			       } keys %data) {
		printf "%-${max_size_subsys}s  ", $data{$name}->{subsys};
		printf "%-${max_size_name}s  ", $name;

		my %arch_table = %{$data{$name}->{table}};

		foreach my $arch (sort keys %arch_table) {
			my $len = $max_size_status;

			$len = length($arch) if ($len < length($arch));

			printf "%-${len}s  ", $arch_table{$arch};
		}
		printf "%-${max_size_kconfig}s  ", $data{$name}->{kconfig};
		printf "%-${max_size_description}s\n", $data{$name}->{description};
	}

	matrix_lines;
}


#
# Parses all feature files located at $prefix dir
#
find({wanted =>\&parse_feat, no_chdir => 1}, $prefix);

print STDERR Data::Dumper->Dump([\%data], [qw(*data)]) if ($debug);

#
# Handles the command
#
if ($cmd eq "current") {
	$arch = qx(uname -m | sed 's/x86_64/x86/' | sed 's/i386/x86/');
	$arch =~s/\s+$//;
}

if ($cmd ne "validate") {
	if ($arch) {
		output_arch_table;
	} elsif ($feat) {
		output_feature;
	} else {
		output_matrix;
	}
}

__END__

=head1 NAME

get_feat.pl - parse the Linux Feature files and produce a ReST book.

=head1 SYNOPSIS

B<get_feat.pl> [--debug] [--man] [--help] [--dir=<dir>]
	       [--arch=<arch>] [--feat=<feature>] <COMAND> [<ARGUMENT>]

Where <COMMAND> can be:

=over 8

B<current>               - output features for this machine's architecture

B<rest>                  - output features in ReST markup language

B<validate>              - validate the feature contents

=back

=head1 OPTIONS

=over 8

=item B<--arch>

Output features for an specific architecture, optionally filtering for
a single specific feature.

=item B<--feat>

Output features for a single specific architecture.

=item B<--dir>

Changes the location of the Feature files. By default, it uses
the Documentation/features directory.

=item B<--debug>

Put the script in verbose mode, useful for debugging. Can be called multiple
times, to increase verbosity.

=item B<--help>

Prints a brief help message and exits.

=item B<--man>

Prints the manual page and exits.

=back

=head1 DESCRIPTION

Parse the Linux feature files from Documentation/features (by default),
optionally producing results at ReST format.

It supports output data per architecture, per feature or a
feature x arch matrix.

When used with B<rest> command, it will use either one of the tree formats:

If neither B<--arch> or B<--feature> arguments are used, it will output a
matrix with features per architecture.

If B<--arch> argument is used, it will output the features availability for
a given architecture.

If B<--feat> argument is used, it will output the content of the feature
file using ReStructured Text markup.

=head1 BUGS

Report bugs to Mauro Carvalho Chehab <mchehab+samsung@kernel.org>

=head1 COPYRIGHT

Copyright (c) 2019 by Mauro Carvalho Chehab <mchehab+samsung@kernel.org>.

License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>.

This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

=cut
