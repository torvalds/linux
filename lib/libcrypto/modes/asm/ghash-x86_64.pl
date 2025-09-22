#!/usr/bin/env perl
#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# March, June 2010
#
# The module implements "4-bit" GCM GHASH function and underlying
# single multiplication operation in GF(2^128). "4-bit" means that
# it uses 256 bytes per-key table [+128 bytes shared table]. GHASH
# function features so called "528B" variant utilizing additional
# 256+16 bytes of per-key storage [+512 bytes shared table].
# Performance results are for this streamed GHASH subroutine and are
# expressed in cycles per processed byte, less is better:
#
#		gcc 3.4.x(*)	assembler
#
# P4		28.6		14.0		+100%
# Opteron	19.3		7.7		+150%
# Core2		17.8		8.1(**)		+120%
#
# (*)	comparison is not completely fair, because C results are
#	for vanilla "256B" implementation, while assembler results
#	are for "528B";-)
# (**)	it's mystery [to me] why Core2 result is not same as for
#	Opteron;

# May 2010
#
# Add PCLMULQDQ version performing at 2.02 cycles per processed byte.
# See ghash-x86.pl for background information and details about coding
# techniques.
#
# Special thanks to David Woodhouse <dwmw2@infradead.org> for
# providing access to a Westmere-based system on behalf of Intel
# Open Source Technology Centre.

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

# common register layout
$nlo="%rax";
$nhi="%rbx";
$Zlo="%r8";
$Zhi="%r9";
$tmp="%r10";
$rem_4bit = "%r11";

$Xi="%rdi";
$Htbl="%rsi";

# per-function register layout
$cnt="%rcx";
$rem="%rdx";

sub LB() { my $r=shift; $r =~ s/%[er]([a-d])x/%\1l/	or
			$r =~ s/%[er]([sd]i)/%\1l/	or
			$r =~ s/%[er](bp)/%\1l/		or
			$r =~ s/%(r[0-9]+)[d]?/%\1b/;   $r; }

sub AUTOLOAD()		# thunk [simplified] 32-bit style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://;
  my $arg = pop;
    $arg = "\$$arg" if ($arg*1 eq $arg);
    $code .= "\t$opcode\t".join(',',$arg,reverse @_)."\n";
}

{ my $N;
  sub loop() {
  my $inp = shift;

	$N++;
$code.=<<___;
	xor	$nlo,$nlo
	xor	$nhi,$nhi
	mov	`&LB("$Zlo")`,`&LB("$nlo")`
	mov	`&LB("$Zlo")`,`&LB("$nhi")`
	shl	\$4,`&LB("$nlo")`
	mov	\$14,$cnt
	mov	8($Htbl,$nlo),$Zlo
	mov	($Htbl,$nlo),$Zhi
	and	\$0xf0,`&LB("$nhi")`
	mov	$Zlo,$rem
	jmp	.Loop$N

.align	16
.Loop$N:
	shr	\$4,$Zlo
	and	\$0xf,$rem
	mov	$Zhi,$tmp
	mov	($inp,$cnt),`&LB("$nlo")`
	shr	\$4,$Zhi
	xor	8($Htbl,$nhi),$Zlo
	shl	\$60,$tmp
	xor	($Htbl,$nhi),$Zhi
	mov	`&LB("$nlo")`,`&LB("$nhi")`
	xor	($rem_4bit,$rem,8),$Zhi
	mov	$Zlo,$rem
	shl	\$4,`&LB("$nlo")`
	xor	$tmp,$Zlo
	dec	$cnt
	js	.Lbreak$N

	shr	\$4,$Zlo
	and	\$0xf,$rem
	mov	$Zhi,$tmp
	shr	\$4,$Zhi
	xor	8($Htbl,$nlo),$Zlo
	shl	\$60,$tmp
	xor	($Htbl,$nlo),$Zhi
	and	\$0xf0,`&LB("$nhi")`
	xor	($rem_4bit,$rem,8),$Zhi
	mov	$Zlo,$rem
	xor	$tmp,$Zlo
	jmp	.Loop$N

.align	16
.Lbreak$N:
	shr	\$4,$Zlo
	and	\$0xf,$rem
	mov	$Zhi,$tmp
	shr	\$4,$Zhi
	xor	8($Htbl,$nlo),$Zlo
	shl	\$60,$tmp
	xor	($Htbl,$nlo),$Zhi
	and	\$0xf0,`&LB("$nhi")`
	xor	($rem_4bit,$rem,8),$Zhi
	mov	$Zlo,$rem
	xor	$tmp,$Zlo

	shr	\$4,$Zlo
	and	\$0xf,$rem
	mov	$Zhi,$tmp
	shr	\$4,$Zhi
	xor	8($Htbl,$nhi),$Zlo
	shl	\$60,$tmp
	xor	($Htbl,$nhi),$Zhi
	xor	$tmp,$Zlo
	xor	($rem_4bit,$rem,8),$Zhi

	bswap	$Zlo
	bswap	$Zhi
___
}}

