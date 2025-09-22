#!/usr/bin/env perl

# ====================================================================
# Written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# AES for PA-RISC.
#
# June 2009.
#
# The module is mechanical transliteration of aes-sparcv9.pl, but with
# a twist: S-boxes are compressed even further down to 1K+256B. On
# PA-7100LC performance is ~40% better than gcc 3.2 generated code and
# is about 33 cycles per byte processed with 128-bit key. Newer CPUs
# perform at 16 cycles per byte. It's not faster than code generated
# by vendor compiler, but recall that it has compressed S-boxes, which
# requires extra processing.
#
# Special thanks to polarhome.com for providing HP-UX account.

$flavour = shift;
$output = shift;
open STDOUT,">$output";

if ($flavour =~ /64/) {
	$LEVEL		="2.0W";
	$SIZE_T		=8;
	$FRAME_MARKER	=80;
	$SAVED_RP	=16;
	$PUSH		="std";
	$PUSHMA		="std,ma";
	$POP		="ldd";
	$POPMB		="ldd,mb";
} else {
	$LEVEL		="1.0";
	$SIZE_T		=4;
	$FRAME_MARKER	=48;
	$SAVED_RP	=20;
	$PUSH		="stw";
	$PUSHMA		="stwm";
	$POP		="ldw";
	$POPMB		="ldwm";
}

$FRAME=16*$SIZE_T+$FRAME_MARKER;# 16 saved regs + frame marker
				#                 [+ argument transfer]
$inp="%r26";	# arg0
$out="%r25";	# arg1
$key="%r24";	# arg2

($s0,$s1,$s2,$s3) = ("%r1","%r2","%r3","%r4");
($t0,$t1,$t2,$t3) = ("%r5","%r6","%r7","%r8");

($acc0, $acc1, $acc2, $acc3, $acc4, $acc5, $acc6, $acc7,
 $acc8, $acc9,$acc10,$acc11,$acc12,$acc13,$acc14,$acc15) =
("%r9","%r10","%r11","%r12","%r13","%r14","%r15","%r16",
"%r17","%r18","%r19","%r20","%r21","%r22","%r23","%r26");

$tbl="%r28";
$rounds="%r29";

$code=<<___;
	.LEVEL	$LEVEL
	.text

	.EXPORT	aes_encrypt_internal,ENTRY,ARGW0=GR,ARGW1=GR,ARGW2=GR
	.ALIGN	64
aes_encrypt_internal
	.PROC
	.CALLINFO	FRAME=`$FRAME-16*$SIZE_T`,NO_CALLS,SAVE_RP,ENTRY_GR=18
	.ENTRY
	$PUSH	%r2,-$SAVED_RP(%sp)	; standard prologue
	$PUSHMA	%r3,$FRAME(%sp)
	$PUSH	%r4,`-$FRAME+1*$SIZE_T`(%sp)
	$PUSH	%r5,`-$FRAME+2*$SIZE_T`(%sp)
	$PUSH	%r6,`-$FRAME+3*$SIZE_T`(%sp)
	$PUSH	%r7,`-$FRAME+4*$SIZE_T`(%sp)
	$PUSH	%r8,`-$FRAME+5*$SIZE_T`(%sp)
	$PUSH	%r9,`-$FRAME+6*$SIZE_T`(%sp)
	$PUSH	%r10,`-$FRAME+7*$SIZE_T`(%sp)
	$PUSH	%r11,`-$FRAME+8*$SIZE_T`(%sp)
	$PUSH	%r12,`-$FRAME+9*$SIZE_T`(%sp)
	$PUSH	%r13,`-$FRAME+10*$SIZE_T`(%sp)
	$PUSH	%r14,`-$FRAME+11*$SIZE_T`(%sp)
	$PUSH	%r15,`-$FRAME+12*$SIZE_T`(%sp)
	$PUSH	%r16,`-$FRAME+13*$SIZE_T`(%sp)
	$PUSH	%r17,`-$FRAME+14*$SIZE_T`(%sp)
	$PUSH	%r18,`-$FRAME+15*$SIZE_T`(%sp)

	ldi	3,$t0
#ifdef __PIC__
	addil	LT'L\$AES_Te, %r19
	ldw	RT'L\$AES_Te(%r1), $tbl
#else
	ldil	L'L\$AES_Te, %t1
	ldo	R'L\$AES_Te(%t1), $tbl
#endif

	and	$inp,$t0,$t0
	sub	$inp,$t0,$inp
	ldw	0($inp),$s0
	ldw	4($inp),$s1
	ldw	8($inp),$s2
	comib,=	0,$t0,L\$enc_inp_aligned
	ldw	12($inp),$s3

	sh3addl	$t0,%r0,$t0
	subi	32,$t0,$t0
	mtctl	$t0,%cr11
	ldw	16($inp),$t1
	vshd	$s0,$s1,$s0
	vshd	$s1,$s2,$s1
	vshd	$s2,$s3,$s2
	vshd	$s3,$t1,$s3

L\$enc_inp_aligned
	bl	_parisc_AES_encrypt,%r31
	nop

	extru,<> $out,31,2,%r0
	b	L\$enc_out_aligned
	nop

	_srm	$s0,24,$acc0
	_srm	$s0,16,$acc1
	stb	$acc0,0($out)
	_srm	$s0,8,$acc2
	stb	$acc1,1($out)
	_srm	$s1,24,$acc4
	stb	$acc2,2($out)
	_srm	$s1,16,$acc5
	stb	$s0,3($out)
	_srm	$s1,8,$acc6
	stb	$acc4,4($out)
	_srm	$s2,24,$acc0
	stb	$acc5,5($out)
	_srm	$s2,16,$acc1
	stb	$acc6,6($out)
	_srm	$s2,8,$acc2
	stb	$s1,7($out)
	_srm	$s3,24,$acc4
	stb	$acc0,8($out)
	_srm	$s3,16,$acc5
	stb	$acc1,9($out)
	_srm	$s3,8,$acc6
	stb	$acc2,10($out)
	stb	$s2,11($out)
	stb	$acc4,12($out)
	stb	$acc5,13($out)
	stb	$acc6,14($out)
	b	L\$enc_done
	stb	$s3,15($out)

L\$enc_out_aligned
	stw	$s0,0($out)
	stw	$s1,4($out)
	stw	$s2,8($out)
	stw	$s3,12($out)

