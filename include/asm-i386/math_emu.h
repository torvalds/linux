#ifndef _I386_MATH_EMU_H
#define _I386_MATH_EMU_H

#include <asm/sigcontext.h>

int restore_i387_soft(void *s387, struct _fpstate __user *buf);
int save_i387_soft(void *s387, struct _fpstate __user *buf);

/* This structure matches the layout of the data saved to the stack
   following a device-not-present interrupt, part of it saved
   automatically by the 80386/80486.
   */
struct info {
	long ___orig_eip;
	long ___ebx;
	long ___ecx;
	long ___edx;
	long ___esi;
	long ___edi;
	long ___ebp;
	long ___eax;
	long ___ds;
	long ___es;
	long ___orig_eax;
	long ___eip;
	long ___cs;
	long ___eflags;
	long ___esp;
	long ___ss;
	long ___vm86_es; /* This and the following only in vm86 mode */
	long ___vm86_ds;
	long ___vm86_fs;
	long ___vm86_gs;
};
#endif
