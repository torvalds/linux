/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Marcel Moolenaar
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <util.h>
#include <vis.h>

#include "makefs.h"

#ifndef ENOATTR
#define	ENOATTR	ENODATA
#endif

#define	IS_DOT(nm)	((nm)[0] == '.' && (nm)[1] == '\0')
#define	IS_DOTDOT(nm)	((nm)[0] == '.' && (nm)[1] == '.' && (nm)[2] == '\0')

struct mtree_fileinfo {
	SLIST_ENTRY(mtree_fileinfo) next;
	FILE *fp;
	const char *name;
	u_int line;
};

/* Global state used while parsing. */
static SLIST_HEAD(, mtree_fileinfo) mtree_fileinfo =
    SLIST_HEAD_INITIALIZER(mtree_fileinfo);
static fsnode *mtree_root;
static fsnode *mtree_current;
static fsnode mtree_global;
static fsinode mtree_global_inode;
static u_int errors, warnings;

static void mtree_error(const char *, ...) __printflike(1, 2);
static void mtree_warning(const char *, ...) __printflike(1, 2);

static int
mtree_file_push(const char *name, FILE *fp)
{
	struct mtree_fileinfo *fi;

	fi = emalloc(sizeof(*fi));
	if (strcmp(name, "-") == 0)
		fi->name = estrdup("(stdin)");
	else
		fi->name = estrdup(name);
	if (fi->name == NULL) {
		free(fi);
		return (ENOMEM);
	}

	fi->fp = fp;
	fi->line = 0;

	SLIST_INSERT_HEAD(&mtree_fileinfo, fi, next);
	return (0);
}

static void
mtree_print(const char *msgtype, const char *fmt, va_list ap)
{
	struct mtree_fileinfo *fi;

	if (msgtype != NULL) {
		fi = SLIST_FIRST(&mtree_fileinfo);
		if (fi != NULL)
			fprintf(stderr, "%s:%u: ", fi->name, fi->line);
		fprintf(stderr, "%s: ", msgtype);
	}
	vfprintf(stderr, fmt, ap);
}

static void
mtree_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	mtree_print("error", fmt, ap);
	va_end(ap);

	errors++;
	fputc('\n', stderr);
}

static void
mtree_warning(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	mtree_print("warning", fmt, ap);
	va_end(ap);

	warnings++;
	fputc('\n', stderr);
}

#ifndef MAKEFS_MAX_TREE_DEPTH
# define MAKEFS_MAX_TREE_DEPTH (MAXPATHLEN/2)
#endif

/* construct path to node->name */
static char *
mtree_file_path(fsnode *node)
{
	fsnode *pnode;
	struct sbuf *sb;
	char *res, *rp[MAKEFS_MAX_TREE_DEPTH];
	int depth;

	depth = 0;
	rp[depth] = node->name;
	for (pnode = node->parent; pnode && depth < MAKEFS_MAX_TREE_DEPTH - 1;
	     pnode = pnode->parent) {
		if (strcmp(pnode->name, ".") == 0)
			break;
		rp[++depth] = pnode->name;
	}
	
	sb = sbuf_new_auto();
	if (sb == NULL) {
		errno = ENOMEM;
		return (NULL);
	}
	while (depth > 0) {
		sbuf_cat(sb, rp[depth--]);
		sbuf_putc(sb, '/');
	}
	sbuf_cat(sb, rp[depth]);
	sbuf_finish(sb);
	res = estrdup(sbuf_data(sb));
	sbuf_delete(sb);
	if (res == NULL)
		errno = ENOMEM;
	return res;

}

