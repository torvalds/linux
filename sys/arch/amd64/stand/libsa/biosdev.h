/*	$OpenBSD: biosdev.h,v 1.6 2020/12/09 18:10:18 krw Exp $	*/

/*
 * Copyright (c) 1996 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Extension support bitmap definition (returned by 41h)
 */
#define EXT_BM_EDA	0x01	/* Extended disk access functions	*/
				/*  (42h-44h, 47h and 48h) supported.	*/
#define EXT_BM_RDC	0x02	/* Removable drive controller functions	*/
				/*  (45h, 46h, 48h, 49h and INT 15 52h)	*/
				/*  supported.				*/
#define EXT_BM_EDD	0x04	/* Enhanced disk drive functions	*/
				/*  (48h and 4eh) supported.		*/
#define EXT_BM_RSV	0xf8	/* Reserved (0)				*/

struct consdev;
struct open_file;
struct diskinfo;

/* biosdev.c */
extern const char *biosdevs[];
int biosstrategy(void *, int, daddr_t, size_t, void *, size_t *);
int biosopen(struct open_file *, ...);
int biosclose(struct open_file *);
int biosioctl(struct open_file *, u_long, void *);
int bios_getdiskinfo(int, bios_diskinfo_t *);
int biosd_diskio(int, struct diskinfo *, u_int, int, void *);
const char * bios_getdisklabel(bios_diskinfo_t *, struct disklabel *);

/* diskprobe.c */
struct diskinfo *dklookup(int);
bios_diskinfo_t *bios_dklookup(int);

/* bioscons.c */
void pc_probe(struct consdev *);
void pc_init(struct consdev *);
int pc_getc(dev_t);
int pc_getshifts(dev_t);
void pc_putc(dev_t, int);
void pc_pollc(dev_t, int);
void com_probe(struct consdev *);
void com_init(struct consdev *);
int comspeed(dev_t, int);
int com_getc(dev_t);
void com_putc(dev_t, int);
void com_pollc(dev_t, int);
