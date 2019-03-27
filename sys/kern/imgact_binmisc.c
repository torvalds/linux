/*-
 * Copyright (c) 2013-16, Stacey D. Son
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

#include <sys/param.h>
#include <sys/ctype.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/imgact_binmisc.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/sx.h>

#include <machine/atomic.h>

/**
 * Miscellaneous binary interpreter image activator.
 *
 * If the given target executable's header matches 'xbe_magic' field in the
 * 'interpreter_list' then it will use the user-level interpreter specified in
 * the 'xbe_interpreter' field to execute the binary. The 'xbe_magic' field may
 * be adjusted to a given offset using the value in the 'xbe_moffset' field
 * and bits of the header may be masked using the 'xbe_mask' field.  The
 * 'interpreter_list' entries are managed using sysctl(3) as described in the
 * <sys/imgact_binmisc.h> file.
 */

/*
 * Node of the interpreter list.
 */
typedef struct imgact_binmisc_entry {
	char				 *ibe_name;
	uint8_t				 *ibe_magic;
	uint32_t			  ibe_moffset;
	uint32_t			  ibe_msize;
	uint8_t				 *ibe_mask;
	uint8_t				 *ibe_interpreter;
	uint32_t			  ibe_interp_argcnt;
	uint32_t			  ibe_interp_length;
	uint32_t			  ibe_flags;
	SLIST_ENTRY(imgact_binmisc_entry) link;
} imgact_binmisc_entry_t;

/*
 * sysctl() commands.
 */
#define IBC_ADD		1	/* Add given entry. */
#define IBC_REMOVE	2	/* Remove entry for a given name. */
#define IBC_DISABLE	3	/* Disable entry for a given name. */
#define IBC_ENABLE	4	/* Enable entry for a given name. */
#define IBC_LOOKUP	5	/* Lookup and return entry for given name. */
#define IBC_LIST	6	/* Get a snapshot of the interpretor list. */

/*
 * Interpreter string macros.
 *
 * They all start with '#' followed by a single letter:
 */
#define	ISM_POUND	'#'	/* "##" is the escape sequence for single #. */
#define	ISM_OLD_ARGV0	'a'	/* "#a" is replaced with the old argv0. */

MALLOC_DEFINE(M_BINMISC, KMOD_NAME, "misc binary image activator");

/* The interpreter list. */
static SLIST_HEAD(, imgact_binmisc_entry) interpreter_list =
	SLIST_HEAD_INITIALIZER(interpreter_list);

static int interp_list_entry_count = 0;

static struct sx interp_list_sx;

/*
 * Populate the entry with the information about the interpreter.
 */
static void
imgact_binmisc_populate_interp(char *str, imgact_binmisc_entry_t *ibe)
{
	uint32_t len = 0, argc = 1;
	char t[IBE_INTERP_LEN_MAX];
	char *sp, *tp;

	memset(t, 0, sizeof(t));

	/*
	 * Normalize interpreter string. Replace white space between args with
	 * single space.
	 */
	sp = str; tp = t;
	while (*sp != '\0') {
		if (*sp == ' ' || *sp == '\t') {
			if (++len >= IBE_INTERP_LEN_MAX)
				break;
			*tp++ = ' ';
			argc++;
			while (*sp == ' ' || *sp == '\t')
				sp++;
			continue;
		} else {
			*tp++ = *sp++;
			len++;
		}
	}
	*tp = '\0';
	len++;

	ibe->ibe_interpreter = malloc(len, M_BINMISC, M_WAITOK|M_ZERO);

	/* Populate all the ibe fields for the interpreter. */
	memcpy(ibe->ibe_interpreter, t, len);
	ibe->ibe_interp_argcnt = argc;
	ibe->ibe_interp_length = len;
}

/*
 * Allocate memory and populate a new entry for the interpreter table.
 */
