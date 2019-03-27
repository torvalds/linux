/*-
 * Copyright (c) 2003 Poul-Henning Kamp
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

#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "mtree.h"
#include "extern.h"

#define FF(a, b, c, d) \
	(((a)->flags & (c)) && ((b)->flags & (c)) && ((a)->d) != ((b)->d))
#define FS(a, b, c, d) \
	(((a)->flags & (c)) && ((b)->flags & (c)) && strcmp((a)->d,(b)->d))
#define FM(a, b, c, d) \
	(((a)->flags & (c)) && ((b)->flags & (c)) && memcmp(&(a)->d,&(b)->d, sizeof (a)->d))

static void
shownode(NODE *n, int f, char const *path)
{
	struct group *gr;
	struct passwd *pw;

	printf("%s%s %s", path, n->name, ftype(n->type));
	if (f & F_CKSUM)
		printf(" cksum=%lu", n->cksum);
	if (f & F_GID)
		printf(" gid=%d", n->st_gid);
	if (f & F_GNAME) {
		gr = getgrgid(n->st_gid);
		if (gr == NULL)
			printf(" gid=%d", n->st_gid);
		else
			printf(" gname=%s", gr->gr_name);
	}
	if (f & F_MODE)
		printf(" mode=%o", n->st_mode);
	if (f & F_NLINK)
		printf(" nlink=%ju", (uintmax_t)n->st_nlink);
	if (f & F_SIZE)
		printf(" size=%jd", (intmax_t)n->st_size);
	if (f & F_UID)
		printf(" uid=%d", n->st_uid);
	if (f & F_UNAME) {
		pw = getpwuid(n->st_uid);
		if (pw == NULL)
			printf(" uid=%d", n->st_uid);
		else
			printf(" uname=%s", pw->pw_name);
	}
	if (f & F_MD5)
		printf(" md5digest=%s", n->md5digest);
	if (f & F_SHA1)
		printf(" sha1digest=%s", n->sha1digest);
	if (f & F_RMD160)
		printf(" rmd160digest=%s", n->rmd160digest);
	if (f & F_SHA256)
		printf(" sha256digest=%s", n->sha256digest);
	if (f & F_FLAGS)
		printf(" flags=%s", flags_to_string(n->st_flags));
	printf("\n");
}

static int
mismatch(NODE *n1, NODE *n2, int differ, char const *path)
{

	if (n2 == NULL) {
		shownode(n1, differ, path);
		return (1);
	}
	if (n1 == NULL) {
		printf("\t");
		shownode(n2, differ, path);
		return (1);
	}
	if (!(differ & keys))
		return(0);
	printf("\t\t");
	shownode(n1, differ, path);
	printf("\t\t");
	shownode(n2, differ, path);
	return (1);
}

static int
compare_nodes(NODE *n1, NODE *n2, char const *path)
{
	int differs;
	
	if (n1 != NULL && n1->type == F_LINK)
		n1->flags &= ~F_MODE;
	if (n2 != NULL && n2->type == F_LINK)
		n2->flags &= ~F_MODE;
	differs = 0;
	if (n1 == NULL && n2 != NULL) {
		differs = n2->flags;
		mismatch(n1, n2, differs, path);
		return (1);
	}
	if (n1 != NULL && n2 == NULL) {
		differs = n1->flags;
		mismatch(n1, n2, differs, path);
		return (1);
	}
	if (n1->type != n2->type) {
		differs = 0;
		mismatch(n1, n2, differs, path);
		return (1);
	}
	if (FF(n1, n2, F_CKSUM, cksum))
		differs |= F_CKSUM;
	if (FF(n1, n2, F_GID, st_gid))
		differs |= F_GID;
	if (FF(n1, n2, F_GNAME, st_gid))
		differs |= F_GNAME;
	if (FF(n1, n2, F_MODE, st_mode))
		differs |= F_MODE;
	if (FF(n1, n2, F_NLINK, st_nlink))
		differs |= F_NLINK;
	if (FF(n1, n2, F_SIZE, st_size))
		differs |= F_SIZE;
	if (FS(n1, n2, F_SLINK, slink))
		differs |= F_SLINK;
	if (FM(n1, n2, F_TIME, st_mtimespec))
		differs |= F_TIME;
	if (FF(n1, n2, F_UID, st_uid))
		differs |= F_UID;
	if (FF(n1, n2, F_UNAME, st_uid))
		differs |= F_UNAME;
	if (FS(n1, n2, F_MD5, md5digest))
		differs |= F_MD5;
	if (FS(n1, n2, F_SHA1, sha1digest))
		differs |= F_SHA1;
	if (FS(n1, n2, F_RMD160, rmd160digest))
		differs |= F_RMD160;
	if (FS(n1, n2, F_SHA256, sha256digest))
		differs |= F_SHA256;
	if (FF(n1, n2, F_FLAGS, st_flags))
		differs |= F_FLAGS;
	if (differs) {
		mismatch(n1, n2, differs, path);
		return (1);
	}
	return (0);	
}
static int
walk_in_the_forest(NODE *t1, NODE *t2, char const *path)
{
	int r, i;
	NODE *c1, *c2, *n1, *n2;
	char *np;

	r = 0;

	if (t1 != NULL)
		c1 = t1->child;
	else
		c1 = NULL;
	if (t2 != NULL)
		c2 = t2->child;
	else
		c2 = NULL;
	while (c1 != NULL || c2 != NULL) {
		n1 = n2 = NULL;
		if (c1 != NULL)
			n1 = c1->next;
		if (c2 != NULL)
			n2 = c2->next;
		if (c1 != NULL && c2 != NULL) {
			if (c1->type != F_DIR && c2->type == F_DIR) {
				n2 = c2;
				c2 = NULL;
			} else if (c1->type == F_DIR && c2->type != F_DIR) {
				n1 = c1;
				c1 = NULL;
			} else {
				i = strcmp(c1->name, c2->name);
				if (i > 0) {
					n1 = c1;
					c1 = NULL;
				} else if (i < 0) {
					n2 = c2;
					c2 = NULL;
				}
			}
		}
		if (c1 == NULL && c2->type == F_DIR) {
			asprintf(&np, "%s%s/", path, c2->name);
			i = walk_in_the_forest(c1, c2, np);
			free(np);
			i += compare_nodes(c1, c2, path);
		} else if (c2 == NULL && c1->type == F_DIR) {
			asprintf(&np, "%s%s/", path, c1->name);
			i = walk_in_the_forest(c1, c2, np);
			free(np);
			i += compare_nodes(c1, c2, path);
		} else if (c1 == NULL || c2 == NULL) {
			i = compare_nodes(c1, c2, path);
		} else if (c1->type == F_DIR && c2->type == F_DIR) {
			asprintf(&np, "%s%s/", path, c1->name);
			i = walk_in_the_forest(c1, c2, np);
			free(np);
			i += compare_nodes(c1, c2, path);
		} else {
			i = compare_nodes(c1, c2, path);
		}
		r += i;
		c1 = n1;
		c2 = n2;
	}
	return (r);	
}

int
mtree_specspec(FILE *fi, FILE *fj)
{
	int rval;
	NODE *root1, *root2;

	root1 = mtree_readspec(fi);
	root2 = mtree_readspec(fj);
	rval = walk_in_the_forest(root1, root2, "");
	rval += compare_nodes(root1, root2, "");
	if (rval > 0)
		return (MISMATCHEXIT);
	return (0);
}
