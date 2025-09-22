#!/usr/bin/env perl

# Ascetic x86_64 AT&T to MASM/NASM assembler translator by <appro>.
#
# Why AT&T to MASM and not vice versa? Several reasons. Because AT&T
# format is way easier to parse. Because it's simpler to "gear" from
# Unix ABI to Windows one [see cross-reference "card" at the end of
# file]. Because Linux targets were available first...
#
# In addition the script also "distills" code suitable for GNU
# assembler, so that it can be compiled with more rigid assemblers,
# such as Solaris /usr/ccs/bin/as.
#
# This translator is not designed to convert *arbitrary* assembler
# code from AT&T format to MASM one. It's designed to convert just
# enough to provide for dual-ABI OpenSSL modules development...
# There *are* limitations and you might have to modify your assembler
# code or this script to achieve the desired result...
#
# Currently recognized limitations:
#
# - can't use multiple ops per line;
#
# Dual-ABI styling rules.
#
# 1. Adhere to Unix register and stack layout [see cross-reference
#    ABI "card" at the end for explanation].
# 2. Forget about "red zone," stick to more traditional blended
#    stack frame allocation. If volatile storage is actually required
#    that is. If not, just leave the stack as is.
# 3. Functions tagged with ".type name,@function" get crafted with
#    unified Win64 prologue and epilogue automatically. If you want
#    to take care of ABI differences yourself, tag functions as
#    ".type name,@abi-omnipotent" instead.
# 4. To optimize the Win64 prologue you can specify number of input
#    arguments as ".type name,@function,N." Keep in mind that if N is
#    larger than 6, then you *have to* write "abi-omnipotent" code,
#    because >6 cases can't be addressed with unified prologue.
# 5. Name local labels as .L*, do *not* use dynamic labels such as 1:
#    (sorry about latter).
# 6. Don't use [or hand-code with .byte] "rep ret." "ret" mnemonic is
#    required to identify the spots, where to inject Win64 epilogue!
#    But on the pros, it's then prefixed with rep automatically:-)
# 7. Stick to explicit ip-relative addressing. If you have to use
#    GOTPCREL addressing, stick to mov symbol@GOTPCREL(%rip),%r??.
#    Both are recognized and translated to proper Win64 addressing
#    modes. To support legacy code a synthetic directive, .picmeup,
#    is implemented. It puts address of the *next* instruction into
#    target register, e.g.:
#
#		.picmeup	%rax
#		lea		.Label-.(%rax),%rax
#
# 8. In order to provide for structured exception handling unified
#    Win64 prologue copies %rsp value to %rax. For further details
#    see SEH paragraph at the end.
# 9. .init segment is allowed to contain calls to functions only.
# a. If function accepts more than 4 arguments *and* >4th argument
#    is declared as non 64-bit value, do clear its upper part.

my $flavour = shift;
my $output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

open STDOUT,">$output" || die "can't open $output: $!"
	if (defined($output));

my $gas=1;	$gas=0 if ($output =~ /\.asm$/);
my $elf=1;	$elf=0 if (!$gas);
my $win64=0;
my $prefix="";
my $decor=".L";

my $masmref=8 + 50727*2**-32;	# 8.00.50727 shipped with VS2005
my $masm=0;
my $PTR=" PTR";

my $nasmref=2.03;
my $nasm=0;

if    ($flavour eq "mingw64")	{ $gas=1; $elf=0; $win64=1;
				  $prefix=`echo __USER_LABEL_PREFIX__ | $ENV{CC} -E -P -`;
				  chomp($prefix);
				}
elsif ($flavour eq "macosx")	{ $gas=1; $elf=0; $prefix="_"; $decor="L\$"; }
elsif ($flavour eq "masm")	{ $gas=0; $elf=0; $masm=$masmref; $win64=1; $decor="\$L\$"; }
elsif ($flavour eq "nasm")	{ $gas=0; $elf=0; $nasm=$nasmref; $win64=1; $decor="\$L\$"; $PTR=""; }
elsif (!$gas)
{   if ($ENV{ASM} =~ m/nasm/ && `nasm -v` =~ m/version ([0-9]+)\.([0-9]+)/i)
    {	$nasm = $1 + $2*0.01; $PTR="";  }
    elsif (`ml64 2>&1` =~ m/Version ([0-9]+)\.([0-9]+)(\.([0-9]+))?/)
    {	$masm = $1 + $2*2**-16 + $4*2**-32;   }
    die "no assembler found on %PATH" if (!($nasm || $masm));
    $win64=1;
    $elf=0;
    $decor="\$L\$";
}

my $current_segment;
my $current_function;
my %globals;