$code=<<___;
.text

.globl	gcm_gmult_4bit
.type	gcm_gmult_4bit,\@function,2
.align	16
gcm_gmult_4bit:
	_CET_ENDBR
	push	%rbx
	push	%rbp		# %rbp and %r12 are pushed exclusively in
	push	%r12		# order to reuse Win64 exception handler...
.Lgmult_prologue:

	movzb	15($Xi),$Zlo
	lea	.Lrem_4bit(%rip),$rem_4bit
___
	&loop	($Xi);
$code.=<<___;
	mov	$Zlo,8($Xi)
	mov	$Zhi,($Xi)

	mov	16(%rsp),%rbx
	lea	24(%rsp),%rsp
.Lgmult_epilogue:
	ret
.size	gcm_gmult_4bit,.-gcm_gmult_4bit
___

# per-function register layout
$inp="%rdx";
$len="%rcx";
$rem_8bit=$rem_4bit;

$code.=<<___;
.globl	gcm_ghash_4bit
.type	gcm_ghash_4bit,\@function,4
.align	16
gcm_ghash_4bit:
	_CET_ENDBR
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	sub	\$280,%rsp
.Lghash_prologue:
	mov	$inp,%r14		# reassign couple of args
	mov	$len,%r15
___
{ my $inp="%r14";
  my $dat="%edx";
  my $len="%r15";
  my @nhi=("%ebx","%ecx");
  my @rem=("%r12","%r13");
  my $Hshr4="%rbp";

	&sub	($Htbl,-128);		# size optimization
	&lea	($Hshr4,"16+128(%rsp)");
	{ my @lo =($nlo,$nhi);
          my @hi =($Zlo,$Zhi);

	  &xor	($dat,$dat);
	  for ($i=0,$j=-2;$i<18;$i++,$j++) {
	    &mov	("$j(%rsp)",&LB($dat))		if ($i>1);
	    &or		($lo[0],$tmp)			if ($i>1);
	    &mov	(&LB($dat),&LB($lo[1]))		if ($i>0 && $i<17);
	    &shr	($lo[1],4)			if ($i>0 && $i<17);
	    &mov	($tmp,$hi[1])			if ($i>0 && $i<17);
	    &shr	($hi[1],4)			if ($i>0 && $i<17);
	    &mov	("8*$j($Hshr4)",$hi[0])		if ($i>1);
	    &mov	($hi[0],"16*$i+0-128($Htbl)")	if ($i<16);
	    &shl	(&LB($dat),4)			if ($i>0 && $i<17);
	    &mov	("8*$j-128($Hshr4)",$lo[0])	if ($i>1);
	    &mov	($lo[0],"16*$i+8-128($Htbl)")	if ($i<16);
	    &shl	($tmp,60)			if ($i>0 && $i<17);

	    push	(@lo,shift(@lo));
	    push	(@hi,shift(@hi));
	  }
	}
	&add	($Htbl,-128);
	&mov	($Zlo,"8($Xi)");
	&mov	($Zhi,"0($Xi)");
	&add	($len,$inp);		# pointer to the end of data
	&lea	($rem_8bit,".Lrem_8bit(%rip)");
	&jmp	(".Louter_loop");

$code.=".align	16\n.Louter_loop:\n";
	&xor	($Zhi,"($inp)");
	&mov	("%rdx","8($inp)");
	&lea	($inp,"16($inp)");
	&xor	("%rdx",$Zlo);
	&mov	("($Xi)",$Zhi);
	&mov	("8($Xi)","%rdx");
	&shr	("%rdx",32);

	&xor	($nlo,$nlo);
	&rol	($dat,8);
	&mov	(&LB($nlo),&LB($dat));
	&movz	($nhi[0],&LB($dat));
	&shl	(&LB($nlo),4);
	&shr	($nhi[0],4);

	for ($j=11,$i=0;$i<15;$i++) {
	    &rol	($dat,8);
	    &xor	($Zlo,"8($Htbl,$nlo)")			if ($i>0);
	    &xor	($Zhi,"($Htbl,$nlo)")			if ($i>0);
	    &mov	($Zlo,"8($Htbl,$nlo)")			if ($i==0);
	    &mov	($Zhi,"($Htbl,$nlo)")			if ($i==0);

	    &mov	(&LB($nlo),&LB($dat));
	    &xor	($Zlo,$tmp)				if ($i>0);
	    &movzw	($rem[1],"($rem_8bit,$rem[1],2)")	if ($i>0);

	    &movz	($nhi[1],&LB($dat));
	    &shl	(&LB($nlo),4);
	    &movzb	($rem[0],"(%rsp,$nhi[0])");

	    &shr	($nhi[1],4)				if ($i<14);
	    &and	($nhi[1],0xf0)				if ($i==14);
	    &shl	($rem[1],48)				if ($i>0);
	    &xor	($rem[0],$Zlo);

	    &mov	($tmp,$Zhi);
	    &xor	($Zhi,$rem[1])				if ($i>0);
	    &shr	($Zlo,8);

	    &movz	($rem[0],&LB($rem[0]));
	    &mov	($dat,"$j($Xi)")			if (--$j%4==0 && $j>=0);
	    &shr	($Zhi,8);

	    &xor	($Zlo,"-128($Hshr4,$nhi[0],8)");
	    &shl	($tmp,56);
	    &xor	($Zhi,"($Hshr4,$nhi[0],8)");

	    unshift	(@nhi,pop(@nhi));		# "rotate" registers
	    unshift	(@rem,pop(@rem));
	}
	&movzw	($rem[1],"($rem_8bit,$rem[1],2)");
	&xor	($Zlo,"8($Htbl,$nlo)");
	&xor	($Zhi,"($Htbl,$nlo)");

	&shl	($rem[1],48);
	&xor	($Zlo,$tmp);

	&xor	($Zhi,$rem[1]);
	&movz	($rem[0],&LB($Zlo));
	&shr	($Zlo,4);

	&mov	($tmp,$Zhi);
	&shl	(&LB($rem[0]),4);
	&shr	($Zhi,4);

	&xor	($Zlo,"8($Htbl,$nhi[0])");
	&movzw	($rem[0],"($rem_8bit,$rem[0],2)");
	&shl	($tmp,60);

	&xor	($Zhi,"($Htbl,$nhi[0])");
	&xor	($Zlo,$tmp);
	&shl	($rem[0],48);

	&bswap	($Zlo);
	&xor	($Zhi,$rem[0]);

	&bswap	($Zhi);
	&cmp	($inp,$len);
	&jb	(".Louter_loop");
}
$code.=<<___;
	mov	$Zlo,8($Xi)
	mov	$Zhi,($Xi)

	lea	280(%rsp),%rsi
	mov	0(%rsi),%r15
	mov	8(%rsi),%r14
	mov	16(%rsi),%r13
	mov	24(%rsi),%r12
	mov	32(%rsi),%rbp
	mov	40(%rsi),%rbx
	lea	48(%rsi),%rsp
