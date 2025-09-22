;	$OpenBSD: milli.S,v 1.6 2022/01/11 09:21:35 jsg Exp $
;
;  (c) Copyright 1986 HEWLETT-PACKARD COMPANY
;
;  To anyone who acknowledges that this file is provided "AS IS"
;  without any express or implied warranty:
;      permission to use, copy, modify, and distribute this file
;  for any purpose is hereby granted without fee, provided that
;  the above copyright notice and this notice appears in all
;  copies, and that the name of Hewlett-Packard Company not be
;  used in advertising or publicity pertaining to distribution
;  of the software without specific, written prior permission.
;  Hewlett-Packard Company makes no representations about the
;  suitability of this software for any purpose.
;

; Standard Hardware Register Definitions for Use with Assembler
; version A.08.06
;	- fr16-31 added at Utah
;~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Hardware General Registers
r0: .equ	0

r1: .equ	1

r2: .equ	2

r3: .equ	3

r4: .equ	4

r5: .equ	5

r6: .equ	6

r7: .equ	7

r8: .equ	8

r9: .equ	9

r10: .equ	10

r11: .equ	11

r12: .equ	12

r13: .equ	13

r14: .equ	14

r15: .equ	15

r16: .equ	16

r17: .equ	17

r18: .equ	18

r19: .equ	19

r20: .equ	20

r21: .equ	21

r22: .equ	22

r23: .equ	23

r24: .equ	24

r25: .equ	25

r26: .equ	26

r27: .equ	27

r28: .equ	28

r29: .equ	29

r30: .equ	30

r31: .equ	31

; Hardware Space Registers
sr0: .equ	0

sr1: .equ	1

sr2: .equ	2

sr3: .equ	3

sr4: .equ	4

sr5: .equ	5

sr6: .equ	6

sr7: .equ	7

; Hardware Floating Point Registers
fr0: .equ	0

fr1: .equ	1

fr2: .equ	2

fr3: .equ	3

fr4: .equ	4

fr5: .equ	5

fr6: .equ	6

fr7: .equ	7

fr8: .equ	8

fr9: .equ	9

fr10: .equ	10

fr11: .equ	11

fr12: .equ	12

fr13: .equ	13

fr14: .equ	14

fr15: .equ	15

fr16: .equ	16

fr17: .equ	17

fr18: .equ	18

fr19: .equ	19

fr20: .equ	20

fr21: .equ	21

fr22: .equ	22

fr23: .equ	23

fr24: .equ	24

fr25: .equ	25

fr26: .equ	26

fr27: .equ	27

fr28: .equ	28

fr29: .equ	29

fr30: .equ	30

fr31: .equ	31

; Hardware Control Registers
cr0: .equ	0

rctr: .equ	0			; Recovery Counter Register

cr8: .equ	8			; Protection ID 1

pidr1: .equ	8

cr9: .equ	9			; Protection ID 2

pidr2: .equ	9

cr10: .equ	10

ccr: .equ	10			; Coprocessor Configuration Register

cr11: .equ	11

sar: .equ	11			; Shift Amount Register

cr12: .equ	12

pidr3: .equ	12			; Protection ID 3

cr13: .equ	13

pidr4: .equ	13			; Protection ID 4

cr14: .equ	14

iva: .equ	14			; Interrupt Vector Address

cr15: .equ	15

eiem: .equ	15			; External Interrupt Enable Mask

cr16: .equ	16

itmr: .equ	16			; Interval Timer

cr17: .equ	17

pcsq: .equ	17			; Program Counter Space queue

cr18: .equ	18

pcoq: .equ	18			; Program Counter Offset queue

cr19: .equ	19

iir: .equ	19			; Interruption Instruction Register

cr20: .equ	20

isr: .equ	20			; Interruption Space Register

cr21: .equ	21

ior: .equ	21			; Interruption Offset Register

cr22: .equ	22

ipsw: .equ	22			; Interruption Processor Status Word

cr23: .equ	23

eirr: .equ	23			; External Interrupt Request

cr24: .equ	24

ppda: .equ	24			; Physical Page Directory Address

tr0: .equ	24			; Temporary register 0

cr25: .equ	25

hta: .equ	25			; Hash Table Address

tr1: .equ	25			; Temporary register 1

cr26: .equ	26

tr2: .equ	26			; Temporary register 2

cr27: .equ	27

tr3: .equ	27			; Temporary register 3

cr28: .equ	28

tr4: .equ	28			; Temporary register 4

cr29: .equ	29

tr5: .equ	29			; Temporary register 5

cr30: .equ	30

tr6: .equ	30			; Temporary register 6

cr31: .equ	31

tr7: .equ	31			; Temporary register 7

;~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Procedure Call Convention						~
; Register Definitions for Use with Assembler				~
; version A.08.06							~
;~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Software Architecture General Registers
rp: .equ	r2	; return pointer

mrp: .equ	r31	; millicode return pointer

ret0: .equ	r28	; return value

ret1: .equ	r29	; return value (high part of double)

sl: .equ	r29	; static link

sp: .equ	r30	; stack pointer

dp: .equ	r27	; data pointer

arg0: .equ	r26	; argument

arg1: .equ	r25	; argument or high part of double argument

arg2: .equ	r24	; argument

arg3: .equ	r23	; argument or high part of double argument

;_____________________________________________________________________________
; Software Architecture Space Registers
;		sr0	; return link form BLE
sret: .equ	sr1	; return value

sarg: .equ	sr1	; argument

;		sr4	; PC SPACE tracker
;		sr5	; process private data
;_____________________________________________________________________________
; Software Architecture Pseudo Registers
previous_sp: .equ	64	; old stack pointer (locates previous frame)

;~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Standard space and subspace definitions.  version A.08.06
; These are generally suitable for programs on HP_UX and HPE.
; Statements commented out are used when building such things as operating
; system kernels.
;;;;;;;;;;;;;;;;
; Additional code subspaces should have ALIGN=8 for an interspace BV
; and should have SORT=24.
;
; For an incomplete executable (program bound to shared libraries),
; sort keys $GLOBAL$ -1 and $GLOBAL$ -2 are reserved for the $DLT$
; and $PLT$ subspaces respectively.
;;;;;;;;;;;;;;;

	.text
	.EXPORT $$remI,millicode
