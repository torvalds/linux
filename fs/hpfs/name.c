// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/name.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  operations with filenames
 */

#include "hpfs_fn.h"

static inline int not_allowed_char(unsigned char c)
{
	return c<' ' || c=='"' || c=='*' || c=='/' || c==':' || c=='<' ||
	      c=='>' || c=='?' || c=='\\' || c=='|';
}

static inline int no_dos_char(unsigned char c)
{	/* Characters that are allowed in HPFS but not in DOS */
	return c=='+' || c==',' || c==';' || c=='=' || c=='[' || c==']';
}

static inline unsigned char upcase(unsigned char *dir, unsigned char a)
{
	if (a<128 || a==255) return a>='a' && a<='z' ? a - 0x20 : a;
	if (!dir) return a;
	return dir[a-128];
}

unsigned char hpfs_upcase(unsigned char *dir, unsigned char a)
{
	return upcase(dir, a);
}

static inline unsigned char locase(unsigned char *dir, unsigned char a)
{
	if (a<128 || a==255) return a>='A' && a<='Z' ? a + 0x20 : a;
	if (!dir) return a;
	return dir[a];
}

int hpfs_chk_name(const unsigned char *name, unsigned *len)
{
	int i;
	if (*len > 254) return -ENAMETOOLONG;
	hpfs_adjust_length(name, len);
	if (!*len) return -EINVAL;
	for (i = 0; i < *len; i++) if (not_allowed_char(name[i])) return -EINVAL;
	if (*len == 1) if (name[0] == '.') return -EINVAL;
	if (*len == 2) if (name[0] == '.' && name[1] == '.') return -EINVAL;
	return 0;
}

unsigned char *hpfs_translate_name(struct super_block *s, unsigned char *from,
			  unsigned len, int lc, int lng)
{
	unsigned char *to;
	int i;
	if (hpfs_sb(s)->sb_chk >= 2) if (hpfs_is_name_long(from, len) != lng) {
		pr_err("Long name flag mismatch - name ");
		for (i = 0; i < len; i++)
			pr_cont("%c", from[i]);
		pr_cont(" misidentified as %s.\n", lng ? "short" : "long");
		pr_err("It's nothing serious. It could happen because of bug in OS/2.\nSet checks=normal to disable this message.\n");
	}
	if (!lc) return from;
	if (!(to = kmalloc(len, GFP_KERNEL))) {
		pr_err("can't allocate memory for name conversion buffer\n");
		return from;
	}
	for (i = 0; i < len; i++) to[i] = locase(hpfs_sb(s)->sb_cp_table,from[i]);
	return to;
}

int hpfs_compare_names(struct super_block *s,
		       const unsigned char *n1, unsigned l1,
		       const unsigned char *n2, unsigned l2, int last)
{
	unsigned l = l1 < l2 ? l1 : l2;
	unsigned i;
	if (last) return -1;
	for (i = 0; i < l; i++) {
		unsigned char c1 = upcase(hpfs_sb(s)->sb_cp_table,n1[i]);
		unsigned char c2 = upcase(hpfs_sb(s)->sb_cp_table,n2[i]);
		if (c1 < c2) return -1;
		if (c1 > c2) return 1;
	}
	if (l1 < l2) return -1;
	if (l1 > l2) return 1;
	return 0;
}

int hpfs_is_name_long(const unsigned char *name, unsigned len)
{
	int i,j;
	for (i = 0; i < len && name[i] != '.'; i++)
		if (no_dos_char(name[i])) return 1;
	if (!i || i > 8) return 1;
	if (i == len) return 0;
	for (j = i + 1; j < len; j++)
		if (name[j] == '.' || no_dos_char(name[i])) return 1;
	return j - i > 4;
}

/* OS/2 clears dots and spaces at the end of file name, so we have to */

void hpfs_adjust_length(const unsigned char *name, unsigned *len)
{
	if (!*len) return;
	if (*len == 1 && name[0] == '.') return;
	if (*len == 2 && name[0] == '.' && name[1] == '.') return;
	while (*len && (name[*len - 1] == '.' || name[*len - 1] == ' '))
		(*len)--;
}
