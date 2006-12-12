#ifndef _SH_SETUP_H
#define _SH_SETUP_H

#define COMMAND_LINE_SIZE 256

#ifdef __KERNEL__

int setup_early_printk(char *);

#endif /* __KERNEL__ */

#endif /* _SH_SETUP_H */