/* mtree_resolve() sets errno to indicate why NULL was returned. */
static char *
mtree_resolve(const char *spec, int *istemp)
{
	struct sbuf *sb;
	char *res, *var = NULL;
	const char *base, *p, *v;
	size_t len;
	int c, error, quoted, subst;

	len = strlen(spec);
	if (len == 0) {
		errno = EINVAL;
		return (NULL);
	}

	c = (len > 1) ? (spec[0] == spec[len - 1]) ? spec[0] : 0 : 0;
	*istemp = (c == '`') ? 1 : 0;
	subst = (c == '`' || c == '"') ? 1 : 0;
	quoted = (subst || c == '\'') ? 1 : 0;

	if (!subst) {
		res = estrdup(spec + quoted);
		if (quoted)
			res[len - 2] = '\0';
		return (res);
	}

	sb = sbuf_new_auto();
	if (sb == NULL) {
		errno = ENOMEM;
		return (NULL);
	}

	base = spec + 1;
	len -= 2;
	error = 0;
	while (len > 0) {
		p = strchr(base, '$');
		if (p == NULL) {
			sbuf_bcat(sb, base, len);
			base += len;
			len = 0;
			continue;
		}
		/* The following is safe. spec always starts with a quote. */
		if (p[-1] == '\\')
			p--;
		if (base != p) {
			sbuf_bcat(sb, base, p - base);
			len -= p - base;
			base = p;
		}
		if (*p == '\\') {
			sbuf_putc(sb, '$');
			base += 2;
			len -= 2;
			continue;
		}
		/* Skip the '$'. */
		base++;
		len--;
		/* Handle ${X} vs $X. */
		v = base;
		if (*base == '{') {
			p = strchr(v, '}');
			if (p == NULL)
				p = v;
		} else
			p = v;
		len -= (p + 1) - base;
		base = p + 1;

		if (v == p) {
			sbuf_putc(sb, *v);
			continue;
		}

		error = ENOMEM;
		var = ecalloc(p - v, 1);
		memcpy(var, v + 1, p - v - 1);
		if (strcmp(var, ".CURDIR") == 0) {
			res = getcwd(NULL, 0);
			if (res == NULL)
				break;
		} else if (strcmp(var, ".PROG") == 0) {
			res = estrdup(getprogname());
		} else {
			v = getenv(var);
			if (v != NULL) {
				res = estrdup(v);
			} else
				res = NULL;
		}
		error = 0;

		if (res != NULL) {
			sbuf_cat(sb, res);
			free(res);
		}
		free(var);
		var = NULL;
	}

	free(var);
	sbuf_finish(sb);
	res = (error == 0) ? strdup(sbuf_data(sb)) : NULL;
	sbuf_delete(sb);
	if (res == NULL)
		errno = ENOMEM;
	return (res);
}

static int
skip_over(FILE *fp, const char *cs)
{
	int c;

	c = getc(fp);
	while (c != EOF && strchr(cs, c) != NULL)
		c = getc(fp);
	if (c != EOF) {
		ungetc(c, fp);
		return (0);
	}
	return (ferror(fp) ? errno : -1);
}

static int
skip_to(FILE *fp, const char *cs)
{
	int c;

	c = getc(fp);
	while (c != EOF && strchr(cs, c) == NULL)
		c = getc(fp);
	if (c != EOF) {
		ungetc(c, fp);
		return (0);
	}
	return (ferror(fp) ? errno : -1);
}

static int
read_word(FILE *fp, char *buf, size_t bufsz)
{
	struct mtree_fileinfo *fi;
	size_t idx, qidx;
	int c, done, error, esc, qlvl;

	if (bufsz == 0)
		return (EINVAL);

	done = 0;
	esc = 0;
	idx = 0;
	qidx = -1;
	qlvl = 0;
	do {
		c = getc(fp);
		switch (c) {
		case EOF:
			buf[idx] = '\0';
			error = ferror(fp) ? errno : -1;
			if (error == -1)
				mtree_error("unexpected end of file");
			return (error);
		case '#':		/* comment -- skip to end of line. */
			if (!esc) {
				error = skip_to(fp, "\n");
				if (!error)
					continue;
			}
			break;
		case '\\':
			esc++;
			break;
		case '`':
		case '\'':
		case '"':
			if (esc)
				break;
			if (qlvl == 0) {
				qlvl++;
				qidx = idx;
			} else if (c == buf[qidx]) {
				qlvl--;
				if (qlvl > 0) {
					do {
						qidx--;
					} while (buf[qidx] != '`' &&
					    buf[qidx] != '\'' &&
					    buf[qidx] != '"');
				} else
					qidx = -1;
			} else {
				qlvl++;
				qidx = idx;
			}
			break;
		case ' ':
		case '\t':
		case '\n':
			if (!esc && qlvl == 0) {
				ungetc(c, fp);
				c = '\0';
				done = 1;
				break;
			}
			if (c == '\n') {
				/*
				 * We going to eat the newline ourselves.
				 */
				if (qlvl > 0)
					mtree_warning("quoted word straddles "
					    "onto next line.");
				fi = SLIST_FIRST(&mtree_fileinfo);
				fi->line++;
			}
			break;
		default:
			if (esc)
				buf[idx++] = '\\';
			break;
		}
		buf[idx++] = c;
		esc = 0;
	} while (idx < bufsz && !done);

	if (idx >= bufsz) {
		mtree_error("word too long to fit buffer (max %zu characters)",
		    bufsz);
		skip_to(fp, " \t\n");
	}
	return (0);
}