L\$enc_done
	$POP	`-$FRAME-$SAVED_RP`(%sp),%r2	; standard epilogue
	$POP	`-$FRAME+1*$SIZE_T`(%sp),%r4
	$POP	`-$FRAME+2*$SIZE_T`(%sp),%r5
	$POP	`-$FRAME+3*$SIZE_T`(%sp),%r6
	$POP	`-$FRAME+4*$SIZE_T`(%sp),%r7
	$POP	`-$FRAME+5*$SIZE_T`(%sp),%r8
	$POP	`-$FRAME+6*$SIZE_T`(%sp),%r9
	$POP	`-$FRAME+7*$SIZE_T`(%sp),%r10
	$POP	`-$FRAME+8*$SIZE_T`(%sp),%r11
	$POP	`-$FRAME+9*$SIZE_T`(%sp),%r12
	$POP	`-$FRAME+10*$SIZE_T`(%sp),%r13
	$POP	`-$FRAME+11*$SIZE_T`(%sp),%r14
	$POP	`-$FRAME+12*$SIZE_T`(%sp),%r15
	$POP	`-$FRAME+13*$SIZE_T`(%sp),%r16
	$POP	`-$FRAME+14*$SIZE_T`(%sp),%r17
	$POP	`-$FRAME+15*$SIZE_T`(%sp),%r18
	bv	(%r2)
	.EXIT
	$POPMB	-$FRAME(%sp),%r3
	.PROCEND

	.ALIGN	16
_parisc_AES_encrypt
	.PROC
	.CALLINFO	MILLICODE
	.ENTRY
	ldw	240($key),$rounds
	ldw	0($key),$t0
	ldw	4($key),$t1
	ldw	8($key),$t2
	_srm	$rounds,1,$rounds
	xor	$t0,$s0,$s0
	ldw	12($key),$t3
	_srm	$s0,24,$acc0
	xor	$t1,$s1,$s1
	ldw	16($key),$t0
	_srm	$s1,16,$acc1
	xor	$t2,$s2,$s2
	ldw	20($key),$t1
	xor	$t3,$s3,$s3
	ldw	24($key),$t2
	ldw	28($key),$t3
L\$enc_loop
	_srm	$s2,8,$acc2
	ldwx,s	$acc0($tbl),$acc0
	_srm	$s3,0,$acc3
	ldwx,s	$acc1($tbl),$acc1
	_srm	$s1,24,$acc4
	ldwx,s	$acc2($tbl),$acc2
	_srm	$s2,16,$acc5
	ldwx,s	$acc3($tbl),$acc3
	_srm	$s3,8,$acc6
	ldwx,s	$acc4($tbl),$acc4
	_srm	$s0,0,$acc7
	ldwx,s	$acc5($tbl),$acc5
	_srm	$s2,24,$acc8
	ldwx,s	$acc6($tbl),$acc6
	_srm	$s3,16,$acc9
	ldwx,s	$acc7($tbl),$acc7
	_srm	$s0,8,$acc10
	ldwx,s	$acc8($tbl),$acc8
	_srm	$s1,0,$acc11
	ldwx,s	$acc9($tbl),$acc9
	_srm	$s3,24,$acc12
	ldwx,s	$acc10($tbl),$acc10
	_srm	$s0,16,$acc13
	ldwx,s	$acc11($tbl),$acc11
	_srm	$s1,8,$acc14
	ldwx,s	$acc12($tbl),$acc12
	_srm	$s2,0,$acc15
	ldwx,s	$acc13($tbl),$acc13
	ldwx,s	$acc14($tbl),$acc14
	ldwx,s	$acc15($tbl),$acc15
	addib,= -1,$rounds,L\$enc_last
	ldo	32($key),$key

		_ror	$acc1,8,$acc1
		xor	$acc0,$t0,$t0
	ldw	0($key),$s0
		_ror	$acc2,16,$acc2
		xor	$acc1,$t0,$t0
	ldw	4($key),$s1
		_ror	$acc3,24,$acc3
		xor	$acc2,$t0,$t0
	ldw	8($key),$s2
		_ror	$acc5,8,$acc5
		xor	$acc3,$t0,$t0
	ldw	12($key),$s3
		_ror	$acc6,16,$acc6
		xor	$acc4,$t1,$t1
		_ror	$acc7,24,$acc7
		xor	$acc5,$t1,$t1
		_ror	$acc9,8,$acc9
		xor	$acc6,$t1,$t1
		_ror	$acc10,16,$acc10
		xor	$acc7,$t1,$t1
		_ror	$acc11,24,$acc11
		xor	$acc8,$t2,$t2
		_ror	$acc13,8,$acc13
		xor	$acc9,$t2,$t2
		_ror	$acc14,16,$acc14
		xor	$acc10,$t2,$t2
		_ror	$acc15,24,$acc15
		xor	$acc11,$t2,$t2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$t3,$t3
	_srm	$t0,24,$acc0
		xor	$acc14,$t3,$t3
	_srm	$t1,16,$acc1
		xor	$acc15,$t3,$t3

	_srm	$t2,8,$acc2
	ldwx,s	$acc0($tbl),$acc0
	_srm	$t3,0,$acc3
	ldwx,s	$acc1($tbl),$acc1
	_srm	$t1,24,$acc4
	ldwx,s	$acc2($tbl),$acc2
	_srm	$t2,16,$acc5
	ldwx,s	$acc3($tbl),$acc3
	_srm	$t3,8,$acc6
	ldwx,s	$acc4($tbl),$acc4
	_srm	$t0,0,$acc7
	ldwx,s	$acc5($tbl),$acc5
	_srm	$t2,24,$acc8
	ldwx,s	$acc6($tbl),$acc6
	_srm	$t3,16,$acc9
	ldwx,s	$acc7($tbl),$acc7
	_srm	$t0,8,$acc10
	ldwx,s	$acc8($tbl),$acc8
	_srm	$t1,0,$acc11
	ldwx,s	$acc9($tbl),$acc9
	_srm	$t3,24,$acc12
	ldwx,s	$acc10($tbl),$acc10
	_srm	$t0,16,$acc13
	ldwx,s	$acc11($tbl),$acc11
	_srm	$t1,8,$acc14
	ldwx,s	$acc12($tbl),$acc12
	_srm	$t2,0,$acc15
	ldwx,s	$acc13($tbl),$acc13
		_ror	$acc1,8,$acc1
	ldwx,s	$acc14($tbl),$acc14

		_ror	$acc2,16,$acc2
		xor	$acc0,$s0,$s0
	ldwx,s	$acc15($tbl),$acc15
		_ror	$acc3,24,$acc3
		xor	$acc1,$s0,$s0
	ldw	16($key),$t0
		_ror	$acc5,8,$acc5
		xor	$acc2,$s0,$s0
	ldw	20($key),$t1
		_ror	$acc6,16,$acc6
		xor	$acc3,$s0,$s0
	ldw	24($key),$t2
		_ror	$acc7,24,$acc7
		xor	$acc4,$s1,$s1
	ldw	28($key),$t3
		_ror	$acc9,8,$acc9
		xor	$acc5,$s1,$s1
	ldw	1024+0($tbl),%r0		; prefetch te4
		_ror	$acc10,16,$acc10
		xor	$acc6,$s1,$s1
	ldw	1024+32($tbl),%r0		; prefetch te4
		_ror	$acc11,24,$acc11
		xor	$acc7,$s1,$s1
	ldw	1024+64($tbl),%r0		; prefetch te4
		_ror	$acc13,8,$acc13
		xor	$acc8,$s2,$s2
	ldw	1024+96($tbl),%r0		; prefetch te4
		_ror	$acc14,16,$acc14
		xor	$acc9,$s2,$s2
	ldw	1024+128($tbl),%r0		; prefetch te4
		_ror	$acc15,24,$acc15
		xor	$acc10,$s2,$s2
	ldw	1024+160($tbl),%r0		; prefetch te4
	_srm	$s0,24,$acc0
		xor	$acc11,$s2,$s2
	ldw	1024+192($tbl),%r0		; prefetch te4
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$s3,$s3
	ldw	1024+224($tbl),%r0		; prefetch te4
	_srm	$s1,16,$acc1
		xor	$acc14,$s3,$s3
	b	L\$enc_loop
		xor	$acc15,$s3,$s3

	.ALIGN	16
