/*	$NetBSD: keymacro.c,v 1.14 2016/02/24 14:25:38 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
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

#include "config.h"
#if !defined(lint) && !defined(SCCSID)
#if 0
static char sccsid[] = "@(#)key.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: keymacro.c,v 1.14 2016/02/24 14:25:38 christos Exp $");
#endif
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * keymacro.c: This module contains the procedures for maintaining
 *	       the extended-key map.
 *
 *      An extended-key (key) is a sequence of keystrokes introduced
 *	with a sequence introducer and consisting of an arbitrary
 *	number of characters.  This module maintains a map (the
 *	el->el_keymacro.map)
 *	to convert these extended-key sequences into input strs
 *	(XK_STR), editor functions (XK_CMD), or unix commands (XK_EXE).
 *
 *      Warning:
 *	  If key is a substr of some other keys, then the longer
 *	  keys are lost!!  That is, if the keys "abcd" and "abcef"
 *	  are in el->el_keymacro.map, adding the key "abc" will cause
 *	  the first two definitions to be lost.
 *
 *      Restrictions:
 *      -------------
 *      1) It is not possible to have one key that is a
 *	   substr of another.
 */
#include <stdlib.h>
#include <string.h>

#include "el.h"

/*
 * The Nodes of the el->el_keymacro.map.  The el->el_keymacro.map is a
 * linked list of these node elements
 */
struct keymacro_node_t {
	Char		 ch;		/* single character of key	 */
	int		 type;		/* node type			 */
	keymacro_value_t val;		/* command code or pointer to str,  */
					/* if this is a leaf		 */
	struct keymacro_node_t *next;	/* ptr to next char of this key  */
	struct keymacro_node_t *sibling;/* ptr to another key with same prefix*/
};

private int		 node_trav(EditLine *, keymacro_node_t *, Char *,
    keymacro_value_t *);
private int		 node__try(EditLine *, keymacro_node_t *, const Char *,
    keymacro_value_t *, int);
private keymacro_node_t	*node__get(wint_t);
private void		 node__free(keymacro_node_t *);
private void		 node__put(EditLine *, keymacro_node_t *);
private int		 node__delete(EditLine *, keymacro_node_t **,
    const Char *);
private int		 node_lookup(EditLine *, const Char *,
    keymacro_node_t *, size_t);
private int		 node_enum(EditLine *, keymacro_node_t *, size_t);

#define	KEY_BUFSIZ	EL_BUFSIZ


/* keymacro_init():
 *	Initialize the key maps
 */
protected int
keymacro_init(EditLine *el)
{

	el->el_keymacro.buf = el_malloc(KEY_BUFSIZ *
	    sizeof(*el->el_keymacro.buf));
	if (el->el_keymacro.buf == NULL)
		return -1;
	el->el_keymacro.map = NULL;
	keymacro_reset(el);
	return 0;
}

/* keymacro_end():
 *	Free the key maps
 */
protected void
keymacro_end(EditLine *el)
{

	el_free(el->el_keymacro.buf);
	el->el_keymacro.buf = NULL;
	node__free(el->el_keymacro.map);
}


/* keymacro_map_cmd():
 *	Associate cmd with a key value
 */
protected keymacro_value_t *
keymacro_map_cmd(EditLine *el, int cmd)
{

	el->el_keymacro.val.cmd = (el_action_t) cmd;
	return &el->el_keymacro.val;
}


/* keymacro_map_str():
 *	Associate str with a key value
 */
protected keymacro_value_t *
keymacro_map_str(EditLine *el, Char *str)
{

	el->el_keymacro.val.str = str;
	return &el->el_keymacro.val;
}


/* keymacro_reset():
 *	Takes all nodes on el->el_keymacro.map and puts them on free list.
 *	Then initializes el->el_keymacro.map with arrow keys
 *	[Always bind the ansi arrow keys?]
 */
protected void
keymacro_reset(EditLine *el)
{

	node__put(el, el->el_keymacro.map);
	el->el_keymacro.map = NULL;
	return;
}


/* keymacro_get():
 *	Calls the recursive function with entry point el->el_keymacro.map
 *      Looks up *ch in map and then reads characters until a
 *      complete match is found or a mismatch occurs. Returns the
 *      type of the match found (XK_STR, XK_CMD, or XK_EXE).
 *      Returns NULL in val.str and XK_STR for no match.
 *      The last character read is returned in *ch.
 */
protected int
keymacro_get(EditLine *el, Char *ch, keymacro_value_t *val)
{

	return node_trav(el, el->el_keymacro.map, ch, val);
}


/* keymacro_add():
 *      Adds key to the el->el_keymacro.map and associates the value in
 *	val with it. If key is already is in el->el_keymacro.map, the new
 *	code is applied to the existing key. Ntype specifies if code is a
 *	command, an out str or a unix command.
 */