.Lghash_epilogue:
	ret
.size	gcm_ghash_4bit,.-gcm_ghash_4bit
___

######################################################################
# PCLMULQDQ version.

@_4args=$win64?	("%rcx","%rdx","%r8", "%r9") :	# Win64 order
		("%rdi","%rsi","%rdx","%rcx");	# Unix order

($Xi,$Xhi)=("%xmm0","%xmm1");	$Hkey="%xmm2";
($T1,$T2,$T3)=("%xmm3","%xmm4","%xmm5");

sub clmul64x64_T2 {	# minimal register pressure
my ($Xhi,$Xi,$Hkey,$modulo)=@_;

$code.=<<___ if (!defined($modulo));
	movdqa		$Xi,$Xhi		#
	pshufd		\$0b01001110,$Xi,$T1
	pshufd		\$0b01001110,$Hkey,$T2
	pxor		$Xi,$T1			#
	pxor		$Hkey,$T2
___
$code.=<<___;
	pclmulqdq	\$0x00,$Hkey,$Xi	#######
	pclmulqdq	\$0x11,$Hkey,$Xhi	#######
	pclmulqdq	\$0x00,$T2,$T1		#######
	pxor		$Xi,$T1			#
	pxor		$Xhi,$T1		#

	movdqa		$T1,$T2			#
	psrldq		\$8,$T1
	pslldq		\$8,$T2			#
	pxor		$T1,$Xhi
	pxor		$T2,$Xi			#
___
}