static fsnode *
create_node(const char *name, u_int type, fsnode *parent, fsnode *global)
{
	fsnode *n;

	n = ecalloc(1, sizeof(*n));
	n->name = estrdup(name);
	n->type = (type == 0) ? global->type : type;
	n->parent = parent;

	n->inode = ecalloc(1, sizeof(*n->inode));

	/* Assign global options/defaults. */
	memcpy(n->inode, global->inode, sizeof(*n->inode));
	n->inode->st.st_mode = (n->inode->st.st_mode & ~S_IFMT) | n->type;

	if (n->type == S_IFLNK)
		n->symlink = global->symlink;
	else if (n->type == S_IFREG)
		n->contents = global->contents;

	return (n);
}

static void
destroy_node(fsnode *n)
{

	assert(n != NULL);
	assert(n->name != NULL);
	assert(n->inode != NULL);

	free(n->inode);
	free(n->name);
	free(n);
}

static int
read_number(const char *tok, u_int base, intmax_t *res, intmax_t min,
    intmax_t max)
{
	char *end;
	intmax_t val;

	val = strtoimax(tok, &end, base);
	if (end == tok || end[0] != '\0')
		return (EINVAL);
	if (val < min || val > max)
		return (EDOM);
	*res = val;
	return (0);
}

static int
read_mtree_keywords(FILE *fp, fsnode *node)
{
	char keyword[PATH_MAX];
	char *name, *p, *value;
	gid_t gid;
	uid_t uid;
	struct stat *st, sb;
	intmax_t num;
	u_long flset, flclr;
	int error, istemp;
	uint32_t type;

	st = &node->inode->st;
	do {
		error = skip_over(fp, " \t");
		if (error)
			break;

		error = read_word(fp, keyword, sizeof(keyword));
		if (error)
			break;

		if (keyword[0] == '\0')
			break;

		value = strchr(keyword, '=');
		if (value != NULL)
			*value++ = '\0';

		/*
		 * We use EINVAL, ENOATTR, ENOSYS and ENXIO to signal
		 * certain conditions:
		 *   EINVAL -	Value provided for a keyword that does
		 *		not take a value. The value is ignored.
		 *   ENOATTR -	Value missing for a keyword that needs
		 *		a value. The keyword is ignored.
		 *   ENOSYS -	Unsupported keyword encountered. The
		 *		keyword is ignored.
		 *   ENXIO -	Value provided for a keyword that does
		 *		not take a value. The value is ignored.
		 */
		switch (keyword[0]) {
		case 'c':
			if (strcmp(keyword, "contents") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				node->contents = estrdup(value);
			} else
				error = ENOSYS;
			break;
		case 'f':
			if (strcmp(keyword, "flags") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				flset = flclr = 0;
#if HAVE_STRUCT_STAT_ST_FLAGS
				if (!strtofflags(&value, &flset, &flclr)) {
					st->st_flags &= ~flclr;
					st->st_flags |= flset;
				} else
					error = errno;
#endif
			} else
				error = ENOSYS;
			break;
		case 'g':
			if (strcmp(keyword, "gid") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				error = read_number(value, 10, &num,
				    0, UINT_MAX);
				if (!error)
					st->st_gid = num;
			} else if (strcmp(keyword, "gname") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				if (gid_from_group(value, &gid) == 0)
					st->st_gid = gid;
				else
					error = EINVAL;
			} else
				error = ENOSYS;
			break;
		case 'l':
			if (strcmp(keyword, "link") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				node->symlink = emalloc(strlen(value) + 1);
				if (node->symlink == NULL) {
					error = errno;
					break;
				}
				if (strunvis(node->symlink, value) < 0) {
					error = errno;
					break;
				}
			} else
				error = ENOSYS;
			break;
		case 'm':
			if (strcmp(keyword, "mode") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				if (value[0] >= '0' && value[0] <= '9') {
					error = read_number(value, 8, &num,
					    0, 07777);
					if (!error) {
						st->st_mode &= S_IFMT;
						st->st_mode |= num;
					}
				} else {
					/* Symbolic mode not supported. */
					error = EINVAL;
					break;
				}
			} else
				error = ENOSYS;
			break;
		case 'o':
			if (strcmp(keyword, "optional") == 0) {
				if (value != NULL)
					error = ENXIO;
				node->flags |= FSNODE_F_OPTIONAL;
			} else
				error = ENOSYS;
			break;
		case 's':
			if (strcmp(keyword, "size") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				error = read_number(value, 10, &num,
				    0, INTMAX_MAX);
				if (!error)
					st->st_size = num;
			} else
				error = ENOSYS;
			break;
		case 't':
			if (strcmp(keyword, "time") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				p = strchr(value, '.');
				if (p != NULL)
					*p++ = '\0';
				error = read_number(value, 10, &num, 0,
				    INTMAX_MAX);
				if (error)
					break;
				st->st_atime = num;
				st->st_ctime = num;
				st->st_mtime = num;
#if HAVE_STRUCT_STAT_ST_MTIMENSEC
				if (p == NULL)
					break;
				error = read_number(p, 10, &num, 0,
				    INTMAX_MAX);
				if (error)
					break;
				st->st_atimensec = num;
				st->st_ctimensec = num;
				st->st_mtimensec = num;
#endif
			} else if (strcmp(keyword, "type") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				if (strcmp(value, "dir") == 0)
					node->type = S_IFDIR;
				else if (strcmp(value, "file") == 0)
					node->type = S_IFREG;
				else if (strcmp(value, "link") == 0)
					node->type = S_IFLNK;
				else
					error = EINVAL;
			} else
				error = ENOSYS;
			break;
		case 'u':
			if (strcmp(keyword, "uid") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				error = read_number(value, 10, &num,
				    0, UINT_MAX);
				if (!error)
					st->st_uid = num;
			} else if (strcmp(keyword, "uname") == 0) {
				if (value == NULL) {
					error = ENOATTR;
					break;
				}
				if (uid_from_user(value, &uid) == 0)
					st->st_uid = uid;
				else
					error = EINVAL;
			} else
				error = ENOSYS;
			break;
		default:
			error = ENOSYS;
			break;
		}

		switch (error) {
		case EINVAL:
			mtree_error("%s: invalid value '%s'", keyword, value);
			break;
		case ENOATTR:
			mtree_error("%s: keyword needs a value", keyword);
			break;
		case ENOSYS:
			mtree_warning("%s: unsupported keyword", keyword);
			break;
		case ENXIO:
			mtree_error("%s: keyword does not take a value",
			    keyword);
			break;
		}
	} while (1);

	if (error)
		return (error);

	st->st_mode = (st->st_mode & ~S_IFMT) | node->type;

	/* Nothing more to do for the global defaults. */
	if (node->name == NULL)
		return (0);

	/*
	 * Be intelligent about the file type.
	 */
	if (node->contents != NULL) {
		if (node->symlink != NULL) {
			mtree_error("%s: both link and contents keywords "
			    "defined", node->name);
			return (0);
		}
		type = S_IFREG;
	} else if (node->type != 0) {
		type = node->type;
		if (type == S_IFREG) {
			/* the named path is the default contents */
			node->contents = mtree_file_path(node);
		}
	} else
		type = (node->symlink != NULL) ? S_IFLNK : S_IFDIR;

	if (node->type == 0)
		node->type = type;

	if (node->type != type) {
		mtree_error("%s: file type and defined keywords to not match",
		    node->name);
		return (0);
	}

	st->st_mode = (st->st_mode & ~S_IFMT) | node->type;

	if (node->contents == NULL)
		return (0);

	name = mtree_resolve(node->contents, &istemp);
	if (name == NULL)
		return (errno);

	if (stat(name, &sb) != 0) {
		mtree_error("%s: contents file '%s' not found", node->name,
		    name);
		free(name);
		return (0);
	}

	/*
         * Check for hardlinks. If the contents key is used, then the check
         * will only trigger if the contents file is a link even if it is used
         * by more than one file
	 */
	if (sb.st_nlink > 1) {
		fsinode *curino;

		st->st_ino = sb.st_ino;
		st->st_dev = sb.st_dev;
		curino = link_check(node->inode);
		if (curino != NULL) {
			free(node->inode);
			node->inode = curino;
			node->inode->nlink++;
		}
	}

	free(node->contents);
	node->contents = name;
	st->st_size = sb.st_size;
	return (0);
}

