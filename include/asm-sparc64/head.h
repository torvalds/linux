/* $Id: head.h,v 1.30 1997/08/08 08:34:33 jj Exp $ */
#ifndef _SPARC64_HEAD_H
#define _SPARC64_HEAD_H

#include <asm/pstate.h>

#define KERNBASE	0x400000

#define	PTREGS_OFF	(STACK_BIAS + STACKFRAME_SZ)

#define __CHEETAH_ID	0x003e0014
#define __JALAPENO_ID	0x003e0016

#define CHEETAH_MANUF		0x003e
#define CHEETAH_IMPL		0x0014
#define CHEETAH_PLUS_IMPL	0x0015
#define JALAPENO_IMPL		0x0016

#define BRANCH_IF_CHEETAH_BASE(tmp1,tmp2,label)	\
	rdpr	%ver, %tmp1;			\
	sethi	%hi(__CHEETAH_ID), %tmp2;	\
	srlx	%tmp1, 32, %tmp1;		\
	or	%tmp2, %lo(__CHEETAH_ID), %tmp2;\
	cmp	%tmp1, %tmp2;			\
	be,pn	%icc, label;			\
	 nop;

#define BRANCH_IF_JALAPENO(tmp1,tmp2,label)	\
	rdpr	%ver, %tmp1;			\
	sethi	%hi(__JALAPENO_ID), %tmp2;	\
	srlx	%tmp1, 32, %tmp1;		\
	or	%tmp2, %lo(__JALAPENO_ID), %tmp2;\
	cmp	%tmp1, %tmp2;			\
	be,pn	%icc, label;			\
	 nop;

#define BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(tmp1,tmp2,label)	\
	rdpr	%ver, %tmp1;			\
	srlx	%tmp1, (32 + 16), %tmp2;	\
	cmp	%tmp2, CHEETAH_MANUF;		\
	bne,pt	%xcc, 99f;			\
	 sllx	%tmp1, 16, %tmp1;		\
	srlx	%tmp1, (32 + 16), %tmp2;	\
	cmp	%tmp2, CHEETAH_PLUS_IMPL;	\
	bgeu,pt	%xcc, label;			\
99:	 nop;

#define BRANCH_IF_ANY_CHEETAH(tmp1,tmp2,label)	\
	rdpr	%ver, %tmp1;			\
	srlx	%tmp1, (32 + 16), %tmp2;	\
	cmp	%tmp2, CHEETAH_MANUF;		\
	bne,pt	%xcc, 99f;			\
	 sllx	%tmp1, 16, %tmp1;		\
	srlx	%tmp1, (32 + 16), %tmp2;	\
	cmp	%tmp2, CHEETAH_IMPL;		\
	bgeu,pt	%xcc, label;			\
99:	 nop;

#endif /* !(_SPARC64_HEAD_H) */