sub reduction_alg9 {	# 17/13 times faster than Intel version
my ($Xhi,$Xi) = @_;

$code.=<<___;
	# 1st phase
	movdqa		$Xi,$T1			#
	psllq		\$1,$Xi
	pxor		$T1,$Xi			#
	psllq		\$5,$Xi			#
	pxor		$T1,$Xi			#
	psllq		\$57,$Xi		#
	movdqa		$Xi,$T2			#
	pslldq		\$8,$Xi
	psrldq		\$8,$T2			#	
	pxor		$T1,$Xi
	pxor		$T2,$Xhi		#

	# 2nd phase
	movdqa		$Xi,$T2
	psrlq		\$5,$Xi
	pxor		$T2,$Xi			#
	psrlq		\$1,$Xi			#
	pxor		$T2,$Xi			#
	pxor		$Xhi,$T2
	psrlq		\$1,$Xi			#
	pxor		$T2,$Xi			#
___
}

{ my ($Htbl,$Xip)=@_4args;

$code.=<<___;
.globl	gcm_init_clmul
.type	gcm_init_clmul,\@abi-omnipotent
.align	16
gcm_init_clmul:
	_CET_ENDBR
	movdqu		($Xip),$Hkey
	pshufd		\$0b01001110,$Hkey,$Hkey	# dword swap

	# <<1 twist
	pshufd		\$0b11111111,$Hkey,$T2	# broadcast uppermost dword
	movdqa		$Hkey,$T1
	psllq		\$1,$Hkey
	pxor		$T3,$T3			#
	psrlq		\$63,$T1
	pcmpgtd		$T2,$T3			# broadcast carry bit
	pslldq		\$8,$T1
	por		$T1,$Hkey		# H<<=1

	# magic reduction
	pand		.L0x1c2_polynomial(%rip),$T3
	pxor		$T3,$Hkey		# if(carry) H^=0x1c2_polynomial

	# calculate H^2
	movdqa		$Hkey,$Xi
___
	&clmul64x64_T2	($Xhi,$Xi,$Hkey);
	&reduction_alg9	($Xhi,$Xi);
$code.=<<___;
	movdqu		$Hkey,($Htbl)		# save H
	movdqu		$Xi,16($Htbl)		# save H^2
	ret
.size	gcm_init_clmul,.-gcm_init_clmul
___
}