L\$enc_last
	ldo	1024($tbl),$rounds
		_ror	$acc1,8,$acc1
		xor	$acc0,$t0,$t0
	ldw	0($key),$s0
		_ror	$acc2,16,$acc2
		xor	$acc1,$t0,$t0
	ldw	4($key),$s1
		_ror	$acc3,24,$acc3
		xor	$acc2,$t0,$t0
	ldw	8($key),$s2
		_ror	$acc5,8,$acc5
		xor	$acc3,$t0,$t0
	ldw	12($key),$s3
		_ror	$acc6,16,$acc6
		xor	$acc4,$t1,$t1
		_ror	$acc7,24,$acc7
		xor	$acc5,$t1,$t1
		_ror	$acc9,8,$acc9
		xor	$acc6,$t1,$t1
		_ror	$acc10,16,$acc10
		xor	$acc7,$t1,$t1
		_ror	$acc11,24,$acc11
		xor	$acc8,$t2,$t2
		_ror	$acc13,8,$acc13
		xor	$acc9,$t2,$t2
		_ror	$acc14,16,$acc14
		xor	$acc10,$t2,$t2
		_ror	$acc15,24,$acc15
		xor	$acc11,$t2,$t2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$t3,$t3
	_srm	$t0,24,$acc0
		xor	$acc14,$t3,$t3
	_srm	$t1,16,$acc1
		xor	$acc15,$t3,$t3

	_srm	$t2,8,$acc2
	ldbx	$acc0($rounds),$acc0
	_srm	$t1,24,$acc4
	ldbx	$acc1($rounds),$acc1
	_srm	$t2,16,$acc5
	_srm	$t3,0,$acc3
	ldbx	$acc2($rounds),$acc2
	ldbx	$acc3($rounds),$acc3
	_srm	$t3,8,$acc6
	ldbx	$acc4($rounds),$acc4
	_srm	$t2,24,$acc8
	ldbx	$acc5($rounds),$acc5
	_srm	$t3,16,$acc9
	_srm	$t0,0,$acc7
	ldbx	$acc6($rounds),$acc6
	ldbx	$acc7($rounds),$acc7
	_srm	$t0,8,$acc10
	ldbx	$acc8($rounds),$acc8
	_srm	$t3,24,$acc12
	ldbx	$acc9($rounds),$acc9
	_srm	$t0,16,$acc13
	_srm	$t1,0,$acc11
	ldbx	$acc10($rounds),$acc10
	_srm	$t1,8,$acc14
	ldbx	$acc11($rounds),$acc11
	ldbx	$acc12($rounds),$acc12
	ldbx	$acc13($rounds),$acc13
	_srm	$t2,0,$acc15
	ldbx	$acc14($rounds),$acc14

		dep	$acc0,7,8,$acc3
	ldbx	$acc15($rounds),$acc15
		dep	$acc4,7,8,$acc7
		dep	$acc1,15,8,$acc3
		dep	$acc5,15,8,$acc7
		dep	$acc2,23,8,$acc3
		dep	$acc6,23,8,$acc7
		xor	$acc3,$s0,$s0
		xor	$acc7,$s1,$s1
		dep	$acc8,7,8,$acc11
		dep	$acc12,7,8,$acc15
		dep	$acc9,15,8,$acc11
		dep	$acc13,15,8,$acc15
		dep	$acc10,23,8,$acc11
		dep	$acc14,23,8,$acc15
		xor	$acc11,$s2,$s2

	bv	(%r31)
	.EXIT
		xor	$acc15,$s3,$s3
	.PROCEND

	.section .rodata
	.ALIGN	64
