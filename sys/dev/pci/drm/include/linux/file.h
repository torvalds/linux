/* Public domain. */

#ifndef _LINUX_FILE_H
#define _LINUX_FILE_H

/* both for printf */
#include <sys/types.h> 
#include <sys/systm.h>

void fd_install(int, struct file *);
void fput(struct file *);

int get_unused_fd_flags(unsigned int);
void put_unused_fd(int);

#endif