{ my ($Xip,$Htbl)=@_4args;

$code.=<<___;
.globl	gcm_gmult_clmul
.type	gcm_gmult_clmul,\@abi-omnipotent
.align	16
gcm_gmult_clmul:
	_CET_ENDBR
	movdqu		($Xip),$Xi
	movdqa		.Lbswap_mask(%rip),$T3
	movdqu		($Htbl),$Hkey
	pshufb		$T3,$Xi
___
	&clmul64x64_T2	($Xhi,$Xi,$Hkey);
	&reduction_alg9	($Xhi,$Xi);
$code.=<<___;
	pshufb		$T3,$Xi
	movdqu		$Xi,($Xip)
	ret
.size	gcm_gmult_clmul,.-gcm_gmult_clmul
___
}

{ my ($Xip,$Htbl,$inp,$len)=@_4args;
  my $Xn="%xmm6";
  my $Xhn="%xmm7";
  my $Hkey2="%xmm8";
  my $T1n="%xmm9";
  my $T2n="%xmm10";

$code.=<<___;
.globl	gcm_ghash_clmul
.type	gcm_ghash_clmul,\@abi-omnipotent
.align	16
gcm_ghash_clmul:
	_CET_ENDBR
___
$code.=<<___ if ($win64);
.LSEH_begin_gcm_ghash_clmul:
	# I can't trust assembler to use specific encoding:-(
	.byte	0x48,0x83,0xec,0x58		#sub	\$0x58,%rsp
	.byte	0x0f,0x29,0x34,0x24		#movaps	%xmm6,(%rsp)
	.byte	0x0f,0x29,0x7c,0x24,0x10	#movdqa	%xmm7,0x10(%rsp)
	.byte	0x44,0x0f,0x29,0x44,0x24,0x20	#movaps	%xmm8,0x20(%rsp)
	.byte	0x44,0x0f,0x29,0x4c,0x24,0x30	#movaps	%xmm9,0x30(%rsp)
	.byte	0x44,0x0f,0x29,0x54,0x24,0x40	#movaps	%xmm10,0x40(%rsp)
___
$code.=<<___;
	movdqa		.Lbswap_mask(%rip),$T3

	movdqu		($Xip),$Xi
	movdqu		($Htbl),$Hkey
	pshufb		$T3,$Xi

	sub		\$0x10,$len
	jz		.Lodd_tail

	movdqu		16($Htbl),$Hkey2
	#######
	# Xi+2 =[H*(Ii+1 + Xi+1)] mod P =
	#	[(H*Ii+1) + (H*Xi+1)] mod P =
	#	[(H*Ii+1) + H^2*(Ii+Xi)] mod P
	#
	movdqu		($inp),$T1		# Ii
	movdqu		16($inp),$Xn		# Ii+1
	pshufb		$T3,$T1
	pshufb		$T3,$Xn
	pxor		$T1,$Xi			# Ii+Xi
___
	&clmul64x64_T2	($Xhn,$Xn,$Hkey);	# H*Ii+1
$code.=<<___;
	movdqa		$Xi,$Xhi		#
	pshufd		\$0b01001110,$Xi,$T1
	pshufd		\$0b01001110,$Hkey2,$T2
	pxor		$Xi,$T1			#
	pxor		$Hkey2,$T2

	lea		32($inp),$inp		# i+=2
	sub		\$0x20,$len
	jbe		.Leven_tail

.Lmod_loop:
___
	&clmul64x64_T2	($Xhi,$Xi,$Hkey2,1);	# H^2*(Ii+Xi)
$code.=<<___;
	movdqu		($inp),$T1		# Ii
	pxor		$Xn,$Xi			# (H*Ii+1) + H^2*(Ii+Xi)
	pxor		$Xhn,$Xhi

	movdqu		16($inp),$Xn		# Ii+1
	pshufb		$T3,$T1
	pshufb		$T3,$Xn

	movdqa		$Xn,$Xhn		#
	pshufd		\$0b01001110,$Xn,$T1n
	pshufd		\$0b01001110,$Hkey,$T2n
	pxor		$Xn,$T1n		#
	pxor		$Hkey,$T2n
	 pxor		$T1,$Xhi		# "Ii+Xi", consume early

	  movdqa	$Xi,$T1			# 1st phase
	  psllq		\$1,$Xi
	  pxor		$T1,$Xi			#
	  psllq		\$5,$Xi			#
	  pxor		$T1,$Xi			#
	pclmulqdq	\$0x00,$Hkey,$Xn	#######
	  psllq		\$57,$Xi		#
	  movdqa	$Xi,$T2			#
	  pslldq	\$8,$Xi
	  psrldq	\$8,$T2			#	
	  pxor		$T1,$Xi
	  pxor		$T2,$Xhi		#

	pclmulqdq	\$0x11,$Hkey,$Xhn	#######
	  movdqa	$Xi,$T2			# 2nd phase
	  psrlq		\$5,$Xi
	  pxor		$T2,$Xi			#
	  psrlq		\$1,$Xi			#
	  pxor		$T2,$Xi			#
	  pxor		$Xhi,$T2
	  psrlq		\$1,$Xi			#
	  pxor		$T2,$Xi			#

	pclmulqdq	\$0x00,$T2n,$T1n	#######
	 movdqa		$Xi,$Xhi		#
	 pshufd		\$0b01001110,$Xi,$T1
	 pshufd		\$0b01001110,$Hkey2,$T2
	 pxor		$Xi,$T1			#
	 pxor		$Hkey2,$T2

	pxor		$Xn,$T1n		#
	pxor		$Xhn,$T1n		#
	movdqa		$T1n,$T2n		#
	psrldq		\$8,$T1n
	pslldq		\$8,$T2n		#
	pxor		$T1n,$Xhn
	pxor		$T2n,$Xn		#

	lea		32($inp),$inp
	sub		\$0x20,$len
	ja		.Lmod_loop

.Leven_tail:
___
	&clmul64x64_T2	($Xhi,$Xi,$Hkey2,1);	# H^2*(Ii+Xi)
$code.=<<___;
	pxor		$Xn,$Xi			# (H*Ii+1) + H^2*(Ii+Xi)
	pxor		$Xhn,$Xhi
___
	&reduction_alg9	($Xhi,$Xi);
$code.=<<___;
	test		$len,$len
	jnz		.Ldone

.Lodd_tail:
	movdqu		($inp),$T1		# Ii
	pshufb		$T3,$T1
	pxor		$T1,$Xi			# Ii+Xi
___
	&clmul64x64_T2	($Xhi,$Xi,$Hkey);	# H*(Ii+Xi)
	&reduction_alg9	($Xhi,$Xi);
$code.=<<___;
.Ldone:
	pshufb		$T3,$Xi
	movdqu		$Xi,($Xip)
___
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	movaps	0x10(%rsp),%xmm7
	movaps	0x20(%rsp),%xmm8
	movaps	0x30(%rsp),%xmm9
	movaps	0x40(%rsp),%xmm10
	add	\$0x58,%rsp
___
$code.=<<___;
	ret
.LSEH_end_gcm_ghash_clmul:
.size	gcm_ghash_clmul,.-gcm_ghash_clmul
___
}

