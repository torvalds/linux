/*-
 * Copyright (c) 2013,2014 Juniper Networks, Inc.
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

#ifndef _MKIMG_SCHEME_H_
#define	_MKIMG_SCHEME_H_

enum alias {
	ALIAS_NONE,		/* Keep first! */
	/* start */
	ALIAS_EBR,
	ALIAS_EFI,
	ALIAS_FAT16B,
	ALIAS_FAT32,
	ALIAS_FREEBSD,
	ALIAS_FREEBSD_BOOT,
	ALIAS_FREEBSD_NANDFS,
	ALIAS_FREEBSD_SWAP,
	ALIAS_FREEBSD_UFS,
	ALIAS_FREEBSD_VINUM,
	ALIAS_FREEBSD_ZFS,
	ALIAS_MBR,
	ALIAS_NTFS,
	ALIAS_PPCBOOT,
	/* end */
	ALIAS_COUNT		/* Keep last! */
};

struct mkimg_alias {
	u_int		alias;
	uintptr_t	type;
#define	ALIAS_PTR2TYPE(p)	(uintptr_t)(p)
#define	ALIAS_INT2TYPE(i)	(i)
#define	ALIAS_TYPE2PTR(p)	(void *)(p)
#define	ALIAS_TYPE2INT(i)	(i)
};

struct mkimg_scheme {
	struct mkimg_scheme *next;
	const char	*name;
	const char	*description;
	struct mkimg_alias *aliases;
	lba_t		(*metadata)(u_int, lba_t);
#define	SCHEME_META_IMG_START		1
#define	SCHEME_META_IMG_END		2
#define	SCHEME_META_PART_BEFORE		3
#define	SCHEME_META_PART_AFTER		4
#define	SCHEME_META_PART_ABSOLUTE	5
	int		(*write)(lba_t, void *);
	u_int		nparts;
	u_int		labellen;
	u_int		bootcode;
	u_int		maxsecsz;
};

#define	SCHEME_DEFINE(nm)						\
static void scheme_register_##nm(void) __attribute__((constructor));	\
static void scheme_register_##nm(void) { scheme_register(&nm); }

struct mkimg_scheme *scheme_iterate(struct mkimg_scheme *);
void	scheme_register(struct mkimg_scheme *);
int	scheme_select(const char *);
struct mkimg_scheme *scheme_selected(void);

int scheme_bootcode(int fd);
int scheme_check_part(struct part *);
u_int scheme_max_parts(void);
u_int scheme_max_secsz(void);
lba_t scheme_metadata(u_int, lba_t);
int scheme_write(lba_t);

#endif /* _MKIMG_SCHEME_H_ */
