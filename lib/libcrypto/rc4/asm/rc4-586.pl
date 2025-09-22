#!/usr/bin/env perl

# ====================================================================
# [Re]written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# At some point it became apparent that the original SSLeay RC4
# assembler implementation performs suboptimally on latest IA-32
# microarchitectures. After re-tuning performance has changed as
# following:
#
# Pentium	-10%
# Pentium III	+12%
# AMD		+50%(*)
# P4		+250%(**)
#
# (*)	This number is actually a trade-off:-) It's possible to
#	achieve	+72%, but at the cost of -48% off PIII performance.
#	In other words code performing further 13% faster on AMD
#	would perform almost 2 times slower on Intel PIII...
#	For reference! This code delivers ~80% of rc4-amd64.pl
#	performance on the same Opteron machine.
# (**)	This number requires compressed key schedule set up by
#	RC4_set_key [see commentary below for further details].
#
#					<appro@fy.chalmers.se>

# May 2011
#
# Optimize for Core2 and Westmere [and incidentally Opteron]. Current
# performance in cycles per processed byte (less is better) and
# improvement relative to previous version of this module is:
#
# Pentium	10.2			# original numbers
# Pentium III	7.8(*)
# Intel P4	7.5
#
# Opteron	6.1/+20%		# new MMX numbers
# Core2		5.3/+67%(**)
# Westmere	5.1/+94%(**)
# Sandy Bridge	5.0/+8%
# Atom		12.6/+6%
#
# (*)	PIII can actually deliver 6.6 cycles per byte with MMX code,
#	but this specific code performs poorly on Core2. And vice
#	versa, below MMX/SSE code delivering 5.8/7.1 on Core2 performs
#	poorly on PIII, at 8.0/14.5:-( As PIII is not a "hot" CPU
#	[anymore], I chose to discard PIII-specific code path and opt
#	for original IALU-only code, which is why MMX/SSE code path
#	is guarded by SSE2 bit (see below), not MMX/SSE.
# (**)	Performance vs. block size on Core2 and Westmere had a maximum
#	at ... 64 bytes block size. And it was quite a maximum, 40-60%
#	in comparison to largest 8KB block size. Above improvement
#	coefficients are for the largest block size.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

&asm_init($ARGV[0],"rc4-586.pl");

$xx="eax";
$yy="ebx";
$tx="ecx";
$ty="edx";
$inp="esi";
$out="ebp";
$dat="edi";

sub RC4_loop {
  my $i=shift;
  my $func = ($i==0)?*mov:*or;

	&add	(&LB($yy),&LB($tx));
	&mov	($ty,&DWP(0,$dat,$yy,4));
	&mov	(&DWP(0,$dat,$yy,4),$tx);
	&mov	(&DWP(0,$dat,$xx,4),$ty);
	&add	($ty,$tx);
	&inc	(&LB($xx));
	&and	($ty,0xff);
	&ror	($out,8)	if ($i!=0);
	if ($i<3) {
	  &mov	($tx,&DWP(0,$dat,$xx,4));
	} else {
	  &mov	($tx,&wparam(3));	# reload [re-biased] out
	}
	&$func	($out,&DWP(0,$dat,$ty,4));
}

