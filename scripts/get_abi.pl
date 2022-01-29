#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0

BEGIN { $Pod::Usage::Formatter = 'Pod::Text::Termcap'; }

use strict;
use warnings;
use utf8;
use Pod::Usage qw(pod2usage);
use Getopt::Long;
use File::Find;
use IO::Handle;
use Fcntl ':mode';
use Cwd 'abs_path';
use Data::Dumper;

my $help = 0;
my $hint = 0;
my $man = 0;
my $debug = 0;
my $enable_lineno = 0;
my $show_warnings = 1;
my $prefix="Documentation/ABI";
my $sysfs_prefix="/sys";
my $search_string;

# Debug options
my $dbg_what_parsing = 1;
my $dbg_what_open = 2;
my $dbg_dump_abi_structs = 4;
my $dbg_undefined = 8;

$Data::Dumper::Indent = 1;
$Data::Dumper::Terse = 1;

#
# If true, assumes that the description is formatted with ReST
#
my $description_is_rst = 1;

GetOptions(
	"debug=i" => \$debug,
	"enable-lineno" => \$enable_lineno,
	"rst-source!" => \$description_is_rst,
	"dir=s" => \$prefix,
	'help|?' => \$help,
	"show-hints" => \$hint,
	"search-string=s" => \$search_string,
	man => \$man
) or pod2usage(2);

pod2usage(1) if $help;
pod2usage(-exitstatus => 0, -noperldoc, -verbose => 2) if $man;

pod2usage(2) if (scalar @ARGV < 1 || @ARGV > 2);

my ($cmd, $arg) = @ARGV;

pod2usage(2) if ($cmd ne "search" && $cmd ne "rest" && $cmd ne "validate" && $cmd ne "undefined");
pod2usage(2) if ($cmd eq "search" && !$arg);

require Data::Dumper if ($debug & $dbg_dump_abi_structs);

my %data;
my %symbols;

#
# Displays an error message, printing file name and line
#
sub parse_error($$$$) {
	my ($file, $ln, $msg, $data) = @_;

	return if (!$show_warnings);

	$data =~ s/\s+$/\n/;

	print STDERR "Warning: file $file#$ln:\n\t$msg";

	if ($data ne "") {
		print STDERR ". Line\n\t\t$data";
	} else {
	    print STDERR "\n";
	}
}