;	.IMPORT	cerror
$$remI:
	.PROC
	.CALLINFO NO_CALLS
	.ENTRY
	addit,= 0,arg1,r0
	add,>= r0,arg0,ret1
	sub r0,ret1,ret1
	sub r0,arg1,r1
	ds r0,r1,r0
	or r0,r0,r1
	add ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	ds r1,arg1,r1
	addc ret1,ret1,ret1
	movb,>=,n r1,ret1,remI300
	add,< arg1,r0,r0
	add,tr r1,arg1,ret1
	sub r1,arg1,ret1
remI300: add,>= arg0,r0,r0

	sub r0,ret1,ret1
	bv r0(r31)
	nop
	.EXIT
	.PROCEND

bit1:  .equ 1

bit30: .equ 30
bit31: .equ 31

len2:  .equ 2

len4:  .equ 4

#if 0
$$dyncall:
	.proc
	.callinfo NO_CALLS
	.export $$dyncall,MILLICODE

	bb,>=,n	22,bit30,noshlibs

	depi	0,bit31,len2,22
	ldw	4(22),19
	ldw	0(22),22
noshlibs:
	ldsid	(22),r1
	mtsp	r1,sr0
	be	0(sr0,r22)
	stw	rp,-24(sp)
	.procend
#endif

$$sh_func_adrs:
	.proc
	.callinfo NO_CALLS
	.export $$sh_func_adrs, millicode
	ldo	0(r26),ret1
	dep	r0,30,1,r26
	probew	(r26),r31,r22
	extru,=	r22,31,1,r22
	bv	r0(r31)
	ldws	0(r26),ret1
	.procend

temp: .EQU	r1

retreg: .EQU	ret1	; r29

	.export $$divU,millicode
	.import $$divU_3,millicode
	.import $$divU_5,millicode
	.import $$divU_6,millicode
	.import $$divU_7,millicode
	.import $$divU_9,millicode
	.import $$divU_10,millicode
	.import $$divU_12,millicode
	.import $$divU_14,millicode
	.import $$divU_15,millicode
$$divU:
	.proc
	.callinfo NO_CALLS
; The subtract is not nullified since it does no harm and can be used
; by the two cases that branch back to "normal".
	comib,>=  15,arg1,special_divisor
	sub	r0,arg1,temp		; clear carry, negate the divisor
	ds	r0,temp,r0		; set V-bit to 1
normal:
	add	arg0,arg0,retreg	; shift msb bit into carry
	ds	r0,arg1,temp		; 1st divide step, if no carry
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 2nd divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 3rd divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 4th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 5th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 6th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 7th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 8th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 9th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 10th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 11th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 12th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 13th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 14th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 15th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 16th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 17th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 18th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 19th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 20th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 21st divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 22nd divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 23rd divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 24th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 25th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 26th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 27th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 28th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 29th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 30th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 31st divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 32nd divide step,
	bv	0(r31)
	addc	retreg,retreg,retreg	; shift last retreg bit into retreg
;_____________________________________________________________________________
; handle the cases where divisor is a small constant or has high bit on
special_divisor:
	blr	arg1,r0
	comib,>,n  0,arg1,big_divisor	; nullify previous instruction
zero_divisor: ; this label is here to provide external visibility

	addit,=	0,arg1,0		; trap for zero dvr
	nop
	bv	0(r31)			; divisor == 1
	copy	arg0,retreg
	bv	0(r31)			; divisor == 2
	extru	arg0,30,31,retreg
	 b,n	$$divU_3		; divisor == 3
	nop
	bv	0(r31)			; divisor == 4
	extru	arg0,29,30,retreg
	 b,n	$$divU_5		; divisor == 5
	nop
	 b,n	$$divU_6		; divisor == 6
	nop
	 b,n	$$divU_7		; divisor == 7
	nop
	bv	0(r31)			; divisor == 8
	extru	arg0,28,29,retreg
	 b,n	$$divU_9		; divisor == 9
	nop
	 b,n	$$divU_10		; divisor == 10
	nop
	b	normal			; divisor == 11
	ds	r0,temp,r0		; set V-bit to 1
	 b,n	$$divU_12		; divisor == 12
	nop
	b	normal			; divisor == 13
	ds	r0,temp,r0		; set V-bit to 1
	 b,n	$$divU_14		; divisor == 14
	nop
	 b,n	$$divU_15		; divisor == 15
	nop
;_____________________________________________________________________________
; Handle the case where the high bit is on in the divisor.
; Compute:	if( dividend>=divisor) quotient=1; else quotient=0;
; Note:		dividend>==divisor iff dividend-divisor does not borrow
; and		not borrow iff carry
big_divisor:
	sub	arg0,arg1,r0
	bv	0(r31)
	addc	r0,r0,retreg
	.procend
	.end

t2: .EQU	r1

; x2	.EQU	arg0	; r26
t1: .EQU	arg1	; r25

; x1	.EQU	ret1	; r29
;_____________________________________________________________________________

$$divide_by_constant:
	.PROC
	.CALLINFO NO_CALLS
	.export $$divide_by_constant,millicode
; Provides a "nice" label for the code covered by the unwind descriptor
; for things like gprof.

$$divI_2:
	.EXPORT	$$divI_2,MILLICODE
	COMCLR,>=	arg0,0,0
	ADDI		1,arg0,arg0
	bv		0(r31)
	EXTRS		arg0,30,31,ret1

$$divI_4:
	.EXPORT		$$divI_4,MILLICODE
	COMCLR,>=	arg0,0,0
	ADDI		3,arg0,arg0
	bv		0(r31)
	EXTRS		arg0,29,30,ret1

$$divI_8:
	.EXPORT		$$divI_8,MILLICODE
	COMCLR,>=	arg0,0,0
	ADDI		7,arg0,arg0
	bv		0(r31)
	EXTRS		arg0,28,29,ret1

$$divI_16:
	.EXPORT		$$divI_16,MILLICODE
	COMCLR,>=	arg0,0,0
	ADDI		15,arg0,arg0
	bv		0(r31)
	EXTRS		arg0,27,28,ret1

$$divI_3:
	.EXPORT		$$divI_3,MILLICODE
	COMB,<,N	arg0,0,$neg3

	ADDI		1,arg0,arg0
	EXTRU		arg0,1,2,ret1
	SH2ADD		arg0,arg0,arg0
	B		$pos
	ADDC		ret1,0,ret1

$neg3:
	SUBI		1,arg0,arg0
	EXTRU		arg0,1,2,ret1
	SH2ADD		arg0,arg0,arg0
	B		$neg
	ADDC		ret1,0,ret1