$code.=<<___;
.section .rodata
.align	64
.Lbswap_mask:
	.byte	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
.L0x1c2_polynomial:
	.byte	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xc2
.align	64
.type	.Lrem_4bit,\@object
.Lrem_4bit:
	.long	0,`0x0000<<16`,0,`0x1C20<<16`,0,`0x3840<<16`,0,`0x2460<<16`
	.long	0,`0x7080<<16`,0,`0x6CA0<<16`,0,`0x48C0<<16`,0,`0x54E0<<16`
	.long	0,`0xE100<<16`,0,`0xFD20<<16`,0,`0xD940<<16`,0,`0xC560<<16`
	.long	0,`0x9180<<16`,0,`0x8DA0<<16`,0,`0xA9C0<<16`,0,`0xB5E0<<16`
.type	.Lrem_8bit,\@object
.Lrem_8bit:
	.value	0x0000,0x01C2,0x0384,0x0246,0x0708,0x06CA,0x048C,0x054E
	.value	0x0E10,0x0FD2,0x0D94,0x0C56,0x0918,0x08DA,0x0A9C,0x0B5E
	.value	0x1C20,0x1DE2,0x1FA4,0x1E66,0x1B28,0x1AEA,0x18AC,0x196E
	.value	0x1230,0x13F2,0x11B4,0x1076,0x1538,0x14FA,0x16BC,0x177E
	.value	0x3840,0x3982,0x3BC4,0x3A06,0x3F48,0x3E8A,0x3CCC,0x3D0E
	.value	0x3650,0x3792,0x35D4,0x3416,0x3158,0x309A,0x32DC,0x331E
	.value	0x2460,0x25A2,0x27E4,0x2626,0x2368,0x22AA,0x20EC,0x212E
	.value	0x2A70,0x2BB2,0x29F4,0x2836,0x2D78,0x2CBA,0x2EFC,0x2F3E
	.value	0x7080,0x7142,0x7304,0x72C6,0x7788,0x764A,0x740C,0x75CE
	.value	0x7E90,0x7F52,0x7D14,0x7CD6,0x7998,0x785A,0x7A1C,0x7BDE
	.value	0x6CA0,0x6D62,0x6F24,0x6EE6,0x6BA8,0x6A6A,0x682C,0x69EE
	.value	0x62B0,0x6372,0x6134,0x60F6,0x65B8,0x647A,0x663C,0x67FE
	.value	0x48C0,0x4902,0x4B44,0x4A86,0x4FC8,0x4E0A,0x4C4C,0x4D8E
	.value	0x46D0,0x4712,0x4554,0x4496,0x41D8,0x401A,0x425C,0x439E
	.value	0x54E0,0x5522,0x5764,0x56A6,0x53E8,0x522A,0x506C,0x51AE
	.value	0x5AF0,0x5B32,0x5974,0x58B6,0x5DF8,0x5C3A,0x5E7C,0x5FBE
	.value	0xE100,0xE0C2,0xE284,0xE346,0xE608,0xE7CA,0xE58C,0xE44E
	.value	0xEF10,0xEED2,0xEC94,0xED56,0xE818,0xE9DA,0xEB9C,0xEA5E
	.value	0xFD20,0xFCE2,0xFEA4,0xFF66,0xFA28,0xFBEA,0xF9AC,0xF86E
	.value	0xF330,0xF2F2,0xF0B4,0xF176,0xF438,0xF5FA,0xF7BC,0xF67E
	.value	0xD940,0xD882,0xDAC4,0xDB06,0xDE48,0xDF8A,0xDDCC,0xDC0E
	.value	0xD750,0xD692,0xD4D4,0xD516,0xD058,0xD19A,0xD3DC,0xD21E
	.value	0xC560,0xC4A2,0xC6E4,0xC726,0xC268,0xC3AA,0xC1EC,0xC02E
	.value	0xCB70,0xCAB2,0xC8F4,0xC936,0xCC78,0xCDBA,0xCFFC,0xCE3E
	.value	0x9180,0x9042,0x9204,0x93C6,0x9688,0x974A,0x950C,0x94CE
	.value	0x9F90,0x9E52,0x9C14,0x9DD6,0x9898,0x995A,0x9B1C,0x9ADE
	.value	0x8DA0,0x8C62,0x8E24,0x8FE6,0x8AA8,0x8B6A,0x892C,0x88EE
	.value	0x83B0,0x8272,0x8034,0x81F6,0x84B8,0x857A,0x873C,0x86FE
	.value	0xA9C0,0xA802,0xAA44,0xAB86,0xAEC8,0xAF0A,0xAD4C,0xAC8E
	.value	0xA7D0,0xA612,0xA454,0xA596,0xA0D8,0xA11A,0xA35C,0xA29E
	.value	0xB5E0,0xB422,0xB664,0xB7A6,0xB2E8,0xB32A,0xB16C,0xB0AE
	.value	0xBBF0,0xBA32,0xB874,0xB9B6,0xBCF8,0xBD3A,0xBF7C,0xBEBE
