#ifndef _ASM_I386_UNWIND_H
#define _ASM_I386_UNWIND_H

#define UNW_PC(frame) ((void)(frame), 0)
#define UNW_SP(frame) ((void)(frame), 0)
#define UNW_FP(frame) ((void)(frame), 0)

static inline int arch_unw_user_mode(const void *info)
{
	return 0;
}

#endif /* _ASM_I386_UNWIND_H */