protected void
keymacro_add(EditLine *el, const Char *key, keymacro_value_t *val, int ntype)
{

	if (key[0] == '\0') {
		(void) fprintf(el->el_errfile,
		    "keymacro_add: Null extended-key not allowed.\n");
		return;
	}
	if (ntype == XK_CMD && val->cmd == ED_SEQUENCE_LEAD_IN) {
		(void) fprintf(el->el_errfile,
		    "keymacro_add: sequence-lead-in command not allowed\n");
		return;
	}
	if (el->el_keymacro.map == NULL)
		/* tree is initially empty.  Set up new node to match key[0] */
		el->el_keymacro.map = node__get(key[0]);
			/* it is properly initialized */

	/* Now recurse through el->el_keymacro.map */
	(void) node__try(el, el->el_keymacro.map, key, val, ntype);
	return;
}


/* keymacro_clear():
 *
 */
protected void
keymacro_clear(EditLine *el, el_action_t *map, const Char *in)
{
#ifdef WIDECHAR
        if (*in > N_KEYS) /* can't be in the map */
                return;
#endif
	if ((map[(unsigned char)*in] == ED_SEQUENCE_LEAD_IN) &&
	    ((map == el->el_map.key &&
	    el->el_map.alt[(unsigned char)*in] != ED_SEQUENCE_LEAD_IN) ||
	    (map == el->el_map.alt &&
	    el->el_map.key[(unsigned char)*in] != ED_SEQUENCE_LEAD_IN)))
		(void) keymacro_delete(el, in);
}


/* keymacro_delete():
 *      Delete the key and all longer keys staring with key, if
 *      they exists.
 */
protected int
keymacro_delete(EditLine *el, const Char *key)
{

	if (key[0] == '\0') {
		(void) fprintf(el->el_errfile,
		    "keymacro_delete: Null extended-key not allowed.\n");
		return -1;
	}
	if (el->el_keymacro.map == NULL)
		return 0;

	(void) node__delete(el, &el->el_keymacro.map, key);
	return 0;
}


/* keymacro_print():
 *	Print the binding associated with key key.
 *	Print entire el->el_keymacro.map if null
 */
protected void
keymacro_print(EditLine *el, const Char *key)
{

	/* do nothing if el->el_keymacro.map is empty and null key specified */
	if (el->el_keymacro.map == NULL && *key == 0)
		return;

	el->el_keymacro.buf[0] = '"';
	if (node_lookup(el, key, el->el_keymacro.map, (size_t)1) <= -1)
		/* key is not bound */
		(void) fprintf(el->el_errfile, "Unbound extended key \"" FSTR
		    "\"\n", key);
	return;
}


/* node_trav():
 *	recursively traverses node in tree until match or mismatch is
 *	found.  May read in more characters.
 */
private int
node_trav(EditLine *el, keymacro_node_t *ptr, Char *ch, keymacro_value_t *val)
{
	wchar_t wc;

	if (ptr->ch == *ch) {
		/* match found */
		if (ptr->next) {
			/* key not complete so get next char */
			if (el_wgetc(el, &wc) != 1) {/* if EOF or error */
				val->cmd = ED_END_OF_FILE;
				return XK_CMD;
				/* PWP: Pretend we just read an end-of-file */
			}
			*ch = (Char)wc;
			return node_trav(el, ptr->next, ch, val);
		} else {
			*val = ptr->val;
			if (ptr->type != XK_CMD)
				*ch = '\0';
			return ptr->type;
		}
	} else {
		/* no match found here */
		if (ptr->sibling) {
			/* try next sibling */
			return node_trav(el, ptr->sibling, ch, val);
		} else {
			/* no next sibling -- mismatch */
			val->str = NULL;
			return XK_STR;
		}
	}
}


/* node__try():
 *	Find a node that matches *str or allocate a new one
 */
private int
node__try(EditLine *el, keymacro_node_t *ptr, const Char *str,
    keymacro_value_t *val, int ntype)
{

	if (ptr->ch != *str) {
		keymacro_node_t *xm;

		for (xm = ptr; xm->sibling != NULL; xm = xm->sibling)
			if (xm->sibling->ch == *str)
				break;
		if (xm->sibling == NULL)
			xm->sibling = node__get(*str);	/* setup new node */
		ptr = xm->sibling;
	}
	if (*++str == '\0') {
		/* we're there */
		if (ptr->next != NULL) {
			node__put(el, ptr->next);
				/* lose longer keys with this prefix */
			ptr->next = NULL;
		}
		switch (ptr->type) {
		case XK_CMD:
		case XK_NOD:
			break;
		case XK_STR:
		case XK_EXE:
			if (ptr->val.str)
				el_free(ptr->val.str);
			break;
		default:
			EL_ABORT((el->el_errfile, "Bad XK_ type %d\n",
			    ptr->type));
			break;
		}

		switch (ptr->type = ntype) {
		case XK_CMD:
			ptr->val = *val;
			break;
		case XK_STR:
		case XK_EXE:
			if ((ptr->val.str = Strdup(val->str)) == NULL)
				return -1;
			break;
		default:
			EL_ABORT((el->el_errfile, "Bad XK_ type %d\n", ntype));
			break;
		}
	} else {
		/* still more chars to go */
		if (ptr->next == NULL)
			ptr->next = node__get(*str);	/* setup new node */
		(void) node__try(el, ptr->next, str, val, ntype);
	}
	return 0;
}