.align	64
.text
___

# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind
.type	se_handler,\@abi-omnipotent
.align	16
se_handler:
	_CET_ENDBR
	push	%rsi
	push	%rdi
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	pushfq
	sub	\$64,%rsp

	mov	120($context),%rax	# pull context->Rax
	mov	248($context),%rbx	# pull context->Rip

	mov	8($disp),%rsi		# disp->ImageBase
	mov	56($disp),%r11		# disp->HandlerData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lin_prologue

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lin_prologue

	lea	24(%rax),%rax		# adjust "rsp"

	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
	mov	-24(%rax),%r12
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12

.Lin_prologue:
	mov	8(%rax),%rdi
	mov	16(%rax),%rsi
	mov	%rax,152($context)	# restore context->Rsp
	mov	%rsi,168($context)	# restore context->Rsi
	mov	%rdi,176($context)	# restore context->Rdi

	mov	40($disp),%rdi		# disp->ContextRecord
	mov	$context,%rsi		# context
	mov	\$`1232/8`,%ecx		# sizeof(CONTEXT)
	.long	0xa548f3fc		# cld; rep movsq

	mov	$disp,%rsi
	xor	%rcx,%rcx		# arg1, UNW_FLAG_NHANDLER
	mov	8(%rsi),%rdx		# arg2, disp->ImageBase
	mov	0(%rsi),%r8		# arg3, disp->ControlPc
	mov	16(%rsi),%r9		# arg4, disp->FunctionEntry
	mov	40(%rsi),%r10		# disp->ContextRecord
	lea	56(%rsi),%r11		# &disp->HandlerData
	lea	24(%rsi),%r12		# &disp->EstablisherFrame
	mov	%r10,32(%rsp)		# arg5
	mov	%r11,40(%rsp)		# arg6
	mov	%r12,48(%rsp)		# arg7
	mov	%rcx,56(%rsp)		# arg8, (NULL)
	call	*__imp_RtlVirtualUnwind(%rip)

	mov	\$1,%eax		# ExceptionContinueSearch
	add	\$64,%rsp
	popfq
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%rbp
	pop	%rbx
	pop	%rdi
	pop	%rsi
	ret
