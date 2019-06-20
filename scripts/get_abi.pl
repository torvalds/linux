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

	my $nametag = "File $name";
	$data{$nametag}->{what} = "File $name";
	$data{$nametag}->{type} = "File";
	$data{$nametag}->{file} = $name;
	$data{$nametag}->{is_file} = 1;

	my $type = $file;
	$type =~ s,.*/(.*)/.*,$1,;

	my $what;
	my $new_what;
	my $tag;
	my $ln;
	my $xrefs;
	my $space;
	my @labels;
	my $label;

	print STDERR "Opening $file\n" if ($debug > 1);
	open IN, $file;
	while(<IN>) {
		$ln++;
		if (m/^(\S+)(:\s*)(.*)/i) {
			my $new_tag = lc($1);
			my $sep = $2;
			my $content = $3;

			if (!($new_tag =~ m/(what|date|kernelversion|contact|description|users)/)) {
				if ($tag eq "description") {
					# New "tag" is actually part of
					# description. Don't consider it a tag
					$new_tag = "";
				} else {
					parse_error($file, $ln, "tag '$tag' is invalid", $_);
				}
			}

			if ($new_tag =~ m/what/) {
				$space = "";
				if ($tag =~ m/what/) {
					$what .= ", " . $content;
				} else {
					parse_error($file, $ln, "What '$what' doesn't have a description", "") if ($what && !$data{$what}->{description});

					$what = $content;
					$label = $content;
					$new_what = 1;
				}
				push @labels, [($content, $label)];
				$tag = $new_tag;

				push @{$data{$nametag}->{xrefs}}, [($content, $label)] if ($data{$nametag}->{what});
				next;
			}

			if ($new_tag) {
				$tag = $new_tag;

				if ($new_what) {
					@{$data{$what}->{label}} = @labels if ($data{$nametag}->{what});
					@labels = ();
					$label = "";
					$new_what = 0;

					$data{$what}->{type} = $type;
					$data{$what}->{file} = $name;
					print STDERR "\twhat: $what\n" if ($debug > 1);
				}

				if (!$what) {
					parse_error($file, $ln, "'What:' should come first:", $_);
					next;
				}
				if ($tag eq "description") {
					next if ($content =~ m/^\s*$/);
					if ($content =~ m/^(\s*)(.*)/) {
						my $new_content = $2;
						$space = $new_tag . $sep . $1;
						while ($space =~ s/\t+/' ' x (length($&) * 8 - length($`) % 8)/e) {}
						$space =~ s/./ /g;
						$data{$what}->{$tag} .= "$new_content\n";
					}
				} else {
					$data{$what}->{$tag} = $content;
				}
				next;
			}
		}

		# Store any contents before tags at the database
		if (!$tag && $data{$nametag}->{what}) {
			$data{$nametag}->{description} .= $_;
			next;
		}

		if ($tag eq "description") {
			if (!$data{$what}->{description}) {
				next if (m/^\s*\n/);
				if (m/^(\s*)(.*)/) {
					$space = $1;
					while ($space =~ s/\t+/' ' x (length($&) * 8 - length($`) % 8)/e) {}
					$data{$what}->{$tag} .= "$2\n";
				}
			} else {
				my $content = $_;
				if (m/^\s*\n/) {
					$data{$what}->{$tag} .= $content;
					next;
				}

				while ($content =~ s/\t+/' ' x (length($&) * 8 - length($`) % 8)/e) {}
				$space = "" if (!($content =~ s/^($space)//));

				# Compress spaces with tabs
				$content =~ s<^ {8}> <\t>;
				$content =~ s<^ {1,7}\t> <\t>;
				$content =~ s< {1,7}\t> <\t>;
				$data{$what}->{$tag} .= $content;
			}
			next;
		}
		if (m/^\s*(.*)/) {
			$data{$what}->{$tag} .= "\n$1";
			$data{$what}->{$tag} =~ s/\n+$//;
			next;
		}

		# Everything else is error
		parse_error($file, $ln, "Unexpected line:", $_);
	}
	$data{$nametag}->{description} =~ s/^\n+//;
	close IN;
}

# Outputs the output on ReST format
sub output_rest {
	foreach my $what (sort keys %data) {
		my $type = $data{$what}->{type};
		my $file = $data{$what}->{file};

		my $w = $what;
		$w =~ s/([\(\)\_\-\*\=\^\~\\])/\\$1/g;

		my $bar = $w;
		$bar =~ s/./-/g;

		foreach my $p (@{$data{$what}->{label}}) {
			my ($content, $label) = @{$p};
			$label = "abi_" . $label . " ";
			$label =~ tr/A-Z/a-z/;

			# Convert special chars to "_"
			$label =~s/([\x00-\x2f\x3a-\x40\x5b-\x60\x7b-\xff])/_/g;
			$label =~ s,_+,_,g;
			$label =~ s,_$,,;

			$data{$what}->{label} .= $label;

			printf ".. _%s:\n\n", $label;

			# only one label is enough
			last;
		}

		print "$w\n$bar\n\n";

		print "- defined on file $file (type: $type)\n\n" if ($type ne "File");

		my $desc = $data{$what}->{description};
		$desc =~ s/^\s+//;

		# Remove title markups from the description, as they won't work
		$desc =~ s/\n[\-\*\=\^\~]+\n/\n/g;

		if (!($desc =~ /^\s*$/)) {
			if ($desc =~ m/\:\n/ || $desc =~ m/\n[\t ]+/  || $desc =~ m/[\x00-\x08\x0b-\x1f\x7b-\xff]/) {
				# put everything inside a code block
				$desc =~ s/\n/\n /g;

				print "::\n\n";
				print " $desc\n\n";
			} else {
				# Escape any special chars from description
				$desc =~s/([\x00-\x08\x0b-\x1f\x21-\x2a\x2d\x2f\x3c-\x40\x5c\x5e-\x60\x7b-\xff])/\\$1/g;

				print "$desc\n\n";
			}
		} else {
			print "DESCRIPTION MISSING for $what\n\n" if (!$data{$what}->{is_file});
		}

		if ($data{$what}->{xrefs}) {
			printf "Has the following ABI:\n\n";

			foreach my $p(@{$data{$what}->{xrefs}}) {
				my ($content, $label) = @{$p};
				$label = "abi_" . $label . " ";
				$label =~ tr/A-Z/a-z/;

				# Convert special chars to "_"
				$label =~s/([\x00-\x2f\x3a-\x40\x5b-\x60\x7b-\xff])/_/g;
				$label =~ s,_+,_,g;
				$label =~ s,_$,,;

				# Escape special chars from content
				$content =~s/([\x00-\x1f\x21-\x2f\x3a-\x40\x7b-\xff])/\\$1/g;

				print "- :ref:`$content <$label>`\n\n";
			}
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