#
# Parse an ABI file, storing its contents at %data
#
sub parse_abi {
	my $file = $File::Find::name;

	my $mode = (stat($file))[2];
	return if ($mode & S_IFDIR);
	return if ($file =~ m,/README,);
	return if ($file =~ m,/\.,);

	my $name = $file;
	$name =~ s,.*/,,;

	my $fn = $file;
	$fn =~ s,Documentation/ABI/,,;

	my $nametag = "File $fn";
	$data{$nametag}->{what} = "File $name";
	$data{$nametag}->{type} = "File";
	$data{$nametag}->{file} = $name;
	$data{$nametag}->{filepath} = $file;
	$data{$nametag}->{is_file} = 1;
	$data{$nametag}->{line_no} = 1;

	my $type = $file;
	$type =~ s,.*/(.*)/.*,$1,;

	my $what;
	my $new_what;
	my $tag = "";
	my $ln;
	my $xrefs;
	my $space;
	my @labels;
	my $label = "";

	print STDERR "Opening $file\n" if ($debug & $dbg_what_open);
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
				parse_error($file, $ln, "tag 'Where' is invalid. Should be 'What:' instead", "");
				$new_tag = "what";
			}

			if ($new_tag =~ m/what/) {
				$space = "";
				$content =~ s/[,.;]$//;

				push @{$symbols{$content}->{file}}, " $file:" . ($ln - 1);

				if ($tag =~ m/what/) {
					$what .= "\xac" . $content;
				} else {
					if ($what) {
						parse_error($file, $ln, "What '$what' doesn't have a description", "") if (!$data{$what}->{description});

						foreach my $w(split /\xac/, $what) {
							$symbols{$w}->{xref} = $what;
						};
					}

					$what = $content;
					$label = $content;
					$new_what = 1;
				}
				push @labels, [($content, $label)];
				$tag = $new_tag;

				push @{$data{$nametag}->{symbols}}, $content if ($data{$nametag}->{what});
				next;
			}

			if ($tag ne "" && $new_tag) {
				$tag = $new_tag;

				if ($new_what) {
					@{$data{$what}->{label_list}} = @labels if ($data{$nametag}->{what});
					@labels = ();
					$label = "";
					$new_what = 0;

					$data{$what}->{type} = $type;
					if (!defined($data{$what}->{file})) {
						$data{$what}->{file} = $name;
						$data{$what}->{filepath} = $file;
					} else {
						$data{$what}->{description} .= "\n\n" if (defined($data{$what}->{description}));
						if ($name ne $data{$what}->{file}) {
							$data{$what}->{file} .= " " . $name;
							$data{$what}->{filepath} .= " " . $file;
						}
					}
					print STDERR "\twhat: $what\n" if ($debug & $dbg_what_parsing);
					$data{$what}->{line_no} = $ln;
				} else {
					$data{$what}->{line_no} = $ln if (!defined($data{$what}->{line_no}));
				}

				if (!$what) {
					parse_error($file, $ln, "'What:' should come first:", $_);
					next;
				}
				if ($new_tag eq "description") {
					$sep =~ s,:, ,;
					$content = ' ' x length($new_tag) . $sep . $content;
					while ($content =~ s/\t+/' ' x (length($&) * 8 - length($`) % 8)/e) {}
					if ($content =~ m/^(\s*)(\S.*)$/) {
						# Preserve initial spaces for the first line
						$space = $1;
						$content = "$2\n";
						$data{$what}->{$tag} .= $content;
					} else {
						undef($space);
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
			my $content = $_;
			while ($content =~ s/\t+/' ' x (length($&) * 8 - length($`) % 8)/e) {}
			if (m/^\s*\n/) {
				$data{$what}->{$tag} .= "\n";
				next;
			}

			if (!defined($space)) {
				# Preserve initial spaces for the first line
				if ($content =~ m/^(\s*)(\S.*)$/) {
					$space = $1;
					$content = "$2\n";
				}
			} else {
				$space = "" if (!($content =~ s/^($space)//));
			}
			$data{$what}->{$tag} .= $content;

			next;
		}
		if (m/^\s*(.*)/) {
			$data{$what}->{$tag} .= "\n$1";
			$data{$what}->{$tag} =~ s/\n+$//;
			next;
		}

		# Everything else is error
		parse_error($file, $ln, "Unexpected content", $_);
	}
	$data{$nametag}->{description} =~ s/^\n+// if ($data{$nametag}->{description});
	if ($what) {
		parse_error($file, $ln, "What '$what' doesn't have a description", "") if (!$data{$what}->{description});

		foreach my $w(split /\xac/,$what) {
			$symbols{$w}->{xref} = $what;
		};
	}
	close IN;
}

sub create_labels {
	my %labels;

	foreach my $what (keys %data) {
		next if ($data{$what}->{file} eq "File");

		foreach my $p (@{$data{$what}->{label_list}}) {
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

			$data{$what}->{label} = $label;

			# only one label is enough
			last;
		}
	}
}

#
# Outputs the book on ReST format
#

# \b doesn't work well with paths. So, we need to define something else:
# Boundaries are punct characters, spaces and end-of-line
my $start = qr {(^|\s|\() }x;
my $bondary = qr { ([,.:;\)\s]|\z) }x;
my $xref_match = qr { $start(\/(sys|config|proc|dev|kvd)\/[^,.:;\)\s]+)$bondary }x;
my $symbols = qr { ([\x01-\x08\x0e-\x1f\x21-\x2f\x3a-\x40\x7b-\xff]) }x;

