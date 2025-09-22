/* apple_proto.h */
/* $OpenBSD: apple_proto.h,v 1.1 2008/03/08 15:36:12 espie Exp $ */
/*
 * Copyright (c) 2008 Marc Espie <espie@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef APPLE_PROTO_H
#define APPLE_PROTO_H

struct directory_entry;
struct hfs_info;

extern void hfs_init(char *, unsigned short, int, int, unsigned int);
extern void clean_hfs(void);
extern int hfs_exclude(char *);
extern int get_hfs_rname(char *, char *, char *);
extern int get_hfs_dir(char *, char *, struct directory_entry *);
extern int get_hfs_info(char *, char *, struct directory_entry *);
extern void print_hfs_info(struct directory_entry *);
extern void delete_rsrc_ent(struct directory_entry *);
extern void del_hfs_info(struct hfs_info *);

#endif
