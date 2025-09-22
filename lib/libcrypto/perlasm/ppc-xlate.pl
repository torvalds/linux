#!/usr/bin/env perl

# PowerPC assembler distiller by <appro>.

my $flavour = shift;
my $output = shift;
open STDOUT,">$output" || die "can't open $output: $!";

my %GLOBALS;
my $dotinlocallabels=($flavour=~/linux/)?1:0;

################################################################
# directives which need special treatment on different platforms
################################################################
my $globl = sub {
    my $junk = shift;
    my $name = shift;
    my $global = \$GLOBALS{$name};
    my $ret;

    $name =~ s|^[\.\_]||;
 
    SWITCH: for ($flavour) {
	/aix/		&& do { $name = ".$name";
				last;
			      };
	/osx/		&& do { $name = "_$name";
				last;
			      };
	/linux.*32/	&& do {	$ret .= ".globl	$name\n";
				$ret .= ".type	$name,\@function";
				last;
			      };
	/linux.*64/	&& do {	$ret .= ".globl	$name\n";
				$ret .= ".type	$name,\@function\n";
				$ret .= ".section	\".opd\",\"aw\"\n";
				$ret .= ".align	3\n";
				$ret .= "$name:\n";
				$ret .= ".quad	.$name,.TOC.\@tocbase,0\n";
				$ret .= ".size	$name,24\n";
				$ret .= ".previous\n";

				$name = ".$name";
				last;
			      };
    }

    $ret = ".globl	$name" if (!$ret);
    $$global = $name;
    $ret;
};
my $text = sub {
    ($flavour =~ /aix/) ? ".csect" : ".text";
};
my $machine = sub {
    my $junk = shift;
    my $arch = shift;
    if ($flavour =~ /osx/)
    {	$arch =~ s/\"//g;
	$arch = ($flavour=~/64/) ? "ppc970-64" : "ppc970" if ($arch eq "any");
    }
    ".machine	$arch";
};
my $size = sub {
    if ($flavour =~ /linux.*32/)
    {	shift;
	".size	" . join(",",@_);
    }
    else
    {	"";	}
};
my $asciz = sub {
    shift;
    my $line = join(",",@_);
    if ($line =~ /^"(.*)"$/)
    {	".byte	" . join(",",unpack("C*",$1),0) . "\n.align	2";	}
    else
    {	"";	}
};

################################################################
# simplified mnemonics not handled by at least one assembler
################################################################
my $cmplw = sub {
    my $f = shift;
    my $cr = 0; $cr = shift if ($#_>1);
    # Some out-of-date 32-bit GNU assembler just can't handle cmplw...
    ($flavour =~ /linux.*32/) ?
	"	.long	".sprintf "0x%x",31<<26|$cr<<23|$_[0]<<16|$_[1]<<11|64 :
	"	cmplw	".join(',',$cr,@_);
};
my $bdnz = sub {
    my $f = shift;
    my $bo = $f=~/[\+\-]/ ? 16+9 : 16;	# optional "to be taken" hint
    "	bc	$bo,0,".shift;
} if ($flavour!~/linux/);
my $bltlr = sub {
    my $f = shift;
    my $bo = $f=~/\-/ ? 12+2 : 12;	# optional "not to be taken" hint
    ($flavour =~ /linux/) ?		# GNU as doesn't allow most recent hints
	"	.long	".sprintf "0x%x",19<<26|$bo<<21|16<<1 :
	"	bclr	$bo,0";
};
my $bnelr = sub {
    my $f = shift;
    my $bo = $f=~/\-/ ? 4+2 : 4;	# optional "not to be taken" hint
    ($flavour =~ /linux/) ?		# GNU as doesn't allow most recent hints
	"	.long	".sprintf "0x%x",19<<26|$bo<<21|2<<16|16<<1 :
	"	bclr	$bo,2";
};
my $beqlr = sub {
    my $f = shift;
    my $bo = $f=~/-/ ? 12+2 : 12;	# optional "not to be taken" hint
    ($flavour =~ /linux/) ?		# GNU as doesn't allow most recent hints
	"	.long	".sprintf "0x%X",19<<26|$bo<<21|2<<16|16<<1 :
	"	bclr	$bo,2";
};
# GNU assembler can't handle extrdi rA,rS,16,48, or when sum of last two
# arguments is 64, with "operand out of range" error.
my $extrdi = sub {
    my ($f,$ra,$rs,$n,$b) = @_;
    $b = ($b+$n)&63; $n = 64-$n;
    "	rldicl	$ra,$rs,$b,$n";
};

while($line=<>) {

    $line =~ s|[#!;].*$||;	# get rid of asm-style comments...
    $line =~ s|/\*.*\*/||;	# ... and C-style comments...
    $line =~ s|^\s+||;		# ... and skip white spaces in beginning...
    $line =~ s|\s+$||;		# ... and at the end

    {
	$line =~ s|\b\.L(\w+)|L$1|g;	# common denominator for Locallabel
	$line =~ s|\bL(\w+)|\.L$1|g	if ($dotinlocallabels);
    }

    {
	$line =~ s|(^[\.\w]+)\:\s*||;
	my $label = $1;
	printf "%s:",($GLOBALS{$label} or $label) if ($label);
    }

    {
	$line =~ s|^\s*(\.?)(\w+)([\.\+\-]?)\s*||;
	my $c = $1; $c = "\t" if ($c eq "");
	my $mnemonic = $2;
	my $f = $3;
	my $opcode = eval("\$$mnemonic");
	$line =~ s|\bc?[rf]([0-9]+)\b|$1|g if ($c ne "." and $flavour !~ /osx/);
	if (ref($opcode) eq 'CODE') { $line = &$opcode($f,split(',',$line)); }
	elsif ($mnemonic)           { $line = $c.$mnemonic.$f."\t".$line; }
    }

    print $line if ($line);
    print "\n";
}

close STDOUT;
