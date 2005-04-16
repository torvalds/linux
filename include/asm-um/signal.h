/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_SIGNAL_H
#define __UM_SIGNAL_H

/* Need to kill the do_signal() declaration in the i386 signal.h */

#define do_signal do_signal_renamed
#include "asm/arch/signal.h"
#undef do_signal
#undef ptrace_signal_deliver

#define ptrace_signal_deliver(regs, cookie) do {} while(0)

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
