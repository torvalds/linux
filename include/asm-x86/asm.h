#ifndef _ASM_X86_ASM_H
#define _ASM_X86_ASM_H

#ifdef CONFIG_X86_32
/* 32 bits */

# define _ASM_PTR	" .long "
# define _ASM_ALIGN	" .balign 4 "
# define _ASM_MOV_UL	" movl "

# define _ASM_INC	" incl "
# define _ASM_DEC	" decl "
# define _ASM_ADD	" addl "
# define _ASM_SUB	" subl "
# define _ASM_XADD	" xaddl "

#else
/* 64 bits */

# define _ASM_PTR	" .quad "
# define _ASM_ALIGN	" .balign 8 "
# define _ASM_MOV_UL	" movq "

# define _ASM_INC	" incq "
# define _ASM_DEC	" decq "
# define _ASM_ADD	" addq "
# define _ASM_SUB	" subq "
# define _ASM_XADD	" xaddq "

#endif /* CONFIG_X86_32 */

#endif /* _ASM_X86_ASM_H */
