#!/usr/bin/perl -w
# (c) 2001, Dave Jones. (the file handling bit)
# (c) 2005, Joel Schopp <jschopp@austin.ibm.com> (the ugly bit)
# (c) 2007,2008, Andy Whitcroft <apw@uk.ibm.com> (new conditions, test suite)
# (c) 2008,2009, Andy Whitcroft <apw@canonical.com>
# Licensed under the terms of the GNU GPL License version 2

use strict;

my $P = $0;
$P =~ s@.*/@@g;

my $V = '0.30';

use Getopt::Long qw(:config no_auto_abbrev);

my $quiet = 0;
my $tree = 1;
my $chk_signoff = 1;
my $chk_patch = 1;
my $tst_only;
my $emacs = 0;
my $terse = 0;
my $file = 0;
my $check = 0;
my $summary = 1;
my $mailback = 0;
my $summary_file = 0;
my $root;
my %debug;
my $help = 0;

sub help {
	my ($exitcode) = @_;

	print << "EOM";
Usage: $P [OPTION]... [FILE]...
Version: $V

Options:
  -q, --quiet                quiet
  --no-tree                  run without a kernel tree
  --no-signoff               do not check for 'Signed-off-by' line
  --patch                    treat FILE as patchfile (default)
  --emacs                    emacs compile window format
  --terse                    one line per report
  -f, --file                 treat FILE as regular source file
  --subjective, --strict     enable more subjective tests
  --root=PATH                PATH to the kernel tree root
  --no-summary               suppress the per-file summary
  --mailback                 only produce a report in case of warnings/errors
  --summary-file             include the filename in summary
  --debug KEY=[0|1]          turn on/off debugging of KEY, where KEY is one of
                             'values', 'possible', 'type', and 'attr' (default
                             is all off)
  --test-only=WORD           report only warnings/errors containing WORD
                             literally
  -h, --help, --version      display this help and exit

When FILE is - read standard input.
EOM

	exit($exitcode);
}

GetOptions(
	'q|quiet+'	=> \$quiet,
	'tree!'		=> \$tree,
	'signoff!'	=> \$chk_signoff,
	'patch!'	=> \$chk_patch,
	'emacs!'	=> \$emacs,
	'terse!'	=> \$terse,
	'f|file!'	=> \$file,
	'subjective!'	=> \$check,
	'strict!'	=> \$check,
	'root=s'	=> \$root,
	'summary!'	=> \$summary,
	'mailback!'	=> \$mailback,
	'summary-file!'	=> \$summary_file,

	'debug=s'	=> \%debug,
	'test-only=s'	=> \$tst_only,
	'h|help'	=> \$help,
	'version'	=> \$help
) or help(1);

help(0) if ($help);

my $exit = 0;

if ($#ARGV < 0) {
	print "$P: no input files\n";
	exit(1);
}

my $dbg_values = 0;
my $dbg_possible = 0;
my $dbg_type = 0;
my $dbg_attr = 0;
for my $key (keys %debug) {
	## no critic
	eval "\${dbg_$key} = '$debug{$key}';";
	die "$@" if ($@);
}

if ($terse) {
	$emacs = 1;
	$quiet++;
}

if ($tree) {
	if (defined $root) {
		if (!top_of_kernel_tree($root)) {
			die "$P: $root: --root does not point at a valid tree\n";
		}
	} else {
		if (top_of_kernel_tree('.')) {
			$root = '.';
		} elsif ($0 =~ m@(.*)/scripts/[^/]*$@ &&
						top_of_kernel_tree($1)) {
			$root = $1;
		}
	}

	if (!defined $root) {
		print "Must be run from the top-level dir. of a kernel tree\n";
		exit(2);
	}
}

my $emitted_corrupt = 0;

our $Ident	= qr{
			[A-Za-z_][A-Za-z\d_]*
			(?:\s*\#\#\s*[A-Za-z_][A-Za-z\d_]*)*
		}x;
our $Storage	= qr{extern|static|asmlinkage};
our $Sparse	= qr{
			__user|
			__kernel|
			__force|
			__iomem|
			__must_check|
			__init_refok|
			__kprobes|
			__ref
		}x;

# Notes to $Attribute:
# We need \b after 'init' otherwise 'initconst' will cause a false positive in a check
our $Attribute	= qr{
			const|
			__read_mostly|
			__kprobes|
			__(?:mem|cpu|dev|)(?:initdata|initconst|init\b)|
			____cacheline_aligned|
			____cacheline_aligned_in_smp|
			____cacheline_internodealigned_in_smp|
			__weak
		  }x;
our $Modifier;
our $Inline	= qr{inline|__always_inline|noinline};
our $Member	= qr{->$Ident|\.$Ident|\[[^]]*\]};
our $Lval	= qr{$Ident(?:$Member)*};

our $Constant	= qr{(?:[0-9]+|0x[0-9a-fA-F]+)[UL]*};
our $Assignment	= qr{(?:\*\=|/=|%=|\+=|-=|<<=|>>=|&=|\^=|\|=|=)};
our $Compare    = qr{<=|>=|==|!=|<|>};
our $Operators	= qr{
			<=|>=|==|!=|
			=>|->|<<|>>|<|>|!|~|
			&&|\|\||,|\^|\+\+|--|&|\||\+|-|\*|\/|%
		  }x;

our $NonptrType;
our $Type;
our $Declare;

our $UTF8	= qr {
	[\x09\x0A\x0D\x20-\x7E]              # ASCII
	| [\xC2-\xDF][\x80-\xBF]             # non-overlong 2-byte
	|  \xE0[\xA0-\xBF][\x80-\xBF]        # excluding overlongs
	| [\xE1-\xEC\xEE\xEF][\x80-\xBF]{2}  # straight 3-byte
	|  \xED[\x80-\x9F][\x80-\xBF]        # excluding surrogates
	|  \xF0[\x90-\xBF][\x80-\xBF]{2}     # planes 1-3
	| [\xF1-\xF3][\x80-\xBF]{3}          # planes 4-15
	|  \xF4[\x80-\x8F][\x80-\xBF]{2}     # plane 16
}x;

our $typeTypedefs = qr{(?x:
	(?:__)?(?:u|s|be|le)(?:8|16|32|64)|
	atomic_t
)};

our $logFunctions = qr{(?x:
	printk|
	pr_(debug|dbg|vdbg|devel|info|warning|err|notice|alert|crit|emerg|cont)|
	dev_(printk|dbg|vdbg|info|warn|err|notice|alert|crit|emerg|WARN)|
	WARN|
	panic
)};

our @typeList = (
	qr{void},
	qr{(?:unsigned\s+)?char},
	qr{(?:unsigned\s+)?short},
	qr{(?:unsigned\s+)?int},
	qr{(?:unsigned\s+)?long},
	qr{(?:unsigned\s+)?long\s+int},
	qr{(?:unsigned\s+)?long\s+long},
	qr{(?:unsigned\s+)?long\s+long\s+int},
	qr{unsigned},
	qr{float},
	qr{double},
	qr{bool},
	qr{struct\s+$Ident},
	qr{union\s+$Ident},
	qr{enum\s+$Ident},
	qr{${Ident}_t},
	qr{${Ident}_handler},
	qr{${Ident}_handler_fn},
);
our @modifierList = (
	qr{fastcall},
);

sub build_types {
	my $mods = "(?x:  \n" . join("|\n  ", @modifierList) . "\n)";
	my $all = "(?x:  \n" . join("|\n  ", @typeList) . "\n)";
	$Modifier	= qr{(?:$Attribute|$Sparse|$mods)};
	$NonptrType	= qr{
			(?:$Modifier\s+|const\s+)*
			(?:
				(?:typeof|__typeof__)\s*\(\s*\**\s*$Ident\s*\)|
				(?:$typeTypedefs\b)|
				(?:${all}\b)
			)
			(?:\s+$Modifier|\s+const)*
		  }x;
	$Type	= qr{
			$NonptrType
			(?:[\s\*]+\s*const|[\s\*]+|(?:\s*\[\s*\])+)?
			(?:\s+$Inline|\s+$Modifier)*
		  }x;
	$Declare	= qr{(?:$Storage\s+)?$Type};
}
build_types();

$chk_signoff = 0 if ($file);

my @dep_includes = ();
my @dep_functions = ();
my $removal = "Documentation/feature-removal-schedule.txt";
if ($tree && -f "$root/$removal") {
	open(my $REMOVE, '<', "$root/$removal") ||
				die "$P: $removal: open failed - $!\n";
	while (<$REMOVE>) {
		if (/^Check:\s+(.*\S)/) {
			for my $entry (split(/[, ]+/, $1)) {
				if ($entry =~ m@include/(.*)@) {
					push(@dep_includes, $1);

				} elsif ($entry !~ m@/@) {
					push(@dep_functions, $entry);
				}
			}
		}
	}
	close($REMOVE);
}

my @rawlines = ();
my @lines = ();
my $vname;
for my $filename (@ARGV) {
	my $FILE;
	if ($file) {
		open($FILE, '-|', "diff -u /dev/null $filename") ||
			die "$P: $filename: diff failed - $!\n";
	} elsif ($filename eq '-') {
		open($FILE, '<&STDIN');
	} else {
		open($FILE, '<', "$filename") ||
			die "$P: $filename: open failed - $!\n";
	}
	if ($filename eq '-') {
		$vname = 'Your patch';
	} else {
		$vname = $filename;
	}
	while (<$FILE>) {
		chomp;
		push(@rawlines, $_);
	}
	close($FILE);
	if (!process($filename)) {
		$exit = 1;
	}
	@rawlines = ();
	@lines = ();
}

exit($exit);

