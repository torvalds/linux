#ifndef _FPU32_H
#define _FPU32_H 1

struct _fpstate_ia32;

int restore_i387_ia32(struct task_struct *tsk, struct _fpstate_ia32 __user *buf, int fsave);
int save_i387_ia32(struct task_struct *tsk, struct _fpstate_ia32 __user *buf, 
		   struct pt_regs *regs, int fsave);

#endif
