/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000 Christoph Herrmann, Thomas-Henning von Kamptz
 * Copyright (c) 1980, 1989, 1993 The Regents of the University of California.
 * All rights reserved.
 * 
 * This code is derived from software contributed to Berkeley by
 * Christoph Herrmann and Thomas-Henning von Kamptz, Munich and Frankfurt.
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
 *    must display the following acknowledgment:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors, as well as Christoph
 *      Herrmann and Thomas-Henning von Kamptz.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $TSHeader: src/sbin/growfs/debug.h,v 1.2 2000/11/16 18:43:50 tom Exp $
 * $FreeBSD$
 *
 */

#ifdef FS_DEBUG

/* ********************************************************** INCLUDES ***** */
#include <sys/param.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

void dbg_open(const char *);
void dbg_close(void);
void dbg_dump_hex(struct fs *, const char *, unsigned char *);
void dbg_dump_fs(struct fs *, const char *);
void dbg_dump_cg(const char *, struct cg *);
void dbg_dump_csum(const char *, struct csum *);
void dbg_dump_csum_total(const char *, struct csum_total *);
void dbg_dump_ufs1_ino(struct fs *, const char *, struct ufs1_dinode *);
void dbg_dump_ufs2_ino(struct fs *, const char *, struct ufs2_dinode *);
void dbg_dump_iblk(struct fs *, const char *, char *, size_t);
void dbg_dump_inmap(struct fs *, const char *, struct cg *);
void dbg_dump_frmap(struct fs *, const char *, struct cg *);
void dbg_dump_clmap(struct fs *, const char *, struct cg *);
void dbg_dump_clsum(struct fs *, const char *, struct cg *);
void dbg_dump_sptbl(struct fs *, const char *, struct cg *);

#define DBG_OPEN(P) dbg_open((P))
#define DBG_CLOSE dbg_close()
#define DBG_DUMP_HEX(F,C,M) dbg_dump_hex((F),(C),(M))
#define DBG_DUMP_FS(F,C) dbg_dump_fs((F),(C))
#define DBG_DUMP_CG(F,C,M) dbg_dump_cg((C),(M))
#define DBG_DUMP_CSUM(F,C,M) dbg_dump_csum((C),(M))
#define DBG_DUMP_INO(F,C,M) (F)->fs_magic == FS_UFS1_MAGIC \
	? dbg_dump_ufs1_ino((F),(C),(struct ufs1_dinode *)(M)) \
	: dbg_dump_ufs2_ino((F),(C),(struct ufs2_dinode *)(M))
#define DBG_DUMP_IBLK(F,C,M,L) dbg_dump_iblk((F),(C),(M),(L))
#define DBG_DUMP_INMAP(F,C,M) dbg_dump_inmap((F),(C),(M))
#define DBG_DUMP_FRMAP(F,C,M) dbg_dump_frmap((F),(C),(M))
#define DBG_DUMP_CLMAP(F,C,M) dbg_dump_clmap((F),(C),(M))
#define DBG_DUMP_CLSUM(F,C,M) dbg_dump_clsum((F),(C),(M))
#ifdef NOT_CURRENTLY
#define DBG_DUMP_SPTBL(F,C,M) dbg_dump_sptbl((F),(C),(M))
#endif

#define DL_TRC	0x01
#define DL_INFO	0x02
extern int _dbg_lvl_;

#define DBG_FUNC(N) char __FKT__[] = {N};
#define DBG_ENTER if(_dbg_lvl_ & DL_TRC) {                                    \
	fprintf(stderr, "~>%s: %s\n", __FILE__, __FKT__ );                    \
	}
#define DBG_LEAVE if(_dbg_lvl_ & DL_TRC) {                                    \
	fprintf(stderr, "~<%s[%d]: %s\n", __FILE__, __LINE__, __FKT__ );      \
	}
#define DBG_TRC if(_dbg_lvl_ & DL_TRC) {                                      \
	fprintf(stderr, "~=%s[%d]: %s\n", __FILE__, __LINE__, __FKT__ );      \
	}
#define DBG_PRINT0(A) if(_dbg_lvl_ & DL_INFO) {                               \
	fprintf(stderr, "~ %s", (A));                                         \
	}
#define DBG_PRINT1(A,B) if(_dbg_lvl_ & DL_INFO) {                             \
	fprintf(stderr, "~ ");                                                \
	fprintf(stderr, (A), (B));                                            \
	}
#define DBG_PRINT2(A,B,C) if(_dbg_lvl_ & DL_INFO) {                           \
	fprintf(stderr, "~ ");                                                \
	fprintf(stderr, (A), (B), (C));                                       \
	}
#define DBG_PRINT3(A,B,C,D) if(_dbg_lvl_ & DL_INFO) {                         \
	fprintf(stderr, "~ ");                                                \
	fprintf(stderr, (A), (B), (C), (D));                                  \
	}
#define DBG_PRINT4(A,B,C,D,E) if(_dbg_lvl_ & DL_INFO) {                       \
	fprintf(stderr, "~ ");                                                \
	fprintf(stderr, (A), (B), (C), (D), (E));                             \
	}
#else /* not FS_DEBUG */

#define DBG_OPEN(P)
#define DBG_CLOSE
#define DBG_DUMP_HEX(F,C,M)
#define DBG_DUMP_FS(F,C)
#define DBG_DUMP_CG(F,C,M)
#define DBG_DUMP_CSUM(F,C,M)
#define DBG_DUMP_INO(F,C,M)
#define DBG_DUMP_IBLK(F,C,M,L)
#define DBG_DUMP_INMAP(F,C,M)
#define DBG_DUMP_FRMAP(F,C,M)
#define DBG_DUMP_CLMAP(F,C,M)
#define DBG_DUMP_CLSUM(F,C,M)
#define DBG_DUMP_SPTBL(F,C,M)
#define DBG_FUNC(N)
#define DBG_ENTER
#define DBG_TRC
#define DBG_LEAVE
#define DBG_PRINT0(A)
#define DBG_PRINT1(A,B)
#define DBG_PRINT2(A,B,C)
#define DBG_PRINT3(A,B,C,D)
#define DBG_PRINT4(A,B,C,D,E)

#endif /* FS_DEBUG */