$$divU_3:
	.EXPORT		$$divU_3,MILLICODE
	ADDI		1,arg0,arg0
	ADDC		0,0,ret1
	SHD		ret1,arg0,30,t1
	SH2ADD		arg0,arg0,arg0
	B		$pos
	ADDC		ret1,t1,ret1

$$divI_5:
	.EXPORT		$$divI_5,MILLICODE
	COMB,<,N	arg0,0,$neg5
	ADDI		3,arg0,t1
	SH1ADD		arg0,t1,arg0
	B		$pos
	ADDC		0,0,ret1

$neg5:
	SUB		0,arg0,arg0
	ADDI		1,arg0,arg0
	SHD		0,arg0,31,ret1
	SH1ADD		arg0,arg0,arg0
	B		$neg
	ADDC		ret1,0,ret1

$$divU_5:
	.EXPORT		$$divU_5,MILLICODE
	ADDI		1,arg0,arg0
	ADDC		0,0,ret1
	SHD		ret1,arg0,31,t1
	SH1ADD		arg0,arg0,arg0
	B		$pos
	ADDC		t1,ret1,ret1

$$divI_6:
	.EXPORT		$$divI_6,MILLICODE
	COMB,<,N	arg0,0,$neg6
	EXTRU		arg0,30,31,arg0
	ADDI		5,arg0,t1
	SH2ADD		arg0,t1,arg0
	B		$pos
	ADDC		0,0,ret1

$neg6:
	SUBI		2,arg0,arg0
	EXTRU		arg0,30,31,arg0
	SHD		0,arg0,30,ret1
	SH2ADD		arg0,arg0,arg0
	B		$neg
	ADDC		ret1,0,ret1

$$divU_6:
	.EXPORT		$$divU_6,MILLICODE
	EXTRU		arg0,30,31,arg0
	ADDI		1,arg0,arg0
	SHD		0,arg0,30,ret1
	SH2ADD		arg0,arg0,arg0
	B		$pos
	ADDC		ret1,0,ret1

$$divU_10:
	.EXPORT		$$divU_10,MILLICODE
	EXTRU		arg0,30,31,arg0
	ADDI		3,arg0,t1
	SH1ADD		arg0,t1,arg0
	ADDC		0,0,ret1
$pos:
	SHD		ret1,arg0,28,t1
	SHD		arg0,0,28,t2
	ADD		arg0,t2,arg0
	ADDC		ret1,t1,ret1
$pos_for_17:
	SHD		ret1,arg0,24,t1
	SHD		arg0,0,24,t2
	ADD		arg0,t2,arg0
	ADDC		ret1,t1,ret1

	SHD		ret1,arg0,16,t1
	SHD		arg0,0,16,t2
	ADD		arg0,t2,arg0
	bv		0(r31)
	ADDC		ret1,t1,ret1

$$divI_10:
	.EXPORT		$$divI_10,MILLICODE
	COMB,<		arg0,0,$neg10
	COPY		0,ret1
	EXTRU		arg0,30,31,arg0
	ADDIB,TR	1,arg0,$pos
	SH1ADD		arg0,arg0,arg0

$neg10:
	SUBI		2,arg0,arg0
	EXTRU		arg0,30,31,arg0
	SH1ADD		arg0,arg0,arg0
$neg:
	SHD		ret1,arg0,28,t1
	SHD		arg0,0,28,t2
	ADD		arg0,t2,arg0
	ADDC		ret1,t1,ret1
$neg_for_17:
	SHD		ret1,arg0,24,t1
	SHD		arg0,0,24,t2
	ADD		arg0,t2,arg0
	ADDC		ret1,t1,ret1

	SHD		ret1,arg0,16,t1
	SHD		arg0,0,16,t2
	ADD		arg0,t2,arg0
	ADDC		ret1,t1,ret1
	bv		0(r31)
	SUB		0,ret1,ret1

$$divI_12:
	.EXPORT		$$divI_12,MILLICODE
	COMB,<		arg0,0,$neg12
	COPY		0,ret1
	EXTRU		arg0,29,30,arg0
	ADDIB,TR	1,arg0,$pos
	SH2ADD		arg0,arg0,arg0

$neg12:
	SUBI		4,arg0,arg0
	EXTRU		arg0,29,30,arg0
	B		$neg
	SH2ADD		arg0,arg0,arg0

$$divU_12:
	.EXPORT		$$divU_12,MILLICODE
	EXTRU		arg0,29,30,arg0
	ADDI		5,arg0,t1
	SH2ADD		arg0,t1,arg0
	B		$pos
	ADDC		0,0,ret1

$$divI_15:
	.EXPORT		$$divI_15,MILLICODE
	COMB,<		arg0,0,$neg15
	COPY		0,ret1
	ADDIB,TR	1,arg0,$pos+4
	SHD		ret1,arg0,28,t1

$neg15:
	B		$neg
	SUBI		1,arg0,arg0

$$divU_15:
	.EXPORT		$$divU_15,MILLICODE
	ADDI		1,arg0,arg0
	B		$pos
	ADDC		0,0,ret1

$$divI_17:
	.EXPORT		$$divI_17,MILLICODE
	COMB,<,N	arg0,0,$neg17
	ADDI		1,arg0,arg0
	SHD		0,arg0,28,t1
	SHD		arg0,0,28,t2
	SUB		t2,arg0,arg0
	B		$pos_for_17
	SUBB		t1,0,ret1

$neg17:
	SUBI		1,arg0,arg0
	SHD		0,arg0,28,t1
	SHD		arg0,0,28,t2
	SUB		t2,arg0,arg0
	B		$neg_for_17
	SUBB		t1,0,ret1

$$divU_17:
	.EXPORT		$$divU_17,MILLICODE
	ADDI		1,arg0,arg0
	ADDC		0,0,ret1
	SHD		ret1,arg0,28,t1
$u17:
	SHD		arg0,0,28,t2
	SUB		t2,arg0,arg0
	B		$pos_for_17
	SUBB		t1,ret1,ret1

$$divI_7:
	.EXPORT		$$divI_7,MILLICODE
	COMB,<,N	arg0,0,$neg7
$7:
	ADDI		1,arg0,arg0
	SHD		0,arg0,29,ret1
	SH3ADD		arg0,arg0,arg0
	ADDC		ret1,0,ret1
$pos7:
	SHD		ret1,arg0,26,t1
	SHD		arg0,0,26,t2
	ADD		arg0,t2,arg0
	ADDC		ret1,t1,ret1

	SHD		ret1,arg0,20,t1
	SHD		arg0,0,20,t2
	ADD		arg0,t2,arg0
	ADDC		ret1,t1,t1

	COPY		0,ret1
	SHD,=		t1,arg0,24,t1
