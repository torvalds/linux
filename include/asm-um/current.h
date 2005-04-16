/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_CURRENT_H
#define __UM_CURRENT_H

#ifndef __ASSEMBLY__

#include "asm/page.h"
#include "linux/thread_info.h"

#define current (current_thread_info()->task)

/*Backward compatibility - it's used inside arch/um.*/
#define current_thread current_thread_info()

#endif /* __ASSEMBLY__ */

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