static imgact_binmisc_entry_t *
imgact_binmisc_new_entry(ximgact_binmisc_entry_t *xbe)
{
	imgact_binmisc_entry_t *ibe = NULL;
	size_t namesz = min(strlen(xbe->xbe_name) + 1, IBE_NAME_MAX);

	ibe = malloc(sizeof(*ibe), M_BINMISC, M_WAITOK|M_ZERO);

	ibe->ibe_name = malloc(namesz, M_BINMISC, M_WAITOK|M_ZERO);
	strlcpy(ibe->ibe_name, xbe->xbe_name, namesz);

	imgact_binmisc_populate_interp(xbe->xbe_interpreter, ibe);

	ibe->ibe_magic = malloc(xbe->xbe_msize, M_BINMISC, M_WAITOK|M_ZERO);
	memcpy(ibe->ibe_magic, xbe->xbe_magic, xbe->xbe_msize);

	ibe->ibe_mask = malloc(xbe->xbe_msize, M_BINMISC, M_WAITOK|M_ZERO);
	memcpy(ibe->ibe_mask, xbe->xbe_mask, xbe->xbe_msize);

	ibe->ibe_moffset = xbe->xbe_moffset;
	ibe->ibe_msize = xbe->xbe_msize;
	ibe->ibe_flags = xbe->xbe_flags;

	return (ibe);
}

/*
 * Free the allocated memory for a given list item.
 */
static void
imgact_binmisc_destroy_entry(imgact_binmisc_entry_t *ibe)
{
	if (!ibe)
		return;
	if (ibe->ibe_magic)
		free(ibe->ibe_magic, M_BINMISC);
	if (ibe->ibe_mask)
		free(ibe->ibe_mask, M_BINMISC);
	if (ibe->ibe_interpreter)
		free(ibe->ibe_interpreter, M_BINMISC);
	if (ibe->ibe_name)
		free(ibe->ibe_name, M_BINMISC);
	if (ibe)
		free(ibe, M_BINMISC);
}

/*
 * Find the interpreter in the list by the given name.  Return NULL if not
 * found.
 */
static imgact_binmisc_entry_t *
imgact_binmisc_find_entry(char *name)
{
	imgact_binmisc_entry_t *ibe;

	sx_assert(&interp_list_sx, SA_LOCKED);

	SLIST_FOREACH(ibe, &interpreter_list, link) {
		if (strncmp(name, ibe->ibe_name, IBE_NAME_MAX) == 0)
			return (ibe);
	}

	return (NULL);
}

/*
 * Add the given interpreter if it doesn't already exist.  Return EEXIST
 * if the name already exist in the interpreter list.
 */
static int
imgact_binmisc_add_entry(ximgact_binmisc_entry_t *xbe)
{
	imgact_binmisc_entry_t *ibe;
	char *p;
	int cnt;

	if (xbe->xbe_msize > IBE_MAGIC_MAX)
		return (EINVAL);

	for(cnt = 0, p = xbe->xbe_name; *p != 0; cnt++, p++)
		if (cnt >= IBE_NAME_MAX || !isascii((int)*p))
			return (EINVAL);

	for(cnt = 0, p = xbe->xbe_interpreter; *p != 0; cnt++, p++)
		if (cnt >= IBE_INTERP_LEN_MAX || !isascii((int)*p))
			return (EINVAL);

	/* Make sure we don't have any invalid #'s. */
	p = xbe->xbe_interpreter;
	while (1) {
		p = strchr(p, '#');
		if (!p)
			break;

		p++;
		switch(*p) {
		case ISM_POUND:
			/* "##" */
			p++;
			break;

		case ISM_OLD_ARGV0:
			/* "#a" */
			p++;
			break;

		case 0:
		default:
			/* Anything besides the above is invalid. */
			return (EINVAL);
		}
	}

	sx_xlock(&interp_list_sx);
	if (imgact_binmisc_find_entry(xbe->xbe_name) != NULL) {
		sx_xunlock(&interp_list_sx);
		return (EEXIST);
	}

	/* Preallocate a new entry. */
	ibe = imgact_binmisc_new_entry(xbe);

	SLIST_INSERT_HEAD(&interpreter_list, ibe, link);
	interp_list_entry_count++;
	sx_xunlock(&interp_list_sx);

	return (0);
}

