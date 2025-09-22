/*-
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 */

#include "am.h"

/*
 * static copy of the options with
 * which to play
 */
static struct am_opts fs_static;

static char *opt_host = hostname;
static char *opt_hostd = hostd;
static char nullstr[] = "";
static char *opt_key = nullstr;
static char *opt_map = nullstr;
static char *opt_path = nullstr;

static char *vars[8];

/*
 * Length of longest option name
 */
#define	NLEN	16	/* conservative */
#define S(x) (x) , (sizeof(x)-1)
static struct opt {
	char *name;		/* Name of the option */
	int nlen;		/* Length of option name */
	char **optp;		/* Pointer to option value string */
	char **sel_p;		/* Pointer to selector value string */
} opt_fields[] = {
	/* Options in something corresponding to frequency of use */
	{ S("opts"), &fs_static.opt_opts, 0 },
	{ S("host"), 0, &opt_host },
	{ S("hostd"), 0, &opt_hostd },
	{ S("type"), &fs_static.opt_type, 0 },
	{ S("rhost"), &fs_static.opt_rhost, 0 },
	{ S("rfs"), &fs_static.opt_rfs, 0 },
	{ S("fs"), &fs_static.opt_fs, 0 },
	{ S("key"), 0, &opt_key },
	{ S("map"), 0, &opt_map },
	{ S("sublink"), &fs_static.opt_sublink, 0 },
	{ S("arch"), 0, &arch },
	{ S("dev"), &fs_static.opt_dev, 0 },
	{ S("pref"), &fs_static.opt_pref, 0 },
	{ S("path"), 0, &opt_path },
	{ S("autodir"), 0, &auto_dir },
	{ S("delay"), &fs_static.opt_delay, 0 },
	{ S("domain"), 0, &hostdomain },
	{ S("karch"), 0, &karch },
	{ S("cluster"), 0, &cluster },
	{ S("wire"), 0, &wire },
	{ S("byte"), 0, &endian },
	{ S("os"), 0, &op_sys },
	{ S("remopts"), &fs_static.opt_remopts, 0 },
	{ S("mount"), &fs_static.opt_mount, 0 },
	{ S("unmount"), &fs_static.opt_unmount, 0 },
	{ S("cache"), &fs_static.opt_cache, 0 },
	{ S("user"), &fs_static.opt_user, 0 },
	{ S("group"), &fs_static.opt_group, 0 },
	{ S("var0"), &vars[0], 0 },
	{ S("var1"), &vars[1], 0 },
	{ S("var2"), &vars[2], 0 },
	{ S("var3"), &vars[3], 0 },
	{ S("var4"), &vars[4], 0 },
	{ S("var5"), &vars[5], 0 },
	{ S("var6"), &vars[6], 0 },
	{ S("var7"), &vars[7], 0 },
	{ 0, 0, 0, 0 },
};

typedef struct opt_apply opt_apply;
struct opt_apply {
	char **opt;
	char *val;
};

/*
 * Specially expand the remote host name first
 */
static opt_apply rhost_expansion[] = {
	{ &fs_static.opt_rhost, "${host}" },
	{ 0, 0 },
};
/*
 * List of options which need to be expanded
 * Note that this the order here _may_ be important.
 */
static opt_apply expansions[] = {
/*	{ &fs_static.opt_dir, 0 },	*/
	{ &fs_static.opt_sublink, 0 },
	{ &fs_static.opt_rfs, "${path}" },
	{ &fs_static.opt_fs, "${autodir}/${rhost}${rfs}" },
	{ &fs_static.opt_opts, "rw" },
	{ &fs_static.opt_remopts, "${opts}" },
	{ &fs_static.opt_mount, 0 },
	{ &fs_static.opt_unmount, 0 },
	{ 0, 0 },
};

/*
 * List of options which need to be free'ed before re-use
 */
