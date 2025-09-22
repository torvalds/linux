#!/usr/bin/env perl
#
# ====================================================================
# Written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# SHA512 block transform for x86. September 2007.
#
# Performance in clock cycles per processed byte (less is better):
#
#		Pentium	PIII	P4	AMD K8	Core2
# gcc		100	75	116	54	66
# icc		97	77	95	55	57
# x86 asm	61	56	82	36	40
# SSE2 asm	-	-	38	24	20
# x86_64 asm(*)	-	-	30	10.0	10.5
#
# (*) x86_64 assembler performance is presented for reference
#     purposes.
#
# IALU code-path is optimized for elder Pentiums. On vanilla Pentium
# performance improvement over compiler generated code reaches ~60%,
# while on PIII - ~35%. On newer µ-archs improvement varies from 15%
# to 50%, but it's less important as they are expected to execute SSE2
# code-path, which is commonly ~2-3x faster [than compiler generated
# code]. SSE2 code-path is as fast as original sha512-sse2.pl, even
# though it does not use 128-bit operations. The latter means that
# SSE2-aware kernel is no longer required to execute the code. Another
# difference is that new code optimizes amount of writes, but at the
# cost of increased data cache "footprint" by 1/2KB.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

&asm_init($ARGV[0],"sha512-586.pl",$ARGV[$#ARGV] eq "386");

$sse2=1;

&external_label("OPENSSL_ia32cap_P") if ($sse2);

$Tlo=&DWP(0,"esp");	$Thi=&DWP(4,"esp");
$Alo=&DWP(8,"esp");	$Ahi=&DWP(8+4,"esp");
$Blo=&DWP(16,"esp");	$Bhi=&DWP(16+4,"esp");
$Clo=&DWP(24,"esp");	$Chi=&DWP(24+4,"esp");
$Dlo=&DWP(32,"esp");	$Dhi=&DWP(32+4,"esp");
$Elo=&DWP(40,"esp");	$Ehi=&DWP(40+4,"esp");
$Flo=&DWP(48,"esp");	$Fhi=&DWP(48+4,"esp");
$Glo=&DWP(56,"esp");	$Ghi=&DWP(56+4,"esp");
$Hlo=&DWP(64,"esp");	$Hhi=&DWP(64+4,"esp");
$K512="ebp";

$Asse2=&QWP(0,"esp");
$Bsse2=&QWP(8,"esp");
$Csse2=&QWP(16,"esp");
$Dsse2=&QWP(24,"esp");
$Esse2=&QWP(32,"esp");
$Fsse2=&QWP(40,"esp");
$Gsse2=&QWP(48,"esp");
$Hsse2=&QWP(56,"esp");

$A="mm0";	# B-D and
$E="mm4";	# F-H are commonly loaded to respectively mm1-mm3 and
		# mm5-mm7, but it's done on on-demand basis...

sub BODY_00_15_sse2 {
    my $prefetch=shift;

	&movq	("mm5",$Fsse2);			# load f
	&movq	("mm6",$Gsse2);			# load g
	&movq	("mm7",$Hsse2);			# load h

	&movq	("mm1",$E);			# %mm1 is sliding right
	&movq	("mm2",$E);			# %mm2 is sliding left
	&psrlq	("mm1",14);
	&movq	($Esse2,$E);			# modulo-scheduled save e
	&psllq	("mm2",23);
	&movq	("mm3","mm1");			# %mm3 is T1
	&psrlq	("mm1",4);
	&pxor	("mm3","mm2");
	&psllq	("mm2",23);
	&pxor	("mm3","mm1");
	&psrlq	("mm1",23);
	&pxor	("mm3","mm2");
	&psllq	("mm2",4);
	&pxor	("mm3","mm1");
	&paddq	("mm7",QWP(0,$K512));		# h+=K512[i]
	&pxor	("mm3","mm2");			# T1=Sigma1_512(e)

	&pxor	("mm5","mm6");			# f^=g
	&movq	("mm1",$Bsse2);			# load b
	&pand	("mm5",$E);			# f&=e
	&movq	("mm2",$Csse2);			# load c
	&pxor	("mm5","mm6");			# f^=g
	&movq	($E,$Dsse2);			# e = load d
	&paddq	("mm3","mm5");			# T1+=Ch(e,f,g)
	&movq	(&QWP(0,"esp"),$A);		# modulo-scheduled save a
	&paddq	("mm3","mm7");			# T1+=h

	&movq	("mm5",$A);			# %mm5 is sliding right
	&movq	("mm6",$A);			# %mm6 is sliding left
	&paddq	("mm3",&QWP(8*9,"esp"));	# T1+=X[0]
	&psrlq	("mm5",28);
	&paddq	($E,"mm3");			# e += T1
	&psllq	("mm6",25);
	&movq	("mm7","mm5");			# %mm7 is T2
	&psrlq	("mm5",6);
	&pxor	("mm7","mm6");
	&psllq	("mm6",5);
	&pxor	("mm7","mm5");
	&psrlq	("mm5",5);
	&pxor	("mm7","mm6");
	&psllq	("mm6",6);
	&pxor	("mm7","mm5");
	&sub	("esp",8);
	&pxor	("mm7","mm6");			# T2=Sigma0_512(a)

	&movq	("mm5",$A);			# %mm5=a
	&por	($A,"mm2");			# a=a|c
	&movq	("mm6",&QWP(8*(9+16-14),"esp"))	if ($prefetch);
	&pand	("mm5","mm2");			# %mm5=a&c
	&pand	($A,"mm1");			# a=(a|c)&b
	&movq	("mm2",&QWP(8*(9+16-1),"esp"))	if ($prefetch);
	&por	("mm5",$A);			# %mm5=(a&c)|((a|c)&b)
	&paddq	("mm7","mm5");			# T2+=Maj(a,b,c)
	&movq	($A,"mm3");			# a=T1

	&mov	(&LB("edx"),&BP(0,$K512));
	&paddq	($A,"mm7");			# a+=T2
	&add	($K512,8);
}

sub BODY_00_15_x86 {
	#define Sigma1(x)	(ROTR((x),14) ^ ROTR((x),18)  ^ ROTR((x),41))
	#	LO		lo>>14^hi<<18 ^ lo>>18^hi<<14 ^ hi>>9^lo<<23
	#	HI		hi>>14^lo<<18 ^ hi>>18^lo<<14 ^ lo>>9^hi<<23
	&mov	("ecx",$Elo);
	&mov	("edx",$Ehi);
	&mov	("esi","ecx");

	&shr	("ecx",9);	# lo>>9
	&mov	("edi","edx");
	&shr	("edx",9);	# hi>>9
	&mov	("ebx","ecx");
	&shl	("esi",14);	# lo<<14
	&mov	("eax","edx");
	&shl	("edi",14);	# hi<<14
	&xor	("ebx","esi");

	&shr	("ecx",14-9);	# lo>>14
	&xor	("eax","edi");
	&shr	("edx",14-9);	# hi>>14
	&xor	("eax","ecx");
	&shl	("esi",18-14);	# lo<<18
	&xor	("ebx","edx");
	&shl	("edi",18-14);	# hi<<18
	&xor	("ebx","esi");

	&shr	("ecx",18-14);	# lo>>18
	&xor	("eax","edi");
	&shr	("edx",18-14);	# hi>>18
	&xor	("eax","ecx");
	&shl	("esi",23-18);	# lo<<23
	&xor	("ebx","edx");
	&shl	("edi",23-18);	# hi<<23
	&xor	("eax","esi");
	&xor	("ebx","edi");			# T1 = Sigma1(e)

	&mov	("ecx",$Flo);
	&mov	("edx",$Fhi);
	&mov	("esi",$Glo);
	&mov	("edi",$Ghi);
	 &add	("eax",$Hlo);
	 &adc	("ebx",$Hhi);			# T1 += h
	&xor	("ecx","esi");
	&xor	("edx","edi");
	&and	("ecx",$Elo);
	&and	("edx",$Ehi);
	 &add	("eax",&DWP(8*(9+15)+0,"esp"));
	 &adc	("ebx",&DWP(8*(9+15)+4,"esp"));	# T1 += X[0]
	&xor	("ecx","esi");
	&xor	("edx","edi");			# Ch(e,f,g) = (f^g)&e)^g

	&mov	("esi",&DWP(0,$K512));
	&mov	("edi",&DWP(4,$K512));		# K[i]
	&add	("eax","ecx");
	&adc	("ebx","edx");			# T1 += Ch(e,f,g)
	&mov	("ecx",$Dlo);
	&mov	("edx",$Dhi);
	&add	("eax","esi");
	&adc	("ebx","edi");			# T1 += K[i]
	&mov	($Tlo,"eax");
	&mov	($Thi,"ebx");			# put T1 away
	&add	("eax","ecx");
	&adc	("ebx","edx");			# d += T1

	#define Sigma0(x)	(ROTR((x),28) ^ ROTR((x),34) ^ ROTR((x),39))
	#	LO		lo>>28^hi<<4  ^ hi>>2^lo<<30 ^ hi>>7^lo<<25
	#	HI		hi>>28^lo<<4  ^ lo>>2^hi<<30 ^ lo>>7^hi<<25
	&mov	("ecx",$Alo);
	&mov	("edx",$Ahi);
	&mov	($Dlo,"eax");
	&mov	($Dhi,"ebx");
	&mov	("esi","ecx");

	&shr	("ecx",2);	# lo>>2
	&mov	("edi","edx");
	&shr	("edx",2);	# hi>>2
	&mov	("ebx","ecx");
	&shl	("esi",4);	# lo<<4
	&mov	("eax","edx");
	&shl	("edi",4);	# hi<<4
	&xor	("ebx","esi");

	&shr	("ecx",7-2);	# lo>>7
	&xor	("eax","edi");
	&shr	("edx",7-2);	# hi>>7
	&xor	("ebx","ecx");
	&shl	("esi",25-4);	# lo<<25
	&xor	("eax","edx");
	&shl	("edi",25-4);	# hi<<25
	&xor	("eax","esi");

	&shr	("ecx",28-7);	# lo>>28
	&xor	("ebx","edi");
	&shr	("edx",28-7);	# hi>>28
	&xor	("eax","ecx");
	&shl	("esi",30-25);	# lo<<30
	&xor	("ebx","edx");
	&shl	("edi",30-25);	# hi<<30
	&xor	("eax","esi");
	&xor	("ebx","edi");			# Sigma0(a)

	&mov	("ecx",$Alo);
	&mov	("edx",$Ahi);
	&mov	("esi",$Blo);
	&mov	("edi",$Bhi);
	&add	("eax",$Tlo);
	&adc	("ebx",$Thi);			# T1 = Sigma0(a)+T1
	&or	("ecx","esi");
	&or	("edx","edi");
	&and	("ecx",$Clo);
	&and	("edx",$Chi);
	&and	("esi",$Alo);
	&and	("edi",$Ahi);
	&or	("ecx","esi");
	&or	("edx","edi");			# Maj(a,b,c) = ((a|b)&c)|(a&b)

	&add	("eax","ecx");
	&adc	("ebx","edx");			# T1 += Maj(a,b,c)
	&mov	($Tlo,"eax");
	&mov	($Thi,"ebx");

	&mov	(&LB("edx"),&BP(0,$K512));	# pre-fetch LSB of *K
	&sub	("esp",8);
	&lea	($K512,&DWP(8,$K512));		# K++
}


&static_label("K512");
&function_begin("sha512_block_data_order");
	&mov	("esi",wparam(0));	# ctx
	&mov	("edi",wparam(1));	# inp
	&mov	("eax",wparam(2));	# num
	&mov	("ebx","esp");		# saved sp

	&picsetup($K512);
if ($sse2) {
	&picsymbol("edx", "OPENSSL_ia32cap_P", $K512);
}
	&picsymbol($K512, &label("K512"), $K512);

	&sub	("esp",16);
	&and	("esp",-64);

	&shl	("eax",7);
	&add	("eax","edi");
	&mov	(&DWP(0,"esp"),"esi");	# ctx
	&mov	(&DWP(4,"esp"),"edi");	# inp
	&mov	(&DWP(8,"esp"),"eax");	# inp+num*128
	&mov	(&DWP(12,"esp"),"ebx");	# saved sp

if ($sse2) {
	&bt	(&DWP(0,"edx"),"\$IA32CAP_BIT0_SSE2");
	&jnc	(&label("loop_x86"));

	# load ctx->h[0-7]
	&movq	($A,&QWP(0,"esi"));
	&movq	("mm1",&QWP(8,"esi"));
	&movq	("mm2",&QWP(16,"esi"));
	&movq	("mm3",&QWP(24,"esi"));
	&movq	($E,&QWP(32,"esi"));
	&movq	("mm5",&QWP(40,"esi"));
	&movq	("mm6",&QWP(48,"esi"));
	&movq	("mm7",&QWP(56,"esi"));
	&sub	("esp",8*10);

&set_label("loop_sse2",16);
	# &movq	($Asse2,$A);
	&movq	($Bsse2,"mm1");
	&movq	($Csse2,"mm2");
	&movq	($Dsse2,"mm3");
	# &movq	($Esse2,$E);
	&movq	($Fsse2,"mm5");
	&movq	($Gsse2,"mm6");
	&movq	($Hsse2,"mm7");

	&mov	("ecx",&DWP(0,"edi"));
	&mov	("edx",&DWP(4,"edi"));
	&add	("edi",8);
	&bswap	("ecx");
	&bswap	("edx");
	&mov	(&DWP(8*9+4,"esp"),"ecx");
	&mov	(&DWP(8*9+0,"esp"),"edx");

&set_label("00_14_sse2",16);
	&mov	("eax",&DWP(0,"edi"));
	&mov	("ebx",&DWP(4,"edi"));
	&add	("edi",8);
	&bswap	("eax");
	&bswap	("ebx");
	&mov	(&DWP(8*8+4,"esp"),"eax");
	&mov	(&DWP(8*8+0,"esp"),"ebx");

	&BODY_00_15_sse2();

	&cmp	(&LB("edx"),0x35);
	&jne	(&label("00_14_sse2"));

	&BODY_00_15_sse2(1);

&set_label("16_79_sse2",16);
	#&movq	("mm2",&QWP(8*(9+16-1),"esp"));	#prefetched in BODY_00_15 
	#&movq	("mm6",&QWP(8*(9+16-14),"esp"));
	&movq	("mm1","mm2");

	&psrlq	("mm2",1);
	&movq	("mm7","mm6");
	&psrlq	("mm6",6);
	&movq	("mm3","mm2");

	&psrlq	("mm2",7-1);
	&movq	("mm5","mm6");
	&psrlq	("mm6",19-6);
	&pxor	("mm3","mm2");

	&psrlq	("mm2",8-7);
	&pxor	("mm5","mm6");
	&psrlq	("mm6",61-19);
	&pxor	("mm3","mm2");

	&movq	("mm2",&QWP(8*(9+16),"esp"));

	&psllq	("mm1",56);
	&pxor	("mm5","mm6");
	&psllq	("mm7",3);
	&pxor	("mm3","mm1");

	&paddq	("mm2",&QWP(8*(9+16-9),"esp"));

	&psllq	("mm1",63-56);
	&pxor	("mm5","mm7");
	&psllq	("mm7",45-3);
	&pxor	("mm3","mm1");
	&pxor	("mm5","mm7");

	&paddq	("mm3","mm5");
	&paddq	("mm3","mm2");
	&movq	(&QWP(8*9,"esp"),"mm3");

	&BODY_00_15_sse2(1);

	&cmp	(&LB("edx"),0x17);
	&jne	(&label("16_79_sse2"));

	# &movq	($A,$Asse2);
	&movq	("mm1",$Bsse2);
	&movq	("mm2",$Csse2);
	&movq	("mm3",$Dsse2);
	# &movq	($E,$Esse2);
	&movq	("mm5",$Fsse2);
	&movq	("mm6",$Gsse2);
	&movq	("mm7",$Hsse2);

	&paddq	($A,&QWP(0,"esi"));
	&paddq	("mm1",&QWP(8,"esi"));
	&paddq	("mm2",&QWP(16,"esi"));
	&paddq	("mm3",&QWP(24,"esi"));
	&paddq	($E,&QWP(32,"esi"));
	&paddq	("mm5",&QWP(40,"esi"));
	&paddq	("mm6",&QWP(48,"esi"));
	&paddq	("mm7",&QWP(56,"esi"));

	&movq	(&QWP(0,"esi"),$A);
	&movq	(&QWP(8,"esi"),"mm1");
	&movq	(&QWP(16,"esi"),"mm2");
	&movq	(&QWP(24,"esi"),"mm3");
	&movq	(&QWP(32,"esi"),$E);
	&movq	(&QWP(40,"esi"),"mm5");
	&movq	(&QWP(48,"esi"),"mm6");
	&movq	(&QWP(56,"esi"),"mm7");

	&add	("esp",8*80);			# destroy frame
	&sub	($K512,8*80);			# rewind K

	&cmp	("edi",&DWP(8*10+8,"esp"));	# are we done yet?
	&jb	(&label("loop_sse2"));

	&emms	();
	&mov	("esp",&DWP(8*10+12,"esp"));	# restore sp
&function_end_A();
}
&set_label("loop_x86",16);
    # copy input block to stack reversing byte and qword order
    for ($i=0;$i<8;$i++) {
	&mov	("eax",&DWP($i*16+0,"edi"));
	&mov	("ebx",&DWP($i*16+4,"edi"));
	&mov	("ecx",&DWP($i*16+8,"edi"));
	&mov	("edx",&DWP($i*16+12,"edi"));
	&bswap	("eax");
	&bswap	("ebx");
	&bswap	("ecx");
	&bswap	("edx");
	&push	("eax");
	&push	("ebx");
	&push	("ecx");
	&push	("edx");
    }
	&add	("edi",128);
	&sub	("esp",9*8);		# place for T,A,B,C,D,E,F,G,H
	&mov	(&DWP(8*(9+16)+4,"esp"),"edi");

	# copy ctx->h[0-7] to A,B,C,D,E,F,G,H on stack
	&lea	("edi",&DWP(8,"esp"));
	&mov	("ecx",16);
	&data_word(0xA5F3F689);		# rep movsd

&set_label("00_15_x86",16);
	&BODY_00_15_x86();

	&cmp	(&LB("edx"),0x94);
	&jne	(&label("00_15_x86"));

&set_label("16_79_x86",16);
	#define sigma0(x)	(ROTR((x),1)  ^ ROTR((x),8)  ^ ((x)>>7))
	#	LO		lo>>1^hi<<31  ^ lo>>8^hi<<24 ^ lo>>7^hi<<25
	#	HI		hi>>1^lo<<31  ^ hi>>8^lo<<24 ^ hi>>7
	&mov	("ecx",&DWP(8*(9+15+16-1)+0,"esp"));
	&mov	("edx",&DWP(8*(9+15+16-1)+4,"esp"));
	&mov	("esi","ecx");

	&shr	("ecx",1);	# lo>>1
	&mov	("edi","edx");
	&shr	("edx",1);	# hi>>1
	&mov	("eax","ecx");
	&shl	("esi",24);	# lo<<24
	&mov	("ebx","edx");
	&shl	("edi",24);	# hi<<24
	&xor	("ebx","esi");

	&shr	("ecx",7-1);	# lo>>7
	&xor	("eax","edi");
	&shr	("edx",7-1);	# hi>>7
	&xor	("eax","ecx");
	&shl	("esi",31-24);	# lo<<31
	&xor	("ebx","edx");
	&shl	("edi",25-24);	# hi<<25
	&xor	("ebx","esi");

	&shr	("ecx",8-7);	# lo>>8
	&xor	("eax","edi");
	&shr	("edx",8-7);	# hi>>8
	&xor	("eax","ecx");
	&shl	("edi",31-25);	# hi<<31
	&xor	("ebx","edx");
	&xor	("eax","edi");			# T1 = sigma0(X[-15])

	&mov	(&DWP(0,"esp"),"eax");
	&mov	(&DWP(4,"esp"),"ebx");		# put T1 away

	#define sigma1(x)	(ROTR((x),19) ^ ROTR((x),61) ^ ((x)>>6))
	#	LO		lo>>19^hi<<13 ^ hi>>29^lo<<3 ^ lo>>6^hi<<26
	#	HI		hi>>19^lo<<13 ^ lo>>29^hi<<3 ^ hi>>6
	&mov	("ecx",&DWP(8*(9+15+16-14)+0,"esp"));
	&mov	("edx",&DWP(8*(9+15+16-14)+4,"esp"));
	&mov	("esi","ecx");

	&shr	("ecx",6);	# lo>>6
	&mov	("edi","edx");
	&shr	("edx",6);	# hi>>6
	&mov	("eax","ecx");
	&shl	("esi",3);	# lo<<3
	&mov	("ebx","edx");
	&shl	("edi",3);	# hi<<3
	&xor	("eax","esi");

	&shr	("ecx",19-6);	# lo>>19
	&xor	("ebx","edi");
	&shr	("edx",19-6);	# hi>>19
	&xor	("eax","ecx");
	&shl	("esi",13-3);	# lo<<13
	&xor	("ebx","edx");
	&shl	("edi",13-3);	# hi<<13
	&xor	("ebx","esi");

	&shr	("ecx",29-19);	# lo>>29
	&xor	("eax","edi");
	&shr	("edx",29-19);	# hi>>29
	&xor	("ebx","ecx");
	&shl	("edi",26-13);	# hi<<26
	&xor	("eax","edx");
	&xor	("eax","edi");			# sigma1(X[-2])

	&mov	("ecx",&DWP(8*(9+15+16)+0,"esp"));
	&mov	("edx",&DWP(8*(9+15+16)+4,"esp"));
	&add	("eax",&DWP(0,"esp"));
	&adc	("ebx",&DWP(4,"esp"));		# T1 = sigma1(X[-2])+T1
	&mov	("esi",&DWP(8*(9+15+16-9)+0,"esp"));
	&mov	("edi",&DWP(8*(9+15+16-9)+4,"esp"));
	&add	("eax","ecx");
	&adc	("ebx","edx");			# T1 += X[-16]
	&add	("eax","esi");
	&adc	("ebx","edi");			# T1 += X[-7]
	&mov	(&DWP(8*(9+15)+0,"esp"),"eax");
	&mov	(&DWP(8*(9+15)+4,"esp"),"ebx");	# save X[0]

	&BODY_00_15_x86();

	&cmp	(&LB("edx"),0x17);
	&jne	(&label("16_79_x86"));

	&mov	("esi",&DWP(8*(9+16+80)+0,"esp"));# ctx
	&mov	("edi",&DWP(8*(9+16+80)+4,"esp"));# inp
    for($i=0;$i<4;$i++) {
	&mov	("eax",&DWP($i*16+0,"esi"));
	&mov	("ebx",&DWP($i*16+4,"esi"));
	&mov	("ecx",&DWP($i*16+8,"esi"));
	&mov	("edx",&DWP($i*16+12,"esi"));
	&add	("eax",&DWP(8+($i*16)+0,"esp"));
	&adc	("ebx",&DWP(8+($i*16)+4,"esp"));
	&mov	(&DWP($i*16+0,"esi"),"eax");
	&mov	(&DWP($i*16+4,"esi"),"ebx");
	&add	("ecx",&DWP(8+($i*16)+8,"esp"));
	&adc	("edx",&DWP(8+($i*16)+12,"esp"));
	&mov	(&DWP($i*16+8,"esi"),"ecx");
	&mov	(&DWP($i*16+12,"esi"),"edx");
    }
	&add	("esp",8*(9+16+80));		# destroy frame
	&sub	($K512,8*80);			# rewind K

	&cmp	("edi",&DWP(8,"esp"));		# are we done yet?
	&jb	(&label("loop_x86"));

	&mov	("esp",&DWP(12,"esp"));		# restore sp
&function_end_A();
&function_end_B("sha512_block_data_order");

	&rodataseg();
&set_label("K512",64);
	&data_word(0xd728ae22,0x428a2f98);	# u64
	&data_word(0x23ef65cd,0x71374491);	# u64
	&data_word(0xec4d3b2f,0xb5c0fbcf);	# u64
	&data_word(0x8189dbbc,0xe9b5dba5);	# u64
	&data_word(0xf348b538,0x3956c25b);	# u64
	&data_word(0xb605d019,0x59f111f1);	# u64
	&data_word(0xaf194f9b,0x923f82a4);	# u64
	&data_word(0xda6d8118,0xab1c5ed5);	# u64
	&data_word(0xa3030242,0xd807aa98);	# u64
	&data_word(0x45706fbe,0x12835b01);	# u64
	&data_word(0x4ee4b28c,0x243185be);	# u64
	&data_word(0xd5ffb4e2,0x550c7dc3);	# u64
	&data_word(0xf27b896f,0x72be5d74);	# u64
	&data_word(0x3b1696b1,0x80deb1fe);	# u64
	&data_word(0x25c71235,0x9bdc06a7);	# u64
	&data_word(0xcf692694,0xc19bf174);	# u64
	&data_word(0x9ef14ad2,0xe49b69c1);	# u64
	&data_word(0x384f25e3,0xefbe4786);	# u64
	&data_word(0x8b8cd5b5,0x0fc19dc6);	# u64
	&data_word(0x77ac9c65,0x240ca1cc);	# u64
	&data_word(0x592b0275,0x2de92c6f);	# u64
	&data_word(0x6ea6e483,0x4a7484aa);	# u64
	&data_word(0xbd41fbd4,0x5cb0a9dc);	# u64
	&data_word(0x831153b5,0x76f988da);	# u64
	&data_word(0xee66dfab,0x983e5152);	# u64
	&data_word(0x2db43210,0xa831c66d);	# u64
	&data_word(0x98fb213f,0xb00327c8);	# u64
	&data_word(0xbeef0ee4,0xbf597fc7);	# u64
	&data_word(0x3da88fc2,0xc6e00bf3);	# u64
	&data_word(0x930aa725,0xd5a79147);	# u64
	&data_word(0xe003826f,0x06ca6351);	# u64
	&data_word(0x0a0e6e70,0x14292967);	# u64
	&data_word(0x46d22ffc,0x27b70a85);	# u64
	&data_word(0x5c26c926,0x2e1b2138);	# u64
	&data_word(0x5ac42aed,0x4d2c6dfc);	# u64
	&data_word(0x9d95b3df,0x53380d13);	# u64
	&data_word(0x8baf63de,0x650a7354);	# u64
	&data_word(0x3c77b2a8,0x766a0abb);	# u64
	&data_word(0x47edaee6,0x81c2c92e);	# u64
	&data_word(0x1482353b,0x92722c85);	# u64
	&data_word(0x4cf10364,0xa2bfe8a1);	# u64
	&data_word(0xbc423001,0xa81a664b);	# u64
	&data_word(0xd0f89791,0xc24b8b70);	# u64
	&data_word(0x0654be30,0xc76c51a3);	# u64
	&data_word(0xd6ef5218,0xd192e819);	# u64
	&data_word(0x5565a910,0xd6990624);	# u64
	&data_word(0x5771202a,0xf40e3585);	# u64
	&data_word(0x32bbd1b8,0x106aa070);	# u64
	&data_word(0xb8d2d0c8,0x19a4c116);	# u64
	&data_word(0x5141ab53,0x1e376c08);	# u64
	&data_word(0xdf8eeb99,0x2748774c);	# u64
	&data_word(0xe19b48a8,0x34b0bcb5);	# u64
	&data_word(0xc5c95a63,0x391c0cb3);	# u64
	&data_word(0xe3418acb,0x4ed8aa4a);	# u64
	&data_word(0x7763e373,0x5b9cca4f);	# u64
	&data_word(0xd6b2b8a3,0x682e6ff3);	# u64
	&data_word(0x5defb2fc,0x748f82ee);	# u64
	&data_word(0x43172f60,0x78a5636f);	# u64
	&data_word(0xa1f0ab72,0x84c87814);	# u64
	&data_word(0x1a6439ec,0x8cc70208);	# u64
	&data_word(0x23631e28,0x90befffa);	# u64
	&data_word(0xde82bde9,0xa4506ceb);	# u64
	&data_word(0xb2c67915,0xbef9a3f7);	# u64
	&data_word(0xe372532b,0xc67178f2);	# u64
	&data_word(0xea26619c,0xca273ece);	# u64
	&data_word(0x21c0c207,0xd186b8c7);	# u64
	&data_word(0xcde0eb1e,0xeada7dd6);	# u64
	&data_word(0xee6ed178,0xf57d4f7f);	# u64
	&data_word(0x72176fba,0x06f067aa);	# u64
	&data_word(0xa2c898a6,0x0a637dc5);	# u64
	&data_word(0xbef90dae,0x113f9804);	# u64
	&data_word(0x131c471b,0x1b710b35);	# u64
	&data_word(0x23047d84,0x28db77f5);	# u64
	&data_word(0x40c72493,0x32caab7b);	# u64
	&data_word(0x15c9bebc,0x3c9ebe0a);	# u64
	&data_word(0x9c100d4c,0x431d67c4);	# u64
	&data_word(0xcb3e42b6,0x4cc5d4be);	# u64
	&data_word(0xfc657e2a,0x597f299c);	# u64
	&data_word(0x3ad6faec,0x5fcb6fab);	# u64
	&data_word(0x4a475817,0x6c44198c);	# u64
	&previous();

&asm_finish();
