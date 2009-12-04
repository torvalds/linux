#!/usr/bin/perl -w
# (c) 2007, Joe Perches <joe@perches.com>
#           created from checkpatch.pl
#
# Print selected MAINTAINERS information for
# the files modified in a patch or for a file
#
# usage: perl scripts/get_maintainer.pl [OPTIONS] <patch>
#        perl scripts/get_maintainer.pl [OPTIONS] -f <file>
#
# Licensed under the terms of the GNU GPL License version 2

use strict;

my $P = $0;
my $V = '0.21';

use Getopt::Long qw(:config no_auto_abbrev);

my $lk_path = "./";
my $email = 1;
my $email_usename = 1;
my $email_maintainer = 1;
my $email_list = 1;
my $email_subscriber_list = 0;
my $email_git = 1;
my $email_git_penguin_chiefs = 0;
my $email_git_min_signatures = 1;
my $email_git_max_maintainers = 5;
my $email_git_min_percent = 5;
my $email_git_since = "1-year-ago";
my $email_git_blame = 0;
my $email_remove_duplicates = 1;
my $output_multiline = 1;
my $output_separator = ", ";
my $scm = 0;
my $web = 0;
my $subsystem = 0;
my $status = 0;
my $keywords = 1;
my $from_filename = 0;
my $pattern_depth = 0;
my $version = 0;
my $help = 0;

my $exit = 0;

my @penguin_chief = ();
push(@penguin_chief,"Linus Torvalds:torvalds\@linux-foundation.org");
#Andrew wants in on most everything - 2009/01/14
#push(@penguin_chief,"Andrew Morton:akpm\@linux-foundation.org");

my @penguin_chief_names = ();
foreach my $chief (@penguin_chief) {
    if ($chief =~ m/^(.*):(.*)/) {
	my $chief_name = $1;
	my $chief_addr = $2;
	push(@penguin_chief_names, $chief_name);
    }
}
my $penguin_chiefs = "\(" . join("|",@penguin_chief_names) . "\)";

# rfc822 email address - preloaded methods go here.
my $rfc822_lwsp = "(?:(?:\\r\\n)?[ \\t])";
my $rfc822_char = '[\\000-\\377]';

if (!GetOptions(
		'email!' => \$email,
		'git!' => \$email_git,
		'git-chief-penguins!' => \$email_git_penguin_chiefs,
		'git-min-signatures=i' => \$email_git_min_signatures,
		'git-max-maintainers=i' => \$email_git_max_maintainers,
		'git-min-percent=i' => \$email_git_min_percent,
		'git-since=s' => \$email_git_since,
		'git-blame!' => \$email_git_blame,
		'remove-duplicates!' => \$email_remove_duplicates,
		'm!' => \$email_maintainer,
		'n!' => \$email_usename,
		'l!' => \$email_list,
		's!' => \$email_subscriber_list,
		'multiline!' => \$output_multiline,
		'separator=s' => \$output_separator,
		'subsystem!' => \$subsystem,
		'status!' => \$status,
		'scm!' => \$scm,
		'web!' => \$web,
		'pattern-depth=i' => \$pattern_depth,
		'k|keywords!' => \$keywords,
		'f|file' => \$from_filename,
		'v|version' => \$version,
		'h|help' => \$help,
		)) {
    usage();
    die "$P: invalid argument\n";
}

if ($help != 0) {
    usage();
    exit 0;
}

if ($version != 0) {
    print("${P} ${V}\n");
    exit 0;
}

if ($#ARGV < 0) {
    usage();
    die "$P: argument missing: patchfile or -f file please\n";
}

if ($output_separator ne ", ") {
    $output_multiline = 0;
}

my $selections = $email + $scm + $status + $subsystem + $web;
if ($selections == 0) {
    usage();
    die "$P:  Missing required option: email, scm, status, subsystem or web\n";
}

if ($email &&
    ($email_maintainer + $email_list + $email_subscriber_list +
     $email_git + $email_git_penguin_chiefs + $email_git_blame) == 0) {
    usage();
    die "$P: Please select at least 1 email option\n";
}