$1:
	ADDB,TR		t1,ret1,$2
	EXTRU		arg0,31,24,arg0

	bv,n  0(r31)

$2:
	ADDB,TR		t1,arg0,$1
	EXTRU,=		arg0,7,8,t1

$neg7:
	SUBI		1,arg0,arg0
$8:
	SHD		0,arg0,29,ret1
	SH3ADD		arg0,arg0,arg0
	ADDC		ret1,0,ret1

$neg7_shift:
	SHD		ret1,arg0,26,t1
	SHD		arg0,0,26,t2
	ADD		arg0,t2,arg0
	ADDC		ret1,t1,ret1

	SHD		ret1,arg0,20,t1
	SHD		arg0,0,20,t2
	ADD		arg0,t2,arg0
	ADDC		ret1,t1,t1

	COPY		0,ret1
	SHD,=		t1,arg0,24,t1
$3:
	ADDB,TR		t1,ret1,$4
	EXTRU		arg0,31,24,arg0

	bv		0(r31)
	SUB		0,ret1,ret1

$4:
	ADDB,TR		t1,arg0,$3
	EXTRU,=		arg0,7,8,t1

$$divU_7:
	.EXPORT		$$divU_7,MILLICODE
	ADDI		1,arg0,arg0
	ADDC		0,0,ret1
	SHD		ret1,arg0,29,t1
	SH3ADD		arg0,arg0,arg0
	B		$pos7
	ADDC		t1,ret1,ret1

$$divI_9:
	.EXPORT		$$divI_9,MILLICODE
	COMB,<,N	arg0,0,$neg9
	ADDI		1,arg0,arg0
	SHD		0,arg0,29,t1
	SHD		arg0,0,29,t2
	SUB		t2,arg0,arg0
	B		$pos7
	SUBB		t1,0,ret1

$neg9:
	SUBI		1,arg0,arg0
	SHD		0,arg0,29,t1
	SHD		arg0,0,29,t2
	SUB		t2,arg0,arg0
	B		$neg7_shift
	SUBB		t1,0,ret1

$$divU_9:
	.EXPORT		$$divU_9,MILLICODE
	ADDI		1,arg0,arg0
	ADDC		0,0,ret1
	SHD		ret1,arg0,29,t1
	SHD		arg0,0,29,t2
	SUB		t2,arg0,arg0
	B		$pos7
	SUBB		t1,ret1,ret1

$$divI_14:
	.EXPORT		$$divI_14,MILLICODE
	COMB,<,N	arg0,0,$neg14
$$divU_14:
	.EXPORT		$$divU_14,MILLICODE
	B		$7
	EXTRU		arg0,30,31,arg0

$neg14:
	SUBI		2,arg0,arg0
	B		$8
	EXTRU		arg0,30,31,arg0

	.PROCEND
	.END

rmndr: .EQU	ret1	; r29

	.export $$remU,millicode
$$remU:
	.proc
	.callinfo NO_CALLS
	.entry

	comib,>=,n  0,arg1,special_case
	sub	r0,arg1,rmndr		; clear carry, negate the divisor
	ds	r0,rmndr,r0		; set V-bit to 1
	add	arg0,arg0,temp		; shift msb bit into carry
	ds	r0,arg1,rmndr		; 1st divide step, if no carry
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 2nd divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 3rd divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 4th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 5th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 6th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 7th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 8th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 9th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 10th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 11th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 12th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 13th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 14th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 15th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 16th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 17th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 18th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 19th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 20th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 21st divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 22nd divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 23rd divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 24th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 25th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 26th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 27th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 28th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 29th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 30th divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 31st divide step
	addc	temp,temp,temp		; shift temp with/into carry
	ds	rmndr,arg1,rmndr		; 32nd divide step,
	comiclr,<= 0,rmndr,r0
	  add	rmndr,arg1,rmndr	; correction
;	.exit
	bv,n  0(r31)
	nop
; Putting >= on the last DS and deleting COMICLR does not work!
;_____________________________________________________________________________
special_case:
	addit,=	0,arg1,r0		; trap on div by zero
	sub,>>=	arg0,arg1,rmndr
	  copy	arg0,rmndr
	.exit
	bv,n  0(r31)
	nop
	.procend
	.end

; Use bv  0(r31) and bv,n  0(r31) instead.
; #define	return		bv	0(%mrp)
; #define	return_n	bv,n	0(%mrp)

	.align 16
$$mulI:

	.proc
	.callinfo NO_CALLS
	.export $$mulI, millicode
	combt,<<=	%r25,%r26,l4	; swap args if unsigned %r25>%r26
	copy		0,%r29		; zero out the result
	xor		%r26,%r25,%r26	; swap %r26 & %r25 using the
	xor		%r26,%r25,%r25	;  old xor trick
	xor		%r26,%r25,%r26
l4: combt,<=	0,%r26,l3		; if %r26>=0 then proceed like unsigned

	zdep		%r25,30,8,%r1	; %r1 = (%r25&0xff)<<1 *********
	sub,>		0,%r25,%r1		; otherwise negate both and
	combt,<=,n	%r26,%r1,l2	;  swap back if |%r26|<|%r25|
	sub		0,%r26,%r25
	movb,tr,n	%r1,%r26,l2	; 10th inst.

l0:	add	%r29,%r1,%r29				; add in this partial product

l1: zdep	%r26,23,24,%r26			; %r26 <<= 8 ******************

l2: zdep		%r25,30,8,%r1	; %r1 = (%r25&0xff)<<1 *********

l3: blr		%r1,0		; case on these 8 bits ******

	extru		%r25,23,24,%r25	; %r25 >>= 8 ******************

;16 insts before this.
;			  %r26 <<= 8 **************************
x0: comb,<>	%r25,0,l2	! zdep	%r26,23,24,%r26	! bv,n  0(r31)	! nop

x1: comb,<>	%r25,0,l1	!	add	%r29,%r26,%r29	! bv,n  0(r31)	! nop

x2: comb,<>	%r25,0,l1	! sh1add	%r26,%r29,%r29	! bv,n  0(r31)	! nop

x3: comb,<>	%r25,0,l0	!	sh1add	%r26,%r26,%r1	! bv	0(r31)	!	add	%r29,%r1,%r29

x4: comb,<>	%r25,0,l1	! sh2add	%r26,%r29,%r29	! bv,n  0(r31)	! nop

x5: comb,<>	%r25,0,l0	!	sh2add	%r26,%r26,%r1	! bv	0(r31)	!	add	%r29,%r1,%r29

