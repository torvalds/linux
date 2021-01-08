#!/usr/bin/perl
# SPDX-License-Identifier: GPL-2.0

use strict;
use Pod::Usage;
use Getopt::Long;
use File::Find;
use Fcntl ':mode';
use Cwd 'abs_path';

my $help;
my $man;
my $debug;
my $arch;
my $feat;

my $basename = abs_path($0);
$basename =~ s,/[^/]+$,/,;

my $prefix=$basename . "../Documentation/features";

# Used only at for full features output. The script will auto-adjust
# such values for the minimal possible values
my $status_size = 1;
my $description_size = 1;

GetOptions(
	"debug|d+" => \$debug,
	"dir=s" => \$prefix,
	'help|?' => \$help,
	'arch=s' => \$arch,
	'feat=s' => \$feat,
	'feature=s' => \$feat,
	man => \$man
) or pod2usage(2);

pod2usage(1) if $help;
pod2usage(-exitstatus => 0, -verbose => 2) if $man;

pod2usage(1) if (scalar @ARGV < 1 || @ARGV > 2);

my ($cmd, $arg) = @ARGV;

pod2usage(2) if ($cmd ne "current" && $cmd ne "rest" && $cmd ne "validate"
		&& $cmd ne "ls" && $cmd ne "list");

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