static int
read_mtree_command(FILE *fp)
{
	char cmd[10];
	int error;

	error = read_word(fp, cmd, sizeof(cmd));
	if (error)
		goto out;

	error = read_mtree_keywords(fp, &mtree_global);

 out:
	skip_to(fp, "\n");
	(void)getc(fp);
	return (error);
}

static int
read_mtree_spec1(FILE *fp, bool def, const char *name)
{
	fsnode *last, *node, *parent;
	u_int type;
	int error;

	assert(name[0] != '\0');

	/*
	 * Treat '..' specially, because it only changes our current
	 * directory. We don't create a node for it. We simply ignore
	 * any keywords that may appear on the line as well.
	 * Going up a directory is a little non-obvious. A directory
	 * node has a corresponding '.' child. The parent of '.' is
	 * not the '.' node of the parent directory, but the directory
	 * node within the parent to which the child relates. However,
	 * going up a directory means we need to find the '.' node to
	 * which the directoy node is linked.  This we can do via the
	 * first * pointer, because '.' is always the first entry in a
	 * directory.
	 */
	if (IS_DOTDOT(name)) {
		/* This deals with NULL pointers as well. */
		if (mtree_current == mtree_root) {
			mtree_warning("ignoring .. in root directory");
			return (0);
		}

		node = mtree_current;

		assert(node != NULL);
		assert(IS_DOT(node->name));
		assert(node->first == node);

		/* Get the corresponding directory node in the parent. */
		node = mtree_current->parent;

		assert(node != NULL);
		assert(!IS_DOT(node->name));

		node = node->first;

		assert(node != NULL);
		assert(IS_DOT(node->name));
		assert(node->first == node);

		mtree_current = node;
		return (0);
	}

	/*
	 * If we don't have a current directory and the first specification
	 * (either implicit or defined) is not '.', then we need to create
	 * a '.' node first (using a recursive call).
	 */
	if (!IS_DOT(name) && mtree_current == NULL) {
		error = read_mtree_spec1(fp, false, ".");
		if (error)
			return (error);
	}

	/*
	 * Lookup the name in the current directory (if we have a current
	 * directory) to make sure we do not create multiple nodes for the
	 * same component. For non-definitions, if we find a node with the
	 * same name, simply change the current directory. For definitions
	 * more happens.
	 */
	last = NULL;
	node = mtree_current;
	while (node != NULL) {
		assert(node->first == mtree_current);

		if (strcmp(name, node->name) == 0) {
			if (def == true) {
				if (!dupsok)
					mtree_error(
					    "duplicate definition of %s",
					    name);
				else
					mtree_warning(
					    "duplicate definition of %s",
					    name);
				return (0);
			}

			if (node->type != S_IFDIR) {
				mtree_error("%s is not a directory", name);
				return (0);
			}

			assert(!IS_DOT(name));

			node = node->child;

			assert(node != NULL);
			assert(IS_DOT(node->name));

			mtree_current = node;
			return (0);
		}

		last = node;
		node = last->next;
	}

	parent = (mtree_current != NULL) ? mtree_current->parent : NULL;
	type = (def == false || IS_DOT(name)) ? S_IFDIR : 0;
	node = create_node(name, type, parent, &mtree_global);
	if (node == NULL)
		return (ENOMEM);

	if (def == true) {
		error = read_mtree_keywords(fp, node);
		if (error) {
			destroy_node(node);
			return (error);
		}
	}

	node->first = (mtree_current != NULL) ? mtree_current : node;

	if (last != NULL)
		last->next = node;

	if (node->type != S_IFDIR)
		return (0);

	if (!IS_DOT(node->name)) {
		parent = node;
		node = create_node(".", S_IFDIR, parent, parent);
		if (node == NULL) {
			last->next = NULL;
			destroy_node(parent);
			return (ENOMEM);
		}
		parent->child = node;
		node->first = node;
	}

	assert(node != NULL);
	assert(IS_DOT(node->name));
	assert(node->first == node);

	mtree_current = node;
	if (mtree_root == NULL)
		mtree_root = node;

	return (0);
}