L\$AES_Te
	.WORD	0xc66363a5, 0xf87c7c84, 0xee777799, 0xf67b7b8d
	.WORD	0xfff2f20d, 0xd66b6bbd, 0xde6f6fb1, 0x91c5c554
	.WORD	0x60303050, 0x02010103, 0xce6767a9, 0x562b2b7d
	.WORD	0xe7fefe19, 0xb5d7d762, 0x4dababe6, 0xec76769a
	.WORD	0x8fcaca45, 0x1f82829d, 0x89c9c940, 0xfa7d7d87
	.WORD	0xeffafa15, 0xb25959eb, 0x8e4747c9, 0xfbf0f00b
	.WORD	0x41adadec, 0xb3d4d467, 0x5fa2a2fd, 0x45afafea
	.WORD	0x239c9cbf, 0x53a4a4f7, 0xe4727296, 0x9bc0c05b
	.WORD	0x75b7b7c2, 0xe1fdfd1c, 0x3d9393ae, 0x4c26266a
	.WORD	0x6c36365a, 0x7e3f3f41, 0xf5f7f702, 0x83cccc4f
	.WORD	0x6834345c, 0x51a5a5f4, 0xd1e5e534, 0xf9f1f108
	.WORD	0xe2717193, 0xabd8d873, 0x62313153, 0x2a15153f
	.WORD	0x0804040c, 0x95c7c752, 0x46232365, 0x9dc3c35e
	.WORD	0x30181828, 0x379696a1, 0x0a05050f, 0x2f9a9ab5
	.WORD	0x0e070709, 0x24121236, 0x1b80809b, 0xdfe2e23d
	.WORD	0xcdebeb26, 0x4e272769, 0x7fb2b2cd, 0xea75759f
	.WORD	0x1209091b, 0x1d83839e, 0x582c2c74, 0x341a1a2e
	.WORD	0x361b1b2d, 0xdc6e6eb2, 0xb45a5aee, 0x5ba0a0fb
	.WORD	0xa45252f6, 0x763b3b4d, 0xb7d6d661, 0x7db3b3ce
	.WORD	0x5229297b, 0xdde3e33e, 0x5e2f2f71, 0x13848497
	.WORD	0xa65353f5, 0xb9d1d168, 0x00000000, 0xc1eded2c
	.WORD	0x40202060, 0xe3fcfc1f, 0x79b1b1c8, 0xb65b5bed
	.WORD	0xd46a6abe, 0x8dcbcb46, 0x67bebed9, 0x7239394b
	.WORD	0x944a4ade, 0x984c4cd4, 0xb05858e8, 0x85cfcf4a
	.WORD	0xbbd0d06b, 0xc5efef2a, 0x4faaaae5, 0xedfbfb16
	.WORD	0x864343c5, 0x9a4d4dd7, 0x66333355, 0x11858594
	.WORD	0x8a4545cf, 0xe9f9f910, 0x04020206, 0xfe7f7f81
	.WORD	0xa05050f0, 0x783c3c44, 0x259f9fba, 0x4ba8a8e3
	.WORD	0xa25151f3, 0x5da3a3fe, 0x804040c0, 0x058f8f8a
	.WORD	0x3f9292ad, 0x219d9dbc, 0x70383848, 0xf1f5f504
	.WORD	0x63bcbcdf, 0x77b6b6c1, 0xafdada75, 0x42212163
	.WORD	0x20101030, 0xe5ffff1a, 0xfdf3f30e, 0xbfd2d26d
	.WORD	0x81cdcd4c, 0x180c0c14, 0x26131335, 0xc3ecec2f
	.WORD	0xbe5f5fe1, 0x359797a2, 0x884444cc, 0x2e171739
	.WORD	0x93c4c457, 0x55a7a7f2, 0xfc7e7e82, 0x7a3d3d47
	.WORD	0xc86464ac, 0xba5d5de7, 0x3219192b, 0xe6737395
	.WORD	0xc06060a0, 0x19818198, 0x9e4f4fd1, 0xa3dcdc7f
	.WORD	0x44222266, 0x542a2a7e, 0x3b9090ab, 0x0b888883
	.WORD	0x8c4646ca, 0xc7eeee29, 0x6bb8b8d3, 0x2814143c
	.WORD	0xa7dede79, 0xbc5e5ee2, 0x160b0b1d, 0xaddbdb76
	.WORD	0xdbe0e03b, 0x64323256, 0x743a3a4e, 0x140a0a1e
	.WORD	0x924949db, 0x0c06060a, 0x4824246c, 0xb85c5ce4
	.WORD	0x9fc2c25d, 0xbdd3d36e, 0x43acacef, 0xc46262a6
	.WORD	0x399191a8, 0x319595a4, 0xd3e4e437, 0xf279798b
	.WORD	0xd5e7e732, 0x8bc8c843, 0x6e373759, 0xda6d6db7
	.WORD	0x018d8d8c, 0xb1d5d564, 0x9c4e4ed2, 0x49a9a9e0
	.WORD	0xd86c6cb4, 0xac5656fa, 0xf3f4f407, 0xcfeaea25
	.WORD	0xca6565af, 0xf47a7a8e, 0x47aeaee9, 0x10080818
	.WORD	0x6fbabad5, 0xf0787888, 0x4a25256f, 0x5c2e2e72
	.WORD	0x381c1c24, 0x57a6a6f1, 0x73b4b4c7, 0x97c6c651
	.WORD	0xcbe8e823, 0xa1dddd7c, 0xe874749c, 0x3e1f1f21
	.WORD	0x964b4bdd, 0x61bdbddc, 0x0d8b8b86, 0x0f8a8a85
	.WORD	0xe0707090, 0x7c3e3e42, 0x71b5b5c4, 0xcc6666aa
	.WORD	0x904848d8, 0x06030305, 0xf7f6f601, 0x1c0e0e12
	.WORD	0xc26161a3, 0x6a35355f, 0xae5757f9, 0x69b9b9d0
	.WORD	0x17868691, 0x99c1c158, 0x3a1d1d27, 0x279e9eb9
	.WORD	0xd9e1e138, 0xebf8f813, 0x2b9898b3, 0x22111133
	.WORD	0xd26969bb, 0xa9d9d970, 0x078e8e89, 0x339494a7
	.WORD	0x2d9b9bb6, 0x3c1e1e22, 0x15878792, 0xc9e9e920
	.WORD	0x87cece49, 0xaa5555ff, 0x50282878, 0xa5dfdf7a
	.WORD	0x038c8c8f, 0x59a1a1f8, 0x09898980, 0x1a0d0d17
	.WORD	0x65bfbfda, 0xd7e6e631, 0x844242c6, 0xd06868b8
	.WORD	0x824141c3, 0x299999b0, 0x5a2d2d77, 0x1e0f0f11
	.WORD	0x7bb0b0cb, 0xa85454fc, 0x6dbbbbd6, 0x2c16163a
	.BYTE	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5
	.BYTE	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76
	.BYTE	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0
	.BYTE	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0
	.BYTE	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc
	.BYTE	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15
	.BYTE	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a
	.BYTE	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75
	.BYTE	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0
	.BYTE	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84
	.BYTE	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b
	.BYTE	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf
	.BYTE	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85
	.BYTE	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8
	.BYTE	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5
	.BYTE	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2
	.BYTE	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17
	.BYTE	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73
	.BYTE	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88
	.BYTE	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb
	.BYTE	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c
	.BYTE	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79
	.BYTE	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9
	.BYTE	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08
	.BYTE	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6
	.BYTE	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a
	.BYTE	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e
	.BYTE	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e
	.BYTE	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94
	.BYTE	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf
	.BYTE	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68
	.BYTE	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
	.previous