if (!top_of_kernel_tree($lk_path)) {
    die "$P: The current directory does not appear to be "
	. "a linux kernel source tree.\n";
}

## Read MAINTAINERS for type/value pairs

my @typevalue = ();
my %keyword_hash;

open(MAINT, "<${lk_path}MAINTAINERS") || die "$P: Can't open MAINTAINERS\n";
while (<MAINT>) {
    my $line = $_;

    if ($line =~ m/^(\C):\s*(.*)/) {
	my $type = $1;
	my $value = $2;

	##Filename pattern matching
	if ($type eq "F" || $type eq "X") {
	    $value =~ s@\.@\\\.@g;       ##Convert . to \.
	    $value =~ s/\*/\.\*/g;       ##Convert * to .*
	    $value =~ s/\?/\./g;         ##Convert ? to .
	    ##if pattern is a directory and it lacks a trailing slash, add one
	    if ((-d $value)) {
		$value =~ s@([^/])$@$1/@;
	    }
	} elsif ($type eq "K") {
	    $keyword_hash{@typevalue} = $value;
	}
	push(@typevalue, "$type:$value");
    } elsif (!/^(\s)*$/) {
	$line =~ s/\n$//g;
	push(@typevalue, $line);
    }
}
close(MAINT);

my %mailmap;