static int
read_mtree_spec(FILE *fp)
{
	char pathspec[PATH_MAX], pathtmp[4*PATH_MAX + 1];
	char *cp;
	int error;

	error = read_word(fp, pathtmp, sizeof(pathtmp));
	if (error)
		goto out;
	if (strnunvis(pathspec, PATH_MAX, pathtmp) == -1) {
		error = errno;
		goto out;
	}
	error = 0;

	cp = strchr(pathspec, '/');
	if (cp != NULL) {
		/* Absolute pathname */
		mtree_current = mtree_root;

		do {
			*cp++ = '\0';

			/* Disallow '..' as a component. */
			if (IS_DOTDOT(pathspec)) {
				mtree_error("absolute path cannot contain "
				    ".. component");
				goto out;
			}

			/* Ignore multiple adjacent slashes and '.'. */
			if (pathspec[0] != '\0' && !IS_DOT(pathspec))
				error = read_mtree_spec1(fp, false, pathspec);
			memmove(pathspec, cp, strlen(cp) + 1);
			cp = strchr(pathspec, '/');
		} while (!error && cp != NULL);

		/* Disallow '.' and '..' as the last component. */
		if (!error && (IS_DOT(pathspec) || IS_DOTDOT(pathspec))) {
			mtree_error("absolute path cannot contain . or .. "
			    "components");
			goto out;
		}
	}

	/* Ignore absolute specfications that end with a slash. */
	if (!error && pathspec[0] != '\0')
		error = read_mtree_spec1(fp, true, pathspec);

 out:
	skip_to(fp, "\n");
	(void)getc(fp);
	return (error);
}

