#!/usr/bin/perl -w
# (c) 2001, Dave Jones. <davej@codemonkey.org.uk> (the file handling bit)
# (c) 2005, Joel Scohpp <jschopp@austin.ibm.com> (the ugly bit)
# (c) 2007, Andy Whitcroft <apw@uk.ibm.com> (new conditions, test suite, etc)
# Licensed under the terms of the GNU GPL License version 2

use strict;

my $P = $0;

my $V = '0.03';

use Getopt::Long qw(:config no_auto_abbrev);

my $quiet = 0;
my $tree = 1;
my $chk_signoff = 1;
my $chk_patch = 1;
GetOptions(
	'q|quiet'	=> \$quiet,
	'tree!'		=> \$tree,
	'signoff!'	=> \$chk_signoff,
	'patch!'	=> \$chk_patch,
) or exit;

my $exit = 0;

if ($#ARGV < 0) {
	print "usage: patchstylecheckemail.pl [options] patchfile\n";
	print "version: $V\n";
	print "options: -q           => quiet\n";
	print "         --no-tree    => run without a kernel tree\n";
	exit(1);
}

if ($tree && !top_of_kernel_tree()) {
	print "Must be run from the top-level dir. of a kernel tree\n";
	exit(2);
}

my @dep_includes = ();
my @dep_functions = ();
my $removal = 'Documentation/feature-removal-schedule.txt';
if ($tree && -f $removal) {
	open(REMOVE, "<$removal") || die "$P: $removal: open failed - $!\n";
	while (<REMOVE>) {
		if (/^Files:\s+(.*\S)/) {
			for my $file (split(/[, ]+/, $1)) {
				if ($file =~ m@include/(.*)@) {
					push(@dep_includes, $1);
				}
			}

		} elsif (/^Funcs:\s+(.*\S)/) {
			for my $func (split(/[, ]+/, $1)) {
				push(@dep_functions, $func);
			}
		}
	}
}

my @lines = ();
while (<>) {
	chomp;
	push(@lines, $_);
	if (eof(ARGV)) {
		if (!process($ARGV, @lines)) {
			$exit = 1;
		}
		@lines = ();
	}
}

exit($exit);

sub top_of_kernel_tree {
	if ((-f "COPYING") && (-f "CREDITS") && (-f "Kbuild") &&
	    (-f "MAINTAINERS") && (-f "Makefile") && (-f "README") &&
	    (-d "Documentation") && (-d "arch") && (-d "include") &&
	    (-d "drivers") && (-d "fs") && (-d "init") && (-d "ipc") &&
	    (-d "kernel") && (-d "lib") && (-d "scripts")) {
		return 1;
	}
	return 0;
}