sub top_of_kernel_tree {
	my ($root) = @_;

	my @tree_check = (
		"COPYING", "CREDITS", "Kbuild", "MAINTAINERS", "Makefile",
		"README", "Documentation", "arch", "include", "drivers",
		"fs", "init", "ipc", "kernel", "lib", "scripts",
	);

	foreach my $check (@tree_check) {
		if (! -e $root . '/' . $check) {
			return 0;
		}
	}
	return 1;
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
sub copy_spacing {
	(my $res = shift) =~ tr/\t/ /c;
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

my $sanitise_quote = '';

sub sanitise_line_reset {
	my ($in_comment) = @_;

	if ($in_comment) {
		$sanitise_quote = '*/';
	} else {
		$sanitise_quote = '';
	}
}
sub sanitise_line {
	my ($line) = @_;

	my $res = '';
	my $l = '';

	my $qlen = 0;
	my $off = 0;
	my $c;

	# Always copy over the diff marker.
	$res = substr($line, 0, 1);

	for ($off = 1; $off < length($line); $off++) {
		$c = substr($line, $off, 1);

		# Comments we are wacking completly including the begin
		# and end, all to $;.
		if ($sanitise_quote eq '' && substr($line, $off, 2) eq '/*') {
			$sanitise_quote = '*/';

			substr($res, $off, 2, "$;$;");
			$off++;
			next;
		}
		if ($sanitise_quote eq '*/' && substr($line, $off, 2) eq '*/') {
			$sanitise_quote = '';
			substr($res, $off, 2, "$;$;");
			$off++;
			next;
		}
		if ($sanitise_quote eq '' && substr($line, $off, 2) eq '//') {
			$sanitise_quote = '//';

			substr($res, $off, 2, $sanitise_quote);
			$off++;
			next;
		}

		# A \ in a string means ignore the next character.
		if (($sanitise_quote eq "'" || $sanitise_quote eq '"') &&
		    $c eq "\\") {
			substr($res, $off, 2, 'XX');
			$off++;
			next;
		}
		# Regular quotes.
		if ($c eq "'" || $c eq '"') {
			if ($sanitise_quote eq '') {
				$sanitise_quote = $c;

				substr($res, $off, 1, $c);
				next;
			} elsif ($sanitise_quote eq $c) {
				$sanitise_quote = '';
			}
		}

		#print "c<$c> SQ<$sanitise_quote>\n";
		if ($off != 0 && $sanitise_quote eq '*/' && $c ne "\t") {
			substr($res, $off, 1, $;);
		} elsif ($off != 0 && $sanitise_quote eq '//' && $c ne "\t") {
			substr($res, $off, 1, $;);
		} elsif ($off != 0 && $sanitise_quote && $c ne "\t") {
			substr($res, $off, 1, 'X');
		} else {
			substr($res, $off, 1, $c);
		}
	}

	if ($sanitise_quote eq '//') {
		$sanitise_quote = '';
	}

	# The pathname on a #include may be surrounded by '<' and '>'.
	if ($res =~ /^.\s*\#\s*include\s+\<(.*)\>/) {
		my $clean = 'X' x length($1);
		$res =~ s@\<.*\>@<$clean>@;

	# The whole of a #error is a string.
	} elsif ($res =~ /^.\s*\#\s*(?:error|warning)\s+(.*)\b/) {
		my $clean = 'X' x length($1);
		$res =~ s@(\#\s*(?:error|warning)\s+).*@$1$clean@;
	}

	return $res;
}

sub ctx_statement_block {
	my ($linenr, $remain, $off) = @_;
	my $line = $linenr - 1;
	my $blk = '';
	my $soff = $off;
	my $coff = $off - 1;
	my $coff_set = 0;

	my $loff = 0;

	my $type = '';
	my $level = 0;
	my @stack = ();
	my $p;
	my $c;
	my $len = 0;

	my $remainder;
	while (1) {
		@stack = (['', 0]) if ($#stack == -1);

		#warn "CSB: blk<$blk> remain<$remain>\n";
		# If we are about to drop off the end, pull in more
		# context.
		if ($off >= $len) {
			for (; $remain > 0; $line++) {
				last if (!defined $lines[$line]);
				next if ($lines[$line] =~ /^-/);
				$remain--;
				$loff = $len;
				$blk .= $lines[$line] . "\n";
				$len = length($blk);
				$line++;
				last;
			}
			# Bail if there is no further context.
			#warn "CSB: blk<$blk> off<$off> len<$len>\n";
			if ($off >= $len) {
				last;
			}
		}
		$p = $c;
		$c = substr($blk, $off, 1);
		$remainder = substr($blk, $off);

		#warn "CSB: c<$c> type<$type> level<$level> remainder<$remainder> coff_set<$coff_set>\n";

		# Handle nested #if/#else.
		if ($remainder =~ /^#\s*(?:ifndef|ifdef|if)\s/) {
			push(@stack, [ $type, $level ]);
		} elsif ($remainder =~ /^#\s*(?:else|elif)\b/) {
			($type, $level) = @{$stack[$#stack - 1]};
		} elsif ($remainder =~ /^#\s*endif\b/) {
			($type, $level) = @{pop(@stack)};
		}

		# Statement ends at the ';' or a close '}' at the
		# outermost level.
		if ($level == 0 && $c eq ';') {
			last;
		}

		# An else is really a conditional as long as its not else if
		if ($level == 0 && $coff_set == 0 &&
				(!defined($p) || $p =~ /(?:\s|\}|\+)/) &&
				$remainder =~ /^(else)(?:\s|{)/ &&
				$remainder !~ /^else\s+if\b/) {
			$coff = $off + length($1) - 1;
			$coff_set = 1;
			#warn "CSB: mark coff<$coff> soff<$soff> 1<$1>\n";
			#warn "[" . substr($blk, $soff, $coff - $soff + 1) . "]\n";
		}

		if (($type eq '' || $type eq '(') && $c eq '(') {
			$level++;
			$type = '(';
		}
		if ($type eq '(' && $c eq ')') {
			$level--;
			$type = ($level != 0)? '(' : '';

			if ($level == 0 && $coff < $soff) {
				$coff = $off;
				$coff_set = 1;
				#warn "CSB: mark coff<$coff>\n";
			}
		}
		if (($type eq '' || $type eq '{') && $c eq '{') {
			$level++;
			$type = '{';
		}
		if ($type eq '{' && $c eq '}') {
			$level--;
			$type = ($level != 0)? '{' : '';

			if ($level == 0) {
				last;
			}
		}
		$off++;
	}
	# We are truly at the end, so shuffle to the next line.
	if ($off == $len) {
		$loff = $len + 1;
		$line++;
		$remain--;
	}

	my $statement = substr($blk, $soff, $off - $soff + 1);
	my $condition = substr($blk, $soff, $coff - $soff + 1);

	#warn "STATEMENT<$statement>\n";
	#warn "CONDITION<$condition>\n";

	#print "coff<$coff> soff<$off> loff<$loff>\n";

	return ($statement, $condition,
			$line, $remain + 1, $off - $loff + 1, $level);
}

sub statement_lines {
	my ($stmt) = @_;

	# Strip the diff line prefixes and rip blank lines at start and end.
	$stmt =~ s/(^|\n)./$1/g;
	$stmt =~ s/^\s*//;
	$stmt =~ s/\s*$//;

	my @stmt_lines = ($stmt =~ /\n/g);

	return $#stmt_lines + 2;
}

sub statement_rawlines {
	my ($stmt) = @_;

	my @stmt_lines = ($stmt =~ /\n/g);

	return $#stmt_lines + 2;
}

sub statement_block_size {
	my ($stmt) = @_;

	$stmt =~ s/(^|\n)./$1/g;
	$stmt =~ s/^\s*{//;
	$stmt =~ s/}\s*$//;
	$stmt =~ s/^\s*//;
	$stmt =~ s/\s*$//;

	my @stmt_lines = ($stmt =~ /\n/g);
	my @stmt_statements = ($stmt =~ /;/g);

	my $stmt_lines = $#stmt_lines + 2;
	my $stmt_statements = $#stmt_statements + 1;

	if ($stmt_lines > $stmt_statements) {
		return $stmt_lines;
	} else {
		return $stmt_statements;
	}
}

sub ctx_statement_full {
	my ($linenr, $remain, $off) = @_;
	my ($statement, $condition, $level);

	my (@chunks);

	# Grab the first conditional/block pair.
	($statement, $condition, $linenr, $remain, $off, $level) =
				ctx_statement_block($linenr, $remain, $off);
	#print "F: c<$condition> s<$statement> remain<$remain>\n";
	push(@chunks, [ $condition, $statement ]);
	if (!($remain > 0 && $condition =~ /^\s*(?:\n[+-])?\s*(?:if|else|do)\b/s)) {
		return ($level, $linenr, @chunks);
	}

	# Pull in the following conditional/block pairs and see if they
	# could continue the statement.
	for (;;) {
		($statement, $condition, $linenr, $remain, $off, $level) =
				ctx_statement_block($linenr, $remain, $off);
		#print "C: c<$condition> s<$statement> remain<$remain>\n";
		last if (!($remain > 0 && $condition =~ /^(?:\s*\n[+-])*\s*(?:else|do)\b/s));
		#print "C: push\n";
		push(@chunks, [ $condition, $statement ]);
	}

	return ($level, $linenr, @chunks);
}

sub ctx_block_get {
	my ($linenr, $remain, $outer, $open, $close, $off) = @_;
	my $line;
	my $start = $linenr - 1;
	my $blk = '';
	my @o;
	my @c;
	my @res = ();

	my $level = 0;
	my @stack = ($level);
	for ($line = $start; $remain > 0; $line++) {
		next if ($rawlines[$line] =~ /^-/);
		$remain--;

		$blk .= $rawlines[$line];

		# Handle nested #if/#else.
		if ($rawlines[$line] =~ /^.\s*#\s*(?:ifndef|ifdef|if)\s/) {
			push(@stack, $level);
		} elsif ($rawlines[$line] =~ /^.\s*#\s*(?:else|elif)\b/) {
			$level = $stack[$#stack - 1];
		} elsif ($rawlines[$line] =~ /^.\s*#\s*endif\b/) {
			$level = pop(@stack);
		}

		foreach my $c (split(//, $rawlines[$line])) {
			##print "C<$c>L<$level><$open$close>O<$off>\n";
			if ($off > 0) {
				$off--;
				next;
			}

			if ($c eq $close && $level > 0) {
				$level--;
				last if ($level == 0);
			} elsif ($c eq $open) {
				$level++;
			}
		}

		if (!$outer || $level <= 1) {
			push(@res, $rawlines[$line]);
		}

		last if ($level == 0);
	}

	return ($level, @res);
}
sub ctx_block_outer {
	my ($linenr, $remain) = @_;

	my ($level, @r) = ctx_block_get($linenr, $remain, 1, '{', '}', 0);
	return @r;
}
sub ctx_block {
	my ($linenr, $remain) = @_;

	my ($level, @r) = ctx_block_get($linenr, $remain, 0, '{', '}', 0);
	return @r;
}
sub ctx_statement {
	my ($linenr, $remain, $off) = @_;

	my ($level, @r) = ctx_block_get($linenr, $remain, 0, '(', ')', $off);
	return @r;
}
sub ctx_block_level {
	my ($linenr, $remain) = @_;

	return ctx_block_get($linenr, $remain, 0, '{', '}', 0);
}
sub ctx_statement_level {
	my ($linenr, $remain, $off) = @_;

	return ctx_block_get($linenr, $remain, 0, '(', ')', $off);
}

sub ctx_locate_comment {
	my ($first_line, $end_line) = @_;

	# Catch a comment on the end of the line itself.
	my ($current_comment) = ($rawlines[$end_line - 1] =~ m@.*(/\*.*\*/)\s*(?:\\\s*)?$@);
	return $current_comment if (defined $current_comment);

	# Look through the context and try and figure out if there is a
	# comment.
	my $in_comment = 0;
	$current_comment = '';
	for (my $linenr = $first_line; $linenr < $end_line; $linenr++) {
		my $line = $rawlines[$linenr - 1];
		#warn "           $line\n";
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

	##print "LINE: $rawlines[$end_line - 1 ]\n";
	##print "CMMT: $cmt\n";

	return ($cmt ne '');
}

sub raw_line {
	my ($linenr, $cnt) = @_;

	my $offset = $linenr - 1;
	$cnt++;

	my $line;
	while ($cnt) {
		$line = $rawlines[$offset++];
		next if (defined($line) && $line =~ /^-/);
		$cnt--;
	}

	return $line;
}

sub cat_vet {
	my ($vet) = @_;
	my ($res, $coded);

	$res = '';
	while ($vet =~ /([^[:cntrl:]]*)([[:cntrl:]]|$)/g) {
		$res .= $1;
		if ($2 ne '') {
			$coded = sprintf("^%c", unpack('C', $2) + 64);
			$res .= $coded;
		}
	}
	$res =~ s/$/\$/;

	return $res;
}

my $av_preprocessor = 0;
my $av_pending;
my @av_paren_type;
my $av_pend_colon;

sub annotate_reset {
	$av_preprocessor = 0;
	$av_pending = '_';
	@av_paren_type = ('E');
	$av_pend_colon = 'O';
}

sub annotate_values {
	my ($stream, $type) = @_;

	my $res;
	my $var = '_' x length($stream);
	my $cur = $stream;

	print "$stream\n" if ($dbg_values > 1);

	while (length($cur)) {
		@av_paren_type = ('E') if ($#av_paren_type < 0);
		print " <" . join('', @av_paren_type) .
				"> <$type> <$av_pending>" if ($dbg_values > 1);
		if ($cur =~ /^(\s+)/o) {
			print "WS($1)\n" if ($dbg_values > 1);
			if ($1 =~ /\n/ && $av_preprocessor) {
				$type = pop(@av_paren_type);
				$av_preprocessor = 0;
			}

		} elsif ($cur =~ /^($Type)\s*(?:$Ident|,|\)|\()/) {
			print "DECLARE($1)\n" if ($dbg_values > 1);
			$type = 'T';

		} elsif ($cur =~ /^($Modifier)\s*/) {
			print "MODIFIER($1)\n" if ($dbg_values > 1);
			$type = 'T';

		} elsif ($cur =~ /^(\#\s*define\s*$Ident)(\(?)/o) {
			print "DEFINE($1,$2)\n" if ($dbg_values > 1);
			$av_preprocessor = 1;
			push(@av_paren_type, $type);
			if ($2 ne '') {
				$av_pending = 'N';
			}
			$type = 'E';

		} elsif ($cur =~ /^(\#\s*(?:undef\s*$Ident|include\b))/o) {
			print "UNDEF($1)\n" if ($dbg_values > 1);
			$av_preprocessor = 1;
			push(@av_paren_type, $type);

		} elsif ($cur =~ /^(\#\s*(?:ifdef|ifndef|if))/o) {
			print "PRE_START($1)\n" if ($dbg_values > 1);
			$av_preprocessor = 1;

			push(@av_paren_type, $type);
			push(@av_paren_type, $type);
			$type = 'E';

		} elsif ($cur =~ /^(\#\s*(?:else|elif))/o) {
			print "PRE_RESTART($1)\n" if ($dbg_values > 1);
			$av_preprocessor = 1;

			push(@av_paren_type, $av_paren_type[$#av_paren_type]);

			$type = 'E';

		} elsif ($cur =~ /^(\#\s*(?:endif))/o) {
			print "PRE_END($1)\n" if ($dbg_values > 1);

			$av_preprocessor = 1;

			# Assume all arms of the conditional end as this
			# one does, and continue as if the #endif was not here.
			pop(@av_paren_type);
			push(@av_paren_type, $type);
			$type = 'E';

		} elsif ($cur =~ /^(\\\n)/o) {
			print "PRECONT($1)\n" if ($dbg_values > 1);

		} elsif ($cur =~ /^(__attribute__)\s*\(?/o) {
			print "ATTR($1)\n" if ($dbg_values > 1);
			$av_pending = $type;
			$type = 'N';

		} elsif ($cur =~ /^(sizeof)\s*(\()?/o) {
			print "SIZEOF($1)\n" if ($dbg_values > 1);
			if (defined $2) {
				$av_pending = 'V';
			}
			$type = 'N';

		} elsif ($cur =~ /^(if|while|for)\b/o) {
			print "COND($1)\n" if ($dbg_values > 1);
			$av_pending = 'E';
			$type = 'N';

		} elsif ($cur =~/^(case)/o) {
			print "CASE($1)\n" if ($dbg_values > 1);
			$av_pend_colon = 'C';
			$type = 'N';

		} elsif ($cur =~/^(return|else|goto|typeof|__typeof__)\b/o) {
			print "KEYWORD($1)\n" if ($dbg_values > 1);
			$type = 'N';

		} elsif ($cur =~ /^(\()/o) {
			print "PAREN('$1')\n" if ($dbg_values > 1);
			push(@av_paren_type, $av_pending);
			$av_pending = '_';
			$type = 'N';

		} elsif ($cur =~ /^(\))/o) {
			my $new_type = pop(@av_paren_type);
			if ($new_type ne '_') {
				$type = $new_type;
				print "PAREN('$1') -> $type\n"
							if ($dbg_values > 1);
			} else {
				print "PAREN('$1')\n" if ($dbg_values > 1);
			}

		} elsif ($cur =~ /^($Ident)\s*\(/o) {
			print "FUNC($1)\n" if ($dbg_values > 1);
			$type = 'V';
			$av_pending = 'V';

		} elsif ($cur =~ /^($Ident\s*):(?:\s*\d+\s*(,|=|;))?/) {
			if (defined $2 && $type eq 'C' || $type eq 'T') {
				$av_pend_colon = 'B';
			} elsif ($type eq 'E') {
				$av_pend_colon = 'L';
			}
			print "IDENT_COLON($1,$type>$av_pend_colon)\n" if ($dbg_values > 1);
			$type = 'V';

		} elsif ($cur =~ /^($Ident|$Constant)/o) {
			print "IDENT($1)\n" if ($dbg_values > 1);
			$type = 'V';

		} elsif ($cur =~ /^($Assignment)/o) {
			print "ASSIGN($1)\n" if ($dbg_values > 1);
			$type = 'N';

		} elsif ($cur =~/^(;|{|})/) {
			print "END($1)\n" if ($dbg_values > 1);
			$type = 'E';
			$av_pend_colon = 'O';

		} elsif ($cur =~/^(,)/) {
			print "COMMA($1)\n" if ($dbg_values > 1);
			$type = 'C';

		} elsif ($cur =~ /^(\?)/o) {
			print "QUESTION($1)\n" if ($dbg_values > 1);
			$type = 'N';

		} elsif ($cur =~ /^(:)/o) {
			print "COLON($1,$av_pend_colon)\n" if ($dbg_values > 1);

			substr($var, length($res), 1, $av_pend_colon);
			if ($av_pend_colon eq 'C' || $av_pend_colon eq 'L') {
				$type = 'E';
			} else {
				$type = 'N';
			}
			$av_pend_colon = 'O';

		} elsif ($cur =~ /^(\[)/o) {
			print "CLOSE($1)\n" if ($dbg_values > 1);
			$type = 'N';

		} elsif ($cur =~ /^(-(?![->])|\+(?!\+)|\*|\&\&|\&)/o) {
			my $variant;

			print "OPV($1)\n" if ($dbg_values > 1);
			if ($type eq 'V') {
				$variant = 'B';
			} else {
				$variant = 'U';
			}

			substr($var, length($res), 1, $variant);
			$type = 'N';

		} elsif ($cur =~ /^($Operators)/o) {
			print "OP($1)\n" if ($dbg_values > 1);
			if ($1 ne '++' && $1 ne '--') {
				$type = 'N';
			}

		} elsif ($cur =~ /(^.)/o) {
			print "C($1)\n" if ($dbg_values > 1);
		}
		if (defined $1) {
			$cur = substr($cur, length($1));
			$res .= $type x length($1);
		}
	}

	return ($res, $var);
}

sub possible {
	my ($possible, $line) = @_;
	my $notPermitted = qr{(?:
		^(?:
			$Modifier|
			$Storage|
			$Type|
			DEFINE_\S+
		)$|
		^(?:
			goto|
			return|
			case|
			else|
			asm|__asm__|
			do
		)(?:\s|$)|
		^(?:typedef|struct|enum)\b
	    )}x;
	warn "CHECK<$possible> ($line)\n" if ($dbg_possible > 2);
	if ($possible !~ $notPermitted) {
		# Check for modifiers.
		$possible =~ s/\s*$Storage\s*//g;
		$possible =~ s/\s*$Sparse\s*//g;
		if ($possible =~ /^\s*$/) {

		} elsif ($possible =~ /\s/) {
			$possible =~ s/\s*$Type\s*//g;
			for my $modifier (split(' ', $possible)) {
				if ($modifier !~ $notPermitted) {
					warn "MODIFIER: $modifier ($possible) ($line)\n" if ($dbg_possible);
					push(@modifierList, $modifier);
				}
			}

		} else {
			warn "POSSIBLE: $possible ($line)\n" if ($dbg_possible);
			push(@typeList, $possible);
		}
		build_types();
	} else {
		warn "NOTPOSS: $possible ($line)\n" if ($dbg_possible > 1);
	}
}

my $prefix = '';

sub report {
	if (defined $tst_only && $_[0] !~ /\Q$tst_only\E/) {
		return 0;
	}
	my $line = $prefix . $_[0];

	$line = (split('\n', $line))[0] . "\n" if ($terse);

	push(our @report, $line);

	return 1;
}
sub report_dump {
	our @report;
}
sub ERROR {
	if (report("ERROR: $_[0]\n")) {
		our $clean = 0;
		our $cnt_error++;
	}
}
sub WARN {
	if (report("WARNING: $_[0]\n")) {
		our $clean = 0;
		our $cnt_warn++;
	}
}
sub CHK {
	if ($check && report("CHECK: $_[0]\n")) {
		our $clean = 0;
		our $cnt_chk++;
	}
}

sub check_absolute_file {
	my ($absolute, $herecurr) = @_;
	my $file = $absolute;

	##print "absolute<$absolute>\n";

	# See if any suffix of this path is a path within the tree.
	while ($file =~ s@^[^/]*/@@) {
		if (-f "$root/$file") {
			##print "file<$file>\n";
			last;
		}
	}
	if (! -f _)  {
		return 0;
	}

	# It is, so see if the prefix is acceptable.
	my $prefix = $absolute;
	substr($prefix, -length($file)) = '';

	##print "prefix<$prefix>\n";
	if ($prefix ne ".../") {
		WARN("use relative pathname instead of absolute in changelog text\n" . $herecurr);
	}
}

sub process {
	my $filename = shift;

	my $linenr=0;
	my $prevline="";
	my $prevrawline="";
	my $stashline="";
	my $stashrawline="";

	my $length;
	my $indent;
	my $previndent=0;
	my $stashindent=0;

	our $clean = 1;
	my $signoff = 0;
	my $is_patch = 0;

	our @report = ();
	our $cnt_lines = 0;
	our $cnt_error = 0;
	our $cnt_warn = 0;
	our $cnt_chk = 0;

	# Trace the real file/line as we go.
	my $realfile = '';
	my $realline = 0;
	my $realcnt = 0;
	my $here = '';
	my $in_comment = 0;
	my $comment_edge = 0;
	my $first_line = 0;
	my $p1_prefix = '';

	my $prev_values = 'E';

	# suppression flags
	my %suppress_ifbraces;
	my %suppress_whiletrailers;
	my %suppress_export;

	# Pre-scan the patch sanitizing the lines.
	# Pre-scan the patch looking for any __setup documentation.
	#
	my @setup_docs = ();
	my $setup_docs = 0;

	sanitise_line_reset();
	my $line;
	foreach my $rawline (@rawlines) {
		$linenr++;
		$line = $rawline;

		if ($rawline=~/^\+\+\+\s+(\S+)/) {
			$setup_docs = 0;
			if ($1 =~ m@Documentation/kernel-parameters.txt$@) {
				$setup_docs = 1;
			}
			#next;
		}
		if ($rawline=~/^\@\@ -\d+(?:,\d+)? \+(\d+)(,(\d+))? \@\@/) {
			$realline=$1-1;
			if (defined $2) {
				$realcnt=$3+1;
			} else {
				$realcnt=1+1;
			}
			$in_comment = 0;

			# Guestimate if this is a continuing comment.  Run
			# the context looking for a comment "edge".  If this
			# edge is a close comment then we must be in a comment
			# at context start.
			my $edge;
			my $cnt = $realcnt;
			for (my $ln = $linenr + 1; $cnt > 0; $ln++) {
				next if (defined $rawlines[$ln - 1] &&
					 $rawlines[$ln - 1] =~ /^-/);
				$cnt--;
				#print "RAW<$rawlines[$ln - 1]>\n";
				last if (!defined $rawlines[$ln - 1]);
				if ($rawlines[$ln - 1] =~ m@(/\*|\*/)@ &&
				    $rawlines[$ln - 1] !~ m@"[^"]*(?:/\*|\*/)[^"]*"@) {
					($edge) = $1;
					last;
				}
			}
			if (defined $edge && $edge eq '*/') {
				$in_comment = 1;
			}

			# Guestimate if this is a continuing comment.  If this
			# is the start of a diff block and this line starts
			# ' *' then it is very likely a comment.
			if (!defined $edge &&
			    $rawlines[$linenr] =~ m@^.\s*(?:\*\*+| \*)(?:\s|$)@)
			{
				$in_comment = 1;
			}

			##print "COMMENT:$in_comment edge<$edge> $rawline\n";
			sanitise_line_reset($in_comment);

		} elsif ($realcnt && $rawline =~ /^(?:\+| |$)/) {
			# Standardise the strings and chars within the input to
			# simplify matching -- only bother with positive lines.
			$line = sanitise_line($rawline);
		}
		push(@lines, $line);

		if ($realcnt > 1) {
			$realcnt-- if ($line =~ /^(?:\+| |$)/);
		} else {
			$realcnt = 0;
		}

		#print "==>$rawline\n";
		#print "-->$line\n";

		if ($setup_docs && $line =~ /^\+/) {
			push(@setup_docs, $line);
		}
	}

	$prefix = '';

	$realcnt = 0;
	$linenr = 0;
	foreach my $line (@lines) {
		$linenr++;

		my $rawline = $rawlines[$linenr - 1];

#extract the line range in the file after the patch is applied
		if ($line=~/^\@\@ -\d+(?:,\d+)? \+(\d+)(,(\d+))? \@\@/) {
			$is_patch = 1;
			$first_line = $linenr + 1;
			$realline=$1-1;
			if (defined $2) {
				$realcnt=$3+1;
			} else {
				$realcnt=1+1;
			}
			annotate_reset();
			$prev_values = 'E';

			%suppress_ifbraces = ();
			%suppress_whiletrailers = ();
			%suppress_export = ();
			next;

# track the line number as we move through the hunk, note that
# new versions of GNU diff omit the leading space on completely
# blank context lines so we need to count that too.
		} elsif ($line =~ /^( |\+|$)/) {
			$realline++;
			$realcnt-- if ($realcnt != 0);

			# Measure the line length and indent.
			($length, $indent) = line_stats($rawline);

			# Track the previous line.
			($prevline, $stashline) = ($stashline, $line);
			($previndent, $stashindent) = ($stashindent, $indent);
			($prevrawline, $stashrawline) = ($stashrawline, $rawline);

			#warn "line<$line>\n";

		} elsif ($realcnt == 1) {
			$realcnt--;
		}

		my $hunk_line = ($realcnt != 0);

#make up the handle for any error we report on this line
		$prefix = "$filename:$realline: " if ($emacs && $file);
		$prefix = "$filename:$linenr: " if ($emacs && !$file);

		$here = "#$linenr: " if (!$file);
		$here = "#$realline: " if ($file);

		# extract the filename as it passes
		if ($line=~/^\+\+\+\s+(\S+)/) {
			$realfile = $1;
			$realfile =~ s@^([^/]*)/@@;

			$p1_prefix = $1;
			if (!$file && $tree && $p1_prefix ne '' &&
			    -e "$root/$p1_prefix") {
				WARN("patch prefix '$p1_prefix' exists, appears to be a -p0 patch\n");
			}

			if ($realfile =~ m@^include/asm/@) {
				ERROR("do not modify files in include/asm, change architecture specific files in include/asm-<architecture>\n" . "$here$rawline\n");
			}
			next;
		}

		$here .= "FILE: $realfile:$realline:" if ($realcnt != 0);

		my $hereline = "$here\n$rawline\n";
		my $herecurr = "$here\n$rawline\n";
		my $hereprev = "$here\n$prevrawline\n$rawline\n";

		$cnt_lines++ if ($realcnt != 0);

#check the patch for a signoff:
		if ($line =~ /^\s*signed-off-by:/i) {
			# This is a signoff, if ugly, so do not double report.
			$signoff++;
			if (!($line =~ /^\s*Signed-off-by:/)) {
				WARN("Signed-off-by: is the preferred form\n" .
					$herecurr);
			}
			if ($line =~ /^\s*signed-off-by:\S/i) {
				WARN("space required after Signed-off-by:\n" .
					$herecurr);
			}
		}

# Check for wrappage within a valid hunk of the file
		if ($realcnt != 0 && $line !~ m{^(?:\+|-| |\\ No newline|$)}) {
			ERROR("patch seems to be corrupt (line wrapped?)\n" .
				$herecurr) if (!$emitted_corrupt++);
		}

# Check for absolute kernel paths.
		if ($tree) {
			while ($line =~ m{(?:^|\s)(/\S*)}g) {
				my $file = $1;

				if ($file =~ m{^(.*?)(?::\d+)+:?$} &&
				    check_absolute_file($1, $herecurr)) {
					#
				} else {
					check_absolute_file($file, $herecurr);
				}
			}
		}

# UTF-8 regex found at http://www.w3.org/International/questions/qa-forms-utf-8.en.php
		if (($realfile =~ /^$/ || $line =~ /^\+/) &&
		    $rawline !~ m/^$UTF8*$/) {
			my ($utf8_prefix) = ($rawline =~ /^($UTF8*)/);

			my $blank = copy_spacing($rawline);
			my $ptr = substr($blank, 0, length($utf8_prefix)) . "^";
			my $hereptr = "$hereline$ptr\n";

			ERROR("Invalid UTF-8, patch and commit message should be encoded in UTF-8\n" . $hereptr);
		}

# ignore non-hunk lines and lines being removed
		next if (!$hunk_line || $line =~ /^-/);

#trailing whitespace
		if ($line =~ /^\+.*\015/) {
			my $herevet = "$here\n" . cat_vet($rawline) . "\n";
			ERROR("DOS line endings\n" . $herevet);

		} elsif ($rawline =~ /^\+.*\S\s+$/ || $rawline =~ /^\+\s+$/) {
			my $herevet = "$here\n" . cat_vet($rawline) . "\n";
			ERROR("trailing whitespace\n" . $herevet);
		}

# check we are in a valid source file if not then ignore this hunk
		next if ($realfile !~ /\.(h|c|s|S|pl|sh)$/);

#80 column limit
		if ($line =~ /^\+/ && $prevrawline !~ /\/\*\*/ &&
		    $rawline !~ /^.\s*\*\s*\@$Ident\s/ &&
		    $line !~ /^\+\s*$logFunctions\s*\(\s*(?:KERN_\S+\s*)?"[X\t]*"\s*(?:,|\)\s*;)\s*$/ &&
		    $length > 80)
		{
			WARN("line over 80 characters\n" . $herecurr);
		}

# check for spaces before a quoted newline
		if ($rawline =~ /^.*\".*\s\\n/) {
			WARN("unnecessary whitespace before a quoted newline\n" . $herecurr);
		}

# check for adding lines without a newline.
		if ($line =~ /^\+/ && defined $lines[$linenr] && $lines[$linenr] =~ /^\\ No newline at end of file/) {
			WARN("adding a line without newline at end of file\n" . $herecurr);
		}

# Blackfin: use hi/lo macros
		if ($realfile =~ m@arch/blackfin/.*\.S$@) {
			if ($line =~ /\.[lL][[:space:]]*=.*&[[:space:]]*0x[fF][fF][fF][fF]/) {
				my $herevet = "$here\n" . cat_vet($line) . "\n";
				ERROR("use the LO() macro, not (... & 0xFFFF)\n" . $herevet);
			}
			if ($line =~ /\.[hH][[:space:]]*=.*>>[[:space:]]*16/) {
				my $herevet = "$here\n" . cat_vet($line) . "\n";
				ERROR("use the HI() macro, not (... >> 16)\n" . $herevet);
			}
		}

# check we are in a valid source file C or perl if not then ignore this hunk
		next if ($realfile !~ /\.(h|c|pl)$/);

# at the beginning of a line any tabs must come first and anything
# more than 8 must use tabs.
		if ($rawline =~ /^\+\s* \t\s*\S/ ||
		    $rawline =~ /^\+\s*        \s*/) {
			my $herevet = "$here\n" . cat_vet($rawline) . "\n";
			ERROR("code indent should use tabs where possible\n" . $herevet);
		}

# check for space before tabs.
		if ($rawline =~ /^\+/ && $rawline =~ / \t/) {
			my $herevet = "$here\n" . cat_vet($rawline) . "\n";
			WARN("please, no space before tabs\n" . $herevet);
		}

# check we are in a valid C source file if not then ignore this hunk
		next if ($realfile !~ /\.(h|c)$/);

# check for RCS/CVS revision markers
		if ($rawline =~ /^\+.*\$(Revision|Log|Id)(?:\$|)/) {
			WARN("CVS style keyword markers, these will _not_ be updated\n". $herecurr);
		}

# Blackfin: don't use __builtin_bfin_[cs]sync
		if ($line =~ /__builtin_bfin_csync/) {
			my $herevet = "$here\n" . cat_vet($line) . "\n";
			ERROR("use the CSYNC() macro in asm/blackfin.h\n" . $herevet);
		}
		if ($line =~ /__builtin_bfin_ssync/) {
			my $herevet = "$here\n" . cat_vet($line) . "\n";
			ERROR("use the SSYNC() macro in asm/blackfin.h\n" . $herevet);
		}

# Check for potential 'bare' types
		my ($stat, $cond, $line_nr_next, $remain_next, $off_next,
		    $realline_next);
		if ($realcnt && $line =~ /.\s*\S/) {
			($stat, $cond, $line_nr_next, $remain_next, $off_next) =
				ctx_statement_block($linenr, $realcnt, 0);
			$stat =~ s/\n./\n /g;
			$cond =~ s/\n./\n /g;

			# Find the real next line.
			$realline_next = $line_nr_next;
			if (defined $realline_next &&
			    (!defined $lines[$realline_next - 1] ||
			     substr($lines[$realline_next - 1], $off_next) =~ /^\s*$/)) {
				$realline_next++;
			}

			my $s = $stat;
			$s =~ s/{.*$//s;

			# Ignore goto labels.
			if ($s =~ /$Ident:\*$/s) {

			# Ignore functions being called
			} elsif ($s =~ /^.\s*$Ident\s*\(/s) {

			} elsif ($s =~ /^.\s*else\b/s) {

			# declarations always start with types
			} elsif ($prev_values eq 'E' && $s =~ /^.\s*(?:$Storage\s+)?(?:$Inline\s+)?(?:const\s+)?((?:\s*$Ident)+?)\b(?:\s+$Sparse)?\s*\**\s*(?:$Ident|\(\*[^\)]*\))(?:\s*$Modifier)?\s*(?:;|=|,|\()/s) {
				my $type = $1;
				$type =~ s/\s+/ /g;
				possible($type, "A:" . $s);

			# definitions in global scope can only start with types
			} elsif ($s =~ /^.(?:$Storage\s+)?(?:$Inline\s+)?(?:const\s+)?($Ident)\b\s*(?!:)/s) {
				possible($1, "B:" . $s);
			}

			# any (foo ... *) is a pointer cast, and foo is a type
			while ($s =~ /\(($Ident)(?:\s+$Sparse)*[\s\*]+\s*\)/sg) {
				possible($1, "C:" . $s);
			}

			# Check for any sort of function declaration.
			# int foo(something bar, other baz);
			# void (*store_gdt)(x86_descr_ptr *);
			if ($prev_values eq 'E' && $s =~ /^(.(?:typedef\s*)?(?:(?:$Storage|$Inline)\s*)*\s*$Type\s*(?:\b$Ident|\(\*\s*$Ident\))\s*)\(/s) {
				my ($name_len) = length($1);

				my $ctx = $s;
				substr($ctx, 0, $name_len + 1, '');
				$ctx =~ s/\)[^\)]*$//;

				for my $arg (split(/\s*,\s*/, $ctx)) {
					if ($arg =~ /^(?:const\s+)?($Ident)(?:\s+$Sparse)*\s*\**\s*(:?\b$Ident)?$/s || $arg =~ /^($Ident)$/s) {

						possible($1, "D:" . $s);
					}
				}
			}

		}

#
# Checks which may be anchored in the context.
#

# Check for switch () and associated case and default
# statements should be at the same indent.
		if ($line=~/\bswitch\s*\(.*\)/) {
			my $err = '';
			my $sep = '';
			my @ctx = ctx_block_outer($linenr, $realcnt);
			shift(@ctx);
			for my $ctx (@ctx) {
				my ($clen, $cindent) = line_stats($ctx);
				if ($ctx =~ /^\+\s*(case\s+|default:)/ &&
							$indent != $cindent) {
					$err .= "$sep$ctx\n";
					$sep = '';
				} else {
					$sep = "[...]\n";
				}
			}
			if ($err ne '') {
				ERROR("switch and case should be at the same indent\n$hereline$err");
			}
		}

# if/while/etc brace do not go on next line, unless defining a do while loop,
# or if that brace on the next line is for something else
		if ($line =~ /(.*)\b((?:if|while|for|switch)\s*\(|do\b|else\b)/ && $line !~ /^.\s*\#/) {
			my $pre_ctx = "$1$2";

			my ($level, @ctx) = ctx_statement_level($linenr, $realcnt, 0);
			my $ctx_cnt = $realcnt - $#ctx - 1;
			my $ctx = join("\n", @ctx);

			my $ctx_ln = $linenr;
			my $ctx_skip = $realcnt;

			while ($ctx_skip > $ctx_cnt || ($ctx_skip == $ctx_cnt &&
					defined $lines[$ctx_ln - 1] &&
					$lines[$ctx_ln - 1] =~ /^-/)) {
				##print "SKIP<$ctx_skip> CNT<$ctx_cnt>\n";
				$ctx_skip-- if (!defined $lines[$ctx_ln - 1] || $lines[$ctx_ln - 1] !~ /^-/);
				$ctx_ln++;
			}

			#print "realcnt<$realcnt> ctx_cnt<$ctx_cnt>\n";
			#print "pre<$pre_ctx>\nline<$line>\nctx<$ctx>\nnext<$lines[$ctx_ln - 1]>\n";

			if ($ctx !~ /{\s*/ && defined($lines[$ctx_ln -1]) && $lines[$ctx_ln - 1] =~ /^\+\s*{/) {
				ERROR("that open brace { should be on the previous line\n" .
					"$here\n$ctx\n$lines[$ctx_ln - 1]\n");
			}
			if ($level == 0 && $pre_ctx !~ /}\s*while\s*\($/ &&
			    $ctx =~ /\)\s*\;\s*$/ &&
			    defined $lines[$ctx_ln - 1])
			{
				my ($nlength, $nindent) = line_stats($lines[$ctx_ln - 1]);
				if ($nindent > $indent) {
					WARN("trailing semicolon indicates no statements, indent implies otherwise\n" .
						"$here\n$ctx\n$lines[$ctx_ln - 1]\n");
				}
			}
		}

# Check relative indent for conditionals and blocks.
		if ($line =~ /\b(?:(?:if|while|for)\s*\(|do\b)/ && $line !~ /^.\s*#/ && $line !~ /\}\s*while\s*/) {
			my ($s, $c) = ($stat, $cond);

			substr($s, 0, length($c), '');

			# Make sure we remove the line prefixes as we have
			# none on the first line, and are going to readd them
			# where necessary.
			$s =~ s/\n./\n/gs;

			# Find out how long the conditional actually is.
			my @newlines = ($c =~ /\n/gs);
			my $cond_lines = 1 + $#newlines;

			# We want to check the first line inside the block
			# starting at the end of the conditional, so remove:
			#  1) any blank line termination
			#  2) any opening brace { on end of the line
			#  3) any do (...) {
			my $continuation = 0;
			my $check = 0;
			$s =~ s/^.*\bdo\b//;
			$s =~ s/^\s*{//;
			if ($s =~ s/^\s*\\//) {
				$continuation = 1;
			}
			if ($s =~ s/^\s*?\n//) {
				$check = 1;
				$cond_lines++;
			}

			# Also ignore a loop construct at the end of a
			# preprocessor statement.
			if (($prevline =~ /^.\s*#\s*define\s/ ||
			    $prevline =~ /\\\s*$/) && $continuation == 0) {
				$check = 0;
			}

			my $cond_ptr = -1;
			$continuation = 0;
			while ($cond_ptr != $cond_lines) {
				$cond_ptr = $cond_lines;

				# If we see an #else/#elif then the code
				# is not linear.
				if ($s =~ /^\s*\#\s*(?:else|elif)/) {
					$check = 0;
				}

				# Ignore:
				#  1) blank lines, they should be at 0,
				#  2) preprocessor lines, and
				#  3) labels.
				if ($continuation ||
				    $s =~ /^\s*?\n/ ||
				    $s =~ /^\s*#\s*?/ ||
				    $s =~ /^\s*$Ident\s*:/) {
					$continuation = ($s =~ /^.*?\\\n/) ? 1 : 0;
					if ($s =~ s/^.*?\n//) {
						$cond_lines++;
					}
				}
			}

			my (undef, $sindent) = line_stats("+" . $s);
			my $stat_real = raw_line($linenr, $cond_lines);

			# Check if either of these lines are modified, else
			# this is not this patch's fault.
			if (!defined($stat_real) ||
			    $stat !~ /^\+/ && $stat_real !~ /^\+/) {
				$check = 0;
			}
			if (defined($stat_real) && $cond_lines > 1) {
				$stat_real = "[...]\n$stat_real";
			}

			#print "line<$line> prevline<$prevline> indent<$indent> sindent<$sindent> check<$check> continuation<$continuation> s<$s> cond_lines<$cond_lines> stat_real<$stat_real> stat<$stat>\n";

			if ($check && (($sindent % 8) != 0 ||
			    ($sindent <= $indent && $s ne ''))) {
				WARN("suspect code indent for conditional statements ($indent, $sindent)\n" . $herecurr . "$stat_real\n");
			}
		}

		# Track the 'values' across context and added lines.
		my $opline = $line; $opline =~ s/^./ /;
		my ($curr_values, $curr_vars) =
				annotate_values($opline . "\n", $prev_values);
		$curr_values = $prev_values . $curr_values;
		if ($dbg_values) {
			my $outline = $opline; $outline =~ s/\t/ /g;
			print "$linenr > .$outline\n";
			print "$linenr > $curr_values\n";
			print "$linenr >  $curr_vars\n";
		}
		$prev_values = substr($curr_values, -1);

#ignore lines not being added
		if ($line=~/^[^\+]/) {next;}

# TEST: allow direct testing of the type matcher.
		if ($dbg_type) {
			if ($line =~ /^.\s*$Declare\s*$/) {
				ERROR("TEST: is type\n" . $herecurr);
			} elsif ($dbg_type > 1 && $line =~ /^.+($Declare)/) {
				ERROR("TEST: is not type ($1 is)\n". $herecurr);
			}
			next;
		}
# TEST: allow direct testing of the attribute matcher.
		if ($dbg_attr) {
			if ($line =~ /^.\s*$Modifier\s*$/) {
				ERROR("TEST: is attr\n" . $herecurr);
			} elsif ($dbg_attr > 1 && $line =~ /^.+($Modifier)/) {
				ERROR("TEST: is not attr ($1 is)\n". $herecurr);
			}
			next;
		}

# check for initialisation to aggregates open brace on the next line
		if ($line =~ /^.\s*{/ &&
		    $prevline =~ /(?:^|[^=])=\s*$/) {
			ERROR("that open brace { should be on the previous line\n" . $hereprev);
		}

#
# Checks which are anchored on the added line.
#

# check for malformed paths in #include statements (uses RAW line)
		if ($rawline =~ m{^.\s*\#\s*include\s+[<"](.*)[">]}) {
			my $path = $1;
			if ($path =~ m{//}) {
				ERROR("malformed #include filename\n" .
					$herecurr);
			}
		}

# no C99 // comments
		if ($line =~ m{//}) {
			ERROR("do not use C99 // comments\n" . $herecurr);
		}
		# Remove C99 comments.
		$line =~ s@//.*@@;
		$opline =~ s@//.*@@;

# EXPORT_SYMBOL should immediately follow the thing it is exporting, consider
# the whole statement.
#print "APW <$lines[$realline_next - 1]>\n";
		if (defined $realline_next &&
		    exists $lines[$realline_next - 1] &&
		    !defined $suppress_export{$realline_next} &&
		    ($lines[$realline_next - 1] =~ /EXPORT_SYMBOL.*\((.*)\)/ ||
		     $lines[$realline_next - 1] =~ /EXPORT_UNUSED_SYMBOL.*\((.*)\)/)) {
			my $name = $1;
			if ($stat !~ /(?:
				\n.}\s*$|
				^.DEFINE_$Ident\(\Q$name\E\)|
				^.DECLARE_$Ident\(\Q$name\E\)|
				^.LIST_HEAD\(\Q$name\E\)|
				^.(?:$Storage\s+)?$Type\s*\(\s*\*\s*\Q$name\E\s*\)\s*\(|
				\b\Q$name\E(?:\s+$Attribute)*\s*(?:;|=|\[|\()
			    )/x) {
#print "FOO A<$lines[$realline_next - 1]> stat<$stat> name<$name>\n";
				$suppress_export{$realline_next} = 2;
			} else {
				$suppress_export{$realline_next} = 1;
			}
		}
		if (!defined $suppress_export{$linenr} &&
		    $prevline =~ /^.\s*$/ &&
		    ($line =~ /EXPORT_SYMBOL.*\((.*)\)/ ||
		     $line =~ /EXPORT_UNUSED_SYMBOL.*\((.*)\)/)) {
#print "FOO B <$lines[$linenr - 1]>\n";
			$suppress_export{$linenr} = 2;
		}
		if (defined $suppress_export{$linenr} &&
		    $suppress_export{$linenr} == 2) {
			WARN("EXPORT_SYMBOL(foo); should immediately follow its function/variable\n" . $herecurr);
		}

# check for external initialisers.
		if ($line =~ /^.$Type\s*$Ident\s*(?:\s+$Modifier)*\s*=\s*(0|NULL|false)\s*;/) {
			ERROR("do not initialise externals to 0 or NULL\n" .
				$herecurr);
		}
# check for static initialisers.
		if ($line =~ /\bstatic\s.*=\s*(0|NULL|false)\s*;/) {
			ERROR("do not initialise statics to 0 or NULL\n" .
				$herecurr);
		}

# check for new typedefs, only function parameters and sparse annotations
# make sense.
		if ($line =~ /\btypedef\s/ &&
		    $line !~ /\btypedef\s+$Type\s*\(\s*\*?$Ident\s*\)\s*\(/ &&
		    $line !~ /\btypedef\s+$Type\s+$Ident\s*\(/ &&
		    $line !~ /\b$typeTypedefs\b/ &&
		    $line !~ /\b__bitwise(?:__|)\b/) {
			WARN("do not add new typedefs\n" . $herecurr);
		}

# * goes on variable not on type
		# (char*[ const])
		if ($line =~ m{\($NonptrType(\s*(?:$Modifier\b\s*|\*\s*)+)\)}) {
			my ($from, $to) = ($1, $1);

			# Should start with a space.
			$to =~ s/^(\S)/ $1/;
			# Should not end with a space.
			$to =~ s/\s+$//;
			# '*'s should not have spaces between.
			while ($to =~ s/\*\s+\*/\*\*/) {
			}

			#print "from<$from> to<$to>\n";
			if ($from ne $to) {
				ERROR("\"(foo$from)\" should be \"(foo$to)\"\n" .  $herecurr);
			}
		} elsif ($line =~ m{\b$NonptrType(\s*(?:$Modifier\b\s*|\*\s*)+)($Ident)}) {
			my ($from, $to, $ident) = ($1, $1, $2);

			# Should start with a space.
			$to =~ s/^(\S)/ $1/;
			# Should not end with a space.
			$to =~ s/\s+$//;
			# '*'s should not have spaces between.
			while ($to =~ s/\*\s+\*/\*\*/) {
			}
			# Modifiers should have spaces.
			$to =~ s/(\b$Modifier$)/$1 /;

			#print "from<$from> to<$to> ident<$ident>\n";
			if ($from ne $to && $ident !~ /^$Modifier$/) {
				ERROR("\"foo${from}bar\" should be \"foo${to}bar\"\n" .  $herecurr);
			}
		}

# # no BUG() or BUG_ON()
# 		if ($line =~ /\b(BUG|BUG_ON)\b/) {
# 			print "Try to use WARN_ON & Recovery code rather than BUG() or BUG_ON()\n";
# 			print "$herecurr";
# 			$clean = 0;
# 		}

		if ($line =~ /\bLINUX_VERSION_CODE\b/) {
			WARN("LINUX_VERSION_CODE should be avoided, code should be for the version to which it is merged\n" . $herecurr);
		}

# printk should use KERN_* levels.  Note that follow on printk's on the
# same line do not need a level, so we use the current block context
# to try and find and validate the current printk.  In summary the current
# printk includes all preceeding printk's which have no newline on the end.
# we assume the first bad printk is the one to report.
		if ($line =~ /\bprintk\((?!KERN_)\s*"/) {
			my $ok = 0;
			for (my $ln = $linenr - 1; $ln >= $first_line; $ln--) {
				#print "CHECK<$lines[$ln - 1]\n";
				# we have a preceeding printk if it ends
				# with "\n" ignore it, else it is to blame
				if ($lines[$ln - 1] =~ m{\bprintk\(}) {
					if ($rawlines[$ln - 1] !~ m{\\n"}) {
						$ok = 1;
					}
					last;
				}
			}
			if ($ok == 0) {
				WARN("printk() should include KERN_ facility level\n" . $herecurr);
			}
		}

# function brace can't be on same line, except for #defines of do while,
# or if closed on same line
		if (($line=~/$Type\s*$Ident\(.*\).*\s{/) and
		    !($line=~/\#\s*define.*do\s{/) and !($line=~/}/)) {
			ERROR("open brace '{' following function declarations go on the next line\n" . $herecurr);
		}

# open braces for enum, union and struct go on the same line.
		if ($line =~ /^.\s*{/ &&
		    $prevline =~ /^.\s*(?:typedef\s+)?(enum|union|struct)(?:\s+$Ident)?\s*$/) {
			ERROR("open brace '{' following $1 go on the same line\n" . $hereprev);
		}

# check for spacing round square brackets; allowed:
#  1. with a type on the left -- int [] a;
#  2. at the beginning of a line for slice initialisers -- [0...10] = 5,
#  3. inside a curly brace -- = { [0...10] = 5 }
		while ($line =~ /(.*?\s)\[/g) {
			my ($where, $prefix) = ($-[1], $1);
			if ($prefix !~ /$Type\s+$/ &&
			    ($where != 0 || $prefix !~ /^.\s+$/) &&
			    $prefix !~ /{\s+$/) {
				ERROR("space prohibited before open square bracket '['\n" . $herecurr);
			}
		}

# check for spaces between functions and their parentheses.
		while ($line =~ /($Ident)\s+\(/g) {
			my $name = $1;
			my $ctx_before = substr($line, 0, $-[1]);
			my $ctx = "$ctx_before$name";

			# Ignore those directives where spaces _are_ permitted.
			if ($name =~ /^(?:
				if|for|while|switch|return|case|
				volatile|__volatile__|
				__attribute__|format|__extension__|
				asm|__asm__)$/x)
			{

			# cpp #define statements have non-optional spaces, ie
			# if there is a space between the name and the open
			# parenthesis it is simply not a parameter group.
			} elsif ($ctx_before =~ /^.\s*\#\s*define\s*$/) {

			# cpp #elif statement condition may start with a (
			} elsif ($ctx =~ /^.\s*\#\s*elif\s*$/) {

			# If this whole things ends with a type its most
			# likely a typedef for a function.
			} elsif ($ctx =~ /$Type$/) {

			} else {
				WARN("space prohibited between function name and open parenthesis '('\n" . $herecurr);
			}
		}
# Check operator spacing.
		if (!($line=~/\#\s*include/)) {
			my $ops = qr{
				<<=|>>=|<=|>=|==|!=|
				\+=|-=|\*=|\/=|%=|\^=|\|=|&=|
				=>|->|<<|>>|<|>|=|!|~|
				&&|\|\||,|\^|\+\+|--|&|\||\+|-|\*|\/|%|
				\?|:
			}x;
			my @elements = split(/($ops|;)/, $opline);
			my $off = 0;

			my $blank = copy_spacing($opline);

			for (my $n = 0; $n < $#elements; $n += 2) {
				$off += length($elements[$n]);

				# Pick up the preceeding and succeeding characters.
				my $ca = substr($opline, 0, $off);
				my $cc = '';
				if (length($opline) >= ($off + length($elements[$n + 1]))) {
					$cc = substr($opline, $off + length($elements[$n + 1]));
				}
				my $cb = "$ca$;$cc";

				my $a = '';
				$a = 'V' if ($elements[$n] ne '');
				$a = 'W' if ($elements[$n] =~ /\s$/);
				$a = 'C' if ($elements[$n] =~ /$;$/);
				$a = 'B' if ($elements[$n] =~ /(\[|\()$/);
				$a = 'O' if ($elements[$n] eq '');
				$a = 'E' if ($ca =~ /^\s*$/);

				my $op = $elements[$n + 1];

				my $c = '';
				if (defined $elements[$n + 2]) {
					$c = 'V' if ($elements[$n + 2] ne '');
					$c = 'W' if ($elements[$n + 2] =~ /^\s/);
					$c = 'C' if ($elements[$n + 2] =~ /^$;/);
					$c = 'B' if ($elements[$n + 2] =~ /^(\)|\]|;)/);
					$c = 'O' if ($elements[$n + 2] eq '');
					$c = 'E' if ($elements[$n + 2] =~ /^\s*\\$/);
				} else {
					$c = 'E';
				}

				my $ctx = "${a}x${c}";

				my $at = "(ctx:$ctx)";

				my $ptr = substr($blank, 0, $off) . "^";
				my $hereptr = "$hereline$ptr\n";

				# Pull out the value of this operator.
				my $op_type = substr($curr_values, $off + 1, 1);

				# Get the full operator variant.
				my $opv = $op . substr($curr_vars, $off, 1);

				# Ignore operators passed as parameters.
				if ($op_type ne 'V' &&
				    $ca =~ /\s$/ && $cc =~ /^\s*,/) {

#				# Ignore comments
#				} elsif ($op =~ /^$;+$/) {

				# ; should have either the end of line or a space or \ after it
				} elsif ($op eq ';') {
					if ($ctx !~ /.x[WEBC]/ &&
					    $cc !~ /^\\/ && $cc !~ /^;/) {
						ERROR("space required after that '$op' $at\n" . $hereptr);
					}

				# // is a comment
				} elsif ($op eq '//') {

				# No spaces for:
				#   ->
				#   :   when part of a bitfield
				} elsif ($op eq '->' || $opv eq ':B') {
					if ($ctx =~ /Wx.|.xW/) {
						ERROR("spaces prohibited around that '$op' $at\n" . $hereptr);
					}

				# , must have a space on the right.
				} elsif ($op eq ',') {
					if ($ctx !~ /.x[WEC]/ && $cc !~ /^}/) {
						ERROR("space required after that '$op' $at\n" . $hereptr);
					}

				# '*' as part of a type definition -- reported already.
				} elsif ($opv eq '*_') {
					#warn "'*' is part of type\n";

				# unary operators should have a space before and
				# none after.  May be left adjacent to another
				# unary operator, or a cast
				} elsif ($op eq '!' || $op eq '~' ||
					 $opv eq '*U' || $opv eq '-U' ||
					 $opv eq '&U' || $opv eq '&&U') {
					if ($ctx !~ /[WEBC]x./ && $ca !~ /(?:\)|!|~|\*|-|\&|\||\+\+|\-\-|\{)$/) {
						ERROR("space required before that '$op' $at\n" . $hereptr);
					}
					if ($op eq '*' && $cc =~/\s*$Modifier\b/) {
						# A unary '*' may be const

					} elsif ($ctx =~ /.xW/) {
						ERROR("space prohibited after that '$op' $at\n" . $hereptr);
					}

				# unary ++ and unary -- are allowed no space on one side.
				} elsif ($op eq '++' or $op eq '--') {
					if ($ctx !~ /[WEOBC]x[^W]/ && $ctx !~ /[^W]x[WOBEC]/) {
						ERROR("space required one side of that '$op' $at\n" . $hereptr);
					}
					if ($ctx =~ /Wx[BE]/ ||
					    ($ctx =~ /Wx./ && $cc =~ /^;/)) {
						ERROR("space prohibited before that '$op' $at\n" . $hereptr);
					}
					if ($ctx =~ /ExW/) {
						ERROR("space prohibited after that '$op' $at\n" . $hereptr);
					}


				# << and >> may either have or not have spaces both sides
				} elsif ($op eq '<<' or $op eq '>>' or
					 $op eq '&' or $op eq '^' or $op eq '|' or
					 $op eq '+' or $op eq '-' or
					 $op eq '*' or $op eq '/' or
					 $op eq '%')
				{
					if ($ctx =~ /Wx[^WCE]|[^WCE]xW/) {
						ERROR("need consistent spacing around '$op' $at\n" .
							$hereptr);
					}

				# A colon needs no spaces before when it is
				# terminating a case value or a label.
				} elsif ($opv eq ':C' || $opv eq ':L') {
					if ($ctx =~ /Wx./) {
						ERROR("space prohibited before that '$op' $at\n" . $hereptr);
					}

				# All the others need spaces both sides.
				} elsif ($ctx !~ /[EWC]x[CWE]/) {
					my $ok = 0;

					# Ignore email addresses <foo@bar>
					if (($op eq '<' &&
					     $cc =~ /^\S+\@\S+>/) ||
					    ($op eq '>' &&
					     $ca =~ /<\S+\@\S+$/))
					{
					    	$ok = 1;
					}

					# Ignore ?:
					if (($opv eq ':O' && $ca =~ /\?$/) ||
					    ($op eq '?' && $cc =~ /^:/)) {
					    	$ok = 1;
					}

					if ($ok == 0) {
						ERROR("spaces required around that '$op' $at\n" . $hereptr);
					}
				}
				$off += length($elements[$n + 1]);
			}
		}

# check for multiple assignments
		if ($line =~ /^.\s*$Lval\s*=\s*$Lval\s*=(?!=)/) {
			CHK("multiple assignments should be avoided\n" . $herecurr);
		}

## # check for multiple declarations, allowing for a function declaration
## # continuation.
## 		if ($line =~ /^.\s*$Type\s+$Ident(?:\s*=[^,{]*)?\s*,\s*$Ident.*/ &&
## 		    $line !~ /^.\s*$Type\s+$Ident(?:\s*=[^,{]*)?\s*,\s*$Type\s*$Ident.*/) {
##
## 			# Remove any bracketed sections to ensure we do not
## 			# falsly report the parameters of functions.
## 			my $ln = $line;
## 			while ($ln =~ s/\([^\(\)]*\)//g) {
## 			}
## 			if ($ln =~ /,/) {
## 				WARN("declaring multiple variables together should be avoided\n" . $herecurr);
## 			}
## 		}

#need space before brace following if, while, etc
		if (($line =~ /\(.*\){/ && $line !~ /\($Type\){/) ||
		    $line =~ /do{/) {
			ERROR("space required before the open brace '{'\n" . $herecurr);
		}

# closing brace should have a space following it when it has anything
# on the line
		if ($line =~ /}(?!(?:,|;|\)))\S/) {
			ERROR("space required after that close brace '}'\n" . $herecurr);
		}

# check spacing on square brackets
		if ($line =~ /\[\s/ && $line !~ /\[\s*$/) {
			ERROR("space prohibited after that open square bracket '['\n" . $herecurr);
		}
		if ($line =~ /\s\]/) {
			ERROR("space prohibited before that close square bracket ']'\n" . $herecurr);
		}

# check spacing on parentheses
		if ($line =~ /\(\s/ && $line !~ /\(\s*(?:\\)?$/ &&
		    $line !~ /for\s*\(\s+;/) {
			ERROR("space prohibited after that open parenthesis '('\n" . $herecurr);
		}
		if ($line =~ /(\s+)\)/ && $line !~ /^.\s*\)/ &&
		    $line !~ /for\s*\(.*;\s+\)/ &&
		    $line !~ /:\s+\)/) {
			ERROR("space prohibited before that close parenthesis ')'\n" . $herecurr);
		}

#goto labels aren't indented, allow a single space however
		if ($line=~/^.\s+[A-Za-z\d_]+:(?![0-9]+)/ and
		   !($line=~/^. [A-Za-z\d_]+:/) and !($line=~/^.\s+default:/)) {
			WARN("labels should not be indented\n" . $herecurr);
		}

# Return is not a function.
		if (defined($stat) && $stat =~ /^.\s*return(\s*)(\(.*);/s) {
			my $spacing = $1;
			my $value = $2;

			# Flatten any parentheses
			$value =~ s/\)\(/\) \(/g;
			while ($value =~ s/\[[^\{\}]*\]/1/ ||
			       $value !~ /(?:$Ident|-?$Constant)\s*
					     $Compare\s*
					     (?:$Ident|-?$Constant)/x &&
			       $value =~ s/\([^\(\)]*\)/1/) {
			}

			if ($value =~ /^(?:$Ident|-?$Constant)$/) {
				ERROR("return is not a function, parentheses are not required\n" . $herecurr);

			} elsif ($spacing !~ /\s+/) {
				ERROR("space required before the open parenthesis '('\n" . $herecurr);
			}
		}

# Need a space before open parenthesis after if, while etc
		if ($line=~/\b(if|while|for|switch)\(/) {
			ERROR("space required before the open parenthesis '('\n" . $herecurr);
		}

# Check for illegal assignment in if conditional -- and check for trailing
# statements after the conditional.
		if ($line =~ /do\s*(?!{)/) {
			my ($stat_next) = ctx_statement_block($line_nr_next,
						$remain_next, $off_next);
			$stat_next =~ s/\n./\n /g;
			##print "stat<$stat> stat_next<$stat_next>\n";

			if ($stat_next =~ /^\s*while\b/) {
				# If the statement carries leading newlines,
				# then count those as offsets.
				my ($whitespace) =
					($stat_next =~ /^((?:\s*\n[+-])*\s*)/s);
				my $offset =
					statement_rawlines($whitespace) - 1;

				$suppress_whiletrailers{$line_nr_next +
								$offset} = 1;
			}
		}
		if (!defined $suppress_whiletrailers{$linenr} &&
		    $line =~ /\b(?:if|while|for)\s*\(/ && $line !~ /^.\s*#/) {
			my ($s, $c) = ($stat, $cond);

			if ($c =~ /\bif\s*\(.*[^<>!=]=[^=].*/s) {
				ERROR("do not use assignment in if condition\n" . $herecurr);
			}

			# Find out what is on the end of the line after the
			# conditional.
			substr($s, 0, length($c), '');
			$s =~ s/\n.*//g;
			$s =~ s/$;//g; 	# Remove any comments
			if (length($c) && $s !~ /^\s*{?\s*\\*\s*$/ &&
			    $c !~ /}\s*while\s*/)
			{
				# Find out how long the conditional actually is.
				my @newlines = ($c =~ /\n/gs);
				my $cond_lines = 1 + $#newlines;
				my $stat_real = '';

				$stat_real = raw_line($linenr, $cond_lines)
							. "\n" if ($cond_lines);
				if (defined($stat_real) && $cond_lines > 1) {
					$stat_real = "[...]\n$stat_real";
				}

				ERROR("trailing statements should be on next line\n" . $herecurr . $stat_real);
			}
		}

# Check for bitwise tests written as boolean
		if ($line =~ /
			(?:
				(?:\[|\(|\&\&|\|\|)
				\s*0[xX][0-9]+\s*
				(?:\&\&|\|\|)
			|
				(?:\&\&|\|\|)
				\s*0[xX][0-9]+\s*
				(?:\&\&|\|\||\)|\])
			)/x)
		{
			WARN("boolean test with hexadecimal, perhaps just 1 \& or \|?\n" . $herecurr);
		}

# if and else should not have general statements after it
		if ($line =~ /^.\s*(?:}\s*)?else\b(.*)/) {
			my $s = $1;
			$s =~ s/$;//g; 	# Remove any comments
			if ($s !~ /^\s*(?:\sif|(?:{|)\s*\\?\s*$)/) {
				ERROR("trailing statements should be on next line\n" . $herecurr);
			}
		}
# if should not continue a brace
		if ($line =~ /}\s*if\b/) {
			ERROR("trailing statements should be on next line\n" .
				$herecurr);
		}
# case and default should not have general statements after them
		if ($line =~ /^.\s*(?:case\s*.*|default\s*):/g &&
		    $line !~ /\G(?:
			(?:\s*$;*)(?:\s*{)?(?:\s*$;*)(?:\s*\\)?\s*$|
			\s*return\s+
		    )/xg)
		{
			ERROR("trailing statements should be on next line\n" . $herecurr);
		}

		# Check for }<nl>else {, these must be at the same
		# indent level to be relevant to each other.
		if ($prevline=~/}\s*$/ and $line=~/^.\s*else\s*/ and
						$previndent == $indent) {
			ERROR("else should follow close brace '}'\n" . $hereprev);
		}

		if ($prevline=~/}\s*$/ and $line=~/^.\s*while\s*/ and
						$previndent == $indent) {
			my ($s, $c) = ctx_statement_block($linenr, $realcnt, 0);

			# Find out what is on the end of the line after the
			# conditional.
			substr($s, 0, length($c), '');
			$s =~ s/\n.*//g;

			if ($s =~ /^\s*;/) {
				ERROR("while should follow close brace '}'\n" . $hereprev);
			}
		}

#studly caps, commented out until figure out how to distinguish between use of existing and adding new
#		if (($line=~/[\w_][a-z\d]+[A-Z]/) and !($line=~/print/)) {
#		    print "No studly caps, use _\n";
#		    print "$herecurr";
#		    $clean = 0;
#		}

#no spaces allowed after \ in define
		if ($line=~/\#\s*define.*\\\s$/) {
			WARN("Whitepspace after \\ makes next lines useless\n" . $herecurr);
		}

#warn if <asm/foo.h> is #included and <linux/foo.h> is available (uses RAW line)
		if ($tree && $rawline =~ m{^.\s*\#\s*include\s*\<asm\/(.*)\.h\>}) {
			my $file = "$1.h";
			my $checkfile = "include/linux/$file";
			if (-f "$root/$checkfile" &&
			    $realfile ne $checkfile &&
			    $1 ne 'irq')
			{
				if ($realfile =~ m{^arch/}) {
					CHK("Consider using #include <linux/$file> instead of <asm/$file>\n" . $herecurr);
				} else {
					WARN("Use #include <linux/$file> instead of <asm/$file>\n" . $herecurr);
				}
			}
		}

# multi-statement macros should be enclosed in a do while loop, grab the
# first statement and ensure its the whole macro if its not enclosed
# in a known good container
		if ($realfile !~ m@/vmlinux.lds.h$@ &&
		    $line =~ /^.\s*\#\s*define\s*$Ident(\()?/) {
			my $ln = $linenr;
			my $cnt = $realcnt;
			my ($off, $dstat, $dcond, $rest);
			my $ctx = '';

			my $args = defined($1);

			# Find the end of the macro and limit our statement
			# search to that.
			while ($cnt > 0 && defined $lines[$ln - 1] &&
				$lines[$ln - 1] =~ /^(?:-|..*\\$)/)
			{
				$ctx .= $rawlines[$ln - 1] . "\n";
				$cnt-- if ($lines[$ln - 1] !~ /^-/);
				$ln++;
			}
			$ctx .= $rawlines[$ln - 1];

			($dstat, $dcond, $ln, $cnt, $off) =
				ctx_statement_block($linenr, $ln - $linenr + 1, 0);
			#print "dstat<$dstat> dcond<$dcond> cnt<$cnt> off<$off>\n";
			#print "LINE<$lines[$ln-1]> len<" . length($lines[$ln-1]) . "\n";

			# Extract the remainder of the define (if any) and
			# rip off surrounding spaces, and trailing \'s.
			$rest = '';
			while ($off != 0 || ($cnt > 0 && $rest =~ /\\\s*$/)) {
				#print "ADDING cnt<$cnt> $off <" . substr($lines[$ln - 1], $off) . "> rest<$rest>\n";
				if ($off != 0 || $lines[$ln - 1] !~ /^-/) {
					$rest .= substr($lines[$ln - 1], $off) . "\n";
					$cnt--;
				}
				$ln++;
				$off = 0;
			}
			$rest =~ s/\\\n.//g;
			$rest =~ s/^\s*//s;
			$rest =~ s/\s*$//s;

			# Clean up the original statement.
			if ($args) {
				substr($dstat, 0, length($dcond), '');
			} else {
				$dstat =~ s/^.\s*\#\s*define\s+$Ident\s*//;
			}
			$dstat =~ s/$;//g;
			$dstat =~ s/\\\n.//g;
			$dstat =~ s/^\s*//s;
			$dstat =~ s/\s*$//s;

			# Flatten any parentheses and braces
			while ($dstat =~ s/\([^\(\)]*\)/1/ ||
			       $dstat =~ s/\{[^\{\}]*\}/1/ ||
			       $dstat =~ s/\[[^\{\}]*\]/1/)
			{
			}

			my $exceptions = qr{
				$Declare|
				module_param_named|
				MODULE_PARAM_DESC|
				DECLARE_PER_CPU|
				DEFINE_PER_CPU|
				__typeof__\(|
				union|
				struct|
				\.$Ident\s*=\s*|
				^\"|\"$
			}x;
			#print "REST<$rest> dstat<$dstat>\n";
			if ($rest ne '') {
				if ($rest !~ /while\s*\(/ &&
				    $dstat !~ /$exceptions/)
				{
					ERROR("Macros with multiple statements should be enclosed in a do - while loop\n" . "$here\n$ctx\n");
				}

			} elsif ($ctx !~ /;/) {
				if ($dstat ne '' &&
				    $dstat !~ /^(?:$Ident|-?$Constant)$/ &&
				    $dstat !~ /$exceptions/ &&
				    $dstat !~ /^\.$Ident\s*=/ &&
				    $dstat =~ /$Operators/)
				{
					ERROR("Macros with complex values should be enclosed in parenthesis\n" . "$here\n$ctx\n");
				}
			}
		}

# make sure symbols are always wrapped with VMLINUX_SYMBOL() ...
# all assignments may have only one of the following with an assignment:
#	.
#	ALIGN(...)
#	VMLINUX_SYMBOL(...)
		if ($realfile eq 'vmlinux.lds.h' && $line =~ /(?:(?:^|\s)$Ident\s*=|=\s*$Ident(?:\s|$))/) {
			WARN("vmlinux.lds.h needs VMLINUX_SYMBOL() around C-visible symbols\n" . $herecurr);
		}

# check for redundant bracing round if etc
		if ($line =~ /(^.*)\bif\b/ && $1 !~ /else\s*$/) {
			my ($level, $endln, @chunks) =
				ctx_statement_full($linenr, $realcnt, 1);
			#print "chunks<$#chunks> linenr<$linenr> endln<$endln> level<$level>\n";
			#print "APW: <<$chunks[1][0]>><<$chunks[1][1]>>\n";
			if ($#chunks > 0 && $level == 0) {
				my $allowed = 0;
				my $seen = 0;
				my $herectx = $here . "\n";
				my $ln = $linenr - 1;
				for my $chunk (@chunks) {
					my ($cond, $block) = @{$chunk};

					# If the condition carries leading newlines, then count those as offsets.
					my ($whitespace) = ($cond =~ /^((?:\s*\n[+-])*\s*)/s);
					my $offset = statement_rawlines($whitespace) - 1;

					#print "COND<$cond> whitespace<$whitespace> offset<$offset>\n";

					# We have looked at and allowed this specific line.
					$suppress_ifbraces{$ln + $offset} = 1;

					$herectx .= "$rawlines[$ln + $offset]\n[...]\n";
					$ln += statement_rawlines($block) - 1;

					substr($block, 0, length($cond), '');

					$seen++ if ($block =~ /^\s*{/);

					#print "cond<$cond> block<$block> allowed<$allowed>\n";
					if (statement_lines($cond) > 1) {
						#print "APW: ALLOWED: cond<$cond>\n";
						$allowed = 1;
					}
					if ($block =~/\b(?:if|for|while)\b/) {
						#print "APW: ALLOWED: block<$block>\n";
						$allowed = 1;
					}
					if (statement_block_size($block) > 1) {
						#print "APW: ALLOWED: lines block<$block>\n";
						$allowed = 1;
					}
				}
				if ($seen && !$allowed) {
					WARN("braces {} are not necessary for any arm of this statement\n" . $herectx);
				}
			}
		}
		if (!defined $suppress_ifbraces{$linenr - 1} &&
					$line =~ /\b(if|while|for|else)\b/) {
			my $allowed = 0;

			# Check the pre-context.
			if (substr($line, 0, $-[0]) =~ /(\}\s*)$/) {
				#print "APW: ALLOWED: pre<$1>\n";
				$allowed = 1;
			}

			my ($level, $endln, @chunks) =
				ctx_statement_full($linenr, $realcnt, $-[0]);

			# Check the condition.
			my ($cond, $block) = @{$chunks[0]};
			#print "CHECKING<$linenr> cond<$cond> block<$block>\n";
			if (defined $cond) {
				substr($block, 0, length($cond), '');
			}
			if (statement_lines($cond) > 1) {
				#print "APW: ALLOWED: cond<$cond>\n";
				$allowed = 1;
			}
			if ($block =~/\b(?:if|for|while)\b/) {
				#print "APW: ALLOWED: block<$block>\n";
				$allowed = 1;
			}
			if (statement_block_size($block) > 1) {
				#print "APW: ALLOWED: lines block<$block>\n";
				$allowed = 1;
			}
			# Check the post-context.
			if (defined $chunks[1]) {
				my ($cond, $block) = @{$chunks[1]};
				if (defined $cond) {
					substr($block, 0, length($cond), '');
				}
				if ($block =~ /^\s*\{/) {
					#print "APW: ALLOWED: chunk-1 block<$block>\n";
					$allowed = 1;
				}
			}
			if ($level == 0 && $block =~ /^\s*\{/ && !$allowed) {
				my $herectx = $here . "\n";;
				my $cnt = statement_rawlines($block);

				for (my $n = 0; $n < $cnt; $n++) {
					$herectx .= raw_line($linenr, $n) . "\n";;
				}

				WARN("braces {} are not necessary for single statement blocks\n" . $herectx);
			}
		}

# don't include deprecated include files (uses RAW line)
		for my $inc (@dep_includes) {
			if ($rawline =~ m@^.\s*\#\s*include\s*\<$inc>@) {
				ERROR("Don't use <$inc>: see Documentation/feature-removal-schedule.txt\n" . $herecurr);
			}
		}

# don't use deprecated functions
		for my $func (@dep_functions) {
			if ($line =~ /\b$func\b/) {
				ERROR("Don't use $func(): see Documentation/feature-removal-schedule.txt\n" . $herecurr);
			}
		}

# no volatiles please
		my $asm_volatile = qr{\b(__asm__|asm)\s+(__volatile__|volatile)\b};
		if ($line =~ /\bvolatile\b/ && $line !~ /$asm_volatile/) {
			WARN("Use of volatile is usually wrong: see Documentation/volatile-considered-harmful.txt\n" . $herecurr);
		}

# SPIN_LOCK_UNLOCKED & RW_LOCK_UNLOCKED are deprecated
		if ($line =~ /\b(SPIN_LOCK_UNLOCKED|RW_LOCK_UNLOCKED)/) {
			ERROR("Use of $1 is deprecated: see Documentation/spinlocks.txt\n" . $herecurr);
		}

# warn about #if 0
		if ($line =~ /^.\s*\#\s*if\s+0\b/) {
			CHK("if this code is redundant consider removing it\n" .
				$herecurr);
		}

# check for needless kfree() checks
		if ($prevline =~ /\bif\s*\(([^\)]*)\)/) {
			my $expr = $1;
			if ($line =~ /\bkfree\(\Q$expr\E\);/) {
				WARN("kfree(NULL) is safe this check is probably not required\n" . $hereprev);
			}
		}
# check for needless usb_free_urb() checks
		if ($prevline =~ /\bif\s*\(([^\)]*)\)/) {
			my $expr = $1;
			if ($line =~ /\busb_free_urb\(\Q$expr\E\);/) {
				WARN("usb_free_urb(NULL) is safe this check is probably not required\n" . $hereprev);
			}
		}

# warn about #ifdefs in C files
#		if ($line =~ /^.\s*\#\s*if(|n)def/ && ($realfile =~ /\.c$/)) {
#			print "#ifdef in C files should be avoided\n";
#			print "$herecurr";
#			$clean = 0;
#		}

# warn about spacing in #ifdefs
		if ($line =~ /^.\s*\#\s*(ifdef|ifndef|elif)\s\s+/) {
			ERROR("exactly one space required after that #$1\n" . $herecurr);
		}

# check for spinlock_t definitions without a comment.
		if ($line =~ /^.\s*(struct\s+mutex|spinlock_t)\s+\S+;/ ||
		    $line =~ /^.\s*(DEFINE_MUTEX)\s*\(/) {
			my $which = $1;
			if (!ctx_has_comment($first_line, $linenr)) {
				CHK("$1 definition without comment\n" . $herecurr);
			}
		}
# check for memory barriers without a comment.
		if ($line =~ /\b(mb|rmb|wmb|read_barrier_depends|smp_mb|smp_rmb|smp_wmb|smp_read_barrier_depends)\(/) {
			if (!ctx_has_comment($first_line, $linenr)) {
				CHK("memory barrier without comment\n" . $herecurr);
			}
		}
# check of hardware specific defines
		if ($line =~ m@^.\s*\#\s*if.*\b(__i386__|__powerpc64__|__sun__|__s390x__)\b@ && $realfile !~ m@include/asm-@) {
			CHK("architecture specific defines should be avoided\n" .  $herecurr);
		}

# check the location of the inline attribute, that it is between
# storage class and type.
		if ($line =~ /\b$Type\s+$Inline\b/ ||
		    $line =~ /\b$Inline\s+$Storage\b/) {
			ERROR("inline keyword should sit between storage class and type\n" . $herecurr);
		}

# Check for __inline__ and __inline, prefer inline
		if ($line =~ /\b(__inline__|__inline)\b/) {
			WARN("plain inline is preferred over $1\n" . $herecurr);
		}

# check for sizeof(&)
		if ($line =~ /\bsizeof\s*\(\s*\&/) {
			WARN("sizeof(& should be avoided\n" . $herecurr);
		}

# check for new externs in .c files.
		if ($realfile =~ /\.c$/ && defined $stat &&
		    $stat =~ /^.\s*(?:extern\s+)?$Type\s+($Ident)(\s*)\(/s)
		{
			my $function_name = $1;
			my $paren_space = $2;

			my $s = $stat;
			if (defined $cond) {
				substr($s, 0, length($cond), '');
			}
			if ($s =~ /^\s*;/ &&
			    $function_name ne 'uninitialized_var')
			{
				WARN("externs should be avoided in .c files\n" .  $herecurr);
			}

			if ($paren_space =~ /\n/) {
				WARN("arguments for function declarations should follow identifier\n" . $herecurr);
			}

		} elsif ($realfile =~ /\.c$/ && defined $stat &&
		    $stat =~ /^.\s*extern\s+/)
		{
			WARN("externs should be avoided in .c files\n" .  $herecurr);
		}

# checks for new __setup's
		if ($rawline =~ /\b__setup\("([^"]*)"/) {
			my $name = $1;

			if (!grep(/$name/, @setup_docs)) {
				CHK("__setup appears un-documented -- check Documentation/kernel-parameters.txt\n" . $herecurr);
			}
		}

# check for pointless casting of kmalloc return
		if ($line =~ /\*\s*\)\s*k[czm]alloc\b/) {
			WARN("unnecessary cast may hide bugs, see http://c-faq.com/malloc/mallocnocast.html\n" . $herecurr);
		}

# check for gcc specific __FUNCTION__
		if ($line =~ /__FUNCTION__/) {
			WARN("__func__ should be used instead of gcc specific __FUNCTION__\n"  . $herecurr);
		}

# check for semaphores used as mutexes
		if ($line =~ /^.\s*(DECLARE_MUTEX|init_MUTEX)\s*\(/) {
			WARN("mutexes are preferred for single holder semaphores\n" . $herecurr);
		}
# check for semaphores used as mutexes
		if ($line =~ /^.\s*init_MUTEX_LOCKED\s*\(/) {
			WARN("consider using a completion\n" . $herecurr);
		}
# recommend strict_strto* over simple_strto*
		if ($line =~ /\bsimple_(strto.*?)\s*\(/) {
			WARN("consider using strict_$1 in preference to simple_$1\n" . $herecurr);
		}
# check for __initcall(), use device_initcall() explicitly please
		if ($line =~ /^.\s*__initcall\s*\(/) {
			WARN("please use device_initcall() instead of __initcall()\n" . $herecurr);
		}
# check for various ops structs, ensure they are const.
		my $struct_ops = qr{acpi_dock_ops|
				address_space_operations|
				backlight_ops|
				block_device_operations|
				dentry_operations|
				dev_pm_ops|
				dma_map_ops|
				extent_io_ops|
				file_lock_operations|
				file_operations|
				hv_ops|
				ide_dma_ops|
				intel_dvo_dev_ops|
				item_operations|
				iwl_ops|
				kgdb_arch|
				kgdb_io|
				kset_uevent_ops|
				lock_manager_operations|
				microcode_ops|
				mtrr_ops|
				neigh_ops|
				nlmsvc_binding|
				pci_raw_ops|
				pipe_buf_operations|
				platform_hibernation_ops|
				platform_suspend_ops|
				proto_ops|
				rpc_pipe_ops|
				seq_operations|
				snd_ac97_build_ops|
				soc_pcmcia_socket_ops|
				stacktrace_ops|
				sysfs_ops|
				tty_operations|
				usb_mon_operations|
				wd_ops}x;
		if ($line !~ /\bconst\b/ &&
		    $line =~ /\bstruct\s+($struct_ops)\b/) {
			WARN("struct $1 should normally be const\n" .
				$herecurr);
		}

# use of NR_CPUS is usually wrong
# ignore definitions of NR_CPUS and usage to define arrays as likely right
		if ($line =~ /\bNR_CPUS\b/ &&
		    $line !~ /^.\s*\s*#\s*if\b.*\bNR_CPUS\b/ &&
		    $line !~ /^.\s*\s*#\s*define\b.*\bNR_CPUS\b/ &&
		    $line !~ /^.\s*$Declare\s.*\[[^\]]*NR_CPUS[^\]]*\]/ &&
		    $line !~ /\[[^\]]*\.\.\.[^\]]*NR_CPUS[^\]]*\]/ &&
		    $line !~ /\[[^\]]*NR_CPUS[^\]]*\.\.\.[^\]]*\]/)
		{
			WARN("usage of NR_CPUS is often wrong - consider using cpu_possible(), num_possible_cpus(), for_each_possible_cpu(), etc\n" . $herecurr);
		}

# check for %L{u,d,i} in strings
		my $string;
		while ($line =~ /(?:^|")([X\t]*)(?:"|$)/g) {
			$string = substr($rawline, $-[1], $+[1] - $-[1]);
			$string =~ s/%%/__/g;
			if ($string =~ /(?<!%)%L[udi]/) {
				WARN("\%Ld/%Lu are not-standard C, use %lld/%llu\n" . $herecurr);
				last;
			}
		}

# whine mightly about in_atomic
		if ($line =~ /\bin_atomic\s*\(/) {
			if ($realfile =~ m@^drivers/@) {
				ERROR("do not use in_atomic in drivers\n" . $herecurr);
			} elsif ($realfile !~ m@^kernel/@) {
				WARN("use of in_atomic() is incorrect outside core kernel code\n" . $herecurr);
			}
		}
	}

	# If we have no input at all, then there is nothing to report on
	# so just keep quiet.
	if ($#rawlines == -1) {
		exit(0);
	}

	# In mailback mode only produce a report in the negative, for
	# things that appear to be patches.
	if ($mailback && ($clean == 1 || !$is_patch)) {
		exit(0);
	}

	# This is not a patch, and we are are in 'no-patch' mode so
	# just keep quiet.
	if (!$chk_patch && !$is_patch) {
		exit(0);
	}

	if (!$is_patch) {
		ERROR("Does not appear to be a unified-diff format patch\n");
	}
	if ($is_patch && $chk_signoff && $signoff == 0) {
		ERROR("Missing Signed-off-by: line(s)\n");
	}

	print report_dump();
	if ($summary && !($clean == 1 && $quiet == 1)) {
		print "$filename " if ($summary_file);
		print "total: $cnt_error errors, $cnt_warn warnings, " .
			(($check)? "$cnt_chk checks, " : "") .
			"$cnt_lines lines checked\n";
		print "\n" if ($quiet == 0);
	}

	if ($clean == 1 && $quiet == 0) {
		print "$vname has no obvious style problems and is ready for submission.\n"
	}
	if ($clean == 0 && $quiet == 0) {
		print "$vname has style problems, please review.  If any of these errors\n";
		print "are false positives report them to the maintainer, see\n";
		print "CHECKPATCH in MAINTAINERS.\n";
	}

	return $clean;
}