/*
 * Remove the interpreter in the list with the given name. Return ENOENT
 * if not found.
 */
static int
imgact_binmisc_remove_entry(char *name)
{
	imgact_binmisc_entry_t *ibe;

	sx_xlock(&interp_list_sx);
	if ((ibe = imgact_binmisc_find_entry(name)) == NULL) {
		sx_xunlock(&interp_list_sx);
		return (ENOENT);
	}
	SLIST_REMOVE(&interpreter_list, ibe, imgact_binmisc_entry, link);
	interp_list_entry_count--;
	sx_xunlock(&interp_list_sx);

	imgact_binmisc_destroy_entry(ibe);

	return (0);
}

/*
 * Disable the interpreter in the list with the given name. Return ENOENT
 * if not found.
 */
static int
imgact_binmisc_disable_entry(char *name)
{
	imgact_binmisc_entry_t *ibe;

	sx_xlock(&interp_list_sx);
	if ((ibe = imgact_binmisc_find_entry(name)) == NULL) {
		sx_xunlock(&interp_list_sx);
		return (ENOENT);
	}

	ibe->ibe_flags &= ~IBF_ENABLED;
	sx_xunlock(&interp_list_sx);

	return (0);
}

/*
 * Enable the interpreter in the list with the given name. Return ENOENT
 * if not found.
 */
static int
imgact_binmisc_enable_entry(char *name)
{
	imgact_binmisc_entry_t *ibe;

	sx_xlock(&interp_list_sx);
	if ((ibe = imgact_binmisc_find_entry(name)) == NULL) {
		sx_xunlock(&interp_list_sx);
		return (ENOENT);
	}

	ibe->ibe_flags |= IBF_ENABLED;
	sx_xunlock(&interp_list_sx);

	return (0);
}

static int
imgact_binmisc_populate_xbe(ximgact_binmisc_entry_t *xbe,
    imgact_binmisc_entry_t *ibe)
{
	uint32_t i;

	sx_assert(&interp_list_sx, SA_LOCKED);

	memset(xbe, 0, sizeof(*xbe));
	strlcpy(xbe->xbe_name, ibe->ibe_name, IBE_NAME_MAX);

	/* Copy interpreter string.  Replace NULL breaks with space. */
	memcpy(xbe->xbe_interpreter, ibe->ibe_interpreter,
	    ibe->ibe_interp_length);
	for(i = 0; i < (ibe->ibe_interp_length - 1); i++)
		if (xbe->xbe_interpreter[i] == '\0')
			xbe->xbe_interpreter[i] = ' ';

	memcpy(xbe->xbe_magic, ibe->ibe_magic, ibe->ibe_msize);
	memcpy(xbe->xbe_mask, ibe->ibe_mask, ibe->ibe_msize);
	xbe->xbe_version = IBE_VERSION;
	xbe->xbe_flags = ibe->ibe_flags;
	xbe->xbe_moffset = ibe->ibe_moffset;
	xbe->xbe_msize = ibe->ibe_msize;

	return (0);
}

/*
 * Retrieve the interpreter with the give name and populate the
 * ximgact_binmisc_entry structure.  Return ENOENT if not found.
 */
static int
imgact_binmisc_lookup_entry(char *name, ximgact_binmisc_entry_t *xbe)
{
	imgact_binmisc_entry_t *ibe;
	int error = 0;

	sx_slock(&interp_list_sx);
	if ((ibe = imgact_binmisc_find_entry(name)) == NULL) {
		sx_sunlock(&interp_list_sx);
		return (ENOENT);
	}

	error = imgact_binmisc_populate_xbe(xbe, ibe);
	sx_sunlock(&interp_list_sx);

	return (error);
}

/*
 * Get a snapshot of all the interpreter entries in the list.
 */