/* node__delete():
 *	Delete node that matches str
 */
private int
node__delete(EditLine *el, keymacro_node_t **inptr, const Char *str)
{
	keymacro_node_t *ptr;
	keymacro_node_t *prev_ptr = NULL;

	ptr = *inptr;

	if (ptr->ch != *str) {
		keymacro_node_t *xm;

		for (xm = ptr; xm->sibling != NULL; xm = xm->sibling)
			if (xm->sibling->ch == *str)
				break;
		if (xm->sibling == NULL)
			return 0;
		prev_ptr = xm;
		ptr = xm->sibling;
	}
	if (*++str == '\0') {
		/* we're there */
		if (prev_ptr == NULL)
			*inptr = ptr->sibling;
		else
			prev_ptr->sibling = ptr->sibling;
		ptr->sibling = NULL;
		node__put(el, ptr);
		return 1;
	} else if (ptr->next != NULL &&
	    node__delete(el, &ptr->next, str) == 1) {
		if (ptr->next != NULL)
			return 0;
		if (prev_ptr == NULL)
			*inptr = ptr->sibling;
		else
			prev_ptr->sibling = ptr->sibling;
		ptr->sibling = NULL;
		node__put(el, ptr);
		return 1;
	} else {
		return 0;
	}
}


/* node__put():
 *	Puts a tree of nodes onto free list using free(3).
 */
private void
node__put(EditLine *el, keymacro_node_t *ptr)
{
	if (ptr == NULL)
		return;

	if (ptr->next != NULL) {
		node__put(el, ptr->next);
		ptr->next = NULL;
	}
	node__put(el, ptr->sibling);

	switch (ptr->type) {
	case XK_CMD:
	case XK_NOD:
		break;
	case XK_EXE:
	case XK_STR:
		if (ptr->val.str != NULL)
			el_free(ptr->val.str);
		break;
	default:
		EL_ABORT((el->el_errfile, "Bad XK_ type %d\n", ptr->type));
		break;
	}
	el_free(ptr);
}


/* node__get():
 *	Returns pointer to a keymacro_node_t for ch.
 */
private keymacro_node_t *
node__get(wint_t ch)
{
	keymacro_node_t *ptr;

	ptr = el_malloc(sizeof(*ptr));
	if (ptr == NULL)
		return NULL;
	ptr->ch = (Char)ch;
	ptr->type = XK_NOD;
	ptr->val.str = NULL;
	ptr->next = NULL;
	ptr->sibling = NULL;
	return ptr;
}

private void
node__free(keymacro_node_t *k)
{
	if (k == NULL)
		return;
	node__free(k->sibling);
	node__free(k->next);
	el_free(k);
}

/* node_lookup():
 *	look for the str starting at node ptr.
 *	Print if last node
 */
private int
node_lookup(EditLine *el, const Char *str, keymacro_node_t *ptr, size_t cnt)
{
	ssize_t used;

	if (ptr == NULL)
		return -1;	/* cannot have null ptr */

	if (!str || *str == 0) {
		/* no more chars in str.  node_enum from here. */
		(void) node_enum(el, ptr, cnt);
		return 0;
	} else {
		/* If match put this char into el->el_keymacro.buf.  Recurse */
		if (ptr->ch == *str) {
			/* match found */
			used = ct_visual_char(el->el_keymacro.buf + cnt,
			    KEY_BUFSIZ - cnt, ptr->ch);
			if (used == -1)
				return -1; /* ran out of buffer space */
			if (ptr->next != NULL)
				/* not yet at leaf */
				return (node_lookup(el, str + 1, ptr->next,
				    (size_t)used + cnt));
			else {
			    /* next node is null so key should be complete */
				if (str[1] == 0) {
					size_t px = cnt + (size_t)used;
					el->el_keymacro.buf[px] = '"';
					el->el_keymacro.buf[px + 1] = '\0';
					keymacro_kprint(el, el->el_keymacro.buf,
					    &ptr->val, ptr->type);
					return 0;
				} else
					return -1;
					/* mismatch -- str still has chars */
			}
		} else {
			/* no match found try sibling */
			if (ptr->sibling)
				return (node_lookup(el, str, ptr->sibling,
				    cnt));
			else
				return -1;
		}
	}
}


