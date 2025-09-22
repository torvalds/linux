/*	$OpenBSD: i386_installboot.h,v 1.8 2025/02/19 21:30:46 kettenis Exp $	*/
/*
 * Copyright (c) 2011 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2003 Tom Cosgrove <tom.cosgrove@arches-consulting.com>
 * Copyright (c) 1997 Michael Shalayeff
 * Copyright (c) 1994 Paul Kranenburg
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Paul Kranenburg.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define INODESEG        0x07e0  /* where we will put /boot's inode's block */
#define BOOTSEG         0x07c0  /* biosboot loaded here */

#define INODEOFF  ((INODESEG-BOOTSEG) << 4)

#define SR_FS_BLOCKSIZE (16 * 1024)

struct  sym_data {
	char		*sym_name;		/* Must be initialised */
	int		sym_size;		/* And this one */
	int		sym_set;		/* Rest set at runtime */
	u_int32_t	sym_value;
};

extern struct sym_data pbr_symbols[];

struct nlist;

int	nlist_elf32(const char *, struct nlist *);
void	pbr_set_symbols(char *, char *, struct sym_data *);
void	sym_set_value(struct sym_data *, char *, u_int32_t);
void	write_bootblocks(int, char *, struct disklabel *);
int	findgptefisys(int, struct disklabel *, int *,
	    struct gpt_partition *);
int	create_filesystem(struct disklabel *, char);
void	write_filesystem(struct disklabel *, char, int,
	    struct gpt_partition *);