___

$code.=<<___;
	.EXPORT	aes_decrypt_internal,ENTRY,ARGW0=GR,ARGW1=GR,ARGW2=GR
	.ALIGN	16
aes_decrypt_internal
	.PROC
	.CALLINFO	FRAME=`$FRAME-16*$SIZE_T`,NO_CALLS,SAVE_RP,ENTRY_GR=18
	.ENTRY
	$PUSH	%r2,-$SAVED_RP(%sp)	; standard prologue
	$PUSHMA	%r3,$FRAME(%sp)
	$PUSH	%r4,`-$FRAME+1*$SIZE_T`(%sp)
	$PUSH	%r5,`-$FRAME+2*$SIZE_T`(%sp)
	$PUSH	%r6,`-$FRAME+3*$SIZE_T`(%sp)
	$PUSH	%r7,`-$FRAME+4*$SIZE_T`(%sp)
	$PUSH	%r8,`-$FRAME+5*$SIZE_T`(%sp)
	$PUSH	%r9,`-$FRAME+6*$SIZE_T`(%sp)
	$PUSH	%r10,`-$FRAME+7*$SIZE_T`(%sp)
	$PUSH	%r11,`-$FRAME+8*$SIZE_T`(%sp)
	$PUSH	%r12,`-$FRAME+9*$SIZE_T`(%sp)
	$PUSH	%r13,`-$FRAME+10*$SIZE_T`(%sp)
	$PUSH	%r14,`-$FRAME+11*$SIZE_T`(%sp)
	$PUSH	%r15,`-$FRAME+12*$SIZE_T`(%sp)
	$PUSH	%r16,`-$FRAME+13*$SIZE_T`(%sp)
	$PUSH	%r17,`-$FRAME+14*$SIZE_T`(%sp)
	$PUSH	%r18,`-$FRAME+15*$SIZE_T`(%sp)

	ldi	3,$t0
#ifdef __PIC__
	addil	LT'L\$AES_Td, %r19
	ldw	RT'L\$AES_Td(%r1), $tbl
#else
	ldil	L'L\$AES_Td, %t1
	ldo	R'L\$AES_Td(%t1), $tbl
#endif

	and	$inp,$t0,$t0
	sub	$inp,$t0,$inp
	ldw	0($inp),$s0
	ldw	4($inp),$s1
	ldw	8($inp),$s2
	comib,=	0,$t0,L\$dec_inp_aligned
	ldw	12($inp),$s3

	sh3addl	$t0,%r0,$t0
	subi	32,$t0,$t0
	mtctl	$t0,%cr11
	ldw	16($inp),$t1
	vshd	$s0,$s1,$s0
	vshd	$s1,$s2,$s1
	vshd	$s2,$s3,$s2
	vshd	$s3,$t1,$s3

L\$dec_inp_aligned
	bl	_parisc_AES_decrypt,%r31
	nop

	extru,<> $out,31,2,%r0
	b	L\$dec_out_aligned
	nop

	_srm	$s0,24,$acc0
	_srm	$s0,16,$acc1
	stb	$acc0,0($out)
	_srm	$s0,8,$acc2
	stb	$acc1,1($out)
	_srm	$s1,24,$acc4
	stb	$acc2,2($out)
	_srm	$s1,16,$acc5
	stb	$s0,3($out)
	_srm	$s1,8,$acc6
	stb	$acc4,4($out)
	_srm	$s2,24,$acc0
	stb	$acc5,5($out)
	_srm	$s2,16,$acc1
	stb	$acc6,6($out)
	_srm	$s2,8,$acc2
	stb	$s1,7($out)
	_srm	$s3,24,$acc4
	stb	$acc0,8($out)
	_srm	$s3,16,$acc5
	stb	$acc1,9($out)
	_srm	$s3,8,$acc6
	stb	$acc2,10($out)
	stb	$s2,11($out)
	stb	$acc4,12($out)
	stb	$acc5,13($out)
	stb	$acc6,14($out)
	b	L\$dec_done
	stb	$s3,15($out)

L\$dec_out_aligned
	stw	$s0,0($out)
	stw	$s1,4($out)
	stw	$s2,8($out)
	stw	$s3,12($out)

L\$dec_done
	$POP	`-$FRAME-$SAVED_RP`(%sp),%r2	; standard epilogue
	$POP	`-$FRAME+1*$SIZE_T`(%sp),%r4
	$POP	`-$FRAME+2*$SIZE_T`(%sp),%r5
	$POP	`-$FRAME+3*$SIZE_T`(%sp),%r6
	$POP	`-$FRAME+4*$SIZE_T`(%sp),%r7
	$POP	`-$FRAME+5*$SIZE_T`(%sp),%r8
	$POP	`-$FRAME+6*$SIZE_T`(%sp),%r9
	$POP	`-$FRAME+7*$SIZE_T`(%sp),%r10
	$POP	`-$FRAME+8*$SIZE_T`(%sp),%r11
	$POP	`-$FRAME+9*$SIZE_T`(%sp),%r12
	$POP	`-$FRAME+10*$SIZE_T`(%sp),%r13
	$POP	`-$FRAME+11*$SIZE_T`(%sp),%r14
	$POP	`-$FRAME+12*$SIZE_T`(%sp),%r15
	$POP	`-$FRAME+13*$SIZE_T`(%sp),%r16
	$POP	`-$FRAME+14*$SIZE_T`(%sp),%r17
	$POP	`-$FRAME+15*$SIZE_T`(%sp),%r18
	bv	(%r2)
	.EXIT
	$POPMB	-$FRAME(%sp),%r3
	.PROCEND

	.ALIGN	16
