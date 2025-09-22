/*	$OpenBSD: libsa.h,v 1.3 2023/03/13 11:59:39 aoyama Exp $	*/

/* public domain */

#include <lib/libsa/stand.h>

void devboot(dev_t, char *);
void machdep(void);
void run_loadfile(uint64_t *, int);

#define	MACHINE_CMD	cmd_machine	/* we have luna88k-specific commands */
