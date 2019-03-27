/*-
 * Copyright (c) 2011 Google, Inc.
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
 *
 * $FreeBSD$
 */

#include "userboot.h"

extern struct loader_callbacks *callbacks;
extern void *callbacks_arg;

#define	CALLBACK(fn, args...) (callbacks->fn(callbacks_arg , ##args))

#define	MAXDEV	31	/* maximum number of distinct devices */

typedef unsigned long physaddr_t;

/* exported devices */
extern struct devsw userboot_disk;
extern int userboot_disk_maxunit;
extern struct devsw host_dev;

/* access to host filesystem */
struct fs_ops host_fsops;

struct bootinfo;
struct preloaded_file;
extern int		bi_load(struct bootinfo *, struct preloaded_file *);

extern void delay(int);

extern int userboot_autoload(void);
extern ssize_t userboot_copyin(const void *, vm_offset_t, size_t);
extern ssize_t userboot_copyout(vm_offset_t, void *, size_t);
extern ssize_t userboot_readin(int, vm_offset_t, size_t);
extern int userboot_getdev(void **, const char *, const char **);
char	*userboot_fmtdev(void *vdev);
int	userboot_setcurrdev(struct env_var *ev, int flags, const void *value);

int	bi_getboothowto(char *kargs);
void	bi_setboothowto(int howto);
vm_offset_t	bi_copyenv(vm_offset_t addr);
int	bi_load32(char *args, int *howtop, int *bootdevp, vm_offset_t *bip,
    vm_offset_t *modulep, vm_offset_t *kernend);
int	bi_load64(char *args, vm_offset_t *modulep, vm_offset_t *kernend);
void	bios_addsmapdata(struct preloaded_file *kfp);