static int
imgact_binmisc_get_all_entries(struct sysctl_req *req)
{
	ximgact_binmisc_entry_t *xbe, *xbep;
	imgact_binmisc_entry_t *ibe;
	int error = 0, count;

	sx_slock(&interp_list_sx);
	count = interp_list_entry_count;
	xbe = malloc(sizeof(*xbe) * count, M_BINMISC, M_WAITOK|M_ZERO);

	xbep = xbe;
	SLIST_FOREACH(ibe, &interpreter_list, link) {
		error = imgact_binmisc_populate_xbe(xbep++, ibe);
		if (error)
			break;
	}
	sx_sunlock(&interp_list_sx);

	if (!error)
		error = SYSCTL_OUT(req, xbe, sizeof(*xbe) * count);

	free(xbe, M_BINMISC);
	return (error);
}

/*
 * sysctl() handler for munipulating interpretor table.
 * Not MP safe (locked by sysctl).
 */
static int
sysctl_kern_binmisc(SYSCTL_HANDLER_ARGS)
{
	ximgact_binmisc_entry_t xbe;
	int error = 0;

	switch(arg2) {
	case IBC_ADD:
		/* Add an entry. Limited to IBE_MAX_ENTRIES. */
		error = SYSCTL_IN(req, &xbe, sizeof(xbe));
		if (error)
			return (error);
		if (IBE_VERSION != xbe.xbe_version)
			return (EINVAL);
		if (interp_list_entry_count == IBE_MAX_ENTRIES)
			return (ENOSPC);
		error = imgact_binmisc_add_entry(&xbe);
		break;

	case IBC_REMOVE:
		/* Remove an entry. */
		error = SYSCTL_IN(req, &xbe, sizeof(xbe));
		if (error)
			return (error);
		if (IBE_VERSION != xbe.xbe_version)
			return (EINVAL);
		error = imgact_binmisc_remove_entry(xbe.xbe_name);
		break;

	case IBC_DISABLE:
		/* Disable an entry. */
		error = SYSCTL_IN(req, &xbe, sizeof(xbe));
		if (error)
			return (error);
		if (IBE_VERSION != xbe.xbe_version)
			return (EINVAL);
		error = imgact_binmisc_disable_entry(xbe.xbe_name);
		break;

	case IBC_ENABLE:
		/* Enable an entry. */
		error = SYSCTL_IN(req, &xbe, sizeof(xbe));
		if (error)
			return (error);
		if (IBE_VERSION != xbe.xbe_version)
			return (EINVAL);
		error = imgact_binmisc_enable_entry(xbe.xbe_name);
		break;

	case IBC_LOOKUP:
		/* Lookup an entry. */
		error = SYSCTL_IN(req, &xbe, sizeof(xbe));
		if (error)
			return (error);
		if (IBE_VERSION != xbe.xbe_version)
			return (EINVAL);
		error = imgact_binmisc_lookup_entry(xbe.xbe_name, &xbe);
		if (!error)
			error = SYSCTL_OUT(req, &xbe, sizeof(xbe));
		break;

	case IBC_LIST:
		/* Return a snapshot of the interpretor list. */

		if (!req->oldptr) {
			/* No pointer then just return the list size. */
			error = SYSCTL_OUT(req, 0, interp_list_entry_count *
			    sizeof(ximgact_binmisc_entry_t));
			return (error);
		} else
			if (!req->oldlen)
				return (EINVAL);

		error = imgact_binmisc_get_all_entries(req);
		break;

	default:
		return (EINVAL);
	}

	return (error);
}

SYSCTL_NODE(_kern, OID_AUTO, binmisc, CTLFLAG_RW, 0,
    "Image activator for miscellaneous binaries");

SYSCTL_PROC(_kern_binmisc, OID_AUTO, add,
    CTLFLAG_MPSAFE|CTLTYPE_STRUCT|CTLFLAG_WR, NULL, IBC_ADD,
    sysctl_kern_binmisc, "S,ximgact_binmisc_entry",
    "Add an activator entry");