if ($email_remove_duplicates) {
    open(MAILMAP, "<${lk_path}.mailmap") || warn "$P: Can't open .mailmap\n";
    while (<MAILMAP>) {
	my $line = $_;

	next if ($line =~ m/^\s*#/);
	next if ($line =~ m/^\s*$/);

	my ($name, $address) = parse_email($line);
	$line = format_email($name, $address);

	next if ($line =~ m/^\s*$/);

	if (exists($mailmap{$name})) {
	    my $obj = $mailmap{$name};
	    push(@$obj, $address);
	} else {
	    my @arr = ($address);
	    $mailmap{$name} = \@arr;
	}
    }
    close(MAILMAP);
}

## use the filenames on the command line or find the filenames in the patchfiles

my @files = ();
my @range = ();
my @keyword_tvi = ();

foreach my $file (@ARGV) {
    ##if $file is a directory and it lacks a trailing slash, add one
    if ((-d $file)) {
	$file =~ s@([^/])$@$1/@;
    } elsif (!(-f $file)) {
	die "$P: file '${file}' not found\n";
    }
    if ($from_filename) {
	push(@files, $file);
	if (-f $file && $keywords) {
	    open(FILE, "<$file") or die "$P: Can't open ${file}\n";
	    while (<FILE>) {
		my $patch_line = $_;
		foreach my $line (keys %keyword_hash) {
		    if ($patch_line =~ m/^.*$keyword_hash{$line}/x) {
			push(@keyword_tvi, $line);
		    }
		}
	    }
	    close(FILE);
	}
    } else {
	my $file_cnt = @files;
	my $lastfile;
	open(PATCH, "<$file") or die "$P: Can't open ${file}\n";
	while (<PATCH>) {
	    my $patch_line = $_;
	    if (m/^\+\+\+\s+(\S+)/) {
		my $filename = $1;
		$filename =~ s@^[^/]*/@@;
		$filename =~ s@\n@@;
		$lastfile = $filename;
		push(@files, $filename);
	    } elsif (m/^\@\@ -(\d+),(\d+)/) {
		if ($email_git_blame) {
		    push(@range, "$lastfile:$1:$2");
		}
	    } elsif ($keywords) {
		foreach my $line (keys %keyword_hash) {
		    if ($patch_line =~ m/^[+-].*$keyword_hash{$line}/x) {
			push(@keyword_tvi, $line);
		    }
		}
	    }
	}
	close(PATCH);
	if ($file_cnt == @files) {
	    warn "$P: file '${file}' doesn't appear to be a patch.  "
		. "Add -f to options?\n";
	}
	@files = sort_and_uniq(@files);
    }
}

my @email_to = ();
my @list_to = ();
my @scm = ();
my @web = ();
my @subsystem = ();
my @status = ();

# Find responsible parties

foreach my $file (@files) {

#Do not match excluded file patterns

    my $exclude = 0;
    foreach my $line (@typevalue) {
	if ($line =~ m/^(\C):\s*(.*)/) {
	    my $type = $1;
	    my $value = $2;
	    if ($type eq 'X') {
		if (file_match_pattern($file, $value)) {
		    $exclude = 1;
		    last;
		}
	    }
	}
    }

    if (!$exclude) {
	my $tvi = 0;
	my %hash;
	foreach my $line (@typevalue) {
	    if ($line =~ m/^(\C):\s*(.*)/) {
		my $type = $1;
		my $value = $2;
		if ($type eq 'F') {
		    if (file_match_pattern($file, $value)) {
			my $value_pd = ($value =~ tr@/@@);
			my $file_pd = ($file  =~ tr@/@@);
			$value_pd++ if (substr($value,-1,1) ne "/");
			if ($pattern_depth == 0 ||
			    (($file_pd - $value_pd) < $pattern_depth)) {
			    $hash{$tvi} = $value_pd;
			}
		    }
		}
	    }
	    $tvi++;
	}
	foreach my $line (sort {$hash{$b} <=> $hash{$a}} keys %hash) {
	    add_categories($line);
	}
    }

    if ($email && $email_git) {
	recent_git_signoffs($file);
    }

    if ($email && $email_git_blame) {
	git_assign_blame($file);
    }
}

if ($keywords) {
    @keyword_tvi = sort_and_uniq(@keyword_tvi);
    foreach my $line (@keyword_tvi) {
	add_categories($line);
    }
}

if ($email) {
    foreach my $chief (@penguin_chief) {
	if ($chief =~ m/^(.*):(.*)/) {
	    my $email_address;

	    $email_address = format_email($1, $2);
	    if ($email_git_penguin_chiefs) {
		push(@email_to, $email_address);
	    } else {
		@email_to = grep(!/${email_address}/, @email_to);
	    }
	}
    }
}

if ($email || $email_list) {
    my @to = ();
    if ($email) {
	@to = (@to, @email_to);
    }
    if ($email_list) {
	@to = (@to, @list_to);
    }
    output(uniq(@to));
}

if ($scm) {
    @scm = uniq(@scm);
    output(@scm);
}

if ($status) {
    @status = uniq(@status);
    output(@status);
}

if ($subsystem) {
    @subsystem = uniq(@subsystem);
    output(@subsystem);
}

if ($web) {
    @web = uniq(@web);
    output(@web);
}

exit($exit);

sub file_match_pattern {
    my ($file, $pattern) = @_;
    if (substr($pattern, -1) eq "/") {
	if ($file =~ m@^$pattern@) {
	    return 1;
	}
    } else {
	if ($file =~ m@^$pattern@) {
	    my $s1 = ($file =~ tr@/@@);
	    my $s2 = ($pattern =~ tr@/@@);
	    if ($s1 == $s2) {
		return 1;
	    }
	}
    }
    return 0;
}

sub usage {
    print <<EOT;
usage: $P [options] patchfile
       $P [options] -f file|directory
version: $V

MAINTAINER field selection options:
  --email => print email address(es) if any
    --git => include recent git \*-by: signers
    --git-chief-penguins => include ${penguin_chiefs}
    --git-min-signatures => number of signatures required (default: 1)
    --git-max-maintainers => maximum maintainers to add (default: 5)
    --git-min-percent => minimum percentage of commits required (default: 5)
    --git-since => git history to use (default: 1-year-ago)
    --git-blame => use git blame to find modified commits for patch or file
    --m => include maintainer(s) if any
    --n => include name 'Full Name <addr\@domain.tld>'
    --l => include list(s) if any
    --s => include subscriber only list(s) if any
    --remove-duplicates => minimize duplicate email names/addresses
  --scm => print SCM tree(s) if any
  --status => print status if any
  --subsystem => print subsystem name if any
  --web => print website(s) if any

Output type options:
  --separator [, ] => separator for multiple entries on 1 line
    using --separator also sets --nomultiline if --separator is not [, ]
  --multiline => print 1 entry per line

Other options:
  --pattern-depth => Number of pattern directory traversals (default: 0 (all))
  --keywords => scan patch for keywords (default: 1 (on))
  --version => show version
  --help => show this help information

Default options:
  [--email --git --m --n --l --multiline --pattern-depth=0 --remove-duplicates]

Notes:
  Using "-f directory" may give unexpected results:
      Used with "--git", git signators for _all_ files in and below
          directory are examined as git recurses directories.
          Any specified X: (exclude) pattern matches are _not_ ignored.
      Used with "--nogit", directory is used as a pattern match,
         no individual file within the directory or subdirectory
         is matched.
      Used with "--git-blame", does not iterate all files in directory
  Using "--git-blame" is slow and may add old committers and authors
      that are no longer active maintainers to the output.
EOT
}

sub top_of_kernel_tree {
	my ($lk_path) = @_;

	if ($lk_path ne "" && substr($lk_path,length($lk_path)-1,1) ne "/") {
	    $lk_path .= "/";
	}
	if (   (-f "${lk_path}COPYING")
	    && (-f "${lk_path}CREDITS")
	    && (-f "${lk_path}Kbuild")
	    && (-f "${lk_path}MAINTAINERS")
	    && (-f "${lk_path}Makefile")
	    && (-f "${lk_path}README")
	    && (-d "${lk_path}Documentation")
	    && (-d "${lk_path}arch")
	    && (-d "${lk_path}include")
	    && (-d "${lk_path}drivers")
	    && (-d "${lk_path}fs")
	    && (-d "${lk_path}init")
	    && (-d "${lk_path}ipc")
	    && (-d "${lk_path}kernel")
	    && (-d "${lk_path}lib")
	    && (-d "${lk_path}scripts")) {
		return 1;
	}
	return 0;
}

sub parse_email {
    my ($formatted_email) = @_;

    my $name = "";
    my $address = "";

    if ($formatted_email =~ /^([^<]+)<(.+\@.*)>.*$/) {
	$name = $1;
	$address = $2;
    } elsif ($formatted_email =~ /^\s*<(.+\@\S*)>.*$/) {
	$address = $1;
    } elsif ($formatted_email =~ /^(.+\@\S*).*$/) {
	$address = $1;
    }

    $name =~ s/^\s+|\s+$//g;
    $name =~ s/^\"|\"$//g;
    $address =~ s/^\s+|\s+$//g;

    if ($name =~ /[^a-z0-9 \.\-]/i) {    ##has "must quote" chars
	$name =~ s/(?<!\\)"/\\"/g;       ##escape quotes
	$name = "\"$name\"";
    }

    return ($name, $address);
}

sub format_email {
    my ($name, $address) = @_;

    my $formatted_email;

    $name =~ s/^\s+|\s+$//g;
    $name =~ s/^\"|\"$//g;
    $address =~ s/^\s+|\s+$//g;

    if ($name =~ /[^a-z0-9 \.\-]/i) {    ##has "must quote" chars
	$name =~ s/(?<!\\)"/\\"/g;       ##escape quotes
	$name = "\"$name\"";
    }

    if ($email_usename) {
	if ("$name" eq "") {
	    $formatted_email = "$address";
	} else {
	    $formatted_email = "$name <${address}>";
	}
    } else {
	$formatted_email = $address;
    }

    return $formatted_email;
}

sub find_starting_index {
    my ($index) = @_;

    while ($index > 0) {
	my $tv = $typevalue[$index];
	if (!($tv =~ m/^(\C):\s*(.*)/)) {
	    last;
	}
	$index--;
    }

    return $index;
}

sub find_ending_index {
    my ($index) = @_;

    while ($index < @typevalue) {
	my $tv = $typevalue[$index];
	if (!($tv =~ m/^(\C):\s*(.*)/)) {
	    last;
	}
	$index++;
    }

    return $index;
}

sub add_categories {
    my ($index) = @_;

    my $i;
    my $start = find_starting_index($index);
    my $end = find_ending_index($index);

    push(@subsystem, $typevalue[$start]);

    for ($i = $start + 1; $i < $end; $i++) {
	my $tv = $typevalue[$i];
	if ($tv =~ m/^(\C):\s*(.*)/) {
	    my $ptype = $1;
	    my $pvalue = $2;
	    if ($ptype eq "L") {
		my $list_address = $pvalue;
		my $list_additional = "";
		if ($list_address =~ m/([^\s]+)\s+(.*)$/) {
		    $list_address = $1;
		    $list_additional = $2;
		}
		if ($list_additional =~ m/subscribers-only/) {
		    if ($email_subscriber_list) {
			push(@list_to, $list_address);
		    }
		} else {
		    if ($email_list) {
			push(@list_to, $list_address);
		    }
		}
	    } elsif ($ptype eq "M") {
		my ($name, $address) = parse_email($pvalue);
		if ($name eq "") {
		    if ($i > 0) {
			my $tv = $typevalue[$i - 1];
			if ($tv =~ m/^(\C):\s*(.*)/) {
			    if ($1 eq "P") {
				$name = $2;
				$pvalue = format_email($name, $address);
			    }
			}
		    }
		}
		if ($email_maintainer) {
		    push_email_addresses($pvalue);
		}
	    } elsif ($ptype eq "T") {
		push(@scm, $pvalue);
	    } elsif ($ptype eq "W") {
		push(@web, $pvalue);
	    } elsif ($ptype eq "S") {
		push(@status, $pvalue);
	    }
	}
    }
}

my %email_hash_name;
my %email_hash_address;

sub email_inuse {
    my ($name, $address) = @_;

    return 1 if (($name eq "") && ($address eq ""));
    return 1 if (($name ne "") && exists($email_hash_name{$name}));
    return 1 if (($address ne "") && exists($email_hash_address{$address}));

    return 0;
}

sub push_email_address {
    my ($line) = @_;

    my ($name, $address) = parse_email($line);

    if ($address eq "") {
	return 0;
    }

    if (!$email_remove_duplicates) {
	push(@email_to, format_email($name, $address));
    } elsif (!email_inuse($name, $address)) {
	push(@email_to, format_email($name, $address));
	$email_hash_name{$name}++;
	$email_hash_address{$address}++;
    }

    return 1;
}

sub push_email_addresses {
    my ($address) = @_;

    my @address_list = ();

    if (rfc822_valid($address)) {
	push_email_address($address);
    } elsif (@address_list = rfc822_validlist($address)) {
	my $array_count = shift(@address_list);
	while (my $entry = shift(@address_list)) {
	    push_email_address($entry);
	}
    } else {
	if (!push_email_address($address)) {
	    warn("Invalid MAINTAINERS address: '" . $address . "'\n");
	}
    }
}

sub which {
    my ($bin) = @_;

    foreach my $path (split(/:/, $ENV{PATH})) {
	if (-e "$path/$bin") {
	    return "$path/$bin";
	}
    }

    return "";
}

sub mailmap {
    my @lines = @_;
    my %hash;

    foreach my $line (@lines) {
	my ($name, $address) = parse_email($line);
	if (!exists($hash{$name})) {
	    $hash{$name} = $address;
	} elsif ($address ne $hash{$name}) {
	    $address = $hash{$name};
	    $line = format_email($name, $address);
	}
	if (exists($mailmap{$name})) {
	    my $obj = $mailmap{$name};
	    foreach my $map_address (@$obj) {
		if (($map_address eq $address) &&
		    ($map_address ne $hash{$name})) {
		    $line = format_email($name, $hash{$name});
		}
	    }
	}
    }

    return @lines;
}

sub recent_git_signoffs {
    my ($file) = @_;

    my $sign_offs = "";
    my $cmd = "";
    my $output = "";
    my $count = 0;
    my @lines = ();
    my %hash;
    my $total_sign_offs;

    if (which("git") eq "") {
	warn("$P: git not found.  Add --nogit to options?\n");
	return;
    }
    if (!(-d ".git")) {
	warn("$P: .git directory not found.  Use a git repository for better results.\n");
	warn("$P: perhaps 'git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux-2.6.git'\n");
	return;
    }

    $cmd = "git log --since=${email_git_since} -- ${file}";

    $output = `${cmd}`;
    $output =~ s/^\s*//gm;

    @lines = split("\n", $output);

    @lines = grep(/^[-_ 	a-z]+by:.*\@.*$/i, @lines);
    if (!$email_git_penguin_chiefs) {
	@lines = grep(!/${penguin_chiefs}/i, @lines);
    }
    # cut -f2- -d":"
    s/.*:\s*(.+)\s*/$1/ for (@lines);

    $total_sign_offs = @lines;

    if ($email_remove_duplicates) {
	@lines = mailmap(@lines);
    }

    @lines = sort(@lines);

    # uniq -c
    $hash{$_}++ for @lines;

    # sort -rn
    foreach my $line (sort {$hash{$b} <=> $hash{$a}} keys %hash) {
	my $sign_offs = $hash{$line};
	$count++;
	last if ($sign_offs < $email_git_min_signatures ||
		 $count > $email_git_max_maintainers ||
		 $sign_offs * 100 / $total_sign_offs < $email_git_min_percent);
	push_email_address($line);
    }
}

sub save_commits {
    my ($cmd, @commits) = @_;
    my $output;
    my @lines = ();

    $output = `${cmd}`;

    @lines = split("\n", $output);
    foreach my $line (@lines) {
	if ($line =~ m/^(\w+) /) {
	    push (@commits, $1);
	}
    }
    return @commits;
}

sub git_assign_blame {
    my ($file) = @_;

    my @lines = ();
    my @commits = ();
    my $cmd;
    my $output;
    my %hash;
    my $total_sign_offs;
    my $count;

    if (@range) {
	foreach my $file_range_diff (@range) {
	    next if (!($file_range_diff =~ m/(.+):(.+):(.+)/));
	    my $diff_file = $1;
	    my $diff_start = $2;
	    my $diff_length = $3;
	    next if (!("$file" eq "$diff_file"));
	    $cmd = "git blame -l -L $diff_start,+$diff_length $file";
	    @commits = save_commits($cmd, @commits);
	}
    } else {
	if (-f $file) {
	    $cmd = "git blame -l $file";
	    @commits = save_commits($cmd, @commits);
	}
    }

    $total_sign_offs = 0;
    @commits = uniq(@commits);
    foreach my $commit (@commits) {
	$cmd = "git log -1 ${commit}";

	$output = `${cmd}`;
	$output =~ s/^\s*//gm;
	@lines = split("\n", $output);

	@lines = grep(/^[-_ 	a-z]+by:.*\@.*$/i, @lines);
	if (!$email_git_penguin_chiefs) {
	    @lines = grep(!/${penguin_chiefs}/i, @lines);
	}

	# cut -f2- -d":"
	s/.*:\s*(.+)\s*/$1/ for (@lines);

	$total_sign_offs += @lines;

	if ($email_remove_duplicates) {
	    @lines = mailmap(@lines);
	}

	$hash{$_}++ for @lines;
    }

    $count = 0;
    foreach my $line (sort {$hash{$b} <=> $hash{$a}} keys %hash) {
	my $sign_offs = $hash{$line};
	$count++;
	last if ($sign_offs < $email_git_min_signatures ||
		 $count > $email_git_max_maintainers ||
		 $sign_offs * 100 / $total_sign_offs < $email_git_min_percent);
	push_email_address($line);
    }
}

sub uniq {
    my @parms = @_;

    my %saw;
    @parms = grep(!$saw{$_}++, @parms);
    return @parms;
}

sub sort_and_uniq {
    my @parms = @_;

    my %saw;
    @parms = sort @parms;
    @parms = grep(!$saw{$_}++, @parms);
    return @parms;
}

sub output {
    my @parms = @_;

    if ($output_multiline) {
	foreach my $line (@parms) {
	    print("${line}\n");
	}
    } else {
	print(join($output_separator, @parms));
	print("\n");
    }
}

my $rfc822re;

sub make_rfc822re {
#   Basic lexical tokens are specials, domain_literal, quoted_string, atom, and
#   comment.  We must allow for rfc822_lwsp (or comments) after each of these.
#   This regexp will only work on addresses which have had comments stripped
#   and replaced with rfc822_lwsp.

    my $specials = '()<>@,;:\\\\".\\[\\]';
    my $controls = '\\000-\\037\\177';

    my $dtext = "[^\\[\\]\\r\\\\]";
    my $domain_literal = "\\[(?:$dtext|\\\\.)*\\]$rfc822_lwsp*";

    my $quoted_string = "\"(?:[^\\\"\\r\\\\]|\\\\.|$rfc822_lwsp)*\"$rfc822_lwsp*";

#   Use zero-width assertion to spot the limit of an atom.  A simple
#   $rfc822_lwsp* causes the regexp engine to hang occasionally.
    my $atom = "[^$specials $controls]+(?:$rfc822_lwsp+|\\Z|(?=[\\[\"$specials]))";
    my $word = "(?:$atom|$quoted_string)";
    my $localpart = "$word(?:\\.$rfc822_lwsp*$word)*";

    my $sub_domain = "(?:$atom|$domain_literal)";
    my $domain = "$sub_domain(?:\\.$rfc822_lwsp*$sub_domain)*";

    my $addr_spec = "$localpart\@$rfc822_lwsp*$domain";

    my $phrase = "$word*";
    my $route = "(?:\@$domain(?:,\@$rfc822_lwsp*$domain)*:$rfc822_lwsp*)";
    my $route_addr = "\\<$rfc822_lwsp*$route?$addr_spec\\>$rfc822_lwsp*";
    my $mailbox = "(?:$addr_spec|$phrase$route_addr)";

    my $group = "$phrase:$rfc822_lwsp*(?:$mailbox(?:,\\s*$mailbox)*)?;\\s*";
    my $address = "(?:$mailbox|$group)";

    return "$rfc822_lwsp*$address";
}

sub rfc822_strip_comments {
    my $s = shift;
#   Recursively remove comments, and replace with a single space.  The simpler
#   regexps in the Email Addressing FAQ are imperfect - they will miss escaped
#   chars in atoms, for example.

    while ($s =~ s/^((?:[^"\\]|\\.)*
                    (?:"(?:[^"\\]|\\.)*"(?:[^"\\]|\\.)*)*)
                    \((?:[^()\\]|\\.)*\)/$1 /osx) {}
    return $s;
}

