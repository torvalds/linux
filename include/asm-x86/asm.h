#ifndef _ASM_X86_ASM_H
#define _ASM_X86_ASM_H

#ifdef __ASSEMBLY__
# define __ASM_FORM(x)	x
#else
# define __ASM_FORM(x)	" " #x " "
#endif

#ifdef CONFIG_X86_32
# define __ASM_SEL(a,b) __ASM_FORM(a)
#else
# define __ASM_SEL(a,b) __ASM_FORM(b)
#endif

#define __ASM_SIZE(inst)	__ASM_SEL(inst##l, inst##q)

#define _ASM_PTR	__ASM_SEL(.long, .quad)
#define _ASM_ALIGN	__ASM_SEL(.balign 4, .balign 8)
#define _ASM_MOV_UL	__ASM_SIZE(mov)

#define _ASM_INC	__ASM_SIZE(inc)
#define _ASM_DEC	__ASM_SIZE(dec)
#define _ASM_ADD	__ASM_SIZE(add)
#define _ASM_SUB	__ASM_SIZE(sub)
#define _ASM_XADD	__ASM_SIZE(xadd)

/* Exception table entry */
# define _ASM_EXTABLE(from,to) \
	" .section __ex_table,\"a\"\n" \
	_ASM_ALIGN "\n" \
	_ASM_PTR #from "," #to "\n" \
	" .previous\n"

#endif /* _ASM_X86_ASM_H */
