#ifndef _LKL_HIJACK_INIT_H
#define _LKL_HIJACK_INIT_H

extern int lkl_running;
extern int dual_fds[];

void __hijack_init(void);
void __hijack_fini(void);

#endif /*_LKL_HIJACK_INIT_H */