#   valid: returns true if the parameter is an RFC822 valid address
#
sub rfc822_valid ($) {
    my $s = rfc822_strip_comments(shift);

    if (!$rfc822re) {
        $rfc822re = make_rfc822re();
    }

    return $s =~ m/^$rfc822re$/so && $s =~ m/^$rfc822_char*$/;
}

#   validlist: In scalar context, returns true if the parameter is an RFC822
#              valid list of addresses.
#
#              In list context, returns an empty list on failure (an invalid
#              address was found); otherwise a list whose first element is the
#              number of addresses found and whose remaining elements are the
#              addresses.  This is needed to disambiguate failure (invalid)
#              from success with no addresses found, because an empty string is
#              a valid list.

sub rfc822_validlist ($) {
    my $s = rfc822_strip_comments(shift);

    if (!$rfc822re) {
        $rfc822re = make_rfc822re();
    }
    # * null list items are valid according to the RFC
    # * the '1' business is to aid in distinguishing failure from no results

    my @r;
    if ($s =~ m/^(?:$rfc822re)?(?:,(?:$rfc822re)?)*$/so &&
	$s =~ m/^$rfc822_char*$/) {
        while ($s =~ m/(?:^|,$rfc822_lwsp*)($rfc822re)/gos) {
            push @r, $1;
        }
        return wantarray ? (scalar(@r), @r) : 1;
    }
    else {
        return wantarray ? () : 0;
    }
}