static opt_apply to_free[] = {
	{ &fs_static.fs_glob, 0 },
	{ &fs_static.fs_local, 0 },
	{ &fs_static.fs_mtab, 0 },
/*	{ &fs_static.opt_dir, 0 },	*/
	{ &fs_static.opt_sublink, 0 },
	{ &fs_static.opt_rfs, 0 },
	{ &fs_static.opt_fs, 0 },
	{ &fs_static.opt_rhost, 0 },
	{ &fs_static.opt_opts, 0 },
	{ &fs_static.opt_remopts, 0 },
	{ &fs_static.opt_mount, 0 },
	{ &fs_static.opt_unmount, 0 },
	{ &vars[0], 0 },
	{ &vars[1], 0 },
	{ &vars[2], 0 },
	{ &vars[3], 0 },
	{ &vars[4], 0 },
	{ &vars[5], 0 },
	{ &vars[6], 0 },
	{ &vars[7], 0 },
	{ 0, 0 },
};

/*
 * Skip to next option in the string
 */
static char *
opt(char **p)
{
	char *cp = *p;
	char *dp = cp;
	char *s = cp;

top:
	while (*cp && *cp != ';') {
		if (*cp == '\"') {
			/*
			 * Skip past string
			 */
			cp++;
			while (*cp && *cp != '\"')
				*dp++ = *cp++;
			if (*cp)
				cp++;
		} else {
			*dp++ = *cp++;
		}
	}

	/*
	 * Skip past any remaining ';'s
	 */
	while (*cp == ';')
		cp++;

	/*
	 * If we have a zero length string
	 * and there are more fields, then
	 * parse the next one.  This allows
	 * sequences of empty fields.
	 */
	if (*cp && dp == s)
		goto top;

	*dp = '\0';

	*p = cp;
	return s;
}

static int
eval_opts(char *opts, char *mapkey)
{
	/*
	 * Fill in the global structure fs_static by
	 * cracking the string opts.  opts may be
	 * scribbled on at will.
	 */
	char *o = opts;
	char *f;

	/*
	 * For each user-specified option
	 */
	while (*(f = opt(&o))) {
		struct opt *op;
		enum vs_opt { OldSyn, SelEQ, SelNE, VarAss } vs_opt;
		char *eq = strchr(f, '=');
		char *opt;
		if (!eq || eq[1] == '\0' || eq == f) {
			/*
			 * No value, just continue
			 */
			plog(XLOG_USER, "key %s: No value component in \"%s\"", mapkey, f);
			continue;
		}

		/*
		 * Check what type of operation is happening
		 * !=, =!  is SelNE
		 * == is SelEQ
		 * := is VarAss
		 * = is OldSyn (either SelEQ or VarAss)
		 */
		if (eq[-1] == '!') {		/* != */
			vs_opt = SelNE;
			eq[-1] = '\0';
			opt = eq + 1;
		} else if (eq[-1] == ':') {	/* := */
			vs_opt = VarAss;
			eq[-1] = '\0';
			opt = eq + 1;
		} else if (eq[1] == '=') {	/* == */
			vs_opt = SelEQ;
			eq[0] = '\0';
			opt = eq + 2;
		} else if (eq[1] == '!') {	/* =! */
			vs_opt = SelNE;
			eq[0] = '\0';
			opt = eq + 2;
		} else {			/* = */
			vs_opt = OldSyn;
			eq[0] = '\0';
			opt = eq + 1;
		}

		/*
		 * For each recognised option
		 */
		for (op = opt_fields; op->name; op++) {
			/*
			 * Check whether they match
			 */
			if (FSTREQ(op->name, f)) {
				switch (vs_opt) {
#if 1	/* XXX ancient compat */
				case OldSyn:
					plog(XLOG_WARNING, "key %s: Old syntax selector found: %s=%s", mapkey, f, opt);
					if (!op->sel_p) {
						*op->optp = opt;
						break;
					}
					/* fall through ... */
#endif
				case SelEQ:
				case SelNE:
					if (op->sel_p && (STREQ(*op->sel_p, opt) == (vs_opt == SelNE))) {
						plog(XLOG_MAP, "key %s: map selector %s (=%s) did not %smatch %s",
							mapkey,
							op->name,
							*op->sel_p,
							vs_opt == SelNE ? "not " : "",
							opt);
						return 0;
					}
					break;

				case VarAss:
					if (op->sel_p) {
						plog(XLOG_USER, "key %s: Can't assign to a selector (%s)", mapkey, op->name);
						return 0;
					}
					*op->optp = opt;
					break;
				}
				break;
			}
		}

		if (!op->name)
			plog(XLOG_USER, "key %s: Unrecognised key/option \"%s\"", mapkey, f);
	}

	return 1;
}

