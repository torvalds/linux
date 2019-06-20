#!/usr/bin/perl

use strict;
use Pod::Usage;
use Getopt::Long;
use File::Find;
use Fcntl ':mode';

my $help;
my $man;
my $debug;

GetOptions(
	"debug|d+" => \$debug,
	'help|?' => \$help,
	man => \$man
) or pod2usage(2);

pod2usage(1) if $help;
pod2usage(-exitstatus => 0, -verbose => 2) if $man;

pod2usage(2) if (scalar @ARGV != 1);

my ($prefix) = @ARGV;

require Data::Dumper if ($debug);

my %data;

#
# Displays an error message, printing file name and line
#
sub parse_error($$$$) {
	my ($file, $ln, $msg, $data) = @_;

	print STDERR "file $file#$ln: $msg at\n\t$data";
}

#
# Parse an ABI file, storing its contents at %data
#
sub parse_abi {
	my $file = $File::Find::name;

	my $mode = (stat($file))[2];
	return if ($mode & S_IFDIR);
	return if ($file =~ m,/README,);

	my $name = $file;
	$name =~ s,.*/,,;

	my $type = $file;
	$type =~ s,.*/(.*)/.*,$1,;

	my $what;
	my $new_what;
	my $tag;
	my $ln;

	print STDERR "Opening $file\n" if ($debug > 1);
	open IN, $file;
	while(<IN>) {
		$ln++;
		if (m/^(\S+):\s*(.*)/i) {
			my $new_tag = lc($1);
			my $content = $2;

			if (!($new_tag =~ m/(what|date|kernelversion|contact|description|users)/)) {
				if ($tag eq "description") {
					$data{$what}->{$tag} .= "\n$content";;
					$data{$what}->{$tag} =~ s/\n+$//;
					next;
				} else {
					parse_error($file, $ln, "tag '$tag' is invalid", $_);
				}
			}

			if ($new_tag =~ m/what/) {
				if ($tag =~ m/what/) {
					$what .= ", " . $content;
				} else {
					$what = $content;
					$new_what = 1;
				}
				$tag = $new_tag;
				next;
			}

			$tag = $new_tag;

			if ($new_what) {
				$new_what = 0;

				$data{$what}->{type} = $type;
				$data{$what}->{file} = $name;
				print STDERR "\twhat: $what\n" if ($debug > 1);
			}

			if (!$what) {
				parse_error($file, $ln, "'What:' should come first:", $_);
				next;
			}
			$data{$what}->{$tag} = $content;
			next;
		}

		# Silently ignore any headers before the database
		next if (!$tag);

		if (m/^\s*(.*)/) {
			$data{$what}->{$tag} .= "\n$1";
			$data{$what}->{$tag} =~ s/\n+$//;
			next;
		}

		# Everything else is error
		parse_error($file, $ln, "Unexpected line:", $_);
	}
	close IN;
}

# Outputs the output on ReST format
sub output_rest {
	foreach my $what (sort keys %data) {
		my $type = $data{$what}->{type};
		my $file = $data{$what}->{file};

		my $w = $what;
		$w =~ s/([\(\)\_\-\*\=\^\~\\])/\\$1/g;

		print "$w\n\n";
		print "- defined on file $file (type: $type)\n\n::\n\n";

		my $desc = $data{$what}->{description};
		$desc =~ s/^\s+//;

		# Remove title markups from the description, as they won't work
		$desc =~ s/\n[\-\*\=\^\~]+\n/\n/g;

		# put everything inside a code block
		$desc =~ s/\n/\n /g;


		if (!($desc =~ /^\s*$/)) {
			print " $desc\n\n";
		} else {
			print " DESCRIPTION MISSING\n\n";
		}
	}
}

#
# Parses all ABI files located at $prefix dir
#
find({wanted =>\&parse_abi, no_chdir => 1}, $prefix);

print STDERR Data::Dumper->Dump([\%data], [qw(*data)]) if ($debug);

#
# Outputs an ReST file with the ABI contents
#
output_rest


__END__

=head1 NAME

abi_book.pl - parse the Linux ABI files and produce a ReST book.

=head1 SYNOPSIS

B<abi_book.pl> [--debug] <ABI_DIR>]

=head1 OPTIONS

=over 8

=item B<--debug>

Put the script in verbose mode, useful for debugging. Can be called multiple
times, to increase verbosity.

=item B<--help>

Prints a brief help message and exits.

=item B<--man>

Prints the manual page and exits.

=back

=head1 DESCRIPTION

Parse the Linux ABI files from ABI DIR (usually located at Documentation/ABI)
and produce a ReST book containing the Linux ABI.

=head1 BUGS

Report bugs to Mauro Carvalho Chehab <mchehab@s-opensource.com>

=head1 COPYRIGHT

Copyright (c) 2016 by Mauro Carvalho Chehab <mchehab@s-opensource.com>.

License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>.

This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

=cut
