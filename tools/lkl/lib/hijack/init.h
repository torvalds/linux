#ifndef _LKL_HIJACK_INIT_H
#define _LKL_HIJACK_INIT_H

extern int lkl_running;
extern int dual_fds[];

void __hijack_init(void);
void __hijack_fini(void);

/*
 * lkl_register_dbg_handler- register a signal handler that loads a debug lib.
 *
 * The signal handler is triggered by Ctrl-Z. It creates a new pthread which
 * call dbg_entrance().
 *
 * If you run the program from shell script, make sure you ignore SIGTSTP by
 * "trap '' TSTP" in the shell script.
 */
void lkl_register_dbg_handler(void);

#endif /*_LKL_HIJACK_INIT_H */