/*
 * Free an option
 */
static void
free_op(opt_apply *p, int b)
{
	if (*p->opt) {
		free(*p->opt);
		*p->opt = 0;
	}
}

/*
 * Normalize slashes in the string.
 */
void
normalize_slash(char *p)
{
	char *f = strchr(p, '/');
	char *f0 = f;
	if (f) {
		char *t = f;
		do {
			/* assert(*f == '/'); */
			if (f == f0 && f[0] == '/' && f[1] == '/') {
				/* copy double slash iff first */
				*t++ = *f++;
				*t++ = *f++;
			} else {
				/* copy a single / across */
				*t++ = *f++;
			}

			/* assert(f[-1] == '/'); */
			/* skip past more /'s */
			while (*f == '/')
				f++;

			/* assert(*f != '/'); */
			/* keep copying up to next / */
			while (*f && *f != '/') {
				*t++ = *f++;
			}

			/* assert(*f == 0 || *f == '/'); */

		} while (*f);
		*t = 0;			/* derived from fix by Steven Glassman */
	}
}

/*
 * Macro-expand an option.  Note that this does not
 * handle recursive expansions.  They will go badly wrong.
 * If sel is true then old expand selectors, otherwise
 * don't expand selectors.
 */
static void
expand_op(opt_apply *p, int sel_p)
{
/*
 * The BUFSPACE macros checks that there is enough space
 * left in the expansion buffer.  If there isn't then we
 * give up completely.  This is done to avoid crashing the
 * automounter itself (which would be a bad thing to do).
 */
#define BUFSPACE(ep, len) (((ep) + (len)) < expbuf+PATH_MAX)
static char expand_error[] = "No space to expand \"%s\"";

	char expbuf[PATH_MAX+1];
	char nbuf[NLEN+1];
	char *ep = expbuf;
	char *cp = *p->opt;
	char *dp;
#ifdef DEBUG
	char *cp_orig = *p->opt;
#endif /* DEBUG */
	struct opt *op;

	while ((dp = strchr(cp, '$'))) {
		char ch;
		/*
		 * First copy up to the $
		 */
		{ int len = dp - cp;
		  if (BUFSPACE(ep, len)) {
			strncpy(ep, cp, len);
			ep += len;
		  } else {
			plog(XLOG_ERROR, expand_error, *p->opt);
			goto out;
		  }
		}
		cp = dp + 1;
		ch = *cp++;
		if (ch == '$') {
			if (BUFSPACE(ep, 1)) {
				*ep++ = '$';
			} else {
				plog(XLOG_ERROR, expand_error, *p->opt);
				goto out;
			}
		} else if (ch == '{') {
			/* Expansion... */
			enum { E_All, E_Dir, E_File, E_Domain, E_Host } todo;
			/*
			 * Find closing brace
			 */
			char *br_p = strchr(cp, '}');
			int len;
			/*
			 * Check we found it
			 */
			if (!br_p) {
				/*
				 * Just give up
				 */
				plog(XLOG_USER, "No closing '}' in \"%s\"", *p->opt);
				goto out;
			}
			len = br_p - cp;
			/*
			 * Figure out which part of the variable to grab.
			 */
			if (*cp == '/') {
				/*
				 * Just take the last component
				 */
				todo = E_File;
				cp++;
				--len;
			} else if (br_p[-1] == '/') {
				/*
				 * Take all but the last component
				 */
				todo = E_Dir;
				--len;
			} else if (*cp == '.') {
				/*
				 * Take domain name
				 */
				todo = E_Domain;
				cp++;
				--len;
			} else if (br_p[-1] == '.') {
				/*
				 * Take host name
				 */
				todo = E_Host;
				--len;
			} else {
				/*
				 * Take the whole lot
				 */
				todo = E_All;
			}
			/*
			 * Truncate if too long.  Since it won't
			 * match anyway it doesn't matter that
			 * it has been cut short.
			 */
			if (len > NLEN)
				len = NLEN;
			/*
			 * Put the string into another buffer so
			 * we can do comparisons.
			 */
			strncpy(nbuf, cp, len);
			nbuf[len] = '\0';
			/*
			 * Advance cp
			 */
			cp = br_p + 1;
			/*
			 * Search the option array
			 */
			for (op = opt_fields; op->name; op++) {
				/*
				 * Check for match
				 */
				if (len == op->nlen && STREQ(op->name, nbuf)) {
					char xbuf[NLEN+3];
					char *val;
					/*
					 * Found expansion.  Copy
					 * the correct value field.
					 */
					if (!(!op->sel_p == !sel_p)) {
						/*
						 * Copy the string across unexpanded
						 */
						snprintf(xbuf, sizeof(xbuf), "${%s%s%s}",
							todo == E_File ? "/" :
								todo == E_Domain ? "." : "",
							nbuf,
							todo == E_Dir ? "/" :
								todo == E_Host ? "." : "");
						val = xbuf;
						/*
						 * Make sure expansion doesn't
						 * munge the value!
						 */
						todo = E_All;
					} else if (op->sel_p) {
						val = *op->sel_p;
					} else {
						val = *op->optp;
					}
					if (val) {
						/*
						 * Do expansion:
						 * ${/var} means take just the last part
						 * ${var/} means take all but the last part
						 * ${.var} means take all but first part
						 * ${var.} means take just the first part
						 * ${var} means take the whole lot
						 */
						int vlen = strlen(val);
						char *vptr = val;
						switch (todo) {
						case E_Dir:
							vptr = strrchr(val, '/');
							if (vptr)
								vlen = vptr - val;
							vptr = val;
							break;
						case E_File:
							vptr = strrchr(val, '/');
							if (vptr) {
								vptr++;
								vlen = strlen(vptr);
							} else
								vptr = val;
							break;
						case E_Domain:
							vptr = strchr(val, '.');
							if (vptr) {
								vptr++;
								vlen = strlen(vptr);
							} else {
								vptr = "";
								vlen = 0;
							}
							break;
						case E_Host:
							vptr = strchr(val, '.');
							if (vptr)
								vlen = vptr - val;
							vptr = val;
							break;
						case E_All:
							break;
						}
#ifdef DEBUG
					/*dlog("Expanding \"%s\" to \"%s\"", nbuf, val);*/
#endif /* DEBUG */
						if (BUFSPACE(ep, vlen)) {
							strlcpy(ep, vptr, expbuf + sizeof expbuf - ep);
							ep += strlen(ep);
						} else {
							plog(XLOG_ERROR, expand_error, *p->opt);
							goto out;
						}
					}
					/*
					 * Done with this variable
					 */
					break;
				}
			}
			/*
			 * Check that the search was successful
			 */
			if (!op->name) {
				/*
				 * If it wasn't then scan the
				 * environment for that name
				 * and use any value found
				 */
				char *env = getenv(nbuf);
				if (env) {
					int vlen = strlen(env);

					if (BUFSPACE(ep, vlen)) {
						strlcpy(ep, env, expbuf + sizeof expbuf - ep);
						ep += strlen(ep);
					} else {
						plog(XLOG_ERROR, expand_error, *p->opt);
						goto out;
					}
#ifdef DEBUG
					Debug(D_STR)
					plog(XLOG_DEBUG, "Environment gave \"%s\" -> \"%s\"", nbuf, env);
#endif /* DEBUG */
				} else {
					plog(XLOG_USER, "Unknown sequence \"${%s}\"", nbuf);
				}
			}
		} else {
			/*
			 * Error, error
			 */
			plog(XLOG_USER, "Unknown $ sequence in \"%s\"", *p->opt);
		}
	}

out:
	/*
	 * Handle common case - no expansion
	 */
	if (cp == *p->opt) {
		*p->opt = strdup(cp);
	} else {
		/*
		 * Finish off the expansion
		 */
		if (BUFSPACE(ep, strlen(cp))) {
			strlcpy(ep, cp, expbuf + sizeof expbuf - ep);
		} else {
			plog(XLOG_ERROR, expand_error, *p->opt);
		}

		/*
		 * Save the exansion
		 */
		*p->opt = strdup(expbuf);
	}

	normalize_slash(*p->opt);

#ifdef DEBUG
	Debug(D_STR) {
		plog(XLOG_DEBUG, "Expansion of \"%s\"...", cp_orig);
		plog(XLOG_DEBUG, "... is \"%s\"", *p->opt);
	}
#endif /* DEBUG */
}