_parisc_AES_decrypt
	.PROC
	.CALLINFO	MILLICODE
	.ENTRY
	ldw	240($key),$rounds
	ldw	0($key),$t0
	ldw	4($key),$t1
	ldw	8($key),$t2
	ldw	12($key),$t3
	_srm	$rounds,1,$rounds
	xor	$t0,$s0,$s0
	ldw	16($key),$t0
	xor	$t1,$s1,$s1
	ldw	20($key),$t1
	_srm	$s0,24,$acc0
	xor	$t2,$s2,$s2
	ldw	24($key),$t2
	xor	$t3,$s3,$s3
	ldw	28($key),$t3
	_srm	$s3,16,$acc1
L\$dec_loop
	_srm	$s2,8,$acc2
	ldwx,s	$acc0($tbl),$acc0
	_srm	$s1,0,$acc3
	ldwx,s	$acc1($tbl),$acc1
	_srm	$s1,24,$acc4
	ldwx,s	$acc2($tbl),$acc2
	_srm	$s0,16,$acc5
	ldwx,s	$acc3($tbl),$acc3
	_srm	$s3,8,$acc6
	ldwx,s	$acc4($tbl),$acc4
	_srm	$s2,0,$acc7
	ldwx,s	$acc5($tbl),$acc5
	_srm	$s2,24,$acc8
	ldwx,s	$acc6($tbl),$acc6
	_srm	$s1,16,$acc9
	ldwx,s	$acc7($tbl),$acc7
	_srm	$s0,8,$acc10
	ldwx,s	$acc8($tbl),$acc8
	_srm	$s3,0,$acc11
	ldwx,s	$acc9($tbl),$acc9
	_srm	$s3,24,$acc12
	ldwx,s	$acc10($tbl),$acc10
	_srm	$s2,16,$acc13
	ldwx,s	$acc11($tbl),$acc11
	_srm	$s1,8,$acc14
	ldwx,s	$acc12($tbl),$acc12
	_srm	$s0,0,$acc15
	ldwx,s	$acc13($tbl),$acc13
	ldwx,s	$acc14($tbl),$acc14
	ldwx,s	$acc15($tbl),$acc15
	addib,= -1,$rounds,L\$dec_last
	ldo	32($key),$key

		_ror	$acc1,8,$acc1
		xor	$acc0,$t0,$t0
	ldw	0($key),$s0
		_ror	$acc2,16,$acc2
		xor	$acc1,$t0,$t0
	ldw	4($key),$s1
		_ror	$acc3,24,$acc3
		xor	$acc2,$t0,$t0
	ldw	8($key),$s2
		_ror	$acc5,8,$acc5
		xor	$acc3,$t0,$t0
	ldw	12($key),$s3
		_ror	$acc6,16,$acc6
		xor	$acc4,$t1,$t1
		_ror	$acc7,24,$acc7
		xor	$acc5,$t1,$t1
		_ror	$acc9,8,$acc9
		xor	$acc6,$t1,$t1
		_ror	$acc10,16,$acc10
		xor	$acc7,$t1,$t1
		_ror	$acc11,24,$acc11
		xor	$acc8,$t2,$t2
		_ror	$acc13,8,$acc13
		xor	$acc9,$t2,$t2
		_ror	$acc14,16,$acc14
		xor	$acc10,$t2,$t2
		_ror	$acc15,24,$acc15
		xor	$acc11,$t2,$t2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$t3,$t3
	_srm	$t0,24,$acc0
		xor	$acc14,$t3,$t3
		xor	$acc15,$t3,$t3
	_srm	$t3,16,$acc1

	_srm	$t2,8,$acc2
	ldwx,s	$acc0($tbl),$acc0
	_srm	$t1,0,$acc3
	ldwx,s	$acc1($tbl),$acc1
	_srm	$t1,24,$acc4
	ldwx,s	$acc2($tbl),$acc2
	_srm	$t0,16,$acc5
	ldwx,s	$acc3($tbl),$acc3
	_srm	$t3,8,$acc6
	ldwx,s	$acc4($tbl),$acc4
	_srm	$t2,0,$acc7
	ldwx,s	$acc5($tbl),$acc5
	_srm	$t2,24,$acc8
	ldwx,s	$acc6($tbl),$acc6
	_srm	$t1,16,$acc9
	ldwx,s	$acc7($tbl),$acc7
	_srm	$t0,8,$acc10
	ldwx,s	$acc8($tbl),$acc8
	_srm	$t3,0,$acc11
	ldwx,s	$acc9($tbl),$acc9
	_srm	$t3,24,$acc12
	ldwx,s	$acc10($tbl),$acc10
	_srm	$t2,16,$acc13
	ldwx,s	$acc11($tbl),$acc11
	_srm	$t1,8,$acc14
	ldwx,s	$acc12($tbl),$acc12
	_srm	$t0,0,$acc15
	ldwx,s	$acc13($tbl),$acc13
		_ror	$acc1,8,$acc1
	ldwx,s	$acc14($tbl),$acc14

		_ror	$acc2,16,$acc2
		xor	$acc0,$s0,$s0
	ldwx,s	$acc15($tbl),$acc15
		_ror	$acc3,24,$acc3
		xor	$acc1,$s0,$s0
	ldw	16($key),$t0
		_ror	$acc5,8,$acc5
		xor	$acc2,$s0,$s0
	ldw	20($key),$t1
		_ror	$acc6,16,$acc6
		xor	$acc3,$s0,$s0
	ldw	24($key),$t2
		_ror	$acc7,24,$acc7
		xor	$acc4,$s1,$s1
	ldw	28($key),$t3
		_ror	$acc9,8,$acc9
		xor	$acc5,$s1,$s1
	ldw	1024+0($tbl),%r0		; prefetch td4
		_ror	$acc10,16,$acc10
		xor	$acc6,$s1,$s1
	ldw	1024+32($tbl),%r0		; prefetch td4
		_ror	$acc11,24,$acc11
		xor	$acc7,$s1,$s1
	ldw	1024+64($tbl),%r0		; prefetch td4
		_ror	$acc13,8,$acc13
		xor	$acc8,$s2,$s2
	ldw	1024+96($tbl),%r0		; prefetch td4
		_ror	$acc14,16,$acc14
		xor	$acc9,$s2,$s2
	ldw	1024+128($tbl),%r0		; prefetch td4
		_ror	$acc15,24,$acc15
		xor	$acc10,$s2,$s2
	ldw	1024+160($tbl),%r0		; prefetch td4
	_srm	$s0,24,$acc0
		xor	$acc11,$s2,$s2
	ldw	1024+192($tbl),%r0		; prefetch td4
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$s3,$s3
	ldw	1024+224($tbl),%r0		; prefetch td4
		xor	$acc14,$s3,$s3
		xor	$acc15,$s3,$s3
	b	L\$dec_loop
	_srm	$s3,16,$acc1

	.ALIGN	16