fsnode *
read_mtree(const char *fname, fsnode *node)
{
	struct mtree_fileinfo *fi;
	FILE *fp;
	int c, error;

	/* We do not yet support nesting... */
	assert(node == NULL);

	if (strcmp(fname, "-") == 0)
		fp = stdin;
	else {
		fp = fopen(fname, "r");
		if (fp == NULL)
			err(1, "Can't open `%s'", fname);
	}

	error = mtree_file_push(fname, fp);
	if (error)
		goto out;

	memset(&mtree_global, 0, sizeof(mtree_global));
	memset(&mtree_global_inode, 0, sizeof(mtree_global_inode));
	mtree_global.inode = &mtree_global_inode;
	mtree_global_inode.nlink = 1;
	mtree_global_inode.st.st_nlink = 1;
	mtree_global_inode.st.st_atime = mtree_global_inode.st.st_ctime =
	    mtree_global_inode.st.st_mtime = time(NULL);
	errors = warnings = 0;

	setgroupent(1);
	setpassent(1);

	mtree_root = node;
	mtree_current = node;
	do {
		/* Start of a new line... */
		fi = SLIST_FIRST(&mtree_fileinfo);
		fi->line++;

		error = skip_over(fp, " \t");
		if (error)
			break;

		c = getc(fp);
		if (c == EOF) {
			error = ferror(fp) ? errno : -1;
			break;
		}

		switch (c) {
		case '\n':		/* empty line */
			error = 0;
			break;
		case '#':		/* comment -- skip to end of line. */
			error = skip_to(fp, "\n");
			if (!error)
				(void)getc(fp);
			break;
		case '/':		/* special commands */
			error = read_mtree_command(fp);
			break;
		default:		/* specification */
			ungetc(c, fp);
			error = read_mtree_spec(fp);
			break;
		}
	} while (!error);

	endpwent();
	endgrent();

	if (error <= 0 && (errors || warnings)) {
		warnx("%u error(s) and %u warning(s) in mtree manifest",
		    errors, warnings);
		if (errors)
			exit(1);
	}

 out:
	if (error > 0)
		errc(1, error, "Error reading mtree file");

	if (fp != stdin)
		fclose(fp);

	if (mtree_root != NULL)
		return (mtree_root);

	/* Handle empty specifications. */
	node = create_node(".", S_IFDIR, NULL, &mtree_global);
	node->first = node;
	return (node);
}
