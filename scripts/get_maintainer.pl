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
my $V = '0.23';

use Getopt::Long qw(:config no_auto_abbrev);

my $lk_path = "./";
my $email = 1;
my $email_usename = 1;
my $email_maintainer = 1;
my $email_list = 1;
my $email_subscriber_list = 0;
my $email_git_penguin_chiefs = 0;
my $email_git = 1;
my $email_git_blame = 0;
my $email_git_min_signatures = 1;
my $email_git_max_maintainers = 5;
my $email_git_min_percent = 5;
my $email_git_since = "1-year-ago";
my $email_hg_since = "-365";
my $email_remove_duplicates = 1;
my $output_multiline = 1;
my $output_separator = ", ";
my $output_roles = 0;
my $output_rolestats = 0;
my $scm = 0;
my $web = 0;
my $subsystem = 0;
my $status = 0;
my $keywords = 1;
my $sections = 0;
my $file_emails = 0;
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

# VCS command support: class-like functions and strings

my %VCS_cmds;

my %VCS_cmds_git = (
    "execute_cmd" => \&git_execute_cmd,
    "available" => '(which("git") ne "") && (-d ".git")',
    "find_signers_cmd" => "git log --no-color --since=\$email_git_since -- \$file",
    "find_commit_signers_cmd" => "git log --no-color -1 \$commit",
    "blame_range_cmd" => "git blame -l -L \$diff_start,+\$diff_length \$file",
    "blame_file_cmd" => "git blame -l \$file",
    "commit_pattern" => "^commit [0-9a-f]{40,40}",
    "blame_commit_pattern" => "^([0-9a-f]+) "
);

my %VCS_cmds_hg = (
    "execute_cmd" => \&hg_execute_cmd,
    "available" => '(which("hg") ne "") && (-d ".hg")',
    "find_signers_cmd" =>
	"hg log --date=\$email_hg_since" .
		" --template='commit {node}\\n{desc}\\n' -- \$file",
    "find_commit_signers_cmd" => "hg log --template='{desc}\\n' -r \$commit",
    "blame_range_cmd" => "",		# not supported
    "blame_file_cmd" => "hg blame -c \$file",
    "commit_pattern" => "^commit [0-9a-f]{40,40}",
    "blame_commit_pattern" => "^([0-9a-f]+):"
);

if (!GetOptions(
		'email!' => \$email,
		'git!' => \$email_git,
		'git-blame!' => \$email_git_blame,
		'git-chief-penguins!' => \$email_git_penguin_chiefs,
		'git-min-signatures=i' => \$email_git_min_signatures,
		'git-max-maintainers=i' => \$email_git_max_maintainers,
		'git-min-percent=i' => \$email_git_min_percent,
		'git-since=s' => \$email_git_since,
		'hg-since=s' => \$email_hg_since,
		'remove-duplicates!' => \$email_remove_duplicates,
		'm!' => \$email_maintainer,
		'n!' => \$email_usename,
		'l!' => \$email_list,
		's!' => \$email_subscriber_list,
		'multiline!' => \$output_multiline,
		'roles!' => \$output_roles,
		'rolestats!' => \$output_rolestats,
		'separator=s' => \$output_separator,
		'subsystem!' => \$subsystem,
		'status!' => \$status,
		'scm!' => \$scm,
		'web!' => \$web,
		'pattern-depth=i' => \$pattern_depth,
		'k|keywords!' => \$keywords,
		'sections!' => \$sections,
		'fe|file-emails!' => \$file_emails,
		'f|file' => \$from_filename,
		'v|version' => \$version,
		'h|help|usage' => \$help,
		)) {
    die "$P: invalid argument - use --help if necessary\n";
}

if ($help != 0) {
    usage();
    exit 0;
}

if ($version != 0) {
    print("${P} ${V}\n");
    exit 0;
}

if (-t STDIN && !@ARGV) {
    # We're talking to a terminal, but have no command line arguments.
    die "$P: missing patchfile or -f file - use --help if necessary\n";
}

if ($output_separator ne ", ") {
    $output_multiline = 0;
}

if ($output_rolestats) {
    $output_roles = 1;
}

if ($sections) {
    $email = 0;
    $email_list = 0;
    $scm = 0;
    $status = 0;
    $subsystem = 0;
    $web = 0;
    $keywords = 0;
} else {
    my $selections = $email + $scm + $status + $subsystem + $web;
    if ($selections == 0) {
	die "$P:  Missing required option: email, scm, status, subsystem or web\n";
    }
}