x6:	sh1add	%r26,%r26,%r1		! comb,<>	%r25,0,l1	! sh1add	%r1,%r29,%r29	! bv,n  0(r31)

x7:	sh1add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	! sh2add	%r26,%r29,%r29	! b,n	ret_t0

x8: comb,<>	%r25,0,l1	! sh3add	%r26,%r29,%r29	! bv,n  0(r31)	! nop

x9: comb,<>	%r25,0,l0	!	sh3add	%r26,%r26,%r1	! bv	0(r31)	!	add	%r29,%r1,%r29

x10:	sh2add	%r26,%r26,%r1		! comb,<>	%r25,0,l1	! sh1add	%r1,%r29,%r29	! bv,n  0(r31)

x11:	sh1add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	! sh3add	%r26,%r29,%r29	! b,n	ret_t0

x12:	sh1add	%r26,%r26,%r1		! comb,<>	%r25,0,l1	! sh2add	%r1,%r29,%r29	! bv,n  0(r31)

x13:	sh2add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	! sh3add	%r26,%r29,%r29	! b,n	ret_t0

x14:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x15:	sh2add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	!	sh1add	%r1,%r1,%r1	! b,n	ret_t0

x16: zdep	%r26,27,28,%r1	! comb,<>	%r25,0,l1	!	add	%r29,%r1,%r29	! bv,n  0(r31)

x17:	sh3add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	! sh3add	%r26,%r1,%r1	! b,n	ret_t0

x18:	sh3add	%r26,%r26,%r1		! comb,<>	%r25,0,l1	! sh1add	%r1,%r29,%r29	! bv,n  0(r31)

x19:	sh3add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	! sh1add	%r1,%r26,%r1	! b,n	ret_t0

x20:	sh2add	%r26,%r26,%r1		! comb,<>	%r25,0,l1	! sh2add	%r1,%r29,%r29	! bv,n  0(r31)

x21:	sh2add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	! sh2add	%r1,%r26,%r1	! b,n	ret_t0

x22:	sh2add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x23:	sh2add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x24:	sh1add	%r26,%r26,%r1		! comb,<>	%r25,0,l1	! sh3add	%r1,%r29,%r29	! bv,n  0(r31)

x25:	sh2add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	!	sh2add	%r1,%r1,%r1	! b,n	ret_t0

x26:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x27:	sh1add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	!	sh3add	%r1,%r1,%r1	! b,n	ret_t0

x28:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x29:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x30:	sh2add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x31: zdep	%r26,26,27,%r1	! comb,<>	%r25,0,l0	! sub	%r1,%r26,%r1	! b,n	ret_t0

x32: zdep	%r26,26,27,%r1	! comb,<>	%r25,0,l1	!	add	%r29,%r1,%r29	! bv,n  0(r31)

x33:	sh3add	%r26,0,%r1		! comb,<>	%r25,0,l0	! sh2add	%r1,%r26,%r1	! b,n	ret_t0

x34: zdep	%r26,27,28,%r1	! add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x35:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sh3add	%r26,%r1,%r1

x36:	sh3add	%r26,%r26,%r1		! comb,<>	%r25,0,l1	! sh2add	%r1,%r29,%r29	! bv,n  0(r31)

x37:	sh3add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	! sh2add	%r1,%r26,%r1	! b,n	ret_t0

x38:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x39:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x40:	sh2add	%r26,%r26,%r1		! comb,<>	%r25,0,l1	! sh3add	%r1,%r29,%r29	! bv,n  0(r31)

x41:	sh2add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	! sh3add	%r1,%r26,%r1	! b,n	ret_t0

x42:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x43:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x44:	sh2add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x45:	sh3add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	!	sh2add	%r1,%r1,%r1	! b,n	ret_t0

x46:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	! add	%r1,%r26,%r1

x47:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	! sh1add	%r26,%r1,%r1

x48:	sh1add	%r26,%r26,%r1		! comb,<>	%r25,0,l0	! zdep	%r1,27,28,%r1	! b,n	ret_t0

x49:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	! sh2add	%r26,%r1,%r1

x50:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x51:	sh3add	%r26,%r26,%r1		! sh3add	%r26,%r1,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x52:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x53:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x54:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x55:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x56:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x57:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x58:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_2t0	! sh2add	%r1,%r26,%r1

x59:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_t02a0	!	sh1add	%r1,%r1,%r1

x60:	sh2add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x61:	sh2add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x62: zdep	%r26,26,27,%r1	! sub	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x63: zdep	%r26,25,26,%r1	! comb,<>	%r25,0,l0	! sub	%r1,%r26,%r1	! b,n	ret_t0

x64: zdep	%r26,25,26,%r1	! comb,<>	%r25,0,l1	!	add	%r29,%r1,%r29	! bv,n  0(r31)

x65:	sh3add	%r26,0,%r1		! comb,<>	%r25,0,l0	! sh3add	%r1,%r26,%r1	! b,n	ret_t0

x66: zdep	%r26,26,27,%r1	! add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x67:	sh3add	%r26,0,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x68:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x69:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x70: zdep	%r26,25,26,%r1	! sh2add	%r26,%r1,%r1	!	b	e_t0	! sh1add	%r26,%r1,%r1

x71:	sh3add	%r26,%r26,%r1		!	sh3add	%r1,0,%r1	!	b	e_t0	! sub	%r1,%r26,%r1

x72:	sh3add	%r26,%r26,%r1		! comb,<>	%r25,0,l1	! sh3add	%r1,%r29,%r29	! bv,n  0(r31)

x73:	sh3add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	! b	e_shift	!	add	%r29,%r1,%r29

x74:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x75:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x76:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x77:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x78:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_2t0	! sh1add	%r1,%r26,%r1

x79: zdep	%r26,27,28,%r1	!	sh2add	%r1,%r1,%r1	!	b	e_t0	! sub	%r1,%r26,%r1

x80: zdep	%r26,27,28,%r1	!	sh2add	%r1,%r1,%r1	! b	e_shift	!	add	%r29,%r1,%r29

x81:	sh3add	%r26,%r26,%r1		!	sh3add	%r1,%r1,%r1	! b	e_shift	!	add	%r29,%r1,%r29

x82:	sh2add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x83:	sh2add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x84:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x85:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x86:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_2t0	! sh1add	%r1,%r26,%r1

x87:	sh3add	%r26,%r26,%r1		!	sh3add	%r1,%r1,%r1	! b	e_t02a0	! sh2add	%r26,%r1,%r1