.size	se_handler,.-se_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_gcm_gmult_4bit
	.rva	.LSEH_end_gcm_gmult_4bit
	.rva	.LSEH_info_gcm_gmult_4bit

	.rva	.LSEH_begin_gcm_ghash_4bit
	.rva	.LSEH_end_gcm_ghash_4bit
	.rva	.LSEH_info_gcm_ghash_4bit

	.rva	.LSEH_begin_gcm_ghash_clmul
	.rva	.LSEH_end_gcm_ghash_clmul
	.rva	.LSEH_info_gcm_ghash_clmul

.section	.xdata
.align	8
.LSEH_info_gcm_gmult_4bit:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lgmult_prologue,.Lgmult_epilogue	# HandlerData
.LSEH_info_gcm_ghash_4bit:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lghash_prologue,.Lghash_epilogue	# HandlerData
.LSEH_info_gcm_ghash_clmul:
	.byte	0x01,0x1f,0x0b,0x00
	.byte	0x1f,0xa8,0x04,0x00	#movaps 0x40(rsp),xmm10
	.byte	0x19,0x98,0x03,0x00	#movaps 0x30(rsp),xmm9
	.byte	0x13,0x88,0x02,0x00	#movaps 0x20(rsp),xmm8
	.byte	0x0d,0x78,0x01,0x00	#movaps 0x10(rsp),xmm7
	.byte	0x08,0x68,0x00,0x00	#movaps (rsp),xmm6
	.byte	0x04,0xa2,0x00,0x00	#sub	rsp,0x58
___
}

$code =~ s/\`([^\`]*)\`/eval($1)/gem;

print $code;

close STDOUT;