if ($email &&
    ($email_maintainer + $email_list + $email_subscriber_list +
     $email_git + $email_git_penguin_chiefs + $email_git_blame) == 0) {
    die "$P: Please select at least 1 email option\n";
}

if (!top_of_kernel_tree($lk_path)) {
    die "$P: The current directory does not appear to be "
	. "a linux kernel source tree.\n";
}

## Read MAINTAINERS for type/value pairs

my @typevalue = ();
my %keyword_hash;

open (my $maint, '<', "${lk_path}MAINTAINERS")
    or die "$P: Can't open MAINTAINERS: $!\n";
while (<$maint>) {
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
close($maint);

my %mailmap;

if ($email_remove_duplicates) {
    open(my $mailmap, '<', "${lk_path}.mailmap")
	or warn "$P: Can't open .mailmap: $!\n";
    while (<$mailmap>) {
	my $line = $_;

	next if ($line =~ m/^\s*#/);
	next if ($line =~ m/^\s*$/);

	my ($name, $address) = parse_email($line);
	$line = format_email($name, $address, $email_usename);

	next if ($line =~ m/^\s*$/);

	if (exists($mailmap{$name})) {
	    my $obj = $mailmap{$name};
	    push(@$obj, $address);
	} else {
	    my @arr = ($address);
	    $mailmap{$name} = \@arr;
	}
    }
    close($mailmap);
}

## use the filenames on the command line or find the filenames in the patchfiles

my @files = ();
my @range = ();
my @keyword_tvi = ();
my @file_emails = ();

if (!@ARGV) {
    push(@ARGV, "&STDIN");
}

foreach my $file (@ARGV) {
    if ($file ne "&STDIN") {
	##if $file is a directory and it lacks a trailing slash, add one
	if ((-d $file)) {
	    $file =~ s@([^/])$@$1/@;
	} elsif (!(-f $file)) {
	    die "$P: file '${file}' not found\n";
	}
    }
    if ($from_filename) {
	push(@files, $file);
	if (-f $file && ($keywords || $file_emails)) {
	    open(my $f, '<', $file)
		or die "$P: Can't open $file: $!\n";
	    my $text = do { local($/) ; <$f> };
	    close($f);
	    if ($keywords) {
		foreach my $line (keys %keyword_hash) {
		    if ($text =~ m/$keyword_hash{$line}/x) {
			push(@keyword_tvi, $line);
		    }
		}
	    }
	    if ($file_emails) {
		my @poss_addr = $text =~ m$[A-Za-zÀ-ÿ\"\' \,\.\+-]*\s*[\,]*\s*[\(\<\{]{0,1}[A-Za-z0-9_\.\+-]+\@[A-Za-z0-9\.-]+\.[A-Za-z0-9]+[\)\>\}]{0,1}$g;
		push(@file_emails, clean_file_emails(@poss_addr));
	    }
	}
    } else {
	my $file_cnt = @files;
	my $lastfile;

	open(my $patch, "< $file")
	    or die "$P: Can't open $file: $!\n";
	while (<$patch>) {
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
	close($patch);

	if ($file_cnt == @files) {
	    warn "$P: file '${file}' doesn't appear to be a patch.  "
		. "Add -f to options?\n";
	}
	@files = sort_and_uniq(@files);
    }
}

@file_emails = uniq(@file_emails);

my @email_to = ();
my @list_to = ();
my @scm = ();
my @web = ();
my @subsystem = ();
my @status = ();

# Find responsible parties

foreach my $file (@files) {

    my %hash;
    my $tvi = find_first_section();
    while ($tvi < @typevalue) {
	my $start = find_starting_index($tvi);
	my $end = find_ending_index($tvi);
	my $exclude = 0;
	my $i;

	#Do not match excluded file patterns

	for ($i = $start; $i < $end; $i++) {
	    my $line = $typevalue[$i];
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
	    for ($i = $start; $i < $end; $i++) {
		my $line = $typevalue[$i];
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
	    }
	}

	$tvi = $end + 1;
    }

    foreach my $line (sort {$hash{$b} <=> $hash{$a}} keys %hash) {
	add_categories($line);
	    if ($sections) {
		my $i;
		my $start = find_starting_index($line);
		my $end = find_ending_index($line);
		for ($i = $start; $i < $end; $i++) {
		    my $line = $typevalue[$i];
		    if ($line =~ /^[FX]:/) {		##Restore file patterns
			$line =~ s/([^\\])\.([^\*])/$1\?$2/g;
			$line =~ s/([^\\])\.$/$1\?/g;	##Convert . back to ?
			$line =~ s/\\\./\./g;       	##Convert \. to .
			$line =~ s/\.\*/\*/g;       	##Convert .* to *
		    }
		    $line =~ s/^([A-Z]):/$1:\t/g;
		    print("$line\n");
		}
		print("\n");
	    }
    }

    if ($email && $email_git) {
	vcs_file_signoffs($file);
    }

    if ($email && $email_git_blame) {
	vcs_file_blame($file);
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

	    $email_address = format_email($1, $2, $email_usename);
	    if ($email_git_penguin_chiefs) {
		push(@email_to, [$email_address, 'chief penguin']);
	    } else {
		@email_to = grep($_->[0] !~ /${email_address}/, @email_to);
	    }
	}
    }

    foreach my $email (@file_emails) {
	my ($name, $address) = parse_email($email);

	my $tmp_email = format_email($name, $address, $email_usename);
	push_email_address($tmp_email, '');
	add_role($tmp_email, 'in file');
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
    output(merge_email(@to));
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
    --git-blame => use git blame to find modified commits for patch or file
    --git-since => git history to use (default: 1-year-ago)
    --hg-since => hg history to use (default: -365)
    --m => include maintainer(s) if any
    --n => include name 'Full Name <addr\@domain.tld>'
    --l => include list(s) if any
    --s => include subscriber only list(s) if any
    --remove-duplicates => minimize duplicate email names/addresses
    --roles => show roles (status:subsystem, git-signer, list, etc...)
    --rolestats => show roles and statistics (commits/total_commits, %)
    --file-emails => add email addresses found in -f file (default: 0 (off))
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
  --sections => print the entire subsystem sections with pattern matches
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
  Using "--roles" or "--rolestats" with git send-email --cc-cmd or any
      other automated tools that expect only ["name"] <email address>
      may not work because of additional output after <email address>.
  Using "--rolestats" and "--git-blame" shows the #/total=% commits,
      not the percentage of the entire file authored.  # of commits is
      not a good measure of amount of code authored.  1 major commit may
      contain a thousand lines, 5 trivial commits may modify a single line.
  If git is not installed, but mercurial (hg) is installed and an .hg
      repository exists, the following options apply to mercurial:
          --git,
          --git-min-signatures, --git-max-maintainers, --git-min-percent, and
          --git-blame
      Use --hg-since not --git-since to control date selection
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

    if ($name =~ /[^\w \-]/i) {  	 ##has "must quote" chars
	$name =~ s/(?<!\\)"/\\"/g;       ##escape quotes
	$name = "\"$name\"";
    }

    return ($name, $address);
}

sub format_email {
    my ($name, $address, $usename) = @_;

    my $formatted_email;

    $name =~ s/^\s+|\s+$//g;
    $name =~ s/^\"|\"$//g;
    $address =~ s/^\s+|\s+$//g;

    if ($name =~ /[^\w \-]/i) {          ##has "must quote" chars
	$name =~ s/(?<!\\)"/\\"/g;       ##escape quotes
	$name = "\"$name\"";
    }

    if ($usename) {
	if ("$name" eq "") {
	    $formatted_email = "$address";
	} else {
	    $formatted_email = "$name <$address>";
	}
    } else {
	$formatted_email = $address;
    }

    return $formatted_email;
}

sub find_first_section {
    my $index = 0;

    while ($index < @typevalue) {
	my $tv = $typevalue[$index];
	if (($tv =~ m/^(\C):\s*(.*)/)) {
	    last;
	}
	$index++;
    }

    return $index;
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

sub get_maintainer_role {
    my ($index) = @_;

    my $i;
    my $start = find_starting_index($index);
    my $end = find_ending_index($index);

    my $role;
    my $subsystem = $typevalue[$start];
    if (length($subsystem) > 20) {
	$subsystem = substr($subsystem, 0, 17);
	$subsystem =~ s/\s*$//;
	$subsystem = $subsystem . "...";
    }

    for ($i = $start + 1; $i < $end; $i++) {
	my $tv = $typevalue[$i];
	if ($tv =~ m/^(\C):\s*(.*)/) {
	    my $ptype = $1;
	    my $pvalue = $2;
	    if ($ptype eq "S") {
		$role = $pvalue;
	    }
	}
    }

    $role = lc($role);
    if      ($role eq "supported") {
	$role = "supporter";
    } elsif ($role eq "maintained") {
	$role = "maintainer";
    } elsif ($role eq "odd fixes") {
	$role = "odd fixer";
    } elsif ($role eq "orphan") {
	$role = "orphan minder";
    } elsif ($role eq "obsolete") {
	$role = "obsolete minder";
    } elsif ($role eq "buried alive in reporters") {
	$role = "chief penguin";
    }

    return $role . ":" . $subsystem;
}

sub get_list_role {
    my ($index) = @_;

    my $i;
    my $start = find_starting_index($index);
    my $end = find_ending_index($index);

    my $subsystem = $typevalue[$start];
    if (length($subsystem) > 20) {
	$subsystem = substr($subsystem, 0, 17);
	$subsystem =~ s/\s*$//;
	$subsystem = $subsystem . "...";
    }

    if ($subsystem eq "THE REST") {
	$subsystem = "";
    }

    return $subsystem;
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
		my $list_role = get_list_role($i);

		if ($list_role ne "") {
		    $list_role = ":" . $list_role;
		}
		if ($list_address =~ m/([^\s]+)\s+(.*)$/) {
		    $list_address = $1;
		    $list_additional = $2;
		}
		if ($list_additional =~ m/subscribers-only/) {
		    if ($email_subscriber_list) {
			push(@list_to, [$list_address, "subscriber list${list_role}"]);
		    }
		} else {
		    if ($email_list) {
			push(@list_to, [$list_address, "open list${list_role}"]);
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
				$pvalue = format_email($name, $address, $email_usename);
			    }
			}
		    }
		}
		if ($email_maintainer) {
		    my $role = get_maintainer_role($i);
		    push_email_addresses($pvalue, $role);
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
    my ($line, $role) = @_;

    my ($name, $address) = parse_email($line);

    if ($address eq "") {
	return 0;
    }

    if (!$email_remove_duplicates) {
	push(@email_to, [format_email($name, $address, $email_usename), $role]);
    } elsif (!email_inuse($name, $address)) {
	push(@email_to, [format_email($name, $address, $email_usename), $role]);
	$email_hash_name{$name}++;
	$email_hash_address{$address}++;
    }

    return 1;
}

sub push_email_addresses {
    my ($address, $role) = @_;

    my @address_list = ();

    if (rfc822_valid($address)) {
	push_email_address($address, $role);
    } elsif (@address_list = rfc822_validlist($address)) {
	my $array_count = shift(@address_list);
	while (my $entry = shift(@address_list)) {
	    push_email_address($entry, $role);
	}
    } else {
	if (!push_email_address($address, $role)) {
	    warn("Invalid MAINTAINERS address: '" . $address . "'\n");
	}
    }
}

sub add_role {
    my ($line, $role) = @_;

    my ($name, $address) = parse_email($line);
    my $email = format_email($name, $address, $email_usename);

    foreach my $entry (@email_to) {
	if ($email_remove_duplicates) {
	    my ($entry_name, $entry_address) = parse_email($entry->[0]);
	    if (($name eq $entry_name || $address eq $entry_address)
		&& ($role eq "" || !($entry->[1] =~ m/$role/))
	    ) {
		if ($entry->[1] eq "") {
		    $entry->[1] = "$role";
		} else {
		    $entry->[1] = "$entry->[1],$role";
		}
	    }
	} else {
	    if ($email eq $entry->[0]
		&& ($role eq "" || !($entry->[1] =~ m/$role/))
	    ) {
		if ($entry->[1] eq "") {
		    $entry->[1] = "$role";
		} else {
		    $entry->[1] = "$entry->[1],$role";
		}
	    }
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
    my (@lines) = @_;
    my %hash;

    foreach my $line (@lines) {
	my ($name, $address) = parse_email($line);
	if (!exists($hash{$name})) {
	    $hash{$name} = $address;
	} elsif ($address ne $hash{$name}) {
	    $address = $hash{$name};
	    $line = format_email($name, $address, $email_usename);
	}
	if (exists($mailmap{$name})) {
	    my $obj = $mailmap{$name};
	    foreach my $map_address (@$obj) {
		if (($map_address eq $address) &&
		    ($map_address ne $hash{$name})) {
		    $line = format_email($name, $hash{$name}, $email_usename);
		}
	    }
	}
    }

    return @lines;
}

sub git_execute_cmd {
    my ($cmd) = @_;
    my @lines = ();

    my $output = `$cmd`;
    $output =~ s/^\s*//gm;
    @lines = split("\n", $output);

    return @lines;
}

sub hg_execute_cmd {
    my ($cmd) = @_;
    my @lines = ();

    my $output = `$cmd`;
    @lines = split("\n", $output);

    return @lines;
}

sub vcs_find_signers {
    my ($cmd) = @_;
    my @lines = ();
    my $commits;

    @lines = &{$VCS_cmds{"execute_cmd"}}($cmd);

    my $pattern = $VCS_cmds{"commit_pattern"};

    $commits = grep(/$pattern/, @lines);	# of commits

    @lines = grep(/^[-_ 	a-z]+by:.*\@.*$/i, @lines);
    if (!$email_git_penguin_chiefs) {
	@lines = grep(!/${penguin_chiefs}/i, @lines);
    }
    # cut -f2- -d":"
    s/.*:\s*(.+)\s*/$1/ for (@lines);

## Reformat email addresses (with names) to avoid badly written signatures

    foreach my $line (@lines) {
	my ($name, $address) = parse_email($line);
	$line = format_email($name, $address, 1);
    }

    return ($commits, @lines);
}

sub vcs_save_commits {
    my ($cmd) = @_;
    my @lines = ();
    my @commits = ();

    @lines = &{$VCS_cmds{"execute_cmd"}}($cmd);

    foreach my $line (@lines) {
	if ($line =~ m/$VCS_cmds{"blame_commit_pattern"}/) {
	    push(@commits, $1);
	}
    }

    return @commits;
}

sub vcs_blame {
    my ($file) = @_;
    my $cmd;
    my @commits = ();

    return @commits if (!(-f $file));

    if (@range && $VCS_cmds{"blame_range_cmd"} eq "") {
	my @all_commits = ();

	$cmd = $VCS_cmds{"blame_file_cmd"};
	$cmd =~ s/(\$\w+)/$1/eeg;		#interpolate $cmd
	@all_commits = vcs_save_commits($cmd);

	foreach my $file_range_diff (@range) {
	    next if (!($file_range_diff =~ m/(.+):(.+):(.+)/));
	    my $diff_file = $1;
	    my $diff_start = $2;
	    my $diff_length = $3;
	    next if ("$file" ne "$diff_file");
	    for (my $i = $diff_start; $i < $diff_start + $diff_length; $i++) {
		push(@commits, $all_commits[$i]);
	    }
	}
    } elsif (@range) {
	foreach my $file_range_diff (@range) {
	    next if (!($file_range_diff =~ m/(.+):(.+):(.+)/));
	    my $diff_file = $1;
	    my $diff_start = $2;
	    my $diff_length = $3;
	    next if ("$file" ne "$diff_file");
	    $cmd = $VCS_cmds{"blame_range_cmd"};
	    $cmd =~ s/(\$\w+)/$1/eeg;		#interpolate $cmd
	    push(@commits, vcs_save_commits($cmd));
	}
    } else {
	$cmd = $VCS_cmds{"blame_file_cmd"};
	$cmd =~ s/(\$\w+)/$1/eeg;		#interpolate $cmd
	@commits = vcs_save_commits($cmd);
    }

    return @commits;
}

my $printed_novcs = 0;
sub vcs_exists {
    %VCS_cmds = %VCS_cmds_git;
    return 1 if eval $VCS_cmds{"available"};
    %VCS_cmds = %VCS_cmds_hg;
    return 1 if eval $VCS_cmds{"available"};
    %VCS_cmds = ();
    if (!$printed_novcs) {
	warn("$P: No supported VCS found.  Add --nogit to options?\n");
	warn("Using a git repository produces better results.\n");
	warn("Try Linus Torvalds' latest git repository using:\n");
	warn("git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux-2.6.git\n");
	$printed_novcs = 1;
    }
    return 0;
}

sub vcs_assign {
    my ($role, $divisor, @lines) = @_;

    my %hash;
    my $count = 0;

    return if (@lines <= 0);

    if ($divisor <= 0) {
	warn("Bad divisor in " . (caller(0))[3] . ": $divisor\n");
	$divisor = 1;
    }

    if ($email_remove_duplicates) {
	@lines = mailmap(@lines);
    }

    @lines = sort(@lines);

    # uniq -c
    $hash{$_}++ for @lines;

    # sort -rn
    foreach my $line (sort {$hash{$b} <=> $hash{$a}} keys %hash) {
	my $sign_offs = $hash{$line};
	my $percent = $sign_offs * 100 / $divisor;

	$percent = 100 if ($percent > 100);
	$count++;
	last if ($sign_offs < $email_git_min_signatures ||
		 $count > $email_git_max_maintainers ||
		 $percent < $email_git_min_percent);
	push_email_address($line, '');
	if ($output_rolestats) {
	    my $fmt_percent = sprintf("%.0f", $percent);
	    add_role($line, "$role:$sign_offs/$divisor=$fmt_percent%");
	} else {
	    add_role($line, $role);
	}
    }
}

sub vcs_file_signoffs {
    my ($file) = @_;

    my @signers = ();
    my $commits;

    return if (!vcs_exists());

    my $cmd = $VCS_cmds{"find_signers_cmd"};
    $cmd =~ s/(\$\w+)/$1/eeg;		# interpolate $cmd

    ($commits, @signers) = vcs_find_signers($cmd);
    vcs_assign("commit_signer", $commits, @signers);
}

sub vcs_file_blame {
    my ($file) = @_;

    my @signers = ();
    my @commits = ();
    my $total_commits;

    return if (!vcs_exists());

    @commits = vcs_blame($file);
    @commits = uniq(@commits);
    $total_commits = @commits;

    foreach my $commit (@commits) {
	my $commit_count;
	my @commit_signers = ();

	my $cmd = $VCS_cmds{"find_commit_signers_cmd"};
	$cmd =~ s/(\$\w+)/$1/eeg;	#interpolate $cmd

	($commit_count, @commit_signers) = vcs_find_signers($cmd);
	push(@signers, @commit_signers);
    }

    if ($from_filename) {
	vcs_assign("commits", $total_commits, @signers);
    } else {
	vcs_assign("modified commits", $total_commits, @signers);
    }
}

sub uniq {
    my (@parms) = @_;

    my %saw;
    @parms = grep(!$saw{$_}++, @parms);
    return @parms;
}

sub sort_and_uniq {
    my (@parms) = @_;

    my %saw;
    @parms = sort @parms;
    @parms = grep(!$saw{$_}++, @parms);
    return @parms;
}

sub clean_file_emails {
    my (@file_emails) = @_;
    my @fmt_emails = ();

    foreach my $email (@file_emails) {
	$email =~ s/[\(\<\{]{0,1}([A-Za-z0-9_\.\+-]+\@[A-Za-z0-9\.-]+)[\)\>\}]{0,1}/\<$1\>/g;
	my ($name, $address) = parse_email($email);
	if ($name eq '"[,\.]"') {
	    $name = "";
	}

	my @nw = split(/[^A-Za-zÀ-ÿ\'\,\.\+-]/, $name);
	if (@nw > 2) {
	    my $first = $nw[@nw - 3];
	    my $middle = $nw[@nw - 2];
	    my $last = $nw[@nw - 1];

	    if (((length($first) == 1 && $first =~ m/[A-Za-z]/) ||
		 (length($first) == 2 && substr($first, -1) eq ".")) ||
		(length($middle) == 1 ||
		 (length($middle) == 2 && substr($middle, -1) eq "."))) {
		$name = "$first $middle $last";
	    } else {
		$name = "$middle $last";
	    }
	}

	if (substr($name, -1) =~ /[,\.]/) {
	    $name = substr($name, 0, length($name) - 1);
	} elsif (substr($name, -2) =~ /[,\.]"/) {
	    $name = substr($name, 0, length($name) - 2) . '"';
	}

	if (substr($name, 0, 1) =~ /[,\.]/) {
	    $name = substr($name, 1, length($name) - 1);
	} elsif (substr($name, 0, 2) =~ /"[,\.]/) {
	    $name = '"' . substr($name, 2, length($name) - 2);
	}

	my $fmt_email = format_email($name, $address, $email_usename);
	push(@fmt_emails, $fmt_email);
    }
    return @fmt_emails;
}

sub merge_email {
    my @lines;
    my %saw;

    for (@_) {
	my ($address, $role) = @$_;
	if (!$saw{$address}) {
	    if ($output_roles) {
		push(@lines, "$address ($role)");
	    } else {
		push(@lines, $address);
	    }
	    $saw{$address} = 1;
	}
    }

    return @lines;
}

sub output {
    my (@parms) = @_;

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
sub rfc822_valid {
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

sub rfc822_validlist {
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
            push(@r, $1);
        }
        return wantarray ? (scalar(@r), @r) : 1;
    }
    return wantarray ? () : 0;
}
