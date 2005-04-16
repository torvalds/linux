#ifndef _M68K_CURRENT_H
#define _M68K_CURRENT_H

register struct task_struct *current __asm__("%a2");

#endif /* !(_M68K_CURRENT_H) */