L\$dec_last
	ldo	1024($tbl),$rounds
		_ror	$acc1,8,$acc1
		xor	$acc0,$t0,$t0
	ldw	0($key),$s0
		_ror	$acc2,16,$acc2
		xor	$acc1,$t0,$t0
	ldw	4($key),$s1
		_ror	$acc3,24,$acc3
		xor	$acc2,$t0,$t0
	ldw	8($key),$s2
		_ror	$acc5,8,$acc5
		xor	$acc3,$t0,$t0
	ldw	12($key),$s3
		_ror	$acc6,16,$acc6
		xor	$acc4,$t1,$t1
		_ror	$acc7,24,$acc7
		xor	$acc5,$t1,$t1
		_ror	$acc9,8,$acc9
		xor	$acc6,$t1,$t1
		_ror	$acc10,16,$acc10
		xor	$acc7,$t1,$t1
		_ror	$acc11,24,$acc11
		xor	$acc8,$t2,$t2
		_ror	$acc13,8,$acc13
		xor	$acc9,$t2,$t2
		_ror	$acc14,16,$acc14
		xor	$acc10,$t2,$t2
		_ror	$acc15,24,$acc15
		xor	$acc11,$t2,$t2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$t3,$t3
	_srm	$t0,24,$acc0
		xor	$acc14,$t3,$t3
		xor	$acc15,$t3,$t3
	_srm	$t3,16,$acc1

	_srm	$t2,8,$acc2
	ldbx	$acc0($rounds),$acc0
	_srm	$t1,24,$acc4
	ldbx	$acc1($rounds),$acc1
	_srm	$t0,16,$acc5
	_srm	$t1,0,$acc3
	ldbx	$acc2($rounds),$acc2
	ldbx	$acc3($rounds),$acc3
	_srm	$t3,8,$acc6
	ldbx	$acc4($rounds),$acc4
	_srm	$t2,24,$acc8
	ldbx	$acc5($rounds),$acc5
	_srm	$t1,16,$acc9
	_srm	$t2,0,$acc7
	ldbx	$acc6($rounds),$acc6
	ldbx	$acc7($rounds),$acc7
	_srm	$t0,8,$acc10
	ldbx	$acc8($rounds),$acc8
	_srm	$t3,24,$acc12
	ldbx	$acc9($rounds),$acc9
	_srm	$t2,16,$acc13
	_srm	$t3,0,$acc11
	ldbx	$acc10($rounds),$acc10
	_srm	$t1,8,$acc14
	ldbx	$acc11($rounds),$acc11
	ldbx	$acc12($rounds),$acc12
	ldbx	$acc13($rounds),$acc13
	_srm	$t0,0,$acc15
	ldbx	$acc14($rounds),$acc14

		dep	$acc0,7,8,$acc3
	ldbx	$acc15($rounds),$acc15
		dep	$acc4,7,8,$acc7
		dep	$acc1,15,8,$acc3
		dep	$acc5,15,8,$acc7
		dep	$acc2,23,8,$acc3
		dep	$acc6,23,8,$acc7
		xor	$acc3,$s0,$s0
		xor	$acc7,$s1,$s1
		dep	$acc8,7,8,$acc11
		dep	$acc12,7,8,$acc15
		dep	$acc9,15,8,$acc11
		dep	$acc13,15,8,$acc15
		dep	$acc10,23,8,$acc11
		dep	$acc14,23,8,$acc15
		xor	$acc11,$s2,$s2

	bv	(%r31)
	.EXIT
		xor	$acc15,$s3,$s3
	.PROCEND

	.section .rodata
	.ALIGN	64
