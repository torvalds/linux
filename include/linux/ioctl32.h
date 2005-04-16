#ifndef IOCTL32_H
#define IOCTL32_H 1

struct file;

typedef int (*ioctl_trans_handler_t)(unsigned int, unsigned int,
					unsigned long, struct file *);

struct ioctl_trans {
	unsigned long cmd;
	ioctl_trans_handler_t handler;
	struct ioctl_trans *next;
};

/* 
 * Register an 32bit ioctl translation handler for ioctl cmd.
 *
 * handler == NULL: use 64bit ioctl handler.
 * arguments to handler:  fd: file descriptor
 *                        cmd: ioctl command.
 *                        arg: ioctl argument
 *                        struct file *file: file descriptor pointer.
 */ 

#ifdef CONFIG_COMPAT
extern int register_ioctl32_conversion(unsigned int cmd,
				ioctl_trans_handler_t handler);
extern int unregister_ioctl32_conversion(unsigned int cmd);

#else

#define register_ioctl32_conversion(cmd, handler)	({ 0; })
#define unregister_ioctl32_conversion(cmd)		({ 0; })

#endif

#endif