x88:	sh2add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x89:	sh2add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	! sh3add	%r1,%r26,%r1

x90:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x91:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x92:	sh2add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_4t0	! sh1add	%r1,%r26,%r1

x93: zdep	%r26,26,27,%r1	! sub	%r1,%r26,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x94:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_2t0	! sh1add	%r26,%r1,%r1

x95:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x96:	sh3add	%r26,0,%r1		!	sh1add	%r1,%r1,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x97:	sh3add	%r26,0,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x98: zdep	%r26,26,27,%r1	!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sh1add	%r26,%r1,%r1

x99:	sh3add	%r26,0,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x100:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x101:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x102: zdep	%r26,26,27,%r1	! sh1add	%r26,%r1,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x103:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_t02a0	! sh2add	%r1,%r26,%r1

x104:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x105:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x106:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_2t0	! sh2add	%r1,%r26,%r1

x107:	sh3add	%r26,%r26,%r1		! sh2add	%r26,%r1,%r1	! b	e_t02a0	! sh3add	%r1,%r26,%r1

x108:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x109:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x110:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_2t0	! sh1add	%r1,%r26,%r1

x111:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x112:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	! zdep	%r1,27,28,%r1

x113:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_t02a0	!	sh1add	%r1,%r1,%r1

x114:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_2t0	!	sh1add	%r1,%r1,%r1

x115:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_2t0a0	!	sh1add	%r1,%r1,%r1

x116:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_4t0	! sh2add	%r1,%r26,%r1

x117:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	!	sh3add	%r1,%r1,%r1

x118:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_t0a0	!	sh3add	%r1,%r1,%r1

x119:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_t02a0	!	sh3add	%r1,%r1,%r1

x120:	sh2add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x121:	sh2add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sh3add	%r1,%r26,%r1

x122:	sh2add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_2t0	! sh2add	%r1,%r26,%r1

x123:	sh2add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x124: zdep	%r26,26,27,%r1	! sub	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x125:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x126: zdep	%r26,25,26,%r1	! sub	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x127: zdep	%r26,24,25,%r1	! comb,<>	%r25,0,l0	! sub	%r1,%r26,%r1	! b,n	ret_t0

x128: zdep	%r26,24,25,%r1	! comb,<>	%r25,0,l1	!	add	%r29,%r1,%r29	! bv,n  0(r31)

x129: zdep	%r26,24,25,%r1	! comb,<>	%r25,0,l0	! add	%r1,%r26,%r1	! b,n	ret_t0

x130: zdep	%r26,25,26,%r1	! add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x131:	sh3add	%r26,0,%r1		! sh3add	%r1,%r26,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x132:	sh3add	%r26,0,%r1		! sh2add	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x133:	sh3add	%r26,0,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x134:	sh3add	%r26,0,%r1		! sh2add	%r1,%r26,%r1	!	b	e_2t0	! sh1add	%r1,%r26,%r1

x135:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x136:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x137:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	! sh3add	%r1,%r26,%r1

x138:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	!	b	e_2t0	! sh2add	%r1,%r26,%r1

x139:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	! b	e_2t0a0	! sh2add	%r1,%r26,%r1

x140:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_4t0	!	sh2add	%r1,%r1,%r1

x141:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	! b	e_4t0a0	! sh1add	%r1,%r26,%r1

x142:	sh3add	%r26,%r26,%r1		!	sh3add	%r1,0,%r1	!	b	e_2t0	! sub	%r1,%r26,%r1

x143: zdep	%r26,27,28,%r1	!	sh3add	%r1,%r1,%r1	!	b	e_t0	! sub	%r1,%r26,%r1

x144:	sh3add	%r26,%r26,%r1		!	sh3add	%r1,0,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x145:	sh3add	%r26,%r26,%r1		!	sh3add	%r1,0,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x146:	sh3add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x147:	sh3add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x148:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x149:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x150:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_2t0	! sh1add	%r1,%r26,%r1

x151:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_2t0a0	! sh1add	%r1,%r26,%r1

x152:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x153:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	! sh3add	%r1,%r26,%r1

x154:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_2t0	! sh2add	%r1,%r26,%r1

x155: zdep	%r26,26,27,%r1	! sub	%r1,%r26,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x156:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_4t0	! sh1add	%r1,%r26,%r1

x157: zdep	%r26,26,27,%r1	! sub	%r1,%r26,%r1	! b	e_t02a0	!	sh2add	%r1,%r1,%r1

x158: zdep	%r26,27,28,%r1	!	sh2add	%r1,%r1,%r1	!	b	e_2t0	! sub	%r1,%r26,%r1

x159: zdep	%r26,26,27,%r1	!	sh2add	%r1,%r1,%r1	!	b	e_t0	! sub	%r1,%r26,%r1

x160:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,0,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x161:	sh3add	%r26,0,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x162:	sh3add	%r26,%r26,%r1		!	sh3add	%r1,%r1,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x163:	sh3add	%r26,%r26,%r1		!	sh3add	%r1,%r1,%r1	!	b	e_t0	! sh1add	%r1,%r26,%r1

x164:	sh2add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x165:	sh3add	%r26,0,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x166:	sh2add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	!	b	e_2t0	! sh1add	%r1,%r26,%r1

x167:	sh2add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	! b	e_2t0a0	! sh1add	%r1,%r26,%r1

x168:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x169:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	! sh3add	%r1,%r26,%r1

x170: zdep	%r26,26,27,%r1	! sh1add	%r26,%r1,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x171:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_t0	!	sh3add	%r1,%r1,%r1

x172:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_4t0	! sh1add	%r1,%r26,%r1

x173:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_t02a0	!	sh3add	%r1,%r1,%r1

x174: zdep	%r26,26,27,%r1	! sh1add	%r26,%r1,%r1	! b	e_t04a0	!	sh2add	%r1,%r1,%r1

x175:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	!	b	e_5t0	! sh1add	%r1,%r26,%r1

x176:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_8t0	! add	%r1,%r26,%r1

x177:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_8t0a0	! add	%r1,%r26,%r1

x178:	sh2add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_2t0	! sh3add	%r1,%r26,%r1

x179:	sh2add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_2t0a0	! sh3add	%r1,%r26,%r1

x180:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x181:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x182:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_2t0	! sh1add	%r1,%r26,%r1

x183:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_2t0a0	! sh1add	%r1,%r26,%r1

x184:	sh2add	%r26,%r26,%r1		!	sh3add	%r1,%r1,%r1	!	b	e_4t0	! add	%r1,%r26,%r1