if ($alt=0) {
  # >20% faster on Atom and Sandy Bridge[!], 8% faster on Opteron,
  # but ~40% slower on Core2 and Westmere... Attempt to add movz
  # brings down Opteron by 25%, Atom and Sandy Bridge by 15%, yet
  # on Core2 with movz it's almost 20% slower than below alternative
  # code... Yes, it's a total mess...
  my @XX=($xx,$out);
  $RC4_loop_mmx = sub {		# SSE actually...
    my $i=shift;
    my $j=$i<=0?0:$i>>1;
    my $mm=$i<=0?"mm0":"mm".($i&1);

	&add	(&LB($yy),&LB($tx));
	&lea	(@XX[1],&DWP(1,@XX[0]));
	&pxor	("mm2","mm0")				if ($i==0);
	&psllq	("mm1",8)				if ($i==0);
	&and	(@XX[1],0xff);
	&pxor	("mm0","mm0")				if ($i<=0);
	&mov	($ty,&DWP(0,$dat,$yy,4));
	&mov	(&DWP(0,$dat,$yy,4),$tx);
	&pxor	("mm1","mm2")				if ($i==0);
	&mov	(&DWP(0,$dat,$XX[0],4),$ty);
	&add	(&LB($ty),&LB($tx));
	&movd	(@XX[0],"mm7")				if ($i==0);
	&mov	($tx,&DWP(0,$dat,@XX[1],4));
	&pxor	("mm1","mm1")				if ($i==1);
	&movq	("mm2",&QWP(0,$inp))			if ($i==1);
	&movq	(&QWP(-8,(@XX[0],$inp)),"mm1")		if ($i==0);
	&pinsrw	($mm,&DWP(0,$dat,$ty,4),$j);

	push	(@XX,shift(@XX))			if ($i>=0);
  }
} else {
  # Using pinsrw here improves performance on Intel CPUs by 2-3%, but
  # brings down AMD by 7%...
  $RC4_loop_mmx = sub {
    my $i=shift;

	&add	(&LB($yy),&LB($tx));
	&psllq	("mm1",8*(($i-1)&7))			if (abs($i)!=1);
	&mov	($ty,&DWP(0,$dat,$yy,4));
	&mov	(&DWP(0,$dat,$yy,4),$tx);
	&mov	(&DWP(0,$dat,$xx,4),$ty);
	&inc	($xx);
	&add	($ty,$tx);
	&movz	($xx,&LB($xx));				# (*)
	&movz	($ty,&LB($ty));				# (*)
	&pxor	("mm2",$i==1?"mm0":"mm1")		if ($i>=0);
	&movq	("mm0",&QWP(0,$inp))			if ($i<=0);
	&movq	(&QWP(-8,($out,$inp)),"mm2")		if ($i==0);
	&mov	($tx,&DWP(0,$dat,$xx,4));
	&movd	($i>0?"mm1":"mm2",&DWP(0,$dat,$ty,4));

	# (*)	This is the key to Core2 and Westmere performance.
	#	Without movz out-of-order execution logic confuses
	#	itself and fails to reorder loads and stores. Problem
	#	appears to be fixed in Sandy Bridge...
  }
}

&external_label("OPENSSL_ia32cap_P");

# void rc4_internal(RC4_KEY *key, size_t len, const unsigned char *inp,
#     unsigned char *out);
&function_begin("rc4_internal");
	&mov	($dat,&wparam(0));	# load key schedule pointer
	&mov	($ty, &wparam(1));	# load len
	&mov	($inp,&wparam(2));	# load inp
	&mov	($out,&wparam(3));	# load out

	&xor	($xx,$xx);		# avoid partial register stalls
	&xor	($yy,$yy);

	&cmp	($ty,0);		# safety net
	&je	(&label("abort"));

	&mov	(&LB($xx),&BP(0,$dat));	# load key->x
	&mov	(&LB($yy),&BP(4,$dat));	# load key->y
	&add	($dat,8);

	&lea	($tx,&DWP(0,$inp,$ty));
	&sub	($out,$inp);		# re-bias out
	&mov	(&wparam(1),$tx);	# save input+len

	&inc	(&LB($xx));

	# detect compressed key schedule...
	&cmp	(&DWP(256,$dat),-1);
	&je	(&label("RC4_CHAR"));

	&mov	($tx,&DWP(0,$dat,$xx,4));

	&and	($ty,-4);		# how many 4-byte chunks?
	&jz	(&label("loop1"));

	&test	($ty,-8);
	&mov	(&wparam(3),$out);	# $out as accumulator in these loops
	&jz	(&label("go4loop4"));

	&picsetup($out);
	&picsymbol($out, "OPENSSL_ia32cap_P", $out);
	# check SSE2 bit [could have been MMX]
	&bt	(&DWP(0,$out),"\$IA32CAP_BIT0_SSE2");
	&jnc	(&label("go4loop4"));

	&mov	($out,&wparam(3))	if (!$alt);
	&movd	("mm7",&wparam(3))	if ($alt);
	&and	($ty,-8);
	&lea	($ty,&DWP(-8,$inp,$ty));
	&mov	(&DWP(-4,$dat),$ty);	# save input+(len/8)*8-8

	&$RC4_loop_mmx(-1);
	&jmp(&label("loop_mmx_enter"));

	&set_label("loop_mmx",16);
		&$RC4_loop_mmx(0);
	&set_label("loop_mmx_enter");
		for 	($i=1;$i<8;$i++) { &$RC4_loop_mmx($i); }
		&mov	($ty,$yy);
		&xor	($yy,$yy);		# this is second key to Core2
		&mov	(&LB($yy),&LB($ty));	# and Westmere performance...
		&cmp	($inp,&DWP(-4,$dat));
		&lea	($inp,&DWP(8,$inp));
	&jb	(&label("loop_mmx"));

    if ($alt) {
	&movd	($out,"mm7");
	&pxor	("mm2","mm0");
	&psllq	("mm1",8);
	&pxor	("mm1","mm2");
	&movq	(&QWP(-8,$out,$inp),"mm1");
    } else {
	&psllq	("mm1",56);
	&pxor	("mm2","mm1");
	&movq	(&QWP(-8,$out,$inp),"mm2");
    }
	&emms	();

	&cmp	($inp,&wparam(1));	# compare to input+len
	&je	(&label("done"));
	&jmp	(&label("loop1"));