L\$AES_Td
	.WORD	0x51f4a750, 0x7e416553, 0x1a17a4c3, 0x3a275e96
	.WORD	0x3bab6bcb, 0x1f9d45f1, 0xacfa58ab, 0x4be30393
	.WORD	0x2030fa55, 0xad766df6, 0x88cc7691, 0xf5024c25
	.WORD	0x4fe5d7fc, 0xc52acbd7, 0x26354480, 0xb562a38f
	.WORD	0xdeb15a49, 0x25ba1b67, 0x45ea0e98, 0x5dfec0e1
	.WORD	0xc32f7502, 0x814cf012, 0x8d4697a3, 0x6bd3f9c6
	.WORD	0x038f5fe7, 0x15929c95, 0xbf6d7aeb, 0x955259da
	.WORD	0xd4be832d, 0x587421d3, 0x49e06929, 0x8ec9c844
	.WORD	0x75c2896a, 0xf48e7978, 0x99583e6b, 0x27b971dd
	.WORD	0xbee14fb6, 0xf088ad17, 0xc920ac66, 0x7dce3ab4
	.WORD	0x63df4a18, 0xe51a3182, 0x97513360, 0x62537f45
	.WORD	0xb16477e0, 0xbb6bae84, 0xfe81a01c, 0xf9082b94
	.WORD	0x70486858, 0x8f45fd19, 0x94de6c87, 0x527bf8b7
	.WORD	0xab73d323, 0x724b02e2, 0xe31f8f57, 0x6655ab2a
	.WORD	0xb2eb2807, 0x2fb5c203, 0x86c57b9a, 0xd33708a5
	.WORD	0x302887f2, 0x23bfa5b2, 0x02036aba, 0xed16825c
	.WORD	0x8acf1c2b, 0xa779b492, 0xf307f2f0, 0x4e69e2a1
	.WORD	0x65daf4cd, 0x0605bed5, 0xd134621f, 0xc4a6fe8a
	.WORD	0x342e539d, 0xa2f355a0, 0x058ae132, 0xa4f6eb75
	.WORD	0x0b83ec39, 0x4060efaa, 0x5e719f06, 0xbd6e1051
	.WORD	0x3e218af9, 0x96dd063d, 0xdd3e05ae, 0x4de6bd46
	.WORD	0x91548db5, 0x71c45d05, 0x0406d46f, 0x605015ff
	.WORD	0x1998fb24, 0xd6bde997, 0x894043cc, 0x67d99e77
	.WORD	0xb0e842bd, 0x07898b88, 0xe7195b38, 0x79c8eedb
	.WORD	0xa17c0a47, 0x7c420fe9, 0xf8841ec9, 0x00000000
	.WORD	0x09808683, 0x322bed48, 0x1e1170ac, 0x6c5a724e
	.WORD	0xfd0efffb, 0x0f853856, 0x3daed51e, 0x362d3927
	.WORD	0x0a0fd964, 0x685ca621, 0x9b5b54d1, 0x24362e3a
	.WORD	0x0c0a67b1, 0x9357e70f, 0xb4ee96d2, 0x1b9b919e
	.WORD	0x80c0c54f, 0x61dc20a2, 0x5a774b69, 0x1c121a16
	.WORD	0xe293ba0a, 0xc0a02ae5, 0x3c22e043, 0x121b171d
	.WORD	0x0e090d0b, 0xf28bc7ad, 0x2db6a8b9, 0x141ea9c8
	.WORD	0x57f11985, 0xaf75074c, 0xee99ddbb, 0xa37f60fd
	.WORD	0xf701269f, 0x5c72f5bc, 0x44663bc5, 0x5bfb7e34
	.WORD	0x8b432976, 0xcb23c6dc, 0xb6edfc68, 0xb8e4f163
	.WORD	0xd731dcca, 0x42638510, 0x13972240, 0x84c61120
	.WORD	0x854a247d, 0xd2bb3df8, 0xaef93211, 0xc729a16d
	.WORD	0x1d9e2f4b, 0xdcb230f3, 0x0d8652ec, 0x77c1e3d0
	.WORD	0x2bb3166c, 0xa970b999, 0x119448fa, 0x47e96422
	.WORD	0xa8fc8cc4, 0xa0f03f1a, 0x567d2cd8, 0x223390ef
	.WORD	0x87494ec7, 0xd938d1c1, 0x8ccaa2fe, 0x98d40b36
	.WORD	0xa6f581cf, 0xa57ade28, 0xdab78e26, 0x3fadbfa4
	.WORD	0x2c3a9de4, 0x5078920d, 0x6a5fcc9b, 0x547e4662
	.WORD	0xf68d13c2, 0x90d8b8e8, 0x2e39f75e, 0x82c3aff5
	.WORD	0x9f5d80be, 0x69d0937c, 0x6fd52da9, 0xcf2512b3
	.WORD	0xc8ac993b, 0x10187da7, 0xe89c636e, 0xdb3bbb7b
	.WORD	0xcd267809, 0x6e5918f4, 0xec9ab701, 0x834f9aa8
	.WORD	0xe6956e65, 0xaaffe67e, 0x21bccf08, 0xef15e8e6
	.WORD	0xbae79bd9, 0x4a6f36ce, 0xea9f09d4, 0x29b07cd6
	.WORD	0x31a4b2af, 0x2a3f2331, 0xc6a59430, 0x35a266c0
	.WORD	0x744ebc37, 0xfc82caa6, 0xe090d0b0, 0x33a7d815
	.WORD	0xf104984a, 0x41ecdaf7, 0x7fcd500e, 0x1791f62f
	.WORD	0x764dd68d, 0x43efb04d, 0xccaa4d54, 0xe49604df
	.WORD	0x9ed1b5e3, 0x4c6a881b, 0xc12c1fb8, 0x4665517f
	.WORD	0x9d5eea04, 0x018c355d, 0xfa877473, 0xfb0b412e
	.WORD	0xb3671d5a, 0x92dbd252, 0xe9105633, 0x6dd64713
	.WORD	0x9ad7618c, 0x37a10c7a, 0x59f8148e, 0xeb133c89
	.WORD	0xcea927ee, 0xb761c935, 0xe11ce5ed, 0x7a47b13c
	.WORD	0x9cd2df59, 0x55f2733f, 0x1814ce79, 0x73c737bf
	.WORD	0x53f7cdea, 0x5ffdaa5b, 0xdf3d6f14, 0x7844db86
	.WORD	0xcaaff381, 0xb968c43e, 0x3824342c, 0xc2a3405f
	.WORD	0x161dc372, 0xbce2250c, 0x283c498b, 0xff0d9541
	.WORD	0x39a80171, 0x080cb3de, 0xd8b4e49c, 0x6456c190
	.WORD	0x7bcb8461, 0xd532b670, 0x486c5c74, 0xd0b85742
	.BYTE	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38
	.BYTE	0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb
	.BYTE	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87
	.BYTE	0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb
	.BYTE	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d
	.BYTE	0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e
	.BYTE	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2
	.BYTE	0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25
	.BYTE	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16
	.BYTE	0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92
	.BYTE	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda
	.BYTE	0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84
	.BYTE	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a
	.BYTE	0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06
	.BYTE	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02
	.BYTE	0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b
	.BYTE	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea
	.BYTE	0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73
	.BYTE	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85
	.BYTE	0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e
	.BYTE	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89
	.BYTE	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b
	.BYTE	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20
	.BYTE	0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4
	.BYTE	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31
	.BYTE	0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f
	.BYTE	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d
	.BYTE	0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef
	.BYTE	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0
	.BYTE	0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61
	.BYTE	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26
	.BYTE	0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
	.previous
___

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	# translate made up instructons: _ror, _srm
	s/_ror(\s+)(%r[0-9]+),/shd$1$2,$2,/				or

	s/_srm(\s+%r[0-9]+),([0-9]+),/
		$SIZE_T==4 ? sprintf("extru%s,%d,8,",$1,31-$2)
		:            sprintf("extrd,u%s,%d,8,",$1,63-$2)/e;

	s/,\*/,/			if ($SIZE_T==4);
	s/\bbv\b(.*\(%r2\))/bve$1/	if ($SIZE_T==8);
	print $_,"\n";
}
close STDOUT;
