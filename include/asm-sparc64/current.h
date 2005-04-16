#ifndef _SPARC64_CURRENT_H
#define _SPARC64_CURRENT_H

#include <linux/thread_info.h>

register struct task_struct *current asm("g4");

#endif /* !(_SPARC64_CURRENT_H) */
