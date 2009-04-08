#!/usr/bin/perl -w
# (c) 2007, Joe Perches <joe@perches.com>
#           created from checkpatch.pl
#
# Print selected MAINTAINERS information for
# the files modified in a patch or for a file
#
# usage: perl scripts/get_maintainers.pl [OPTIONS] <patch>
#        perl scripts/get_maintainers.pl [OPTIONS] -f <file>
#
# Licensed under the terms of the GNU GPL License version 2

use strict;

my $P = $0;
my $V = '0.14';

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
my $email_git_since = "1-year-ago";
my $output_multiline = 1;
my $output_separator = ", ";
my $scm = 0;
my $web = 0;
my $subsystem = 0;
my $status = 0;
my $onefile = 0;
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

if (!GetOptions(
		'email!' => \$email,
		'git!' => \$email_git,
		'git-chief-penguins!' => \$email_git_penguin_chiefs,
		'git-min-signatures=i' => \$email_git_min_signatures,
		'git-max-maintainers=i' => \$email_git_max_maintainers,
		'git-since=s' => \$email_git_since,
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
		'f|file' => \$onefile,
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

my $infile = $ARGV[0];

if ($#ARGV < 0) {
    usage();
    die "$P: argument missing: patchfile or -f file please\n";
}

my $selections = $email + $scm + $status + $subsystem + $web;
if ($selections == 0) {
    usage();
    die "$P:  Missing required option: email, scm, status, subsystem or web\n";
}

if ($email && ($email_maintainer + $email_list + $email_subscriber_list
	       + $email_git + $email_git_penguin_chiefs) == 0) {
    usage();
    die "$P: Please select at least 1 email option\n";
}

if (!top_of_kernel_tree($lk_path)) {
    die "$P: The current directory does not appear to be "
	. "a linux kernel source tree.\n";
}

## Read MAINTAINERS for type/value pairs

my @typevalue = ();
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
	}
	push(@typevalue, "$type:$value");
    } elsif (!/^(\s)*$/) {
	$line =~ s/\n$//g;
	push(@typevalue, $line);
    }
}
close(MAINT);

## use the filename on the command line or find the filenames in the patchfile

my @files = ();

if ($onefile) {
    if (!(-f $infile)) {
	die "$P: file '${infile}' not found\n";
    }
    push(@files, $infile);
} else {
    open(PATCH, "<$infile") or die "$P: Can't open ${infile}\n";
    while (<PATCH>) {
	if (m/^\+\+\+\s+(\S+)/) {
	    my $file = $1;
	    $file =~ s@^[^/]*/@@;
	    $file =~ s@\n@@;
	    push(@files, $file);
	}
    }
    close(PATCH);
    my $file_cnt = @files;
    if ($file_cnt == 0) {
	print STDERR "$P: file '${infile}' doesn't appear to be a patch.  "
	    . "Add -f to options?\n";
    }
    @files = sort_and_uniq(@files);
}

my @email_to = ();
my @scm = ();
my @web = ();
my @subsystem = ();
my @status = ();

# Find responsible parties

foreach my $file (@files) {

#Do not match excluded file patterns

    my $exclude = 0;
    foreach my $line (@typevalue) {
	if ($line =~ m/^(\C):(.*)/) {
	    my $type = $1;
	    my $value = $2;
	    if ($type eq 'X') {
		if (file_match_pattern($file, $value)) {
		    $exclude = 1;
		}
	    }
	}
    }

    if (!$exclude) {
	my $tvi = 0;
	foreach my $line (@typevalue) {
	    if ($line =~ m/^(\C):(.*)/) {
		my $type = $1;
		my $value = $2;
		if ($type eq 'F') {
		    if (file_match_pattern($file, $value)) {
			add_categories($tvi);
		    }
		}
	    }
	    $tvi++;
	}
    }

    if ($email_git) {
	recent_git_signoffs($file);
    }

}

if ($email_git_penguin_chiefs) {
    foreach my $chief (@penguin_chief) {
	if ($chief =~ m/^(.*):(.*)/) {
	    my $chief_name = $1;
	    my $chief_addr = $2;
	    if ($email_usename) {
		push(@email_to, format_email($chief_name, $chief_addr));
	    } else {
		push(@email_to, $chief_addr);
	    }
	}
    }
}

if ($email) {
    my $address_cnt = @email_to;
    if ($address_cnt == 0 && $email_list) {
	push(@email_to, "linux-kernel\@vger.kernel.org");
    }

#Don't sort email address list, but do remove duplicates
    @email_to = uniq(@email_to);
    output(@email_to);
}

if ($scm) {
    if (!$onefile) {
	@scm = sort_and_uniq(@scm);
    }
    output(@scm);
}

if ($status) {
    if (!$onefile) {
	@status = sort_and_uniq(@status);
    }
    output(@status);
}

if ($subsystem) {
    if (!$onefile) {
	@subsystem = sort_and_uniq(@subsystem);
    }
    output(@subsystem);
}