x185:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x186: zdep	%r26,26,27,%r1	! sub	%r1,%r26,%r1	!	b	e_2t0	!	sh1add	%r1,%r1,%r1

x187:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_t02a0	!	sh2add	%r1,%r1,%r1

x188:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_4t0	! sh1add	%r26,%r1,%r1

x189:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_t0	!	sh3add	%r1,%r1,%r1

x190:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_2t0	!	sh2add	%r1,%r1,%r1

x191: zdep	%r26,25,26,%r1	!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sub	%r1,%r26,%r1

x192:	sh3add	%r26,0,%r1		!	sh1add	%r1,%r1,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x193:	sh3add	%r26,0,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sh3add	%r1,%r26,%r1

x194:	sh3add	%r26,0,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_2t0	! sh2add	%r1,%r26,%r1

x195:	sh3add	%r26,0,%r1		! sh3add	%r1,%r26,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x196:	sh3add	%r26,0,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_4t0	! sh1add	%r1,%r26,%r1

x197:	sh3add	%r26,0,%r1		!	sh1add	%r1,%r1,%r1	! b	e_4t0a0	! sh1add	%r1,%r26,%r1

x198: zdep	%r26,25,26,%r1	! sh1add	%r26,%r1,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x199:	sh3add	%r26,0,%r1		! sh2add	%r1,%r26,%r1	! b	e_2t0a0	!	sh1add	%r1,%r1,%r1

x200:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x201:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	! sh3add	%r1,%r26,%r1

x202:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_2t0	! sh2add	%r1,%r26,%r1

x203:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_2t0a0	! sh2add	%r1,%r26,%r1

x204:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	!	b	e_4t0	!	sh1add	%r1,%r1,%r1

x205:	sh2add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x206: zdep	%r26,25,26,%r1	! sh2add	%r26,%r1,%r1	! b	e_t02a0	!	sh1add	%r1,%r1,%r1

x207:	sh3add	%r26,0,%r1		! sh1add	%r1,%r26,%r1	!	b	e_3t0	! sh2add	%r1,%r26,%r1

x208:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_8t0	! add	%r1,%r26,%r1

x209:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_8t0a0	! add	%r1,%r26,%r1

x210:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_2t0	!	sh2add	%r1,%r1,%r1

x211:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_2t0a0	!	sh2add	%r1,%r1,%r1

x212:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_4t0	! sh2add	%r1,%r26,%r1

x213:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_4t0a0	! sh2add	%r1,%r26,%r1

x214:	sh3add	%r26,%r26,%r1		! sh2add	%r26,%r1,%r1	! b	e2t04a0	! sh3add	%r1,%r26,%r1

x215:	sh2add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_5t0	! sh1add	%r1,%r26,%r1

x216:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x217:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_t0	! sh3add	%r1,%r26,%r1

x218:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_2t0	! sh2add	%r1,%r26,%r1

x219:	sh3add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x220:	sh1add	%r26,%r26,%r1		!	sh3add	%r1,%r1,%r1	!	b	e_4t0	! sh1add	%r1,%r26,%r1

x221:	sh1add	%r26,%r26,%r1		!	sh3add	%r1,%r1,%r1	! b	e_4t0a0	! sh1add	%r1,%r26,%r1

x222:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_2t0	!	sh1add	%r1,%r1,%r1

x223:	sh3add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_2t0a0	!	sh1add	%r1,%r1,%r1

x224:	sh3add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_8t0	! add	%r1,%r26,%r1

x225:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_t0	!	sh2add	%r1,%r1,%r1

x226:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_t02a0	! zdep	%r1,26,27,%r1

x227:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_t02a0	!	sh2add	%r1,%r1,%r1

x228:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_4t0	!	sh1add	%r1,%r1,%r1

x229:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_4t0a0	!	sh1add	%r1,%r1,%r1

x230:	sh3add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_5t0	! add	%r1,%r26,%r1

x231:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_3t0	! sh2add	%r1,%r26,%r1

x232:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	!	b	e_8t0	! sh2add	%r1,%r26,%r1

x233:	sh1add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e_8t0a0	! sh2add	%r1,%r26,%r1

x234:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	!	b	e_2t0	!	sh3add	%r1,%r1,%r1

x235:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e_2t0a0	!	sh3add	%r1,%r1,%r1

x236:	sh3add	%r26,%r26,%r1		! sh1add	%r1,%r26,%r1	! b	e4t08a0	!	sh1add	%r1,%r1,%r1

x237: zdep	%r26,27,28,%r1	!	sh2add	%r1,%r1,%r1	!	b	e_3t0	! sub	%r1,%r26,%r1

x238:	sh1add	%r26,%r26,%r1		! sh2add	%r1,%r26,%r1	! b	e2t04a0	!	sh3add	%r1,%r1,%r1

x239: zdep	%r26,27,28,%r1	!	sh2add	%r1,%r1,%r1	! b	e_t0ma0	!	sh1add	%r1,%r1,%r1

x240:	sh3add	%r26,%r26,%r1		! add	%r1,%r26,%r1	!	b	e_8t0	!	sh1add	%r1,%r1,%r1

x241:	sh3add	%r26,%r26,%r1		! add	%r1,%r26,%r1	! b	e_8t0a0	!	sh1add	%r1,%r1,%r1

x242:	sh2add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_2t0	! sh3add	%r1,%r26,%r1

x243:	sh3add	%r26,%r26,%r1		!	sh3add	%r1,%r1,%r1	!	b	e_t0	!	sh1add	%r1,%r1,%r1

x244:	sh2add	%r26,%r26,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_4t0	! sh2add	%r1,%r26,%r1

x245:	sh3add	%r26,0,%r1		!	sh1add	%r1,%r1,%r1	!	b	e_5t0	! sh1add	%r1,%r26,%r1

x246:	sh2add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	!	b	e_2t0	!	sh1add	%r1,%r1,%r1

x247:	sh2add	%r26,%r26,%r1		! sh3add	%r1,%r26,%r1	! b	e_2t0a0	!	sh1add	%r1,%r1,%r1

x248: zdep	%r26,26,27,%r1	! sub	%r1,%r26,%r1	! b	e_shift	! sh3add	%r1,%r29,%r29

x249: zdep	%r26,26,27,%r1	! sub	%r1,%r26,%r1	!	b	e_t0	! sh3add	%r1,%r26,%r1

x250:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	!	b	e_2t0	!	sh2add	%r1,%r1,%r1

