/*
   Per-processor Data Areas
   Jeremy Fitzhardinge <jeremy@goop.org> 2006
   Based on asm-x86_64/pda.h by Andi Kleen.
 */
#ifndef _I386_PDA_H
#define _I386_PDA_H

#include <linux/stddef.h>

struct i386_pda
{
	struct i386_pda *_pda;		/* pointer to self */

	int cpu_number;
};

extern struct i386_pda *_cpu_pda[];

#define cpu_pda(i)	(_cpu_pda[i])

#define pda_offset(field) offsetof(struct i386_pda, field)

extern void __bad_pda_field(void);

/* This variable is never instantiated.  It is only used as a stand-in
   for the real per-cpu PDA memory, so that gcc can understand what
   memory operations the inline asms() below are performing.  This
   eliminates the need to make the asms volatile or have memory
   clobbers, so gcc can readily analyse them. */
extern struct i386_pda _proxy_pda;

#define pda_to_op(op,field,val)						\
	do {								\
		typedef typeof(_proxy_pda.field) T__;			\
		if (0) { T__ tmp__; tmp__ = (val); }			\
		switch (sizeof(_proxy_pda.field)) {			\
		case 1:							\
			asm(op "b %1,%%gs:%c2"				\
			    : "+m" (_proxy_pda.field)			\
			    :"ri" ((T__)val),				\
			     "i"(pda_offset(field)));			\
			break;						\
		case 2:							\
			asm(op "w %1,%%gs:%c2"				\
			    : "+m" (_proxy_pda.field)			\
			    :"ri" ((T__)val),				\
			     "i"(pda_offset(field)));			\
			break;						\
		case 4:							\
			asm(op "l %1,%%gs:%c2"				\
			    : "+m" (_proxy_pda.field)			\
			    :"ri" ((T__)val),				\
			     "i"(pda_offset(field)));			\
			break;						\
		default: __bad_pda_field();				\
		}							\
	} while (0)

#define pda_from_op(op,field)						\
	({								\
		typeof(_proxy_pda.field) ret__;				\
		switch (sizeof(_proxy_pda.field)) {			\
		case 1:							\
			asm(op "b %%gs:%c1,%0"				\
			    : "=r" (ret__)				\
			    : "i" (pda_offset(field)),			\
			      "m" (_proxy_pda.field));			\
			break;						\
		case 2:							\
			asm(op "w %%gs:%c1,%0"				\
			    : "=r" (ret__)				\
			    : "i" (pda_offset(field)),			\
			      "m" (_proxy_pda.field));			\
			break;						\
		case 4:							\
			asm(op "l %%gs:%c1,%0"				\
			    : "=r" (ret__)				\
			    : "i" (pda_offset(field)),			\
			      "m" (_proxy_pda.field));			\
			break;						\
		default: __bad_pda_field();				\
		}							\
		ret__; })

/* Return a pointer to a pda field */
#define pda_addr(field)							\
	((typeof(_proxy_pda.field) *)((unsigned char *)read_pda(_pda) + \
				      pda_offset(field)))

#define read_pda(field) pda_from_op("mov",field)
#define write_pda(field,val) pda_to_op("mov",field,val)
#define add_pda(field,val) pda_to_op("add",field,val)
#define sub_pda(field,val) pda_to_op("sub",field,val)
#define or_pda(field,val) pda_to_op("or",field,val)

#endif	/* _I386_PDA_H */
