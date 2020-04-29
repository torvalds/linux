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
my $prefix="Documentation/ABI";

GetOptions(
	"debug|d+" => \$debug,
	"dir=s" => \$prefix,
	'help|?' => \$help,
	man => \$man
) or pod2usage(2);

pod2usage(1) if $help;
pod2usage(-exitstatus => 0, -verbose => 2) if $man;

pod2usage(2) if (scalar @ARGV < 1 || @ARGV > 2);

my ($cmd, $arg) = @ARGV;

pod2usage(2) if ($cmd ne "search" && $cmd ne "rest" && $cmd ne "validate");
pod2usage(2) if ($cmd eq "search" && !$arg);

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
	$data{$nametag}->{filepath} = $file;
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

			if (!($new_tag =~ m/(what|where|date|kernelversion|contact|description|users)/)) {
				if ($tag eq "description") {
					# New "tag" is actually part of
					# description. Don't consider it a tag
					$new_tag = "";
				} elsif ($tag ne "") {
					parse_error($file, $ln, "tag '$tag' is invalid", $_);
				}
			}

			# Invalid, but it is a common mistake
			if ($new_tag eq "where") {
				parse_error($file, $ln, "tag 'Where' is invalid. Should be 'What:' instead", $_);
				$new_tag = "what";
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

			if ($tag ne "" && $new_tag) {
				$tag = $new_tag;

				if ($new_what) {
					@{$data{$what}->{label}} = @labels if ($data{$nametag}->{what});
					@labels = ();
					$label = "";
					$new_what = 0;

					$data{$what}->{type} = $type;
					$data{$what}->{file} = $name;
					$data{$what}->{filepath} = $file;
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

#
# Outputs the book on ReST format
#

my %labels;

sub output_rest {
	foreach my $what (sort {
				($data{$a}->{type} eq "File") cmp ($data{$b}->{type} eq "File") ||
				$a cmp $b
			       } keys %data) {
		my $type = $data{$what}->{type};
		my $file = $data{$what}->{file};
		my $filepath = $data{$what}->{filepath};

		my $w = $what;
		$w =~ s/([\(\)\_\-\*\=\^\~\\])/\\$1/g;


		foreach my $p (@{$data{$what}->{label}}) {
			my ($content, $label) = @{$p};
			$label = "abi_" . $label . " ";
			$label =~ tr/A-Z/a-z/;

			# Convert special chars to "_"
			$label =~s/([\x00-\x2f\x3a-\x40\x5b-\x60\x7b-\xff])/_/g;
			$label =~ s,_+,_,g;
			$label =~ s,_$,,;

			# Avoid duplicated labels
			while (defined($labels{$label})) {
			    my @chars = ("A".."Z", "a".."z");
			    $label .= $chars[rand @chars];
			}
			$labels{$label} = 1;

			$data{$what}->{label} .= $label;

			printf ".. _%s:\n\n", $label;

			# only one label is enough
			last;
		}


		$filepath =~ s,.*/(.*/.*),\1,;;
		$filepath =~ s,[/\-],_,g;;
		my $fileref = "abi_file_".$filepath;

		if ($type eq "File") {
			my $bar = $w;
			$bar =~ s/./-/g;

			print ".. _$fileref:\n\n";
			print "$w\n$bar\n\n";
		} else {
			my @names = split /\s*,\s*/,$w;

			my $len = 0;

			foreach my $name (@names) {
				$len = length($name) if (length($name) > $len);
			}

			print "What:\n\n";

			print "+-" . "-" x $len . "-+\n";
			foreach my $name (@names) {
				printf "| %s", $name . " " x ($len - length($name)) . " |\n";
				print "+-" . "-" x $len . "-+\n";
			}
			print "\n";
		}

		print "Defined on file :ref:`$file <$fileref>`\n\n" if ($type ne "File");

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
# Searches for ABI symbols
#
sub search_symbols {
	foreach my $what (sort keys %data) {
		next if (!($what =~ m/($arg)/));

		my $type = $data{$what}->{type};
		next if ($type eq "File");

		my $file = $data{$what}->{filepath};

		my $bar = $what;
		$bar =~ s/./-/g;

		print "\n$what\n$bar\n\n";

		my $kernelversion = $data{$what}->{kernelversion};
		my $contact = $data{$what}->{contact};
		my $users = $data{$what}->{users};
		my $date = $data{$what}->{date};
		my $desc = $data{$what}->{description};
		$kernelversion =~ s/^\s+//;
		$contact =~ s/^\s+//;
		$users =~ s/^\s+//;
		$users =~ s/\n//g;
		$date =~ s/^\s+//;
		$desc =~ s/^\s+//;

		printf "Kernel version:\t\t%s\n", $kernelversion if ($kernelversion);
		printf "Date:\t\t\t%s\n", $date if ($date);
		printf "Contact:\t\t%s\n", $contact if ($contact);
		printf "Users:\t\t\t%s\n", $users if ($users);
		print "Defined on file:\t$file\n\n";
		print "Description:\n\n$desc";
	}
}


#
# Parses all ABI files located at $prefix dir
#
find({wanted =>\&parse_abi, no_chdir => 1}, $prefix);

print STDERR Data::Dumper->Dump([\%data], [qw(*data)]) if ($debug);

#
# Handles the command
#
if ($cmd eq "rest") {
	output_rest;
} elsif ($cmd eq "search") {
	search_symbols;
}


__END__

=head1 NAME

abi_book.pl - parse the Linux ABI files and produce a ReST book.

=head1 SYNOPSIS

B<abi_book.pl> [--debug] [--man] [--help] [--dir=<dir>] <COMAND> [<ARGUMENT>]

Where <COMMAND> can be:

=over 8

B<search> [SEARCH_REGEX] - search for [SEARCH_REGEX] inside ABI

B<rest>                  - output the ABI in ReST markup language

B<validate>              - validate the ABI contents

=back

=head1 OPTIONS

=over 8

=item B<--dir>

Changes the location of the ABI search. By default, it uses
the Documentation/ABI directory.

=item B<--debug>

Put the script in verbose mode, useful for debugging. Can be called multiple
times, to increase verbosity.

=item B<--help>

Prints a brief help message and exits.

=item B<--man>

Prints the manual page and exits.

=back

=head1 DESCRIPTION

Parse the Linux ABI files from ABI DIR (usually located at Documentation/ABI),
allowing to search for ABI symbols or to produce a ReST book containing
the Linux ABI documentation.

=head1 EXAMPLES

Search for all stable symbols with the word "usb":

=over 8

$ scripts/get_abi.pl search usb --dir Documentation/ABI/stable

=back

Search for all symbols that match the regex expression "usb.*cap":

=over 8

$ scripts/get_abi.pl search usb.*cap

=back

Output all obsoleted symbols in ReST format

=over 8

$ scripts/get_abi.pl rest --dir Documentation/ABI/obsolete

=back

=head1 BUGS

Report bugs to Mauro Carvalho Chehab <mchehab+samsung@kernel.org>

=head1 COPYRIGHT

Copyright (c) 2016-2019 by Mauro Carvalho Chehab <mchehab+samsung@kernel.org>.

License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>.

This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

=cut