&set_label("go4loop4",16);
	&lea	($ty,&DWP(-4,$inp,$ty));
	&mov	(&wparam(2),$ty);	# save input+(len/4)*4-4

	&set_label("loop4");
		for ($i=0;$i<4;$i++) { RC4_loop($i); }
		&ror	($out,8);
		&xor	($out,&DWP(0,$inp));
		&cmp	($inp,&wparam(2));	# compare to input+(len/4)*4-4
		&mov	(&DWP(0,$tx,$inp),$out);# $tx holds re-biased out here
		&lea	($inp,&DWP(4,$inp));
		&mov	($tx,&DWP(0,$dat,$xx,4));
	&jb	(&label("loop4"));

	&cmp	($inp,&wparam(1));	# compare to input+len
	&je	(&label("done"));
	&mov	($out,&wparam(3));	# restore $out

	&set_label("loop1",16);
		&add	(&LB($yy),&LB($tx));
		&mov	($ty,&DWP(0,$dat,$yy,4));
		&mov	(&DWP(0,$dat,$yy,4),$tx);
		&mov	(&DWP(0,$dat,$xx,4),$ty);
		&add	($ty,$tx);
		&inc	(&LB($xx));
		&and	($ty,0xff);
		&mov	($ty,&DWP(0,$dat,$ty,4));
		&xor	(&LB($ty),&BP(0,$inp));
		&lea	($inp,&DWP(1,$inp));
		&mov	($tx,&DWP(0,$dat,$xx,4));
		&cmp	($inp,&wparam(1));	# compare to input+len
		&mov	(&BP(-1,$out,$inp),&LB($ty));
	&jb	(&label("loop1"));

	&jmp	(&label("done"));

# this is essentially Intel P4 specific codepath...
&set_label("RC4_CHAR",16);
	&movz	($tx,&BP(0,$dat,$xx));
	# strangely enough unrolled loop performs over 20% slower...
	&set_label("cloop1");
		&add	(&LB($yy),&LB($tx));
		&movz	($ty,&BP(0,$dat,$yy));
		&mov	(&BP(0,$dat,$yy),&LB($tx));
		&mov	(&BP(0,$dat,$xx),&LB($ty));
		&add	(&LB($ty),&LB($tx));
		&movz	($ty,&BP(0,$dat,$ty));
		&add	(&LB($xx),1);
		&xor	(&LB($ty),&BP(0,$inp));
		&lea	($inp,&DWP(1,$inp));
		&movz	($tx,&BP(0,$dat,$xx));
		&cmp	($inp,&wparam(1));
		&mov	(&BP(-1,$out,$inp),&LB($ty));
	&jb	(&label("cloop1"));

&set_label("done");
	&dec	(&LB($xx));
	&mov	(&DWP(-4,$dat),$yy);		# save key->y
	&mov	(&BP(-8,$dat),&LB($xx));	# save key->x