/* node_enum():
 *	Traverse the node printing the characters it is bound in buffer
 */
private int
node_enum(EditLine *el, keymacro_node_t *ptr, size_t cnt)
{
        ssize_t used;

	if (cnt >= KEY_BUFSIZ - 5) {	/* buffer too small */
		el->el_keymacro.buf[++cnt] = '"';
		el->el_keymacro.buf[++cnt] = '\0';
		(void) fprintf(el->el_errfile,
		    "Some extended keys too long for internal print buffer");
		(void) fprintf(el->el_errfile, " \"" FSTR "...\"\n",
		    el->el_keymacro.buf);
		return 0;
	}
	if (ptr == NULL) {
#ifdef DEBUG_EDIT
		(void) fprintf(el->el_errfile,
		    "node_enum: BUG!! Null ptr passed\n!");
#endif
		return -1;
	}
	/* put this char at end of str */
        used = ct_visual_char(el->el_keymacro.buf + cnt, KEY_BUFSIZ - cnt,
	    ptr->ch);
	if (ptr->next == NULL) {
		/* print this key and function */
		el->el_keymacro.buf[cnt + (size_t)used   ] = '"';
		el->el_keymacro.buf[cnt + (size_t)used + 1] = '\0';
		keymacro_kprint(el, el->el_keymacro.buf, &ptr->val, ptr->type);
	} else
		(void) node_enum(el, ptr->next, cnt + (size_t)used);

	/* go to sibling if there is one */
	if (ptr->sibling)
		(void) node_enum(el, ptr->sibling, cnt);
	return 0;
}


/* keymacro_kprint():
 *	Print the specified key and its associated
 *	function specified by val
 */
protected void
keymacro_kprint(EditLine *el, const Char *key, keymacro_value_t *val, int ntype)
{
	el_bindings_t *fp;
	char unparsbuf[EL_BUFSIZ];
	static const char fmt[] = "%-15s->  %s\n";

	if (val != NULL)
		switch (ntype) {
		case XK_STR:
		case XK_EXE:
			(void) keymacro__decode_str(val->str, unparsbuf,
			    sizeof(unparsbuf),
			    ntype == XK_STR ? "\"\"" : "[]");
			(void) fprintf(el->el_outfile, fmt,
			    ct_encode_string(key, &el->el_scratch), unparsbuf);
			break;
		case XK_CMD:
			for (fp = el->el_map.help; fp->name; fp++)
				if (val->cmd == fp->func) {
                    ct_wcstombs(unparsbuf, fp->name, sizeof(unparsbuf));
                    unparsbuf[sizeof(unparsbuf) -1] = '\0';
					(void) fprintf(el->el_outfile, fmt,
                        ct_encode_string(key, &el->el_scratch), unparsbuf);
					break;
				}
#ifdef DEBUG_KEY
			if (fp->name == NULL)
				(void) fprintf(el->el_outfile,
				    "BUG! Command not found.\n");
#endif

			break;
		default:
			EL_ABORT((el->el_errfile, "Bad XK_ type %d\n", ntype));
			break;
		}
	else
		(void) fprintf(el->el_outfile, fmt, ct_encode_string(key,
		    &el->el_scratch), "no input");
}


#define ADDC(c) \
	if (b < eb) \
		*b++ = c; \
	else \
		b++
/* keymacro__decode_str():
 *	Make a printable version of the ey
 */
protected size_t
keymacro__decode_str(const Char *str, char *buf, size_t len, const char *sep)
{
	char *b = buf, *eb = b + len;
	const Char *p;

	b = buf;
	if (sep[0] != '\0') {
		ADDC(sep[0]);
	}
	if (*str == '\0') {
		ADDC('^');
		ADDC('@');
		goto add_endsep;
	}
	for (p = str; *p != 0; p++) {
		Char dbuf[VISUAL_WIDTH_MAX];
		Char *p2 = dbuf;
		ssize_t l = ct_visual_char(dbuf, VISUAL_WIDTH_MAX, *p);
		while (l-- > 0) {
			ssize_t n = ct_encode_char(b, (size_t)(eb - b), *p2++);
			if (n == -1) /* ran out of space */
				goto add_endsep;
			else
				b += n;
		}
	}
add_endsep:
	if (sep[0] != '\0' && sep[1] != '\0') {
		ADDC(sep[1]);
	}
	ADDC('\0');
	if ((size_t)(b - buf) >= len)
	    buf[len - 1] = '\0';
	return (size_t)(b - buf);
}