sub output_rest {
	create_labels();

	my $part = "";

	foreach my $what (sort {
				($data{$a}->{type} eq "File") cmp ($data{$b}->{type} eq "File") ||
				$a cmp $b
			       } keys %data) {
		my $type = $data{$what}->{type};

		my @file = split / /, $data{$what}->{file};
		my @filepath = split / /, $data{$what}->{filepath};

		if ($enable_lineno) {
			printf "#define LINENO %s%s#%s\n\n",
			       $prefix, $file[0],
			       $data{$what}->{line_no};
		}

		my $w = $what;

		if ($type ne "File") {
			my $cur_part = $what;
			if ($what =~ '/') {
				if ($what =~ m#^(\/?(?:[\w\-]+\/?){1,2})#) {
					$cur_part = "Symbols under $1";
					$cur_part =~ s,/$,,;
				}
			}

			if ($cur_part ne "" && $part ne $cur_part) {
			    $part = $cur_part;
			    my $bar = $part;
			    $bar =~ s/./-/g;
			    print "$part\n$bar\n\n";
			}

			printf ".. _%s:\n\n", $data{$what}->{label};

			my @names = split /\xac/,$w;
			my $len = 0;

			foreach my $name (@names) {
				$name =~ s/$symbols/\\$1/g;
				$name = "**$name**";
				$len = length($name) if (length($name) > $len);
			}

			print "+-" . "-" x $len . "-+\n";
			foreach my $name (@names) {
				printf "| %s", $name . " " x ($len - length($name)) . " |\n";
				print "+-" . "-" x $len . "-+\n";
			}

			print "\n";
		}

		for (my $i = 0; $i < scalar(@filepath); $i++) {
			my $path = $filepath[$i];
			my $f = $file[$i];

			$path =~ s,.*/(.*/.*),$1,;;
			$path =~ s,[/\-],_,g;;
			my $fileref = "abi_file_".$path;

			if ($type eq "File") {
				print ".. _$fileref:\n\n";
			} else {
				print "Defined on file :ref:`$f <$fileref>`\n\n";
			}
		}

		if ($type eq "File") {
			my $bar = $w;
			$bar =~ s/./-/g;
			print "$w\n$bar\n\n";
		}

		my $desc = "";
		$desc = $data{$what}->{description} if (defined($data{$what}->{description}));
		$desc =~ s/\s+$/\n/;

		if (!($desc =~ /^\s*$/)) {
			if ($description_is_rst) {
				# Remove title markups from the description
				# Having titles inside ABI files will only work if extra
				# care would be taken in order to strictly follow the same
				# level order for each markup.
				$desc =~ s/\n[\-\*\=\^\~]+\n/\n\n/g;

				# Enrich text by creating cross-references

				my $new_desc = "";
				my $init_indent = -1;
				my $literal_indent = -1;

				open(my $fh, "+<", \$desc);
				while (my $d = <$fh>) {
					my $indent = $d =~ m/^(\s+)/;
					my $spaces = length($indent);
					$init_indent = $indent if ($init_indent < 0);
					if ($literal_indent >= 0) {
						if ($spaces > $literal_indent) {
							$new_desc .= $d;
							next;
						} else {
							$literal_indent = -1;
						}
					} else {
						if ($d =~ /()::$/ && !($d =~ /^\s*\.\./)) {
							$literal_indent = $spaces;
						}
					}

					$d =~ s,Documentation/(?!devicetree)(\S+)\.rst,:doc:`/$1`,g;

					my @matches = $d =~ m,Documentation/ABI/([\w\/\-]+),g;
					foreach my $f (@matches) {
						my $xref = $f;
						my $path = $f;
						$path =~ s,.*/(.*/.*),$1,;;
						$path =~ s,[/\-],_,g;;
						$xref .= " <abi_file_" . $path . ">";
						$d =~ s,\bDocumentation/ABI/$f\b,:ref:`$xref`,g;
					}

					# Seek for cross reference symbols like /sys/...
					@matches = $d =~ m/$xref_match/g;

					foreach my $s (@matches) {
						next if (!($s =~ m,/,));
						if (defined($data{$s}) && defined($data{$s}->{label})) {
							my $xref = $s;

							$xref =~ s/$symbols/\\$1/g;
							$xref = ":ref:`$xref <" . $data{$s}->{label} . ">`";

							$d =~ s,$start$s$bondary,$1$xref$2,g;
						}
					}
					$new_desc .= $d;
				}
				close $fh;


				print "$new_desc\n\n";
			} else {
				$desc =~ s/^\s+//;

				# Remove title markups from the description, as they won't work
				$desc =~ s/\n[\-\*\=\^\~]+\n/\n\n/g;

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
			}
		} else {
			print "DESCRIPTION MISSING for $what\n\n" if (!$data{$what}->{is_file});
		}

		if ($data{$what}->{symbols}) {
			printf "Has the following ABI:\n\n";

			foreach my $content(@{$data{$what}->{symbols}}) {
				my $label = $data{$symbols{$content}->{xref}}->{label};

				# Escape special chars from content
				$content =~s/([\x00-\x1f\x21-\x2f\x3a-\x40\x7b-\xff])/\\$1/g;

				print "- :ref:`$content <$label>`\n\n";
			}
		}

		if (defined($data{$what}->{users})) {
			my $users = $data{$what}->{users};

			$users =~ s/\n/\n\t/g;
			printf "Users:\n\t%s\n\n", $users if ($users ne "");
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

		$what =~ s/\xac/, /g;
		my $bar = $what;
		$bar =~ s/./-/g;

		print "\n$what\n$bar\n\n";

		my $kernelversion = $data{$what}->{kernelversion} if (defined($data{$what}->{kernelversion}));
		my $contact = $data{$what}->{contact} if (defined($data{$what}->{contact}));
		my $users = $data{$what}->{users} if (defined($data{$what}->{users}));
		my $date = $data{$what}->{date} if (defined($data{$what}->{date}));
		my $desc = $data{$what}->{description} if (defined($data{$what}->{description}));

		$kernelversion =~ s/^\s+// if ($kernelversion);
		$contact =~ s/^\s+// if ($contact);
		if ($users) {
			$users =~ s/^\s+//;
			$users =~ s/\n//g;
		}
		$date =~ s/^\s+// if ($date);
		$desc =~ s/^\s+// if ($desc);

		printf "Kernel version:\t\t%s\n", $kernelversion if ($kernelversion);
		printf "Date:\t\t\t%s\n", $date if ($date);
		printf "Contact:\t\t%s\n", $contact if ($contact);
		printf "Users:\t\t\t%s\n", $users if ($users);
		print "Defined on file(s):\t$file\n\n";
		print "Description:\n\n$desc";
	}
}

# Exclude /sys/kernel/debug and /sys/kernel/tracing from the search path
sub dont_parse_special_attributes {
	if (($File::Find::dir =~ m,^/sys/kernel,)) {
		return grep {!/(debug|tracing)/ } @_;
	}

	if (($File::Find::dir =~ m,^/sys/fs,)) {
		return grep {!/(pstore|bpf|fuse)/ } @_;
	}

	return @_
}

my %leaf;
my %aliases;
my @files;
my %root;

sub graph_add_file {
	my $file = shift;
	my $type = shift;

	my $dir = $file;
	$dir =~ s,^(.*/).*,$1,;
	$file =~ s,.*/,,;

	my $name;
	my $file_ref = \%root;
	foreach my $edge(split "/", $dir) {
		$name .= "$edge/";
		if (!defined ${$file_ref}{$edge}) {
			${$file_ref}{$edge} = { };
		}
		$file_ref = \%{$$file_ref{$edge}};
		${$file_ref}{"__name"} = [ $name ];
	}
	$name .= "$file";
	${$file_ref}{$file} = {
		"__name" => [ $name ]
	};

	return \%{$$file_ref{$file}};
}

sub graph_add_link {
	my $file = shift;
	my $link = shift;

	# Traverse graph to find the reference
	my $file_ref = \%root;
	foreach my $edge(split "/", $file) {
		$file_ref = \%{$$file_ref{$edge}} || die "Missing node!";
	}

	# do a BFS

	my @queue;
	my %seen;
	my $st;

	push @queue, $file_ref;
	$seen{$start}++;

	while (@queue) {
		my $v = shift @queue;
		my @child = keys(%{$v});

		foreach my $c(@child) {
			next if $seen{$$v{$c}};
			next if ($c eq "__name");

			if (!defined($$v{$c}{"__name"})) {
				printf STDERR "Error: Couldn't find a non-empty name on a children of $file/.*: ";
				print STDERR Dumper(%{$v});
				exit;
			}

			# Add new name
			my $name = @{$$v{$c}{"__name"}}[0];
			if ($name =~ s#^$file/#$link/#) {
				push @{$$v{$c}{"__name"}}, $name;
			}
			# Add child to the queue and mark as seen
			push @queue, $$v{$c};
			$seen{$c}++;
		}
	}
}

my $escape_symbols = qr { ([\x01-\x08\x0e-\x1f\x21-\x29\x2b-\x2d\x3a-\x40\x7b-\xfe]) }x;
sub parse_existing_sysfs {
	my $file = $File::Find::name;

	my $mode = (lstat($file))[2];
	my $abs_file = abs_path($file);

	my @tmp;
	push @tmp, $file;
	push @tmp, $abs_file if ($abs_file ne $file);

	foreach my $f(@tmp) {
		# Ignore cgroup, as this is big and has zero docs under ABI
		return if ($f =~ m#^/sys/fs/cgroup/#);

		# Ignore firmware as it is documented elsewhere
		# Either ACPI or under Documentation/devicetree/bindings/
		return if ($f =~ m#^/sys/firmware/#);

		# Ignore some sysfs nodes that aren't actually part of ABI
		return if ($f =~ m#/sections|notes/#);

		# Would need to check at
		# Documentation/admin-guide/kernel-parameters.txt, but this
		# is not easily parseable.
		return if ($f =~ m#/parameters/#);
	}

	if (S_ISLNK($mode)) {
		$aliases{$file} = $abs_file;
		return;
	}

	return if (S_ISDIR($mode));

	# Trivial: file is defined exactly the same way at ABI What:
	return if (defined($data{$file}));
	return if (defined($data{$abs_file}));

	push @files, graph_add_file($abs_file, "file");
}

sub get_leave($)
{
	my $what = shift;
	my $leave;

	my $l = $what;
	my $stop = 1;

	$leave = $l;
	$leave =~ s,/$,,;
	$leave =~ s,.*/,,;
	$leave =~ s/[\(\)]//g;

	# $leave is used to improve search performance at
	# check_undefined_symbols, as the algorithm there can seek
	# for a small number of "what". It also allows giving a
	# hint about a leave with the same name somewhere else.
	# However, there are a few occurences where the leave is
	# either a wildcard or a number. Just group such cases
	# altogether.
	if ($leave =~ m/\.\*/ || $leave eq "" || $leave =~ /\\d/) {
		$leave = "others";
	}

	return $leave;
}

my @not_found;

sub check_file($$)
{
	my $file_ref = shift;
	my $names_ref = shift;
	my @names = @{$names_ref};
	my $file = $names[0];

	my $found_string;

	my $leave = get_leave($file);
	if (!defined($leaf{$leave})) {
		$leave = "others";
	}
	my @expr = @{$leaf{$leave}->{expr}};
	die ("\rmissing rules for $leave") if (!defined($leaf{$leave}));

	my $path = $file;
	$path =~ s,(.*/).*,$1,;

	if ($search_string) {
		return if (!($file =~ m#$search_string#));
		$found_string = 1;
	}

	for (my $i = 0; $i < @names; $i++) {
		if ($found_string && $hint) {
			if (!$i) {
				print STDERR "--> $names[$i]\n";
			} else {
				print STDERR "    $names[$i]\n";
			}
		}
		foreach my $re (@expr) {
			print STDERR "$names[$i] =~ /^$re\$/\n" if ($debug && $dbg_undefined);
			if ($names[$i] =~ $re) {
				return;
			}
		}
	}

	if ($leave ne "others") {
		my @expr = @{$leaf{"others"}->{expr}};
		for (my $i = 0; $i < @names; $i++) {
			foreach my $re (@expr) {
				print STDERR "$names[$i] =~ /^$re\$/\n" if ($debug && $dbg_undefined);
				if ($names[$i] =~ $re) {
					return;
				}
			}
		}
	}

	push @not_found, $file if (!$search_string || $found_string);

	if ($hint && (!$search_string || $found_string)) {
		my $what = $leaf{$leave}->{what};
		$what =~ s/\xac/\n\t/g;
		if ($leave ne "others") {
			print STDERR "\r    more likely regexes:\n\t$what\n";
		} else {
			print STDERR "\r    tested regexes:\n\t$what\n";
		}
	}
}

sub check_undefined_symbols {
	my $num_files = scalar @files;
	my $next_i = 0;
	my $start_time = times;

	@files = sort @files;

	my $last_time = $start_time;

	# When either debug or hint is enabled, there's no sense showing
	# progress, as the progress will be overriden.
	if ($hint || ($debug && $dbg_undefined)) {
		$next_i = $num_files;
	}

	my $is_console;
	$is_console = 1 if (-t STDERR);

	for (my $i = 0; $i < $num_files; $i++) {
		my $file_ref = $files[$i];
		my @names = @{$$file_ref{"__name"}};

		check_file($file_ref, \@names);

		my $cur_time = times;

		if ($i == $next_i || $cur_time > $last_time + 1) {
			my $percent = $i * 100 / $num_files;

			my $tm = $cur_time - $start_time;
			my $time = sprintf "%d:%02d", int($tm), 60 * ($tm - int($tm));

			printf STDERR "\33[2K\r", if ($is_console);
			printf STDERR "%s: processing sysfs files... %i%%: $names[0]", $time, $percent;
			printf STDERR "\n", if (!$is_console);
			STDERR->flush();

			$next_i = int (($percent + 1) * $num_files / 100);
			$last_time = $cur_time;
		}
	}

	my $cur_time = times;
	my $tm = $cur_time - $start_time;
	my $time = sprintf "%d:%02d", int($tm), 60 * ($tm - int($tm));

	printf STDERR "\33[2K\r", if ($is_console);
	printf STDERR "%s: processing sysfs files... done\n", $time;

	foreach my $file (@not_found) {
		print "$file not found.\n";
	}
}

sub undefined_symbols {
	print STDERR "Reading $sysfs_prefix directory contents...";
	find({
		wanted =>\&parse_existing_sysfs,
		preprocess =>\&dont_parse_special_attributes,
		no_chdir => 1
	     }, $sysfs_prefix);
	print STDERR "done.\n";

	$leaf{"others"}->{what} = "";

	print STDERR "Converting ABI What fields into regexes...";
	foreach my $w (sort keys %data) {
		foreach my $what (split /\xac/,$w) {
			next if (!($what =~ m/^$sysfs_prefix/));

			# Convert what into regular expressions

			# Escape dot characters
			$what =~ s/\./\xf6/g;

			# Temporarily change [0-9]+ type of patterns
			$what =~ s/\[0\-9\]\+/\xff/g;

			# Temporarily change [\d+-\d+] type of patterns
			$what =~ s/\[0\-\d+\]/\xff/g;
			$what =~ s/\[(\d+)\]/\xf4$1\xf5/g;

			# Temporarily change [0-9] type of patterns
			$what =~ s/\[(\d)\-(\d)\]/\xf4$1-$2\xf5/g;

			# Handle multiple option patterns
			$what =~ s/[\{\<\[]([\w_]+)(?:[,|]+([\w_]+)){1,}[\}\>\]]/($1|$2)/g;

			# Handle wildcards
			$what =~ s,\*,.*,g;
			$what =~ s,/\xf6..,/.*,g;
			$what =~ s/\<[^\>]+\>/.*/g;
			$what =~ s/\{[^\}]+\}/.*/g;
			$what =~ s/\[[^\]]+\]/.*/g;

			$what =~ s/[XYZ]/.*/g;

			# Recover [0-9] type of patterns
			$what =~ s/\xf4/[/g;
			$what =~ s/\xf5/]/g;

			# Remove duplicated spaces
			$what =~ s/\s+/ /g;

			# Special case: this ABI has a parenthesis on it
			$what =~ s/sqrt\(x^2\+y^2\+z^2\)/sqrt\(x^2\+y^2\+z^2\)/;

			# Special case: drop comparition as in:
			#	What: foo = <something>
			# (this happens on a few IIO definitions)
			$what =~ s,\s*\=.*$,,;

			# Escape all other symbols
			$what =~ s/$escape_symbols/\\$1/g;
			$what =~ s/\\\\/\\/g;
			$what =~ s/\\([\[\]\(\)\|])/$1/g;
			$what =~ s/(\d+)\\(-\d+)/$1$2/g;

			$what =~ s/\xff/\\d+/g;

			# Special case: IIO ABI which a parenthesis.
			$what =~ s/sqrt(.*)/sqrt\(.*\)/;

			# Simplify regexes with multiple .*
			$what =~ s#(?:\.\*){2,}##g;
#			$what =~ s#\.\*/\.\*#.*#g;

			# Recover dot characters
			$what =~ s/\xf6/\./g;

			my $leave = get_leave($what);

			my $added = 0;
			foreach my $l (split /\|/, $leave) {
				if (defined($leaf{$l})) {
					next if ($leaf{$l}->{what} =~ m/\b$what\b/);
					$leaf{$l}->{what} .= "\xac" . $what;
					$added = 1;
				} else {
					$leaf{$l}->{what} = $what;
					$added = 1;
				}
			}
			if ($search_string && $added) {
				print STDERR "What: $what\n" if ($what =~ m#$search_string#);
			}

		}
	}
	# Compile regexes
	foreach my $l (sort keys %leaf) {
		my @expr;
		foreach my $w(sort split /\xac/, $leaf{$l}->{what}) {
			push @expr, qr /^$w$/;
		}
		$leaf{$l}->{expr} = \@expr;
	}

	# Take links into account
	foreach my $link (sort keys %aliases) {
		my $abs_file = $aliases{$link};
		graph_add_link($abs_file, $link);
	}
	print STDERR "done.\n";

	check_undefined_symbols;
}

# Ensure that the prefix will always end with a slash
# While this is not needed for find, it makes the patch nicer
# with --enable-lineno
$prefix =~ s,/?$,/,;

if ($cmd eq "undefined" || $cmd eq "search") {
	$show_warnings = 0;
}
#
# Parses all ABI files located at $prefix dir
#
find({wanted =>\&parse_abi, no_chdir => 1}, $prefix);

print STDERR Data::Dumper->Dump([\%data], [qw(*data)]) if ($debug & $dbg_dump_abi_structs);

#
# Handles the command
#
if ($cmd eq "undefined") {
	undefined_symbols;
} elsif ($cmd eq "search") {
	search_symbols;
} else {
	if ($cmd eq "rest") {
		output_rest;
	}

	# Warn about duplicated ABI entries
	foreach my $what(sort keys %symbols) {
		my @files = @{$symbols{$what}->{file}};

		next if (scalar(@files) == 1);

		printf STDERR "Warning: $what is defined %d times: @files\n",
		    scalar(@files);
	}
}

__END__

=head1 NAME

abi_book.pl - parse the Linux ABI files and produce a ReST book.

=head1 SYNOPSIS

B<abi_book.pl> [--debug <level>] [--enable-lineno] [--man] [--help]
	       [--(no-)rst-source] [--dir=<dir>] [--show-hints]
	       [--search-string <regex>]
	       <COMAND> [<ARGUMENT>]

Where B<COMMAND> can be:

=over 8

B<search> I<SEARCH_REGEX> - search for I<SEARCH_REGEX> inside ABI

B<rest>                   - output the ABI in ReST markup language

B<validate>               - validate the ABI contents

B<undefined>              - existing symbols at the system that aren't
                            defined at Documentation/ABI

=back

=head1 OPTIONS

=over 8

=item B<--dir>

Changes the location of the ABI search. By default, it uses
the Documentation/ABI directory.

=item B<--rst-source> and B<--no-rst-source>

The input file may be using ReST syntax or not. Those two options allow
selecting between a rst-compliant source ABI (B<--rst-source>), or a
plain text that may be violating ReST spec, so it requres some escaping
logic (B<--no-rst-source>).

=item B<--enable-lineno>

Enable output of #define LINENO lines.

=item B<--debug> I<debug level>

Print debug information according with the level, which is given by the
following bitmask:

    -  1: Debug parsing What entries from ABI files;
    -  2: Shows what files are opened from ABI files;
    -  4: Dump the structs used to store the contents of the ABI files.

=item B<--show-hints>

Show hints about possible definitions for the missing ABI symbols.
Used only when B<undefined>.

=item B<--search-string> I<regex string>

Show only occurences that match a search string.
Used only when B<undefined>.

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

Report bugs to Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

=head1 COPYRIGHT

Copyright (c) 2016-2021 by Mauro Carvalho Chehab <mchehab+huawei@kernel.org>.

License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>.

This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

=cut