/*
 * Wrapper for expand_op
 */
static void
expand_opts(opt_apply *p, int sel_p)
{
	if (*p->opt) {
		expand_op(p, sel_p);
	} else if (p->val) {
		/*
		 * Do double expansion, remembering
		 * to free the string from the first
		 * expansion...
		 */
		char *s = *p->opt = expand_key(p->val);
		expand_op(p, sel_p);
		free(s);
	}
}

/*
 * Apply a function to a list of options
 */
static void
apply_opts(void (*op)(), opt_apply ppp[], int b)
{
	opt_apply *pp;
	for (pp = ppp; pp->opt; pp++)
		(*op)(pp, b);
}

/*
 * Free the option table
 */
void
free_opts(am_opts *fo)
{
	/*
	 * Copy in the structure we are playing with
	 */
	fs_static = *fo;

	/*
	 * Free previously allocated memory
	 */
	apply_opts(free_op, to_free, FALSE);
}

/*
 * Expand lookup key
 */
char *
expand_key(char *key)
{
	opt_apply oa;

	oa.opt = &key; oa.val = 0;
	expand_opts(&oa, TRUE);

	return key;
}

/*
 * Remove trailing /'s from a string
 * unless the string is a single / (Steven Glassman)
 */
void
deslashify(char *s)
{
	if (s && *s) {
		char *sl = s + strlen(s);
		while (*--sl == '/' && sl > s)
			*sl = '\0';
	}
}

