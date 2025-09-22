/*	$OpenBSD: libsa.h,v 1.17 2023/02/23 19:48:22 miod Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <lib/libsa/stand.h>

#define	EXEC_ELF
#define	EXEC_SOM

extern dev_t bootdev;

void pdc_init(void);
struct pz_device;
struct pz_device *pdc_findev(int, int);

int iodcstrategy(void *, int, daddr_t, size_t, void *, size_t *);

int ctopen(struct open_file *, ...);
int ctclose(struct open_file *);

int dkopen(struct open_file *, ...);
int dkclose(struct open_file *);

int lfopen(struct open_file *, ...);
int lfstrategy(void *, int, daddr_t, size_t, void *, size_t *);
int lfclose(struct open_file *);

void ite_probe(struct consdev *);
void ite_init(struct consdev *);
int ite_getc(dev_t);
void ite_putc(dev_t, int);
void ite_pollc(dev_t, int);

void machdep(void);
void devboot(dev_t, char *);
void fcacheall(void);
void run_loadfile(uint64_t *marks, int howto);

int lif_open(char *path, struct open_file *f);
int lif_close(struct open_file *f);
int lif_read(struct open_file *f, void *buf, size_t size, size_t *resid);
int lif_write(struct open_file *f, void *buf, size_t size, size_t *resid);
off_t lif_seek(struct open_file *f, off_t offset, int where);
int lif_stat(struct open_file *f, struct stat *sb);
int lif_readdir(struct open_file *f, char *name);

union x_header;
struct x_param;
int som_probe(int, union x_header *);
int som_load(int, struct x_param *);
int som_ldsym(int, struct x_param *);

extern int debug;

#define	MACHINE_CMD	cmd_machine	/* we have hppa specific commands */