&set_label("abort");
&function_end("rc4_internal");

########################################################################

$inp="esi";
$out="edi";
$idi="ebp";
$ido="ecx";
$idx="edx";

# void rc4_set_key_internal(RC4_KEY *key,int len,const unsigned char *data);
&function_begin("rc4_set_key_internal");
	&mov	($out,&wparam(0));		# load key
	&mov	($idi,&wparam(1));		# load len
	&mov	($inp,&wparam(2));		# load data

	&picsetup($idx);
	&picsymbol($idx, "OPENSSL_ia32cap_P", $idx);

	&lea	($out,&DWP(2*4,$out));		# &key->data
	&lea	($inp,&DWP(0,$inp,$idi));	# $inp to point at the end
	&neg	($idi);
	&xor	("eax","eax");
	&mov	(&DWP(-4,$out),$idi);		# borrow key->y

	&bt	(&DWP(0,$idx),"\$IA32CAP_BIT0_INTELP4");
	&jc	(&label("c1stloop"));

&set_label("w1stloop",16);
	&mov	(&DWP(0,$out,"eax",4),"eax");	# key->data[i]=i;
	&add	(&LB("eax"),1);			# i++;
	&jnc	(&label("w1stloop"));

	&xor	($ido,$ido);
	&xor	($idx,$idx);

&set_label("w2ndloop",16);
	&mov	("eax",&DWP(0,$out,$ido,4));
	&add	(&LB($idx),&BP(0,$inp,$idi));
	&add	(&LB($idx),&LB("eax"));
	&add	($idi,1);
	&mov	("ebx",&DWP(0,$out,$idx,4));
	&jnz	(&label("wnowrap"));
	  &mov	($idi,&DWP(-4,$out));
	&set_label("wnowrap");
	&mov	(&DWP(0,$out,$idx,4),"eax");
	&mov	(&DWP(0,$out,$ido,4),"ebx");
	&add	(&LB($ido),1);
	&jnc	(&label("w2ndloop"));
&jmp	(&label("exit"));

# Unlike all other x86 [and x86_64] implementations, Intel P4 core
# [including EM64T] was found to perform poorly with above "32-bit" key
# schedule, a.k.a. RC4_INT. Performance improvement for IA-32 hand-coded
# assembler turned out to be 3.5x if re-coded for compressed 8-bit one,
# a.k.a. RC4_CHAR! It's however inappropriate to just switch to 8-bit
# schedule for x86[_64], because non-P4 implementations suffer from
# significant performance losses then, e.g. PIII exhibits >2x
# deterioration, and so does Opteron. In order to assure optimal
# all-round performance, we detect P4 at run-time and set up compressed
# key schedule, which is recognized by RC4 procedure.

&set_label("c1stloop",16);
	&mov	(&BP(0,$out,"eax"),&LB("eax"));	# key->data[i]=i;
	&add	(&LB("eax"),1);			# i++;
	&jnc	(&label("c1stloop"));

	&xor	($ido,$ido);
	&xor	($idx,$idx);
	&xor	("ebx","ebx");

&set_label("c2ndloop",16);
	&mov	(&LB("eax"),&BP(0,$out,$ido));
	&add	(&LB($idx),&BP(0,$inp,$idi));
	&add	(&LB($idx),&LB("eax"));
	&add	($idi,1);
	&mov	(&LB("ebx"),&BP(0,$out,$idx));
	&jnz	(&label("cnowrap"));
	  &mov	($idi,&DWP(-4,$out));
	&set_label("cnowrap");
	&mov	(&BP(0,$out,$idx),&LB("eax"));
	&mov	(&BP(0,$out,$ido),&LB("ebx"));
	&add	(&LB($ido),1);
	&jnc	(&label("c2ndloop"));

	&mov	(&DWP(256,$out),-1);		# mark schedule as compressed

&set_label("exit");
	&xor	("eax","eax");
	&mov	(&DWP(-8,$out),"eax");		# key->x=0;
	&mov	(&DWP(-4,$out),"eax");		# key->y=0;
&function_end("rc4_set_key_internal");

&asm_finish();
