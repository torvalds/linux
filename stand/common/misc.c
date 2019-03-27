/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <string.h>
#include <stand.h>
#include <bootstrap.h>

/*
 * Concatenate the (argc) elements of (argv) into a single string, and return
 * a copy of same.
 */
char *
unargv(int argc, char *argv[])
{
    size_t	hlong;
    int		i;
    char	*cp;

    for (i = 0, hlong = 0; i < argc; i++)
	hlong += strlen(argv[i]) + 2;

    if(hlong == 0)
	return(NULL);

    cp = malloc(hlong);
    cp[0] = 0;
    for (i = 0; i < argc; i++) {
	strcat(cp, argv[i]);
	if (i < (argc - 1))
	  strcat(cp, " ");
    }
	  
    return(cp);
}

/*
 * Get the length of a string in kernel space
 */
size_t
strlenout(vm_offset_t src)
{
    char	c;
    size_t	len;
    
    for (len = 0; ; len++) {
	archsw.arch_copyout(src++, &c, 1);
	if (c == 0)
	    break;
    }
    return(len);
}

/*
 * Make a duplicate copy of a string in kernel space
 */
char *
strdupout(vm_offset_t str)
{
    char	*result, *cp;
    
    result = malloc(strlenout(str) + 1);
    for (cp = result; ;cp++) {
	archsw.arch_copyout(str++, cp, 1);
	if (*cp == 0)
	    break;
    }
    return(result);
}

/* Zero a region in kernel space. */
void
kern_bzero(vm_offset_t dest, size_t len)
{
	char buf[256];
	size_t chunk, resid;

	bzero(buf, sizeof(buf));
	resid = len;
	while (resid > 0) {
		chunk = min(sizeof(buf), resid);
		archsw.arch_copyin(buf, dest, chunk);
		resid -= chunk;
		dest += chunk;
	}
}

/*
 * Read the specified part of a file to kernel space.  Unlike regular
 * pread, the file pointer is advanced to the end of the read data,
 * and it just returns 0 if successful.
 */
int
kern_pread(int fd, vm_offset_t dest, size_t len, off_t off)
{

	if (lseek(fd, off, SEEK_SET) == -1) {
#ifdef DEBUG
		printf("\nlseek failed\n");
#endif
		return (-1);
	}
	if ((size_t)archsw.arch_readin(fd, dest, len) != len) {
#ifdef DEBUG
		printf("\nreadin failed\n");
#endif
		return (-1);
	}
	return (0);
}

/*
 * Read the specified part of a file to a malloced buffer.  The file
 * pointer is advanced to the end of the read data.
 */
/* coverity[ -tainted_data_return ] */
void *
alloc_pread(int fd, off_t off, size_t len)
{
	void *buf;

	buf = malloc(len);
	if (buf == NULL) {
#ifdef DEBUG
		printf("\nmalloc(%d) failed\n", (int)len);
#endif
		return (NULL);
	}
	if (lseek(fd, off, SEEK_SET) == -1) {
#ifdef DEBUG
		printf("\nlseek failed\n");
#endif
		free(buf);
		return (NULL);
	}
	if ((size_t)read(fd, buf, len) != len) {
#ifdef DEBUG
		printf("\nread failed\n");
#endif
		free(buf);
		return (NULL);
	}
	return (buf);
}

/*
 * Display a region in traditional hexdump format.
 */
void
hexdump(caddr_t region, size_t len)
{
    caddr_t	line;
    int		x, c;
    char	lbuf[80];
#define emit(fmt, args...)	{sprintf(lbuf, fmt , ## args); pager_output(lbuf);}

    pager_open();
    for (line = region; line < (region + len); line += 16) {
	emit("%08lx  ", (long) line);
	
	for (x = 0; x < 16; x++) {
	    if ((line + x) < (region + len)) {
		emit("%02x ", *(uint8_t *)(line + x));
	    } else {
		emit("-- ");
	    }
	    if (x == 7)
		emit(" ");
	}
	emit(" |");
	for (x = 0; x < 16; x++) {
	    if ((line + x) < (region + len)) {
		c = *(uint8_t *)(line + x);
		if ((c < ' ') || (c > '~'))	/* !isprint(c) */
		    c = '.';
		emit("%c", c);
	    } else {
		emit(" ");
	    }
	}
	emit("|\n");
    }
    pager_close();
}

void
dev_cleanup(void)
{
    int		i;

    /* Call cleanup routines */
    for (i = 0; devsw[i] != NULL; ++i)
	if (devsw[i]->dv_cleanup != NULL)
	    (devsw[i]->dv_cleanup)();
}