SYSCTL_PROC(_kern_binmisc, OID_AUTO, remove,
    CTLFLAG_MPSAFE|CTLTYPE_STRUCT|CTLFLAG_WR, NULL, IBC_REMOVE,
    sysctl_kern_binmisc, "S,ximgact_binmisc_entry",
    "Remove an activator entry");

SYSCTL_PROC(_kern_binmisc, OID_AUTO, disable,
    CTLFLAG_MPSAFE|CTLTYPE_STRUCT|CTLFLAG_WR, NULL, IBC_DISABLE,
    sysctl_kern_binmisc, "S,ximgact_binmisc_entry",
    "Disable an activator entry");

SYSCTL_PROC(_kern_binmisc, OID_AUTO, enable,
    CTLFLAG_MPSAFE|CTLTYPE_STRUCT|CTLFLAG_WR, NULL, IBC_ENABLE,
    sysctl_kern_binmisc, "S,ximgact_binmisc_entry",
    "Enable an activator entry");

SYSCTL_PROC(_kern_binmisc, OID_AUTO, lookup,
    CTLFLAG_MPSAFE|CTLTYPE_STRUCT|CTLFLAG_RW|CTLFLAG_ANYBODY, NULL, IBC_LOOKUP,
    sysctl_kern_binmisc, "S,ximgact_binmisc_entry",
    "Lookup an activator entry");

SYSCTL_PROC(_kern_binmisc, OID_AUTO, list,
    CTLFLAG_MPSAFE|CTLTYPE_STRUCT|CTLFLAG_RD|CTLFLAG_ANYBODY, NULL, IBC_LIST,
    sysctl_kern_binmisc, "S,ximgact_binmisc_entry",
    "Get snapshot of all the activator entries");

static imgact_binmisc_entry_t *
imgact_binmisc_find_interpreter(const char *image_header)
{
	imgact_binmisc_entry_t *ibe;
	const char *p;
	int i;
	size_t sz;

	sx_assert(&interp_list_sx, SA_LOCKED);

	SLIST_FOREACH(ibe, &interpreter_list, link) {
		if (!(IBF_ENABLED & ibe->ibe_flags))
			continue;

		p = image_header + ibe->ibe_moffset;
		sz = ibe->ibe_msize;
		if (IBF_USE_MASK & ibe->ibe_flags) {
			/* Compare using mask. */
			for (i = 0; i < sz; i++)
				if ((*p++ ^ ibe->ibe_magic[i]) &
				    ibe->ibe_mask[i])
					break;
		} else {
			for (i = 0; i < sz; i++)
				if (*p++ ^ ibe->ibe_magic[i])
					break;
		}
		if (i == ibe->ibe_msize)
			return (ibe);
	}
	return (NULL);
}