sub expand_tabs {
	my ($str) = @_;

	my $res = '';
	my $n = 0;
	for my $c (split(//, $str)) {
		if ($c eq "\t") {
			$res .= ' ';
			$n++;
			for (; ($n % 8) != 0; $n++) {
				$res .= ' ';
			}
			next;
		}
		$res .= $c;
		$n++;
	}

	return $res;
}

sub line_stats {
	my ($line) = @_;

	# Drop the diff line leader and expand tabs
	$line =~ s/^.//;
	$line = expand_tabs($line);

	# Pick the indent from the front of the line.
	my ($white) = ($line =~ /^(\s*)/);

	return (length($line), length($white));
}

sub ctx_block_get {
	my ($linenr, $remain, $outer) = @_;
	my $line;
	my $start = $linenr - 1;
	my $end = $linenr - 1 + $remain;
	my $blk = '';
	my @o;
	my @c;
	my @res = ();

	for ($line = $start; $line < $end; $line++) {
		$blk .= $lines[$line];

		@o = ($blk =~ /\{/g);
		@c = ($blk =~ /\}/g);

		if (!$outer || (scalar(@o) - scalar(@c)) == 1) {
			push(@res, $lines[$line]);
		}

		last if (scalar(@o) == scalar(@c));
	}

	return @res;
}
sub ctx_block_outer {
	my ($linenr, $remain) = @_;

	return ctx_block_get($linenr, $remain, 1);
}
sub ctx_block {
	my ($linenr, $remain) = @_;

	return ctx_block_get($linenr, $remain, 0);
}

sub ctx_locate_comment {
	my ($first_line, $end_line) = @_;

	# Catch a comment on the end of the line itself.
	my ($current_comment) = ($lines[$end_line - 1] =~ m@.*(/\*.*\*/)\s*$@);
	return $current_comment if (defined $current_comment);

	# Look through the context and try and figure out if there is a
	# comment.
	my $in_comment = 0;
	$current_comment = '';
	for (my $linenr = $first_line; $linenr < $end_line; $linenr++) {
		my $line = $lines[$linenr - 1];
		##warn "           $line\n";
		if ($linenr == $first_line and $line =~ m@^.\s*\*@) {
			$in_comment = 1;
		}
		if ($line =~ m@/\*@) {
			$in_comment = 1;
		}
		if (!$in_comment && $current_comment ne '') {
			$current_comment = '';
		}
		$current_comment .= $line . "\n" if ($in_comment);
		if ($line =~ m@\*/@) {
			$in_comment = 0;
		}
	}

	chomp($current_comment);
	return($current_comment);
}
sub ctx_has_comment {
	my ($first_line, $end_line) = @_;
	my $cmt = ctx_locate_comment($first_line, $end_line);

	##print "LINE: $lines[$end_line - 1 ]\n";
	##print "CMMT: $cmt\n";

	return ($cmt ne '');
}

sub cat_vet {
	my ($vet) = @_;

	$vet =~ s/\t/^I/;
	$vet =~ s/$/\$/;

	return $vet;
}

sub has_non_quoted {
	return ($_[0] =~ m{$_[1]} and $_[0] !~ m{\".*$_[1].*\"});
}

sub process {
	my $filename = shift;
	my @lines = @_;

	my $linenr=0;
	my $prevline="";
	my $stashline="";

	my $length;
	my $indent;
	my $previndent=0;
	my $stashindent=0;

	my $clean = 1;
	my $signoff = 0;
	my $is_patch = 0;

	# Trace the real file/line as we go.
	my $realfile = '';
	my $realline = 0;
	my $realcnt = 0;
	my $here = '';
	my $in_comment = 0;
	my $first_line = 0;

	foreach my $line (@lines) {
		$linenr++;

#extract the filename as it passes
		if ($line=~/^\+\+\+\s+(\S+)/) {
			$realfile=$1;
			$in_comment = 0;
			next;
		}
#extract the line range in the file after the patch is applied
		if ($line=~/^\@\@ -\d+,\d+ \+(\d+)(,(\d+))? \@\@/) {
			$is_patch = 1;
			$first_line = $linenr + 1;
			$in_comment = 0;
			$realline=$1-1;
			if (defined $2) {
				$realcnt=$3+1;
			} else {
				$realcnt=1+1;
			}
			next;
		}

# track the line number as we move through the hunk, note that
# new versions of GNU diff omit the leading space on completely
# blank context lines so we need to count that too.
		if ($line =~ /^( |\+|$)/) {
			$realline++;
			$realcnt-- if ($realcnt != 0);

			# track any sort of multi-line comment.  Obviously if
			# the added text or context do not include the whole
			# comment we will not see it. Such is life.
			#
			# Guestimate if this is a continuing comment.  If this
			# is the start of a diff block and this line starts
			# ' *' then it is very likely a comment.
			if ($linenr == $first_line and $line =~ m@^.\s*\*@) {
				$in_comment = 1;
			}
			if ($line =~ m@/\*@) {
				$in_comment = 1;
			}
			if ($line =~ m@\*/@) {
				$in_comment = 0;
			}

			# Measure the line length and indent.
			($length, $indent) = line_stats($line);

			# Track the previous line.
			($prevline, $stashline) = ($stashline, $line);
			($previndent, $stashindent) = ($stashindent, $indent);
		}

#make up the handle for any error we report on this line
		$here = "PATCH: $ARGV:$linenr:";
		$here .= "\nFILE: $realfile:$realline:" if ($realcnt != 0);

		my $herecurr = "$here\n$line\n\n";
		my $hereprev = "$here\n$prevline\n$line\n\n";

#check the patch for a signoff:
		if ($line =~ /^\s*Signed-off-by:\s/) {
			$signoff++;

		} elsif ($line =~ /^\s*signed-off-by:/i) {
			# This is a signoff, if ugly, so do not double report.
			$signoff++;
			if (!($line =~ /^\s*Signed-off-by:/)) {
				print "use Signed-off-by:\n";
				print "$herecurr";
				$clean = 0;
			}
			if ($line =~ /^\s*signed-off-by:\S/i) {
				print "need space after Signed-off-by:\n";
				print "$herecurr";
				$clean = 0;
			}
		}

#ignore lines not being added
		if ($line=~/^[^\+]/) {next;}

# check we are in a valid source file *.[hcsS] if not then ignore this hunk
		next if ($realfile !~ /\.[hcsS]$/);

#trailing whitespace
		if ($line=~/\S\s+$/) {
			my $herevet = "$here\n" . cat_vet($line) . "\n\n";
			print "trailing whitespace\n";
			print "$herevet";
			$clean = 0;
		}
#80 column limit
		if (!($prevline=~/\/\*\*/) && $length > 80) {
			print "line over 80 characters\n";
			print "$herecurr";
			$clean = 0;
		}

# check we are in a valid source file *.[hc] if not then ignore this hunk
		next if ($realfile !~ /\.[hc]$/);

# at the beginning of a line any tabs must come first and anything
# more than 8 must use tabs.
		if ($line=~/^\+\s* \t\s*\S/ or $line=~/^\+\s*        \s*/) {
			my $herevet = "$here\n" . cat_vet($line) . "\n\n";
			print "use tabs not spaces\n";
			print "$herevet";
			$clean = 0;
		}

		#
		# The rest of our checks refer specifically to C style
		# only apply those _outside_ comments.
		#
		next if ($in_comment);

# no C99 // comments
		if (has_non_quoted($line, '//')) {
			print "do not use C99 // comments\n";
			print "$herecurr";
			$clean = 0;
		}

		# Remove comments from the line before processing.
		$line =~ s@/\*.*\*/@@g;
		$line =~ s@/\*.*@@;
		$line =~ s@.*\*/@@;
		$line =~ s@//.*@@;

#EXPORT_SYMBOL should immediately follow its function closing }.
		if (($line =~ /EXPORT_SYMBOL.*\(.*\)/) ||
		    ($line =~ /EXPORT_UNUSED_SYMBOL.*\(.*\)/)) {
			if (($prevline !~ /^}/) &&
			   ($prevline !~ /^\+}/) &&
			   ($prevline !~ /^ }/)) {
				print "EXPORT_SYMBOL(func); should immediately follow its function\n";
				print "$herecurr";
				$clean = 0;
			}
		}

		# check for static initialisers.
		if ($line=~/\s*static\s.*=\s+(0|NULL);/) {
			print "do not initialise statics to 0 or NULL\n";
			print "$herecurr";
			$clean = 0;
		}

		# check for new typedefs.
		if ($line=~/\s*typedef\s/) {
			print "do not add new typedefs\n";
			print "$herecurr";
			$clean = 0;
		}

# * goes on variable not on type
		if ($line=~/[A-Za-z\d_]+\* [A-Za-z\d_]+/) {
			print "\"foo* bar\" should be \"foo *bar\"\n";
			print "$herecurr";
			$clean = 0;
		}

# # no BUG() or BUG_ON()
# 		if ($line =~ /\b(BUG|BUG_ON)\b/) {
# 			print "Try to use WARN_ON & Recovery code rather than BUG() or BUG_ON()\n";
# 			print "$herecurr";
# 			$clean = 0;
# 		}

# printk should use KERN_* levels
		if ($line =~ /\bprintk\((?!KERN_)/) {
			print "printk() should include KERN_ facility level\n";
			print "$herecurr";
			$clean = 0;
		}

#function brace can't be on same line, except for #defines of do while, or if closed on same line
		if (($line=~/[A-Za-z\d_]+\**\s+\**[A-Za-z\d_]+\(.*\).* {/) and
		    !($line=~/\#define.*do\s{/) and !($line=~/}/)) {
			print "braces following function declarations go on the next line\n";
			print "$herecurr";
			$clean = 0;
		}
		# Note we expand the line with the leading + as the real
		# line will be displayed with the leading + and the tabs
		# will therefore also expand that way.
		my $opline = $line;
		$opline = expand_tabs($opline);
		$opline =~ s/^.//;
		if (!($line=~/\#\s*include/)) {
			# Check operator spacing.
			my @elements = split(/(<<=|>>=|<=|>=|==|!=|\+=|-=|\*=|\/=|%=|\^=|\|=|&=|->|<<|>>|<|>|=|!|~|&&|\|\||,|\^|\+\+|--|;|&|\||\+|-|\*|\/\/|\/)/, $opline);
			my $off = 1;
			for (my $n = 0; $n < $#elements; $n += 2) {
				$off += length($elements[$n]);

				my $a = '';
				$a = 'V' if ($elements[$n] ne '');
				$a = 'W' if ($elements[$n] =~ /\s$/);
				$a = 'B' if ($elements[$n] =~ /(\[|\()$/);
				$a = 'O' if ($elements[$n] eq '');
				$a = 'E' if ($elements[$n] eq '' && $n == 0);

				my $op = $elements[$n + 1];

				my $c = '';
				if (defined $elements[$n + 2]) {
					$c = 'V' if ($elements[$n + 2] ne '');
					$c = 'W' if ($elements[$n + 2] =~ /^\s/);
					$c = 'B' if ($elements[$n + 2] =~ /^(\)|\]|;)/);
					$c = 'O' if ($elements[$n + 2] eq '');
				} else {
					$c = 'E';
				}

				my $ctx = "${a}x${c}";

				my $at = "(ctx:$ctx)";

				my $ptr = (" " x $off) . "^";
				my $hereptr = "$here\n$line\n$ptr\n\n";

				##print "<$s1:$op:$s2> <$elements[$n]:$elements[$n + 1]:$elements[$n + 2]>\n";
				# Skip things apparently in quotes.
				next if ($line=~/\".*\Q$op\E.*\"/ or $line=~/\'\Q$op\E\'/);

				# We need ; as an operator.  // is a comment.
				if ($op eq ';' or $op eq '//') {

				# -> should have no spaces
				} elsif ($op eq '->') {
					if ($ctx =~ /Wx.|.xW/) {
						print "no spaces around that '$op' $at\n";
						print "$hereptr";
						$clean = 0;
					}

				# , must have a space on the right.
				} elsif ($op eq ',') {
					if ($ctx !~ /.xW|.xE/) {
						print "need space after that '$op' $at\n";
						print "$hereptr";
						$clean = 0;
					}

				# unary ! and unary ~ are allowed no space on the right
				} elsif ($op eq '!' or $op eq '~') {
					if ($ctx !~ /[WOEB]x./) {
						print "need space before that '$op' $at\n";
						print "$hereptr";
						$clean = 0;
					}
					if ($ctx =~ /.xW/) {
						print "no space after that '$op' $at\n";
						print "$hereptr";
						$clean = 0;
					}

				# unary ++ and unary -- are allowed no space on one side.
				} elsif ($op eq '++' or $op eq '--') {
					if ($ctx !~ /[WOB]x[^W]|[^W]x[WOB]/) {
						print "need space one side of that '$op' $at\n";
						print "$hereptr";
						$clean = 0;
					}

				# & is both unary and binary
				# unary:
				# 	a &b
				# binary (consistent spacing):
				#	a&b		OK
				#	a & b		OK
				#
				# boiling down to: if there is a space on the right then there
				# should be one on the left.
				#
				# - is the same
				#
				# * is the same only adding:
				# type:
				# 	(foo *)
				#	(foo **)
				#
				} elsif ($op eq '&' or $op eq '-') {
					if ($ctx !~ /VxV|[EWB]x[WE]|[EWB]x[VO]/) {
						print "need space before that '$op' $at\n";
						print "$hereptr";
						$clean = 0;
					}

				} elsif ($op eq '*') {
					if ($ctx !~ /VxV|[EWB]x[WE]|[EWB]x[VO]|[EWO]x[OBV]/) {
						print "need space before that '$op' $at\n";
						print "$hereptr";
						$clean = 0;
					}

				# << and >> may either have or not have spaces both sides
				} elsif ($op eq '<<' or $op eq '>>' or $op eq '+' or $op eq '/' or
					 $op eq '^' or $op eq '|')
				{
					if ($ctx !~ /VxV|WxW|VxE|WxE/) {
						print "need consistent spacing around '$op' $at\n";
						print "$hereptr";
						$clean = 0;
					}

				# All the others need spaces both sides.
				} elsif ($ctx !~ /[EW]x[WE]/) {
					print "need spaces around that '$op' $at\n";
					print "$hereptr";
					$clean = 0;
				}
				$off += length($elements[$n + 1]);
			}
		}

#need space before brace following if, while, etc
		if ($line=~/\(.*\){/) {
			print "need a space before the brace\n";
			print "$herecurr";
			$clean = 0;
		}

#goto labels aren't indented, allow a single space however
		if ($line=~/^.\s+[A-Za-z\d_]+:(?![0-9]+)/ and
		   !($line=~/^. [A-Za-z\d_]+:/) and !($line=~/^.\s+default:/)) {
			print "labels should not be indented\n";
			print "$herecurr";
			$clean = 0;
		}

# Need a space before open parenthesis after if, while etc
		if ($line=~/\b(if|while|for|switch)\(/) {
			print "need a space before the open parenthesis\n";
			print "$herecurr";
			$clean = 0;
		}

# Check for illegal assignment in if conditional.
		if ($line=~/\b(if|while)\s*\(.*[^<>!=]=[^=].*\)/) {
			print "do not use assignment in condition\n";
			print "$herecurr";
			$clean = 0;
		}

		# Check for }<nl>else {, these must be at the same
		# indent level to be relevant to each other.
		if ($prevline=~/}\s*$/ and $line=~/^.\s*else\s*/ and
						$previndent == $indent) {
			print "else should follow close brace\n";
			print "$hereprev";
			$clean = 0;
		}

		# Check for switch () and associated case and default
		# statements should be at the same indent.
		if ($line=~/\bswitch\s*\(.*\)/) {
			my $err = '';
			my $sep = '';
			my @ctx = ctx_block_outer($linenr, $realcnt);
			shift(@ctx);
			for my $ctx (@ctx) {
				my ($clen, $cindent) = line_stats($ctx);
				if ($ctx =~ /\s*(case\s+|default:)/ &&
							$indent != $cindent) {
					$err .= "$sep$ctx\n";
					$sep = '';
				} else {
					$sep = "[...]\n";
				}
			}
			if ($err ne '') {
				print "switch and case should be at the same indent\n";
				print "$here\n$line\n$err\n";
				$clean = 0;
			}
		}

#studly caps, commented out until figure out how to distinguish between use of existing and adding new
#		if (($line=~/[\w_][a-z\d]+[A-Z]/) and !($line=~/print/)) {
#		    print "No studly caps, use _\n";
#		    print "$herecurr";
#		    $clean = 0;
#		}

#no spaces allowed after \ in define
		if ($line=~/\#define.*\\\s$/) {
			print("Whitepspace after \\ makes next lines useless\n");
			print "$herecurr";
			$clean = 0;
		}

#warn if <asm/foo.h> is #included and <linux/foo.h> is available.
		if ($tree && $line =~ qr|\s*\#\s*include\s*\<asm\/(.*)\.h\>|) {
			my $checkfile = "include/linux/$1.h";
			if (-f $checkfile) {
				print "Use #include <linux/$1.h> instead of <asm/$1.h>\n";
				print $herecurr;
				$clean = 0;
			}
		}

#if/while/etc brace do not go on next line, unless #defining a do while loop, or if that brace on the next line is for something else
		if ($prevline=~/\b(if|while|for|switch)\s*\(/) {
			my @opened = $prevline=~/\(/g;
			my @closed = $prevline=~/\)/g;
			my $nr_line = $linenr;
			my $remaining = $realcnt;
			my $next_line = $line;
			my $extra_lines = 0;
			my $display_segment = $prevline;

			while ($remaining > 1 && scalar @opened > scalar @closed) {
				$prevline .= $next_line;
				$display_segment .= "\n" . $next_line;
				$next_line = $lines[$nr_line];
				$nr_line++;
				$remaining--;

				@opened = $prevline=~/\(/g;
				@closed = $prevline=~/\)/g;
			}

			if (($prevline=~/\b(if|while|for|switch)\s*\(.*\)\s*$/) and ($next_line=~/{/) and
			   !($next_line=~/\b(if|while|for)/) and !($next_line=~/\#define.*do.*while/)) {
				print "That { should be on the previous line\n";
				print "$here\n$display_segment\n$next_line\n\n";
				$clean = 0;
			}
		}

#multiline macros should be enclosed in a do while loop
		if (($prevline=~/\#define.*\\/) and !($prevline=~/do\s+{/) and
		   !($prevline=~/\(\{/) and ($line=~/;\s*\\/) and
		   !($line=~/do.*{/) and !($line=~/\(\{/)) {
			print "Macros with multiple statements should be enclosed in a do - while loop\n";
			print "$hereprev";
			$clean = 0;
		}

# don't include deprecated include files
		for my $inc (@dep_includes) {
			if ($line =~ m@\#\s*include\s*\<$inc>@) {
				print "Don't use <$inc>: see Documentation/feature-removal-schedule.txt\n";
				print "$herecurr";
				$clean = 0;
			}
		}

# don't use deprecated functions
		for my $func (@dep_functions) {
			if (has_non_quoted($line, '\b' . $func . '\b')) {
				print "Don't use $func(): see Documentation/feature-removal-schedule.txt\n";
				print "$herecurr";
				$clean = 0;
			}
		}

# no volatiles please
 		if (has_non_quoted($line, '\bvolatile\b')) {
			print "Use of volatile is usually wrong: see Documentation/volatile-considered-harmful.txt\n";
			print "$herecurr";
			$clean = 0;
		}

# warn about #ifdefs in C files
		if ($line =~ /^.#\s*if(|n)def/ && ($realfile =~ /\.c$/)) {
			print "#ifdef in C files should be avoided\n";
			print "$herecurr";
			$clean = 0;
		}

# check for spinlock_t definitions without a comment.
		if ($line =~ /^.\s*(struct\s+mutex|spinlock_t)\s+\S+;/) {
			my $which = $1;
			if (!ctx_has_comment($first_line, $linenr)) {
				print "$1 definition without comment\n";
				print "$herecurr";
				$clean = 0;
			}
		}
# check for memory barriers without a comment.
		if ($line =~ /\b(mb|rmb|wmb|read_barrier_depends|smp_mb|smp_rmb|smp_wmb|smp_read_barrier_depends)\(/) {
			if (!ctx_has_comment($first_line, $linenr)) {
				print "memory barrier without comment\n";
				print "$herecurr";
				$clean = 0;
			}
		}
# check of hardware specific defines
		if ($line =~ m@^.#\s*if.*\b(__i386__|__powerpc64__|__sun__|__s390x__)\b@) {
			print "architecture specific defines should be avoided\n";
			print "$herecurr";
			$clean = 0;
		}
	}

	if ($chk_patch && !$is_patch) {
		$clean = 0;
		print "Does not appear to be a unified-diff format patch\n";
	}
	if ($is_patch && $chk_signoff && $signoff == 0) {
		$clean = 0;
		print "Missing Signed-off-by: line(s)\n";
	}

	if ($clean == 1 && $quiet == 0) {
		print "Your patch has no obvious style problems and is ready for submission.\n"
	}
	if ($clean == 0 && $quiet == 0) {
		print "Your patch has style problems, please review.  If any of these errors\n";
		print "are false positives report them to the maintainer, see\n";
		print "CHECKPATCH in MAINTAINERS.\n";
	}
	return $clean;
}
