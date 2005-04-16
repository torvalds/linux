/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_PTRACE_I386_H
#define __UM_PTRACE_I386_H

#include "sysdep/ptrace.h"
#include "asm/ptrace-generic.h"

#define PT_REGS_EAX(r) UPT_EAX(&(r)->regs)
#define PT_REGS_EBX(r) UPT_EBX(&(r)->regs)
#define PT_REGS_ECX(r) UPT_ECX(&(r)->regs)
#define PT_REGS_EDX(r) UPT_EDX(&(r)->regs)
#define PT_REGS_ESI(r) UPT_ESI(&(r)->regs)
#define PT_REGS_EDI(r) UPT_EDI(&(r)->regs)
#define PT_REGS_EBP(r) UPT_EBP(&(r)->regs)

#define PT_REGS_CS(r) UPT_CS(&(r)->regs)
#define PT_REGS_SS(r) UPT_SS(&(r)->regs)
#define PT_REGS_DS(r) UPT_DS(&(r)->regs)
#define PT_REGS_ES(r) UPT_ES(&(r)->regs)
#define PT_REGS_FS(r) UPT_FS(&(r)->regs)
#define PT_REGS_GS(r) UPT_GS(&(r)->regs)

#define PT_REGS_EFLAGS(r) UPT_EFLAGS(&(r)->regs)

#define PT_REGS_ORIG_SYSCALL(r) PT_REGS_EAX(r)
#define PT_REGS_SYSCALL_RET(r) PT_REGS_EAX(r)
#define PT_FIX_EXEC_STACK(sp) do ; while(0)

#define user_mode(r) UPT_IS_USER(&(r)->regs)

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