int
eval_fs_opts(am_opts *fo, char *opts, char *g_opts, char *path,
    char *key, char *map)
{
	int ok = TRUE;

	free_opts(fo);

	/*
	 * Clear out the option table
	 */
	bzero(&fs_static, sizeof(fs_static));
	bzero(vars, sizeof(vars));
	bzero(fo, sizeof(*fo));

	/*
	 * Set key, map & path before expansion
	 */
	opt_key = key;
	opt_map = map;
	opt_path = path;

	/*
	 * Expand global options
	 */
	fs_static.fs_glob = expand_key(g_opts);

	/*
	 * Expand local options
	 */
	fs_static.fs_local = expand_key(opts);

	/*
	 * Expand default (global) options
	 */
	if (!eval_opts(fs_static.fs_glob, key))
		ok = FALSE;

	/*
	 * Expand local options
	 */
	if (ok && !eval_opts(fs_static.fs_local, key))
		ok = FALSE;

	/*
	 * Normalise remote host name.
	 * 1.  Expand variables
	 * 2.  Normalize relative to host tables
	 * 3.  Strip local domains from the remote host
	 *     name before using it in other expansions.
	 *     This makes mount point names and other things
	 *     much shorter, while allowing cross domain
	 *     sharing of mount maps.
	 */
	apply_opts(expand_opts, rhost_expansion, FALSE);
	if (ok && fs_static.opt_rhost && *fs_static.opt_rhost)
		host_normalize(&fs_static.opt_rhost);

	/*
	 * Macro expand the options.
	 * Do this regardless of whether we are accepting
	 * this mount - otherwise nasty things happen
	 * with memory allocation.
	 */
	apply_opts(expand_opts, expansions, FALSE);

	/*
	 * Strip trailing slashes from local pathname...
	 */
	deslashify(fs_static.opt_fs);

	/*
	 * ok... copy the data back out.
	 */
	*fo = fs_static;

	/*
	 * Clear defined options
	 */
	opt_key = opt_map = opt_path = nullstr;

	return ok;
}