my $max_size_arch = 0;
my $max_size_arch_with_header;
my $max_description_word = 0;

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

			foreach my $word (split /\s+/, $description) {
				if (length($word) > $max_description_word) {
					$max_description_word = length($word);
				}
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

	$max_size_arch_with_header = $max_size_arch + length($h_arch);
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
				("\L$a" cmp "\L$b")
			       } keys %data) {
		next if ($feat && $name ne $feat);

		my %arch_table = %{$data{$name}->{table}};
		printf "%-${max_size_subsys}s  ", $data{$name}->{subsys};
		printf "%-${max_size_name}s  ", $name;
		printf "%-${max_size_kconfig}s  ", $data{$name}->{kconfig};
		printf "%-${max_size_status}s  ", $arch_table{$arch};
		printf "%-s\n", $data{$name}->{description};
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
# list feature(s) for a given architecture
#
sub list_arch_features {
	print "#\n# Kernel feature support matrix of the '$arch' architecture:\n#\n";

	foreach my $name (sort {
				($data{$a}->{subsys} cmp $data{$b}->{subsys}) ||
				("\L$a" cmp "\L$b")
			       } keys %data) {
		next if ($feat && $name ne $feat);

		my %arch_table = %{$data{$name}->{table}};

		my $status = $arch_table{$arch};
		$status = " " x ((4 - length($status)) / 2) . $status;

		printf " %${max_size_subsys}s/ ", $data{$name}->{subsys};
		printf "%-${max_size_name}s: ", $name;
		printf "%-5s|   ", $status;
		printf "%${max_size_kconfig}s # ", $data{$name}->{kconfig};
		printf " %s\n", $data{$name}->{description};
	}
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

	print "=" x $max_size_arch_with_header;
	print "  ";
	print "=" x $max_size_status;
	print "\n";

	printf "%-${max_size_arch}s  ", $h_arch;
	printf "%-${max_size_status}s", $h_status . "\n";

	print "=" x $max_size_arch_with_header;
	print "  ";
	print "=" x $max_size_status;
	print "\n";

	my %arch_table = %{$data{$feat}->{table}};
	foreach my $arch (sort keys %arch_table) {
		printf "%-${max_size_arch}s  ", $arch;
		printf "%-${max_size_status}s\n", $arch_table{$arch};
	}

	print "=" x $max_size_arch_with_header;
	print "  ";
	print "=" x $max_size_status;
	print "\n";
}

#
# Output all features for all architectures
#

sub matrix_lines($$$) {
	my $desc_size = shift;
	my $status_size = shift;
	my $header = shift;
	my $fill;
	my $ln_marker;

	if ($header) {
		$ln_marker = "=";
	} else {
		$ln_marker = "-";
	}

	$fill = $ln_marker;

	print "+";
	print $fill x $max_size_name;
	print "+";
	print $fill x $desc_size;
	print "+";
	print $ln_marker x $status_size;
	print "+\n";
}

sub output_matrix {
	my $title = "Feature status on all architectures";
	my $notcompat = "Not compatible";

	print "=" x length($title) . "\n";
	print "$title\n";
	print "=" x length($title) . "\n\n";

	my $desc_title = "$h_kconfig / $h_description";

	my $desc_size = $max_size_kconfig + 4;
	if (!$description_size) {
		$desc_size = $max_size_description if ($max_size_description > $desc_size);
	} else {
		$desc_size = $description_size if ($description_size > $desc_size);
	}
	$desc_size = $max_description_word if ($max_description_word > $desc_size);

	$desc_size = length($desc_title) if (length($desc_title) > $desc_size);

	$max_size_status = length($notcompat) if (length($notcompat) > $max_size_status);

	# Ensure that the status will fit
	my $min_status_size = $max_size_status + $max_size_arch + 6;
	$status_size = $min_status_size if ($status_size < $min_status_size);


	my $cur_subsys = "";
	foreach my $name (sort {
				($data{$a}->{subsys} cmp $data{$b}->{subsys}) or
				("\L$a" cmp "\L$b")
			       } keys %data) {

		if ($cur_subsys ne $data{$name}->{subsys}) {
			if ($cur_subsys ne "") {
				printf "\n";
			}

			$cur_subsys = $data{$name}->{subsys};

			my $title = "Subsystem: $cur_subsys";
			print "$title\n";
			print "=" x length($title) . "\n\n";


			matrix_lines($desc_size, $status_size, 0);

			printf "|%-${max_size_name}s", $h_name;
			printf "|%-${desc_size}s", $desc_title;

			printf "|%-${status_size}s|\n", "Status per architecture";
			matrix_lines($desc_size, $status_size, 1);
		}

		my %arch_table = %{$data{$name}->{table}};
		my $cur_status = "";

		my (@lines, @descs);
		my $line = "";
		foreach my $arch (sort {
					($arch_table{$b} cmp $arch_table{$a}) or
					("\L$a" cmp "\L$b")
				       } keys %arch_table) {

			my $status = $arch_table{$arch};

			if ($status eq "---") {
				$status = $notcompat;
			}

			if ($status ne $cur_status) {
				if ($line ne "") {
					push @lines, $line;
					$line = "";
				}
				$line = "- **" . $status . "**: " . $arch;
			} elsif (length($line) + length ($arch) + 2 < $status_size) {
				$line .= ", " . $arch;
			} else {
				push @lines, $line;
				$line = "  " . $arch;
			}
			$cur_status = $status;
		}
		push @lines, $line if ($line ne "");

		my $description = $data{$name}->{description};
		while (length($description) > $desc_size) {
			my $d = substr $description, 0, $desc_size;

			# Ensure that it will end on a space
			# if it can't, it means that the size is too small
			# Instead of aborting it, let's print what we have
			if (!($d =~ s/^(.*)\s+.*/$1/)) {
				$d = substr $d, 0, -1;
				push @descs, "$d\\";
				$description =~ s/^\Q$d\E//;
			} else {
				push @descs, $d;
				$description =~ s/^\Q$d\E\s+//;
			}
		}
		push @descs, $description;

		# Ensure that the full description will be printed
		push @lines, "" while (scalar(@lines) < 2 + scalar(@descs));

		my $ln = 0;
		for my $line(@lines) {
			if (!$ln) {
				printf "|%-${max_size_name}s", $name;
				printf "|%-${desc_size}s", "``" . $data{$name}->{kconfig} . "``";
			} elsif ($ln >= 2 && scalar(@descs)) {
				printf "|%-${max_size_name}s", "";
				printf "|%-${desc_size}s", shift @descs;
			} else {
				printf "|%-${max_size_name}s", "";
				printf "|%-${desc_size}s", "";
			}

			printf "|%-${status_size}s|\n", $line;

			$ln++;
		}
		matrix_lines($desc_size, $status_size, 0);
	}
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

if ($cmd eq "ls" or $cmd eq "list") {
	if (!$arch) {
		$arch = qx(uname -m | sed 's/x86_64/x86/' | sed 's/i386/x86/');
		$arch =~s/\s+$//;
	}

	list_arch_features;

	exit;
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

B<get_feat.pl> [--debug] [--man] [--help] [--dir=<dir>] [--arch=<arch>]
	       [--feature=<feature>|--feat=<feature>] <COMAND> [<ARGUMENT>]

Where <COMMAND> can be:

=over 8

B<current>               - output table in ReST compatible ASCII format
			   with features for this machine's architecture

B<rest>                  - output table(s)  in ReST compatible ASCII format
			   with features in ReST markup language. The output
			   is affected by --arch or --feat/--feature flags.

B<validate>              - validate the contents of the files under
			   Documentation/features.

B<ls> or B<list>         - list features for this machine's architecture,
			   using an easier to parse format.
			   The output is affected by --arch flag.

=back

=head1 OPTIONS

=over 8

=item B<--arch>

Output features for an specific architecture, optionally filtering for
a single specific feature.

=item B<--feat> or B<--feature>

Output features for a single specific feature.

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