{ package opcode;	# pick up opcodes
    sub re {
	my	$self = shift;	# single instance in enough...
	local	*line = shift;
	undef	$ret;

	if ($line =~ /^([a-z][a-z0-9]*)/i) {
	    $self->{op} = $1;
	    $ret = $self;
	    $line = substr($line,@+[0]); $line =~ s/^\s+//;

	    undef $self->{sz};
	    if ($self->{op} =~ /^(movz)x?([bw]).*/) {	# movz is pain...
		$self->{op} = $1;
		$self->{sz} = $2;
	    } elsif ($self->{op} =~ /call|jmp/) {
		$self->{sz} = "";
	    } elsif ($self->{op} =~ /^p/ && $' !~ /^(ush|op|insrw)/) { # SSEn
		$self->{sz} = "";
	    } elsif ($self->{op} =~ /^v/) { # VEX
		$self->{sz} = "";
	    } elsif ($self->{op} =~ /mov[dq]/ && $line =~ /%xmm/) {
		$self->{sz} = "";
	    } elsif ($self->{op} =~ /([a-z]{3,})([qlwb])$/) {
		$self->{op} = $1;
		$self->{sz} = $2;
	    }
	}
	$ret;
    }
    sub size {
	my $self = shift;
	my $sz   = shift;
	$self->{sz} = $sz if (defined($sz) && !defined($self->{sz}));
	$self->{sz};
    }
    sub out {
	my $self = shift;
	if ($gas) {
	    if ($self->{op} eq "movz") {	# movz is pain...
		sprintf "%s%s%s",$self->{op},$self->{sz},shift;
	    } elsif ($self->{op} =~ /^set/) { 
		"$self->{op}";
	    } elsif ($self->{op} eq "ret") {
		my $epilogue = "";
		if ($win64 && $current_function->{abi} eq "svr4") {
		    $epilogue = "movq	8(%rsp),%rdi\n\t" .
				"movq	16(%rsp),%rsi\n\t";
		}
	    	$epilogue . "retq";
	    } elsif ($self->{op} eq "call" && !$elf && $current_segment eq ".init") {
		".p2align\t3\n\t.quad";
	    } else {
		"$self->{op}$self->{sz}";
	    }
	} else {
	    $self->{op} =~ s/^movz/movzx/;
	    if ($self->{op} eq "ret") {
		$self->{op} = "";
		if ($win64 && $current_function->{abi} eq "svr4") {
		    $self->{op} = "mov	rdi,QWORD${PTR}[8+rsp]\t;WIN64 epilogue\n\t".
				  "mov	rsi,QWORD${PTR}[16+rsp]\n\t";
	    	}
		$self->{op} .= "DB\t0F3h,0C3h\t\t;repret";
	    } elsif ($self->{op} =~ /^(pop|push)f/) {
		$self->{op} .= $self->{sz};
	    } elsif ($self->{op} eq "call" && $current_segment eq ".CRT\$XCU") {
		$self->{op} = "\tDQ";
	    } 
	    $self->{op};
	}
    }
    sub mnemonic {
	my $self=shift;
	my $op=shift;
	$self->{op}=$op if (defined($op));
	$self->{op};
    }
}
{ package const;	# pick up constants, which start with $
    sub re {
	my	$self = shift;	# single instance in enough...
	local	*line = shift;
	undef	$ret;

	if ($line =~ /^\$([^,]+)/) {
	    $self->{value} = $1;
	    $ret = $self;
	    $line = substr($line,@+[0]); $line =~ s/^\s+//;
	}
	$ret;
    }
    sub out {
    	my $self = shift;

	if ($gas) {
	    # Solaris /usr/ccs/bin/as can't handle multiplications
	    # in $self->{value}
	    $self->{value} =~ s/(?<![\w\$\.])(0x?[0-9a-f]+)/oct($1)/egi;
	    $self->{value} =~ s/([0-9]+\s*[\*\/\%]\s*[0-9]+)/eval($1)/eg;
	    sprintf "\$%s",$self->{value};
	} else {
	    $self->{value} =~ s/(0b[0-1]+)/oct($1)/eig;
	    $self->{value} =~ s/0x([0-9a-f]+)/0$1h/ig if ($masm);
	    sprintf "%s",$self->{value};
	}
    }
}
{ package ea;		# pick up effective addresses: expr(%reg,%reg,scale)
    sub re {
	my	$self = shift;	# single instance in enough...
	local	*line = shift;
	undef	$ret;

	# optional * ---vvv--- appears in indirect jmp/call
	if ($line =~ /^(\*?)([^\(,]*)\(([%\w,]+)\)/) {
	    $self->{asterisk} = $1;
	    $self->{label} = $2;
	    ($self->{base},$self->{index},$self->{scale})=split(/,/,$3);
	    $self->{scale} = 1 if (!defined($self->{scale}));
	    $ret = $self;
	    $line = substr($line,@+[0]); $line =~ s/^\s+//;

	    if ($win64 && $self->{label} =~ s/\@GOTPCREL//) {
		die if (opcode->mnemonic() ne "mov");
		opcode->mnemonic("lea");
	    }
	    $self->{base}  =~ s/^%//;
	    $self->{index} =~ s/^%// if (defined($self->{index}));
	}
	$ret;
    }
    sub size {}
    sub out {
    	my $self = shift;
	my $sz = shift;

	$self->{label} =~ s/([_a-z][_a-z0-9]*)/$globals{$1} or $1/gei;
	$self->{label} =~ s/\.L/$decor/g;

	# Silently convert all EAs to 64-bit. This is required for
	# elder GNU assembler and results in more compact code,
	# *but* most importantly AES module depends on this feature!
	$self->{index} =~ s/^[er](.?[0-9xpi])[d]?$/r\1/;
	$self->{base}  =~ s/^[er](.?[0-9xpi])[d]?$/r\1/;

	# Solaris /usr/ccs/bin/as can't handle multiplications
	# in $self->{label}, new gas requires sign extension...
	use integer;
	$self->{label} =~ s/(?<![\w\$\.])(0x?[0-9a-f]+)/oct($1)/egi;
	$self->{label} =~ s/([0-9]+\s*[\*\/\%]\s*[0-9]+)/eval($1)/eg;
	$self->{label} =~ s/([0-9]+)/$1<<32>>32/eg;

	if ($gas) {
	    $self->{label} =~ s/^___imp_/__imp__/   if ($flavour eq "mingw64");

	    if (defined($self->{index})) {
		sprintf "%s%s(%s,%%%s,%d)",$self->{asterisk},
					$self->{label},
					$self->{base}?"%$self->{base}":"",
					$self->{index},$self->{scale};
	    } else {
		sprintf "%s%s(%%%s)",	$self->{asterisk},$self->{label},$self->{base};
	    }
	} else {
	    %szmap = (	b=>"BYTE$PTR", w=>"WORD$PTR", l=>"DWORD$PTR",
	    		q=>"QWORD$PTR",o=>"OWORD$PTR",x=>"XMMWORD$PTR" );

	    $self->{label} =~ s/\./\$/g;
	    $self->{label} =~ s/(?<![\w\$\.])0x([0-9a-f]+)/0$1h/ig;
	    $self->{label} = "($self->{label})" if ($self->{label} =~ /[\*\+\-\/]/);
	    $sz="q" if ($self->{asterisk} || opcode->mnemonic() eq "movq");
	    $sz="l" if (opcode->mnemonic() eq "movd");

	    if (defined($self->{index})) {
		sprintf "%s[%s%s*%d%s]",$szmap{$sz},
					$self->{label}?"$self->{label}+":"",
					$self->{index},$self->{scale},
					$self->{base}?"+$self->{base}":"";
	    } elsif ($self->{base} eq "rip") {
		sprintf "%s[%s]",$szmap{$sz},$self->{label};
	    } else {
		sprintf "%s[%s%s]",$szmap{$sz},
					$self->{label}?"$self->{label}+":"",
					$self->{base};
	    }
	}
    }
}
{ package register;	# pick up registers, which start with %.
    sub re {
	my	$class = shift;	# multiple instances...
	my	$self = {};
	local	*line = shift;
	undef	$ret;

	# optional * ---vvv--- appears in indirect jmp/call
	if ($line =~ /^(\*?)%(\w+)/) {
	    bless $self,$class;
	    $self->{asterisk} = $1;
	    $self->{value} = $2;
	    $ret = $self;
	    $line = substr($line,@+[0]); $line =~ s/^\s+//;
	}
	$ret;
    }
    sub size {
	my	$self = shift;
	undef	$ret;

	if    ($self->{value} =~ /^r[\d]+b$/i)	{ $ret="b"; }
	elsif ($self->{value} =~ /^r[\d]+w$/i)	{ $ret="w"; }
	elsif ($self->{value} =~ /^r[\d]+d$/i)	{ $ret="l"; }
	elsif ($self->{value} =~ /^r[\w]+$/i)	{ $ret="q"; }
	elsif ($self->{value} =~ /^[a-d][hl]$/i){ $ret="b"; }
	elsif ($self->{value} =~ /^[\w]{2}l$/i)	{ $ret="b"; }
	elsif ($self->{value} =~ /^[\w]{2}$/i)	{ $ret="w"; }
	elsif ($self->{value} =~ /^e[a-z]{2}$/i){ $ret="l"; }

	$ret;
    }
    sub out {
    	my $self = shift;
	if ($gas)	{ sprintf "%s%%%s",$self->{asterisk},$self->{value}; }
	else		{ $self->{value}; }
    }
}
{ package label;	# pick up labels, which end with :
    sub re {
	my	$self = shift;	# single instance is enough...
	local	*line = shift;
	undef	$ret;

	if ($line =~ /(^[\.\w]+)\:/) {
	    $self->{value} = $1;
	    $ret = $self;
	    $line = substr($line,@+[0]); $line =~ s/^\s+//;

	    $self->{value} =~ s/^\.L/$decor/;
	}
	$ret;
    }
    sub out {
	my $self = shift;

	if ($gas) {
	    my $func = ($globals{$self->{value}} or $self->{value}) . ":";
	    if ($win64	&&
			$current_function->{name} eq $self->{value} &&
			$current_function->{abi} eq "svr4") {
		$func .= "\n";
		$func .= "	movq	%rdi,8(%rsp)\n";
		$func .= "	movq	%rsi,16(%rsp)\n";
		$func .= "	movq	%rsp,%rax\n";
		$func .= "${decor}SEH_begin_$current_function->{name}:\n";
		my $narg = $current_function->{narg};
		$narg=6 if (!defined($narg));
		$func .= "	movq	%rcx,%rdi\n" if ($narg>0);
		$func .= "	movq	%rdx,%rsi\n" if ($narg>1);
		$func .= "	movq	%r8,%rdx\n"  if ($narg>2);
		$func .= "	movq	%r9,%rcx\n"  if ($narg>3);
		$func .= "	movq	40(%rsp),%r8\n" if ($narg>4);
		$func .= "	movq	48(%rsp),%r9\n" if ($narg>5);
	    }
	    $func;
	} elsif ($self->{value} ne "$current_function->{name}") {
	    $self->{value} .= ":" if ($masm && $ret!~m/^\$/);
	    $self->{value} . ":";
	} elsif ($win64 && $current_function->{abi} eq "svr4") {
	    my $func =	"$current_function->{name}" .
			($nasm ? ":" : "\tPROC $current_function->{scope}") .
			"\n";
	    $func .= "	mov	QWORD${PTR}[8+rsp],rdi\t;WIN64 prologue\n";
	    $func .= "	mov	QWORD${PTR}[16+rsp],rsi\n";
	    $func .= "	mov	rax,rsp\n";
	    $func .= "${decor}SEH_begin_$current_function->{name}:";
	    $func .= ":" if ($masm);
	    $func .= "\n";
	    my $narg = $current_function->{narg};
	    $narg=6 if (!defined($narg));
	    $func .= "	mov	rdi,rcx\n" if ($narg>0);
	    $func .= "	mov	rsi,rdx\n" if ($narg>1);
	    $func .= "	mov	rdx,r8\n"  if ($narg>2);
	    $func .= "	mov	rcx,r9\n"  if ($narg>3);
	    $func .= "	mov	r8,QWORD${PTR}[40+rsp]\n" if ($narg>4);
	    $func .= "	mov	r9,QWORD${PTR}[48+rsp]\n" if ($narg>5);
	    $func .= "\n";
	} else {
	   "$current_function->{name}".
			($nasm ? ":" : "\tPROC $current_function->{scope}");
	}
    }
}
{ package expr;		# pick up expressions
    sub re {
	my	$self = shift;	# single instance is enough...
	local	*line = shift;
	undef	$ret;

	if ($line =~ /(^[^,]+)/) {
	    $self->{value} = $1;
	    $ret = $self;
	    $line = substr($line,@+[0]); $line =~ s/^\s+//;

	    $self->{value} =~ s/\@PLT// if (!$elf);
	    $self->{value} =~ s/([_a-z][_a-z0-9]*)/$globals{$1} or $1/gei;
	    $self->{value} =~ s/\.L/$decor/g;
	}
	$ret;
    }
    sub out {
	my $self = shift;
	if ($nasm && opcode->mnemonic()=~m/^j/) {
	    "NEAR ".$self->{value};
	} else {
	    $self->{value};
	}
    }
}
{ package directive;	# pick up directives, which start with .
    sub re {
	my	$self = shift;	# single instance is enough...
	local	*line = shift;
	undef	$ret;
	my	$dir;
	my	%opcode =	# lea 2f-1f(%rip),%dst; 1: nop; 2:
		(	"%rax"=>0x01058d48,	"%rcx"=>0x010d8d48,
			"%rdx"=>0x01158d48,	"%rbx"=>0x011d8d48,
			"%rsp"=>0x01258d48,	"%rbp"=>0x012d8d48,
			"%rsi"=>0x01358d48,	"%rdi"=>0x013d8d48,
			"%r8" =>0x01058d4c,	"%r9" =>0x010d8d4c,
			"%r10"=>0x01158d4c,	"%r11"=>0x011d8d4c,
			"%r12"=>0x01258d4c,	"%r13"=>0x012d8d4c,
			"%r14"=>0x01358d4c,	"%r15"=>0x013d8d4c	);

	if ($line =~ /^\s*(\.\w+)/) {
	    $dir = $1;
	    $ret = $self;
	    undef $self->{value};
	    $line = substr($line,@+[0]); $line =~ s/^\s+//;

	    SWITCH: for ($dir) {
		/\.picmeup/ && do { if ($line =~ /(%r[\w]+)/i) {
			    		$dir="\t.long";
					$line=sprintf "0x%x,0x90000000",$opcode{$1};
				    }
				    last;
				  };
		/\.global|\.globl|\.extern/
			    && do { $globals{$line} = $prefix . $line;
				    $line = $globals{$line} if ($prefix);
				    last;
				  };
		/\.type/    && do { ($sym,$type,$narg) = split(',',$line);
				    if ($type eq "\@function") {
					undef $current_function;
					$current_function->{name} = $sym;
					$current_function->{abi}  = "svr4";
					$current_function->{narg} = $narg;
					$current_function->{scope} = defined($globals{$sym})?"PUBLIC":"PRIVATE";
				    } elsif ($type eq "\@abi-omnipotent") {
					undef $current_function;
					$current_function->{name} = $sym;
					$current_function->{scope} = defined($globals{$sym})?"PUBLIC":"PRIVATE";
				    }
				    $line =~ s/\@abi\-omnipotent/\@function/;
				    $line =~ s/\@function.*/\@function/;
				    last;
				  };
		/\.asciz/   && do { if ($line =~ /^"(.*)"$/) {
					$dir  = ".byte";
					$line = join(",",unpack("C*",$1),0);
				    }
				    last;
				  };
		/\.rva|\.long|\.quad/
			    && do { $line =~ s/([_a-z][_a-z0-9]*)/$globals{$1} or $1/gei;
				    $line =~ s/\.L/$decor/g;
				    last;
				  };
	    }

	    if ($gas) {
		$self->{value} = $dir . "\t" . $line;

		if ($dir =~ /\.extern/) {
		    $self->{value} = ""; # swallow extern
		} elsif (!$elf && $dir =~ /\.type/) {
		    $self->{value} = "";
		    $self->{value} = ".def\t" . ($globals{$1} or $1) . ";\t" .
				(defined($globals{$1})?".scl 2;":".scl 3;") .
				"\t.type 32;\t.endef"
				if ($win64 && $line =~ /([^,]+),\@function/);
		} elsif (!$elf && $dir =~ /\.size/) {
		    $self->{value} = "";
		    if (defined($current_function)) {
			$self->{value} .= "${decor}SEH_end_$current_function->{name}:"
				if ($win64 && $current_function->{abi} eq "svr4");
			undef $current_function;
		    }
		} elsif (!$elf && $dir =~ /\.align/) {
		    $self->{value} = ".p2align\t" . (log($line)/log(2));
		} elsif ($dir eq ".section") {
		    $current_segment=$line;
		    if (!$elf && $current_segment eq ".rodata") {
			if	($flavour eq "macosx")	{ $self->{value} = ".section\t__DATA,__const"; }
		    }
		    if (!$elf && $current_segment eq ".init") {
			if	($flavour eq "macosx")	{ $self->{value} = ".mod_init_func"; }
			elsif	($flavour eq "mingw64")	{ $self->{value} = ".section\t.ctors"; }
		    }
		} elsif ($dir =~ /\.(text|data)/) {
		    $current_segment=".$1";
		} elsif ($dir =~ /\.hidden/) {
		    if    ($flavour eq "macosx")  { $self->{value} = ".private_extern\t$prefix$line"; }
		    elsif ($flavour eq "mingw64") { $self->{value} = ""; }
		} elsif ($dir =~ /\.comm/) {
		    $self->{value} = "$dir\t$prefix$line";
		    $self->{value} =~ s|,([0-9]+),([0-9]+)$|",$1,".log($2)/log(2)|e if ($flavour eq "macosx");
		}
		$line = "";
		return $self;
	    }

	    # non-gas case or nasm/masm
	    SWITCH: for ($dir) {
		/\.text/    && do { my $v=undef;
				    if ($nasm) {
					$v="section	.text code align=64\n";
				    } else {
					$v="$current_segment\tENDS\n" if ($current_segment);
					$current_segment = ".text\$";
					$v.="$current_segment\tSEGMENT ";
					$v.=$masm>=$masmref ? "ALIGN(64)" : "PAGE";
					$v.=" 'CODE'";
				    }
				    $self->{value} = $v;
				    last;
				  };
		/\.data/    && do { my $v=undef;
				    if ($nasm) {
					$v="section	.data data align=8\n";
				    } else {
					$v="$current_segment\tENDS\n" if ($current_segment);
					$current_segment = "_DATA";
					$v.="$current_segment\tSEGMENT";
				    }
				    $self->{value} = $v;
				    last;
				  };
		/\.section/ && do { my $v=undef;
				    $line =~ s/([^,]*).*/$1/;
				    $line = ".CRT\$XCU" if ($line eq ".init");
				    $line = ".rdata" if ($line eq ".rodata");
				    if ($nasm) {
					$v="section	$line";
					if ($line=~/\.([prx])data/) {
					    $v.=" rdata align=";
					    $v.=$1 eq "p"? 4 : 8;
					} elsif ($line=~/\.CRT\$/i) {
					    $v.=" rdata align=8";
					}
				    } else {
					$v="$current_segment\tENDS\n" if ($current_segment);
					$v.="$line\tSEGMENT";
					if ($line=~/\.([prx])data/) {
					    $v.=" READONLY";
					    if ($masm>=$masmref) {
						    if ($1 eq "r") {
							    $v.=" ALIGN(64)";
						    } elsif ($1 eq "p") {
							    $v.=" ALIGN(4)";
						    } else {
							    $v.=" ALIGN(8)";
						    }
					    }
					} elsif ($line=~/\.CRT\$/i) {
					    $v.=" READONLY ";
					    $v.=$masm>=$masmref ? "ALIGN(8)" : "DWORD";
					}
				    }
				    $current_segment = $line;
				    $self->{value} = $v;
				    last;
				  };
		/\.extern/  && do { $self->{value}  = "EXTERN\t".$line;
				    $self->{value} .= ":NEAR" if ($masm);
				    last;
				  };
		/\.globl|.global/
			    && do { $self->{value}  = $masm?"PUBLIC":"global";
				    $self->{value} .= "\t".$line;
				    last;
				  };
		/\.size/    && do { if (defined($current_function)) {
					undef $self->{value};
					if ($current_function->{abi} eq "svr4") {
					    $self->{value}="${decor}SEH_end_$current_function->{name}:";
					    $self->{value}.=":\n" if($masm);
					}
					$self->{value}.="$current_function->{name}\tENDP" if($masm && $current_function->{name});
					undef $current_function;
				    }
				    last;
				  };
		/\.align/   && do { $self->{value} = "ALIGN\t".$line; last; };
		/\.(value|long|rva|quad)/
			    && do { my $sz  = substr($1,0,1);
				    my @arr = split(/,\s*/,$line);
				    my $last = pop(@arr);
				    my $conv = sub  {	my $var=shift;
							$var=~s/^(0b[0-1]+)/oct($1)/eig;
							$var=~s/^0x([0-9a-f]+)/0$1h/ig if ($masm);
							if ($sz eq "D" && ($current_segment=~/.[px]data/ || $dir eq ".rva"))
							{ $var=~s/([_a-z\$\@][_a-z0-9\$\@]*)/$nasm?"$1 wrt ..imagebase":"imagerel $1"/egi; }
							$var;
						    };  

				    $sz =~ tr/bvlrq/BWDDQ/;
				    $self->{value} = "\tD$sz\t";
				    for (@arr) { $self->{value} .= &$conv($_).","; }
				    $self->{value} .= &$conv($last);
				    last;
				  };
		/\.byte/    && do { my @str=split(/,\s*/,$line);
				    map(s/(0b[0-1]+)/oct($1)/eig,@str);
				    map(s/0x([0-9a-f]+)/0$1h/ig,@str) if ($masm);	
				    while ($#str>15) {
					$self->{value}.="DB\t"
						.join(",",@str[0..15])."\n";
					foreach (0..15) { shift @str; }
				    }
				    $self->{value}.="DB\t"
						.join(",",@str) if (@str);
				    last;
				  };
		/\.comm/    && do { my @str=split(/,\s*/,$line);
				    my $v=undef;
				    if ($nasm) {
					$v.="common	$prefix@str[0] @str[1]";
				    } else {
					$v="$current_segment\tENDS\n" if ($current_segment);
					$current_segment = "_DATA";
					$v.="$current_segment\tSEGMENT\n";
					$v.="COMM	@str[0]:DWORD:".@str[1]/4;
				    }
				    $self->{value} = $v;
				    last;
				  };
	    }
	    $line = "";
	}

	$ret;
    }
    sub out {
	my $self = shift;
	$self->{value};
    }
}

sub rex {
 local *opcode=shift;
 my ($dst,$src,$rex)=@_;

   $rex|=0x04 if($dst>=8);
   $rex|=0x01 if($src>=8);
   push @opcode,($rex|0x40) if ($rex);
}

# older gas and ml64 don't handle SSE>2 instructions
my %regrm = (	"%eax"=>0, "%ecx"=>1, "%edx"=>2, "%ebx"=>3,
		"%esp"=>4, "%ebp"=>5, "%esi"=>6, "%edi"=>7	);

if ($flavour ne "openbsd") {

$movq = sub {	# elderly gas can't handle inter-register movq
  my $arg = shift;
  my @opcode=(0x66);
    if ($arg =~ /%xmm([0-9]+),\s*%r(\w+)/) {
	my ($src,$dst)=($1,$2);
	if ($dst !~ /[0-9]+/)	{ $dst = $regrm{"%e$dst"}; }
	rex(\@opcode,$src,$dst,0x8);
	push @opcode,0x0f,0x7e;
	push @opcode,0xc0|(($src&7)<<3)|($dst&7);	# ModR/M
	@opcode;
    } elsif ($arg =~ /%r(\w+),\s*%xmm([0-9]+)/) {
	my ($src,$dst)=($2,$1);
	if ($dst !~ /[0-9]+/)	{ $dst = $regrm{"%e$dst"}; }
	rex(\@opcode,$src,$dst,0x8);
	push @opcode,0x0f,0x6e;
	push @opcode,0xc0|(($src&7)<<3)|($dst&7);	# ModR/M
	@opcode;
    } else {
	();
    }
};

}

my $pextrd = sub {
    if (shift =~ /\$([0-9]+),\s*%xmm([0-9]+),\s*(%\w+)/) {
      my @opcode=(0x66);
	$imm=$1;
	$src=$2;
	$dst=$3;
	if ($dst =~ /%r([0-9]+)d/)	{ $dst = $1; }
	elsif ($dst =~ /%e/)		{ $dst = $regrm{$dst}; }
	rex(\@opcode,$src,$dst);
	push @opcode,0x0f,0x3a,0x16;
	push @opcode,0xc0|(($src&7)<<3)|($dst&7);	# ModR/M
	push @opcode,$imm;
	@opcode;
    } else {
	();
    }
};

my $pinsrd = sub {
    if (shift =~ /\$([0-9]+),\s*(%\w+),\s*%xmm([0-9]+)/) {
      my @opcode=(0x66);
	$imm=$1;
	$src=$2;
	$dst=$3;
	if ($src =~ /%r([0-9]+)/)	{ $src = $1; }
	elsif ($src =~ /%e/)		{ $src = $regrm{$src}; }
	rex(\@opcode,$dst,$src);
	push @opcode,0x0f,0x3a,0x22;
	push @opcode,0xc0|(($dst&7)<<3)|($src&7);	# ModR/M
	push @opcode,$imm;
	@opcode;
    } else {
	();
    }
};

if ($flavour ne "openbsd") {

$pshufb = sub {
    if (shift =~ /%xmm([0-9]+),\s*%xmm([0-9]+)/) {
      my @opcode=(0x66);
	rex(\@opcode,$2,$1);
	push @opcode,0x0f,0x38,0x00;
	push @opcode,0xc0|($1&7)|(($2&7)<<3);		# ModR/M
	@opcode;
    } else {
	();
    }
};

$palignr = sub {
    if (shift =~ /\$([0-9]+),\s*%xmm([0-9]+),\s*%xmm([0-9]+)/) {
      my @opcode=(0x66);
	rex(\@opcode,$3,$2);
	push @opcode,0x0f,0x3a,0x0f;
	push @opcode,0xc0|($2&7)|(($3&7)<<3);		# ModR/M
	push @opcode,$1;
	@opcode;
    } else {
	();
    }
};

$pclmulqdq = sub {
    if (shift =~ /\$([x0-9a-f]+),\s*%xmm([0-9]+),\s*%xmm([0-9]+)/) {
      my @opcode=(0x66);
	rex(\@opcode,$3,$2);
	push @opcode,0x0f,0x3a,0x44;
	push @opcode,0xc0|($2&7)|(($3&7)<<3);		# ModR/M
	my $c=$1;
	push @opcode,$c=~/^0/?oct($c):$c;
	@opcode;
    } else {
	();
    }
};

}

if ($nasm) {
    print <<___;
default	rel
%define XMMWORD
___
} elsif ($masm) {
    print <<___;
OPTION	DOTNAME
___
}

if ($nasm) {
	print <<___;
\%define _CET_ENDBR
___
} else {
	print <<___;
#if defined(__CET__)
#include <cet.h>
#else
#define _CET_ENDBR
#endif

___
}

print "#include \"x86_arch.h\"\n";

while($line=<>) {

    chomp($line);

    $line =~ s|[#!].*$||;	# get rid of asm-style comments...
    $line =~ s|/\*.*\*/||;	# ... and C-style comments...
    $line =~ s|^\s+||;		# ... and skip white spaces in beginning

    undef $label;
    undef $opcode;
    undef @args;

    if ($label=label->re(\$line))	{ print $label->out(); }

    if (directive->re(\$line)) {
	printf "%s",directive->out();
    } elsif ($opcode=opcode->re(\$line)) {
	my $asm = eval("\$".$opcode->mnemonic());
	undef @bytes;
	
	if ((ref($asm) eq 'CODE') && scalar(@bytes=&$asm($line))) {
	    print $gas?".byte\t":"DB\t",join(',',@bytes),"\n";
	    next;
	}

	ARGUMENT: while (1) {
	my $arg;

	if ($arg=register->re(\$line))	{ opcode->size($arg->size()); }
	elsif ($arg=const->re(\$line))	{ }
	elsif ($arg=ea->re(\$line))	{ }
	elsif ($arg=expr->re(\$line))	{ }
	else				{ last ARGUMENT; }

	push @args,$arg;

	last ARGUMENT if ($line !~ /^,/);

	$line =~ s/^,\s*//;
	} # ARGUMENT:

	if ($#args>=0) {
	    my $insn;
	    my $sz=opcode->size();

	    if ($gas) {
		$insn = $opcode->out($#args>=1?$args[$#args]->size():$sz);
		@args = map($_->out($sz),@args);
		printf "\t%s\t%s",$insn,join(",",@args);
	    } else {
		$insn = $opcode->out();
		foreach (@args) {
		    my $arg = $_->out();
		    # $insn.=$sz compensates for movq, pinsrw, ...
		    if ($arg =~ /^xmm[0-9]+$/) { $insn.=$sz; $sz="x" if(!$sz); last; }
		    if ($arg =~ /^mm[0-9]+$/)  { $insn.=$sz; $sz="q" if(!$sz); last; }
		}
		@args = reverse(@args);
		undef $sz if ($nasm && $opcode->mnemonic() eq "lea");
		printf "\t%s\t%s",$insn,join(",",map($_->out($sz),@args));
	    }
	} else {
	    printf "\t%s",$opcode->out();
	}
    }

    print $line,"\n";
}

print "\n$current_segment\tENDS\n"	if ($current_segment && $masm);
print "END\n"				if ($masm);

close STDOUT;

#################################################
# Cross-reference x86_64 ABI "card"
#
# 		Unix		Win64
# %rax		*		*
# %rbx		-		-
# %rcx		#4		#1
# %rdx		#3		#2
# %rsi		#2		-
# %rdi		#1		-
# %rbp		-		-
# %rsp		-		-
# %r8		#5		#3
# %r9		#6		#4
# %r10		*		*
# %r11		*		*
# %r12		-		-
# %r13		-		-
# %r14		-		-
# %r15		-		-
# 
# (*)	volatile register
# (-)	preserved by callee
# (#)	Nth argument, volatile
#
# In Unix terms top of stack is argument transfer area for arguments
# which could not be accommodated in registers. Or in other words 7th
# [integer] argument resides at 8(%rsp) upon function entry point.
# 128 bytes above %rsp constitute a "red zone" which is not touched
# by signal handlers and can be used as temporal storage without
# allocating a frame.
#
# In Win64 terms N*8 bytes on top of stack is argument transfer area,
# which belongs to/can be overwritten by callee. N is the number of
# arguments passed to callee, *but* not less than 4! This means that
# upon function entry point 5th argument resides at 40(%rsp), as well
# as that 32 bytes from 8(%rsp) can always be used as temporal
# storage [without allocating a frame]. One can actually argue that
# one can assume a "red zone" above stack pointer under Win64 as well.
# Point is that at apparently no occasion Windows kernel would alter
# the area above user stack pointer in true asynchronous manner...
#
# All the above means that if assembler programmer adheres to Unix
# register and stack layout, but disregards the "red zone" existence,
# it's possible to use following prologue and epilogue to "gear" from
# Unix to Win64 ABI in leaf functions with not more than 6 arguments.
#
# omnipotent_function:
# ifdef WIN64
#	movq	%rdi,8(%rsp)
#	movq	%rsi,16(%rsp)
#	movq	%rcx,%rdi	; if 1st argument is actually present
#	movq	%rdx,%rsi	; if 2nd argument is actually ...
#	movq	%r8,%rdx	; if 3rd argument is ...
#	movq	%r9,%rcx	; if 4th argument ...
#	movq	40(%rsp),%r8	; if 5th ...
#	movq	48(%rsp),%r9	; if 6th ...
# endif
#	...
# ifdef WIN64
#	movq	8(%rsp),%rdi
#	movq	16(%rsp),%rsi
# endif
#	ret
#
#################################################
# Win64 SEH, Structured Exception Handling.
#
# Unlike on Unix systems(*) lack of Win64 stack unwinding information
# has undesired side-effect at run-time: if an exception is raised in
# assembler subroutine such as those in question (basically we're
# referring to segmentation violations caused by malformed input
# parameters), the application is briskly terminated without invoking
# any exception handlers, most notably without generating memory dump
# or any user notification whatsoever. This poses a problem. It's
# possible to address it by registering custom language-specific
# handler that would restore processor context to the state at
# subroutine entry point and return "exception is not handled, keep
# unwinding" code. Writing such handler can be a challenge... But it's
# doable, though requires certain coding convention. Consider following
# snippet:
#
# .type	function,@function
# function:
#	movq	%rsp,%rax	# copy rsp to volatile register
#	pushq	%r15		# save non-volatile registers
#	pushq	%rbx
#	pushq	%rbp
#	movq	%rsp,%r11
#	subq	%rdi,%r11	# prepare [variable] stack frame
#	andq	$-64,%r11
#	movq	%rax,0(%r11)	# check for exceptions
#	movq	%r11,%rsp	# allocate [variable] stack frame
#	movq	%rax,0(%rsp)	# save original rsp value
# magic_point:
#	...
#	movq	0(%rsp),%rcx	# pull original rsp value
#	movq	-24(%rcx),%rbp	# restore non-volatile registers
#	movq	-16(%rcx),%rbx
#	movq	-8(%rcx),%r15
#	movq	%rcx,%rsp	# restore original rsp
#	ret
# .size function,.-function
#
# The key is that up to magic_point copy of original rsp value remains
# in chosen volatile register and no non-volatile register, except for
# rsp, is modified. While past magic_point rsp remains constant till
# the very end of the function. In this case custom language-specific
# exception handler would look like this:
#
# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
# {	ULONG64 *rsp = (ULONG64 *)context->Rax;
#	if (context->Rip >= magic_point)
#	{   rsp = ((ULONG64 **)context->Rsp)[0];
#	    context->Rbp = rsp[-3];
#	    context->Rbx = rsp[-2];
#	    context->R15 = rsp[-1];
#	}
#	context->Rsp = (ULONG64)rsp;
#	context->Rdi = rsp[1];
#	context->Rsi = rsp[2];
#
#	memcpy (disp->ContextRecord,context,sizeof(CONTEXT));
#	RtlVirtualUnwind(UNW_FLAG_NHANDLER,disp->ImageBase,
#		dips->ControlPc,disp->FunctionEntry,disp->ContextRecord,
#		&disp->HandlerData,&disp->EstablisherFrame,NULL);
#	return ExceptionContinueSearch;
# }
#
# It's appropriate to implement this handler in assembler, directly in
# function's module. In order to do that one has to know members'
# offsets in CONTEXT and DISPATCHER_CONTEXT structures and some constant
# values. Here they are:
#
#	CONTEXT.Rax				120
#	CONTEXT.Rcx				128
#	CONTEXT.Rdx				136
#	CONTEXT.Rbx				144
#	CONTEXT.Rsp				152
#	CONTEXT.Rbp				160
#	CONTEXT.Rsi				168
#	CONTEXT.Rdi				176
#	CONTEXT.R8				184
#	CONTEXT.R9				192
#	CONTEXT.R10				200
#	CONTEXT.R11				208
#	CONTEXT.R12				216
#	CONTEXT.R13				224
#	CONTEXT.R14				232
#	CONTEXT.R15				240
#	CONTEXT.Rip				248
#	CONTEXT.Xmm6				512
#	sizeof(CONTEXT)				1232
#	DISPATCHER_CONTEXT.ControlPc		0
#	DISPATCHER_CONTEXT.ImageBase		8
#	DISPATCHER_CONTEXT.FunctionEntry	16
#	DISPATCHER_CONTEXT.EstablisherFrame	24
#	DISPATCHER_CONTEXT.TargetIp		32
#	DISPATCHER_CONTEXT.ContextRecord	40
#	DISPATCHER_CONTEXT.LanguageHandler	48
#	DISPATCHER_CONTEXT.HandlerData		56
#	UNW_FLAG_NHANDLER			0
#	ExceptionContinueSearch			1
#
# In order to tie the handler to the function one has to compose
# couple of structures: one for .xdata segment and one for .pdata.
#
# UNWIND_INFO structure for .xdata segment would be
#
# function_unwind_info:
#	.byte	9,0,0,0
#	.rva	handler
#
# This structure designates exception handler for a function with
# zero-length prologue, no stack frame or frame register.
#
# To facilitate composing of .pdata structures, auto-generated "gear"
# prologue copies rsp value to rax and denotes next instruction with
# .LSEH_begin_{function_name} label. This essentially defines the SEH
# styling rule mentioned in the beginning. Position of this label is
# chosen in such manner that possible exceptions raised in the "gear"
# prologue would be accounted to caller and unwound from latter's frame.
# End of function is marked with respective .LSEH_end_{function_name}
# label. To summarize, .pdata segment would contain
#
#	.rva	.LSEH_begin_function
#	.rva	.LSEH_end_function
#	.rva	function_unwind_info
#
# Reference to functon_unwind_info from .xdata segment is the anchor.
# In case you wonder why references are 32-bit .rvas and not 64-bit
# .quads. References put into these two segments are required to be
# *relative* to the base address of the current binary module, a.k.a.
# image base. No Win64 module, be it .exe or .dll, can be larger than
# 2GB and thus such relative references can be and are accommodated in
# 32 bits.
#
# Having reviewed the example function code, one can argue that "movq
# %rsp,%rax" above is redundant. It is not! Keep in mind that on Unix
# rax would contain an undefined value. If this "offends" you, use
# another register and refrain from modifying rax till magic_point is
# reached, i.e. as if it was a non-volatile register. If more registers
# are required prior [variable] frame setup is completed, note that
# nobody says that you can have only one "magic point." You can
# "liberate" non-volatile registers by denoting last stack off-load
# instruction and reflecting it in finer grade unwind logic in handler.
# After all, isn't it why it's called *language-specific* handler...
#
# Attentive reader can notice that exceptions would be mishandled in
# auto-generated "gear" epilogue. Well, exception effectively can't
# occur there, because if memory area used by it was subject to
# segmentation violation, then it would be raised upon call to the
# function (and as already mentioned be accounted to caller, which is
# not a problem). If you're still not comfortable, then define tail
# "magic point" just prior ret instruction and have handler treat it...
#
# (*)	Note that we're talking about run-time, not debug-time. Lack of
#	unwind information makes debugging hard on both Windows and
#	Unix. "Unlike" referes to the fact that on Unix signal handler
#	will always be invoked, core dumped and appropriate exit code
#	returned to parent (for user notification).