static int
imgact_binmisc_exec(struct image_params *imgp)
{
	const char *image_header = imgp->image_header;
	const char *fname = NULL;
	int error = 0;
	size_t offset, l;
	imgact_binmisc_entry_t *ibe;
	struct sbuf *sname;
	char *s, *d;

	/* Do we have an interpreter for the given image header? */
	sx_slock(&interp_list_sx);
	if ((ibe = imgact_binmisc_find_interpreter(image_header)) == NULL) {
		sx_sunlock(&interp_list_sx);
		return (-1);
	}

	/* No interpreter nesting allowed. */
	if (imgp->interpreted & IMGACT_BINMISC) {
		sx_sunlock(&interp_list_sx);
		return (ENOEXEC);
	}

	imgp->interpreted |= IMGACT_BINMISC;

	if (imgp->args->fname != NULL) {
		fname = imgp->args->fname;
		sname = NULL;
	} else {
		/* Use the fdescfs(5) path for fexecve(2). */
		sname = sbuf_new_auto();
		sbuf_printf(sname, "/dev/fd/%d", imgp->args->fd);
		sbuf_finish(sname);
		fname = sbuf_data(sname);
	}


	/*
	 * We need to "push" the interpreter in the arg[] list.  To do this,
	 * we first shift all the other values in the `begin_argv' area to
	 * provide the exact amount of room for the values added.  Set up
	 * `offset' as the number of bytes to be added to the `begin_argv'
	 * area.
	 */
	offset = ibe->ibe_interp_length;

	/* Adjust the offset for #'s. */
	s = ibe->ibe_interpreter;
	while (1) {
		s = strchr(s, '#');
		if (!s)
			break;

		s++;
		switch(*s) {
		case ISM_POUND:
			/* "##" -> "#": reduce offset by one. */
			offset--;
			break;

		case ISM_OLD_ARGV0:
			/* "#a" -> (old argv0): increase offset to fit fname */
			offset += strlen(fname) - 2;
			break;

		default:
			/* Hmm... This shouldn't happen. */
			sx_sunlock(&interp_list_sx);
			printf("%s: Unknown macro #%c sequence in "
			    "interpreter string\n", KMOD_NAME, *(s + 1));
			error = EINVAL;
			goto done;
		}
		s++;
	}

	/* Make room for the interpreter */
	error = exec_args_adjust_args(imgp->args, 0, offset);
	if (error != 0) {
		sx_sunlock(&interp_list_sx);
		goto done;
	}

	/* Add the new argument(s) in the count. */
	imgp->args->argc += ibe->ibe_interp_argcnt;

	/*
	 * The original arg[] list has been shifted appropriately.  Copy in
	 * the interpreter path.
	 */
	s = ibe->ibe_interpreter;
	d = imgp->args->begin_argv;
	while(*s != '\0') {
		switch (*s) {
		case '#':
			/* Handle "#" in interpreter string. */
			s++;
			switch(*s) {
			case ISM_POUND:
				/* "##": Replace with a single '#' */
				*d++ = '#';
				break;

			case ISM_OLD_ARGV0:
				/* "#a": Replace with old arg0 (fname). */
				if ((l = strlen(fname)) != 0) {
					memcpy(d, fname, l);
					d += l;
				}
				break;

			default:
				/* Shouldn't happen but skip it if it does. */
				break;
			}
			break;

		case ' ':
			/* Replace space with NUL to separate arguments. */
			*d++ = '\0';
			break;

		default:
			*d++ = *s;
			break;
		}
		s++;
	}
	*d = '\0';
	sx_sunlock(&interp_list_sx);

	if (!error)
		imgp->interpreter_name = imgp->args->begin_argv;


done:
	if (sname)
		sbuf_delete(sname);
	return (error);
}

static void
imgact_binmisc_init(void *arg)
{

	sx_init(&interp_list_sx, KMOD_NAME);
}

static void
imgact_binmisc_fini(void *arg)
{
	imgact_binmisc_entry_t *ibe, *ibe_tmp;

	/* Free all the interpreters. */
	sx_xlock(&interp_list_sx);
	SLIST_FOREACH_SAFE(ibe, &interpreter_list, link, ibe_tmp) {
		SLIST_REMOVE(&interpreter_list, ibe, imgact_binmisc_entry,
		    link);
		imgact_binmisc_destroy_entry(ibe);
	}
	sx_xunlock(&interp_list_sx);

	sx_destroy(&interp_list_sx);
}

SYSINIT(imgact_binmisc, SI_SUB_EXEC, SI_ORDER_MIDDLE, imgact_binmisc_init,
    NULL);
SYSUNINIT(imgact_binmisc, SI_SUB_EXEC, SI_ORDER_MIDDLE, imgact_binmisc_fini,
    NULL);

/*
 * Tell kern_execve.c about it, with a little help from the linker.
 */
static struct execsw imgact_binmisc_execsw = {
	.ex_imgact = imgact_binmisc_exec,
	.ex_name = KMOD_NAME
};
EXEC_SET(imgact_binmisc, imgact_binmisc_execsw);