x251:	sh2add	%r26,%r26,%r1		!	sh2add	%r1,%r1,%r1	! b	e_2t0a0	!	sh2add	%r1,%r1,%r1

x252: zdep	%r26,25,26,%r1	! sub	%r1,%r26,%r1	! b	e_shift	! sh2add	%r1,%r29,%r29

x253: zdep	%r26,25,26,%r1	! sub	%r1,%r26,%r1	!	b	e_t0	! sh2add	%r1,%r26,%r1

x254: zdep	%r26,24,25,%r1	! sub	%r1,%r26,%r1	! b	e_shift	! sh1add	%r1,%r29,%r29

x255: zdep	%r26,23,24,%r1	! comb,<>	%r25,0,l0	! sub	%r1,%r26,%r1	! b,n	ret_t0

;1040 insts before this.
ret_t0: bv	0(r31)

e_t0:	add	%r29,%r1,%r29

e_shift: comb,<>	%r25,0,l2

	zdep	%r26,23,24,%r26	; %r26 <<= 8 ***********
	bv,n  0(r31)
e_t0ma0: comb,<>	%r25,0,l0

	sub	%r1,%r26,%r1
	bv	0(r31)
	add	%r29,%r1,%r29
e_t0a0: comb,<>	%r25,0,l0

	add	%r1,%r26,%r1
	bv	0(r31)
	add	%r29,%r1,%r29
e_t02a0: comb,<>	%r25,0,l0

	sh1add	%r26,%r1,%r1
	bv	0(r31)
	add	%r29,%r1,%r29
e_t04a0: comb,<>	%r25,0,l0

	sh2add	%r26,%r1,%r1
	bv	0(r31)
	add	%r29,%r1,%r29
e_2t0: comb,<>	%r25,0,l1

	sh1add	%r1,%r29,%r29
	bv,n  0(r31)
e_2t0a0: comb,<>	%r25,0,l0

	sh1add	%r1,%r26,%r1
	bv	0(r31)
	add	%r29,%r1,%r29
e2t04a0: sh1add	%r26,%r1,%r1

	comb,<>	%r25,0,l1
	sh1add	%r1,%r29,%r29
	bv,n  0(r31)
e_3t0: comb,<>	%r25,0,l0

	sh1add	%r1,%r1,%r1
	bv	0(r31)
	add	%r29,%r1,%r29
e_4t0: comb,<>	%r25,0,l1

	sh2add	%r1,%r29,%r29
	bv,n  0(r31)
e_4t0a0: comb,<>	%r25,0,l0

	sh2add	%r1,%r26,%r1
	bv	0(r31)
	add	%r29,%r1,%r29
e4t08a0: sh1add	%r26,%r1,%r1

	comb,<>	%r25,0,l1
	sh2add	%r1,%r29,%r29
	bv,n  0(r31)
e_5t0: comb,<>	%r25,0,l0

	sh2add	%r1,%r1,%r1
	bv	0(r31)
	add	%r29,%r1,%r29
e_8t0: comb,<>	%r25,0,l1

	sh3add	%r1,%r29,%r29
	bv,n  0(r31)
e_8t0a0: comb,<>	%r25,0,l0

	sh3add	%r1,%r26,%r1
	bv	0(r31)
	add	%r29,%r1,%r29

	.procend
	.end

	.import $$divI_2,millicode
	.import $$divI_3,millicode
	.import $$divI_4,millicode
	.import $$divI_5,millicode
	.import $$divI_6,millicode
	.import $$divI_7,millicode
	.import $$divI_8,millicode
	.import $$divI_9,millicode
	.import $$divI_10,millicode
	.import $$divI_12,millicode
	.import $$divI_14,millicode
	.import $$divI_15,millicode
	.export $$divI,millicode
	.export	$$divoI,millicode
$$divoI:
	.proc
	.callinfo	NO_CALLS
	comib,=,n  -1,arg1,negative1	; when divisor == -1
$$divI:
	comib,>>=,n 15,arg1,small_divisor
	add,>=	0,arg0,retreg		; move dividend, if retreg < 0,
normal1:
	  sub	0,retreg,retreg		;   make it positive
	sub	0,arg1,temp		; clear carry,
					;   negate the divisor
	ds	0,temp,0		; set V-bit to the comple-
					;   ment of the divisor sign
	add	retreg,retreg,retreg	; shift msb bit into carry
	ds	r0,arg1,temp		; 1st divide step, if no carry
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 2nd divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 3rd divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 4th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 5th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 6th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 7th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 8th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 9th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 10th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 11th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 12th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 13th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 14th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 15th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 16th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 17th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 18th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 19th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 20th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 21st divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 22nd divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 23rd divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 24th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 25th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 26th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 27th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 28th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 29th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 30th divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 31st divide step
	addc	retreg,retreg,retreg	; shift retreg with/into carry
	ds	temp,arg1,temp		; 32nd divide step,
	addc	retreg,retreg,retreg	; shift last retreg bit into retreg
	xor,>=	arg0,arg1,0		; get correct sign of quotient
	  sub	0,retreg,retreg		;   based on operand signs
	bv,n  0(r31)
	nop
;______________________________________________________________________
small_divisor:
	blr,n	arg1,r0
	nop
; table for divisor == 0,1, ... ,15
	addit,=	0,arg1,r0	; trap if divisor == 0
	nop
	bv	0(r31)		; divisor == 1
	copy	arg0,retreg
	 b,n	$$divI_2	; divisor == 2
	nop
	 b,n	$$divI_3	; divisor == 3
	nop
	 b,n	$$divI_4	; divisor == 4
	nop
	 b,n	$$divI_5	; divisor == 5
	nop
	 b,n	$$divI_6	; divisor == 6
	nop
	 b,n	$$divI_7	; divisor == 7
	nop
	 b,n	$$divI_8	; divisor == 8
	nop
	 b,n	$$divI_9	; divisor == 9
	nop
	 b,n	$$divI_10	; divisor == 10
	nop
	b	normal1		; divisor == 11
	add,>=	0,arg0,retreg
	 b,n	$$divI_12	; divisor == 12
	nop
	b	normal1		; divisor == 13
	add,>=	0,arg0,retreg
	 b,n	$$divI_14	; divisor == 14
	nop
	 b,n	$$divI_15	; divisor == 15
	nop
;______________________________________________________________________
negative1:
	sub	0,arg0,retreg	; result is negation of dividend
	bv	0(r31)
	addo	arg0,arg1,r0	; trap iff dividend==0x80000000 && divisor==-1
	.procend
	.end