if ($web) {
    if (!$onefile) {
	@web = sort_and_uniq(@web);
    }
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
       $P [options] -f file
version: $V

MAINTAINER field selection options:
  --email => print email address(es) if any
    --git => include recent git \*-by: signers
    --git-chief-penguins => include ${penguin_chiefs}
    --git-min-signatures => number of signatures required (default: 1)
    --git-max-maintainers => maximum maintainers to add (default: 5)
    --git-since => git history to use (default: 1-year-ago)
    --m => include maintainer(s) if any
    --n => include name 'Full Name <addr\@domain.tld>'
    --l => include list(s) if any
    --s => include subscriber only list(s) if any
  --scm => print SCM tree(s) if any
  --status => print status if any
  --subsystem => print subsystem name if any
  --web => print website(s) if any

Output type options:
  --separator [, ] => separator for multiple entries on 1 line
  --multiline => print 1 entry per line

Default options:
  [--email --git --m --l --multiline]

Other options:
  --version -> show version
  --help => show this help information

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

sub format_email {
    my ($name, $email) = @_;

    $name =~ s/^\s+|\s+$//g;
    $email =~ s/^\s+|\s+$//g;

    my $formatted_email = "";

    if ($name =~ /[^a-z0-9 \.\-]/i) {    ##has "must quote" chars
	$name =~ s/(?<!\\)"/\\"/g;       ##escape quotes
	$formatted_email = "\"${name}\"\ \<${email}\>";
    } else {
	$formatted_email = "${name} \<${email}\>";
    }
    return $formatted_email;
}

sub add_categories {
    my ($index) = @_;

    $index = $index - 1;
    while ($index >= 0) {
	my $tv = $typevalue[$index];
	if ($tv =~ m/^(\C):(.*)/) {
	    my $ptype = $1;
	    my $pvalue = $2;
	    if ($ptype eq "L") {
		my $subscr = $pvalue;
		if ($subscr =~ m/\s*\(subscribers-only\)/) {
		    if ($email_subscriber_list) {
			$subscr =~ s/\s*\(subscribers-only\)//g;
			push(@email_to, $subscr);
		    }
		} else {
		    if ($email_list) {
			push(@email_to, $pvalue);
		    }
		}
	    } elsif ($ptype eq "M") {
		if ($email_maintainer) {
		    if ($index >= 0) {
			my $tv = $typevalue[$index - 1];
			if ($tv =~ m/^(\C):(.*)/) {
			    if ($1 eq "P" && $email_usename) {
				push(@email_to, format_email($2, $pvalue));
			    } else {
				push(@email_to, $pvalue);
			    }
			}
		    } else {
			push(@email_to, $pvalue);
		    }
		}
	    } elsif ($ptype eq "T") {
		push(@scm, $pvalue);
	    } elsif ($ptype eq "W") {
		push(@web, $pvalue);
	    } elsif ($ptype eq "S") {
		push(@status, $pvalue);
	    }

	    $index--;
	} else {
	    push(@subsystem,$tv);
	    $index = -1;
	}
    }
}

sub which {
    my ($bin) = @_;

    foreach my $path (split /:/, $ENV{PATH}) {
	if (-e "$path/$bin") {
	    return "$path/$bin";
	}
    }

    return "";
}

sub recent_git_signoffs {
    my ($file) = @_;

    my $sign_offs = "";
    my $cmd = "";
    my $output = "";
    my $count = 0;
    my @lines = ();

    if (which("git") eq "") {
	die("$P: git not found.  Add --nogit to options?\n");
    }

    $cmd = "git log --since=${email_git_since} -- ${file}";
    $cmd .= " | grep -P '^    [-A-Za-z]+by:.*\\\@'";
    if (!$email_git_penguin_chiefs) {
	$cmd .= " | grep -E -v \"${penguin_chiefs}\"";
    }
    $cmd .= " | sort | uniq -c | sort -rn";

    $output = `${cmd}`;
    $output =~ s/^\s*//gm;

    @lines = split("\n", $output);
    foreach my $line (@lines) {
	if ($line =~ m/([0-9]+)\s+([-A-Za-z]+by:)\s+(.*)/) {
	    my $sign_offs = $1;
	    $line = $3;
	    $count++;
	    if ($sign_offs < $email_git_min_signatures ||
	        $count > $email_git_max_maintainers) {
		last;
	    }
	} else {
	    die("$P: Unexpected git output: ${line}\n");
	}
	if ($line =~ m/(.*) <(.*)>/) {
	    my $git_name = $1;
	    my $git_addr = $2;
	    $git_name =~ tr/^\"//;
	    $git_name =~ tr/\"$//;
	    if ($email_usename) {
		push(@email_to, format_email($git_name, $git_addr));
	    } else {
		push(@email_to, $git_addr);
	    }
	} elsif ($line =~ m/<(.*)>/) {
	    my $git_addr = $1;
	    push(@email_to, $git_addr);
	} else {
	    push(@email_to, $line);
	}
    }
    return $output;
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
