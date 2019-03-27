/*-
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/ctype.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#else /* !_KERNEL */

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#endif /* _KERNEL */

#include "bhnd_nvram_private.h"

#include "bhnd_nvram_datavar.h"

#include "bhnd_nvram_data_bcmreg.h"	/* for BCM_NVRAM_MAGIC */

/**
 * Broadcom "Board Text" data class.
 *
 * This format is used to provide external NVRAM data for some
 * fullmac WiFi devices, and as an input format when programming
 * NVRAM/SPROM/OTP.
 */

struct bhnd_nvram_btxt {
	struct bhnd_nvram_data	 nv;	/**< common instance state */
	struct bhnd_nvram_io	*data;	/**< memory-backed board text data */
	size_t			 count;	/**< variable count */
};

BHND_NVRAM_DATA_CLASS_DEFN(btxt, "Broadcom Board Text",
    BHND_NVRAM_DATA_CAP_DEVPATHS, sizeof(struct bhnd_nvram_btxt))

/** Minimal identification header */
union bhnd_nvram_btxt_ident {
	uint32_t	bcm_magic;
	char		btxt[8];
};

static void	*bhnd_nvram_btxt_offset_to_cookiep(struct bhnd_nvram_btxt *btxt,
		 size_t io_offset);
static size_t	 bhnd_nvram_btxt_cookiep_to_offset(struct bhnd_nvram_btxt *btxt,
		     void *cookiep);

static int	bhnd_nvram_btxt_entry_len(struct bhnd_nvram_io *io,
		    size_t offset, size_t *line_len, size_t *env_len);
static int	bhnd_nvram_btxt_seek_next(struct bhnd_nvram_io *io,
		    size_t *offset);
static int	bhnd_nvram_btxt_seek_eol(struct bhnd_nvram_io *io,
		    size_t *offset);

static int
bhnd_nvram_btxt_probe(struct bhnd_nvram_io *io)
{
	union bhnd_nvram_btxt_ident	ident;
	char				c;
	int				error;

	/* Look at the initial header for something that looks like 
	 * an ASCII board text file */
	if ((error = bhnd_nvram_io_read(io, 0x0, &ident, sizeof(ident))))
		return (error);

	/* The BCM NVRAM format uses a 'FLSH' little endian magic value, which
	 * shouldn't be interpreted as BTXT */
	if (le32toh(ident.bcm_magic) == BCM_NVRAM_MAGIC)
		return (ENXIO);

	/* Don't match on non-ASCII/non-printable data */
	for (size_t i = 0; i < nitems(ident.btxt); i++) {
		c = ident.btxt[i];
		if (!bhnd_nv_isprint(c))
			return (ENXIO);
	}

	/* The first character should either be a valid key char (alpha),
	 * whitespace, or the start of a comment ('#') */
	c = ident.btxt[0];
	if (!bhnd_nv_isspace(c) && !bhnd_nv_isalpha(c) && c != '#')
		return (ENXIO);

	/* We assert a low priority, given that we've only scanned an
	 * initial few bytes of the file. */
	return (BHND_NVRAM_DATA_PROBE_MAYBE);
}


/**
 * Parser states for bhnd_nvram_bcm_getvar_direct_common().
 */
typedef enum {
	BTXT_PARSE_LINE_START,
	BTXT_PARSE_KEY,
	BTXT_PARSE_KEY_END,
	BTXT_PARSE_NEXT_LINE,
	BTXT_PARSE_VALUE_START,
	BTXT_PARSE_VALUE
} btxt_parse_state;

static int
bhnd_nvram_btxt_getvar_direct(struct bhnd_nvram_io *io, const char *name,
    void *outp, size_t *olen, bhnd_nvram_type otype)
{
	char				 buf[512];
	btxt_parse_state		 pstate;
	size_t				 limit, offset;
	size_t				 buflen, bufpos;
	size_t				 namelen, namepos;
	size_t				 vlen;
	int				 error;

	limit = bhnd_nvram_io_getsize(io);
	offset = 0;

	/* Loop our parser until we find the requested variable, or hit EOF */
	pstate = BTXT_PARSE_LINE_START;
	buflen = 0;
	bufpos = 0;
	namelen = strlen(name);
	namepos = 0;
	vlen = 0;

	while ((offset - bufpos) < limit) {
		BHND_NV_ASSERT(bufpos <= buflen,
		    ("buf position invalid (%zu > %zu)", bufpos, buflen));
		BHND_NV_ASSERT(buflen <= sizeof(buf),
		    ("buf length invalid (%zu > %zu", buflen, sizeof(buf)));

		/* Repopulate our parse buffer? */
		if (buflen - bufpos == 0) {
			BHND_NV_ASSERT(offset < limit, ("offset overrun"));

			buflen = bhnd_nv_ummin(sizeof(buf), limit - offset);
			bufpos = 0;

			error = bhnd_nvram_io_read(io, offset, buf, buflen);
			if (error)
				return (error);

			offset += buflen;
		}

		switch (pstate) {
		case BTXT_PARSE_LINE_START:
			BHND_NV_ASSERT(bufpos < buflen, ("empty buffer!"));

			/* Reset name matching position */
			namepos = 0;

			/* Trim any leading whitespace */
			while (bufpos < buflen && bhnd_nv_isspace(buf[bufpos]))
			{
				bufpos++;
			}

			if (bufpos == buflen) {
				/* Continue parsing the line */
				pstate = BTXT_PARSE_LINE_START;
			} else if (bufpos < buflen && buf[bufpos] == '#') {
				/* Comment; skip to next line */
				pstate = BTXT_PARSE_NEXT_LINE;
			} else {
				/* Start name matching */
				pstate = BTXT_PARSE_KEY;
			}


			break;

		case BTXT_PARSE_KEY: {
			size_t navail, nleft;

			nleft = namelen - namepos;
			navail = bhnd_nv_ummin(buflen - bufpos, nleft);

			if (strncmp(name+namepos, buf+bufpos, navail) == 0) {
				/* Matched */
				namepos += navail;
				bufpos += navail;

				if (namepos == namelen) {
					/* Matched the full variable; look for
					 * its trailing delimiter */
					pstate = BTXT_PARSE_KEY_END;
				} else {
					/* Continue matching the name */
					pstate = BTXT_PARSE_KEY;
				}
			} else {
				/* No match; advance to next entry and restart
				 * name matching */
				pstate = BTXT_PARSE_NEXT_LINE;
			}

			break;
		}

		case BTXT_PARSE_KEY_END:
			BHND_NV_ASSERT(bufpos < buflen, ("empty buffer!"));

			if (buf[bufpos] == '=') {
				/* Key fully matched; advance past '=' and
				 * parse the value */
				bufpos++;
				pstate = BTXT_PARSE_VALUE_START;
			} else {
				/* No match; advance to next line and restart
				 * name matching */
				pstate = BTXT_PARSE_NEXT_LINE;
			}

			break;

		case BTXT_PARSE_NEXT_LINE: {
			const char *p;

			/* Scan for a '\r', '\n', or '\r\n' terminator */
			p = memchr(buf+bufpos, '\n', buflen - bufpos);
			if (p == NULL)
				p = memchr(buf+bufpos, '\r', buflen - bufpos);

			if (p != NULL) {
				/* Found entry terminator; restart name
				 * matching at next line */
				pstate = BTXT_PARSE_LINE_START;
				bufpos = (p - buf);
			} else {
				/* Consumed full buffer looking for newline; 
				 * force repopulation of the buffer and
				 * retry */
				pstate = BTXT_PARSE_NEXT_LINE;
				bufpos = buflen;
			}

			break;
		}

		case BTXT_PARSE_VALUE_START: {
			const char *p;

			/* Scan for a terminating newline */
			p = memchr(buf+bufpos, '\n', buflen - bufpos);
			if (p == NULL)
				p = memchr(buf+bufpos, '\r', buflen - bufpos);

			if (p != NULL) {
				/* Found entry terminator; parse the value */
				vlen = p - &buf[bufpos];
				pstate = BTXT_PARSE_VALUE;

			} else if (p == NULL && offset == limit) {
				/* Hit EOF without a terminating newline;
				 * treat the entry as implicitly terminated */
				vlen = buflen - bufpos;
				pstate = BTXT_PARSE_VALUE;

			} else if (p == NULL && bufpos > 0) {
				size_t	nread;

				/* Move existing value data to start of
				 * buffer */
				memmove(buf, buf+bufpos, buflen - bufpos);
				buflen = bufpos;
				bufpos = 0;

				/* Populate full buffer to allow retry of
				 * value parsing */
				nread = bhnd_nv_ummin(sizeof(buf) - buflen,
				    limit - offset);

				error = bhnd_nvram_io_read(io, offset,
				    buf+buflen, nread);
				if (error)
					return (error);

				offset += nread;
				buflen += nread;
			} else {
				/* Value exceeds our buffer capacity */
				BHND_NV_LOG("cannot parse value for '%s' "
				    "(exceeds %zu byte limit)\n", name,
				    sizeof(buf));

				return (ENXIO);
			}

			break;
		}

		case BTXT_PARSE_VALUE:
			BHND_NV_ASSERT(vlen <= buflen, ("value buf overrun"));

			/* Trim any trailing whitespace */
			while (vlen > 0 && bhnd_nv_isspace(buf[bufpos+vlen-1]))
				vlen--;

			/* Write the value to the caller's buffer */
			return (bhnd_nvram_value_coerce(buf+bufpos, vlen,
			    BHND_NVRAM_TYPE_STRING, outp, olen, otype));
		}
	}

	/* Variable not found */
	return (ENOENT);
}

static int
bhnd_nvram_btxt_serialize(bhnd_nvram_data_class *cls, bhnd_nvram_plist *props,
    bhnd_nvram_plist *options, void *outp, size_t *olen)
{
	bhnd_nvram_prop	*prop;
	size_t		 limit, nbytes;
	int		 error;

	/* Determine output byte limit */
	if (outp != NULL)
		limit = *olen;
	else
		limit = 0;

	nbytes = 0;

	/* Write all properties */
	prop = NULL;
	while ((prop = bhnd_nvram_plist_next(props, prop)) != NULL) {
		const char	*name;
		char		*p;
		size_t		 prop_limit;
		size_t		 name_len, value_len;

		if (outp == NULL || limit < nbytes) {
			p = NULL;
			prop_limit = 0;
		} else {
			p = ((char *)outp) + nbytes;
			prop_limit = limit - nbytes;
		}

		/* Fetch and write 'name=' to output */
		name = bhnd_nvram_prop_name(prop);
		name_len = strlen(name) + 1;

		if (prop_limit > name_len) {
			memcpy(p, name, name_len - 1);
			p[name_len - 1] = '=';

			prop_limit -= name_len;
			p += name_len;
		} else {
			prop_limit = 0;
			p = NULL;
		}

		/* Advance byte count */
		if (SIZE_MAX - nbytes < name_len)
			return (EFTYPE); /* would overflow size_t */

		nbytes += name_len;

		/* Write NUL-terminated value to output, rewrite NUL as
		 * '\n' record delimiter */
		value_len = prop_limit;
		error = bhnd_nvram_prop_encode(prop, p, &value_len,
		    BHND_NVRAM_TYPE_STRING);
		if (p != NULL && error == 0) {
			/* Replace trailing '\0' with newline */
			BHND_NV_ASSERT(value_len > 0, ("string length missing "
			    "minimum required trailing NUL"));

			*(p + (value_len - 1)) = '\n';
		} else if (error && error != ENOMEM) {
			/* If encoding failed for any reason other than ENOMEM
			 * (which we'll detect and report after encoding all
			 * properties), return immediately */
			BHND_NV_LOG("error serializing %s to required type "
			    "%s: %d\n", name,
			    bhnd_nvram_type_name(BHND_NVRAM_TYPE_STRING),
			    error);
			return (error);
		}

		/* Advance byte count */
		if (SIZE_MAX - nbytes < value_len)
			return (EFTYPE); /* would overflow size_t */

		nbytes += value_len;
	}

	/* Provide required length */
	*olen = nbytes;
	if (limit < *olen) {
		if (outp == NULL)
			return (0);

		return (ENOMEM);
	}

	return (0);
}

/**
 * Initialize @p btxt with the provided board text data mapped by @p src.
 * 
 * @param btxt A newly allocated data instance.
 */
static int
bhnd_nvram_btxt_init(struct bhnd_nvram_btxt *btxt, struct bhnd_nvram_io *src)
{
	const void		*ptr;
	const char		*name, *value;
	size_t			 name_len, value_len;
	size_t			 line_len, env_len;
	size_t			 io_offset, io_size, str_size;
	int			 error;

	BHND_NV_ASSERT(btxt->data == NULL, ("btxt data already allocated"));
	
	if ((btxt->data = bhnd_nvram_iobuf_copy(src)) == NULL)
		return (ENOMEM);

	io_size = bhnd_nvram_io_getsize(btxt->data);
	io_offset = 0;

	/* Fetch a pointer mapping the entirity of the board text data */
	error = bhnd_nvram_io_read_ptr(btxt->data, 0x0, &ptr, io_size, NULL);
	if (error)
		return (error);

	/* Determine the actual size, minus any terminating NUL. We
	 * parse NUL-terminated C strings, but do not include NUL termination
	 * in our internal or serialized representations */
	str_size = strnlen(ptr, io_size);

	/* If the terminating NUL is not found at the end of the buffer,
	 * this is BCM-RAW or other NUL-delimited NVRAM format. */
	if (str_size < io_size && str_size + 1 < io_size)
		return (EINVAL);

	/* Adjust buffer size to account for NUL termination (if any) */
	io_size = str_size;
	if ((error = bhnd_nvram_io_setsize(btxt->data, io_size)))
		return (error);

	/* Process the buffer */
	btxt->count = 0;
	while (io_offset < io_size) {
		const void	*envp;

		/* Seek to the next key=value entry */
		if ((error = bhnd_nvram_btxt_seek_next(btxt->data, &io_offset)))
			return (error);

		/* Determine the entry and line length */
		error = bhnd_nvram_btxt_entry_len(btxt->data, io_offset,
		    &line_len, &env_len);
		if (error)
			return (error);
	
		/* EOF? */
		if (env_len == 0) {
			BHND_NV_ASSERT(io_offset == io_size,
		           ("zero-length record returned from "
			    "bhnd_nvram_btxt_seek_next()"));
			break;
		}

		/* Fetch a pointer to the line start */
		error = bhnd_nvram_io_read_ptr(btxt->data, io_offset, &envp,
		    env_len, NULL);
		if (error)
			return (error);

		/* Parse the key=value string */
		error = bhnd_nvram_parse_env(envp, env_len, '=', &name,
		    &name_len, &value, &value_len);
		if (error) {
			return (error);
		}

		/* Insert a '\0' character, replacing the '=' delimiter and
		 * allowing us to vend references directly to the variable
		 * name */
		error = bhnd_nvram_io_write(btxt->data, io_offset+name_len,
		    &(char){'\0'}, 1);
		if (error)
			return (error);

		/* Add to variable count */
		btxt->count++;

		/* Advance past EOL */
		io_offset += line_len;
	}

	return (0);
}

static int
bhnd_nvram_btxt_new(struct bhnd_nvram_data *nv, struct bhnd_nvram_io *io)
{
	struct bhnd_nvram_btxt	*btxt;
	int			 error;

	/* Allocate and initialize the BTXT data instance */
	btxt = (struct bhnd_nvram_btxt *)nv;

	/* Parse the BTXT input data and initialize our backing
	 * data representation */
	if ((error = bhnd_nvram_btxt_init(btxt, io))) {
		bhnd_nvram_btxt_free(nv);
		return (error);
	}

	return (0);
}

static void
bhnd_nvram_btxt_free(struct bhnd_nvram_data *nv)
{
	struct bhnd_nvram_btxt *btxt = (struct bhnd_nvram_btxt *)nv;
	if (btxt->data != NULL)
		bhnd_nvram_io_free(btxt->data);
}

size_t
bhnd_nvram_btxt_count(struct bhnd_nvram_data *nv)
{
	struct bhnd_nvram_btxt *btxt = (struct bhnd_nvram_btxt *)nv;
	return (btxt->count);
}

static bhnd_nvram_plist *
bhnd_nvram_btxt_options(struct bhnd_nvram_data *nv)
{
	return (NULL);
}

static uint32_t
bhnd_nvram_btxt_caps(struct bhnd_nvram_data *nv)
{
	return (BHND_NVRAM_DATA_CAP_READ_PTR|BHND_NVRAM_DATA_CAP_DEVPATHS);
}

static void *
bhnd_nvram_btxt_find(struct bhnd_nvram_data *nv, const char *name)
{
	return (bhnd_nvram_data_generic_find(nv, name));
}

static const char *
bhnd_nvram_btxt_next(struct bhnd_nvram_data *nv, void **cookiep)
{
	struct bhnd_nvram_btxt	*btxt;
	const void		*nptr;
	size_t			 io_offset, io_size;
	int			 error;

	btxt = (struct bhnd_nvram_btxt *)nv;

	io_size = bhnd_nvram_io_getsize(btxt->data);

	if (*cookiep == NULL) {
		/* Start search at initial file offset */
		io_offset = 0x0;
	} else {
		/* Start search after the current entry */
		io_offset = bhnd_nvram_btxt_cookiep_to_offset(btxt, *cookiep);

		/* Scan past the current entry by finding the next newline */
		error = bhnd_nvram_btxt_seek_eol(btxt->data, &io_offset);
		if (error) {
			BHND_NV_LOG("unexpected error in seek_eol(): %d\n",
			    error);
			return (NULL);
		}
	}

	/* Already at EOF? */
	if (io_offset == io_size)
		return (NULL);

	/* Seek to the first valid entry, or EOF */
	if ((error = bhnd_nvram_btxt_seek_next(btxt->data, &io_offset))) {
		BHND_NV_LOG("unexpected error in seek_next(): %d\n", error);
		return (NULL);
	}

	/* Hit EOF? */
	if (io_offset == io_size)
		return (NULL);

	/* Provide the new cookie for this offset */
	*cookiep = bhnd_nvram_btxt_offset_to_cookiep(btxt, io_offset);

	/* Fetch the name pointer; it must be at least 1 byte long */
	error = bhnd_nvram_io_read_ptr(btxt->data, io_offset, &nptr, 1, NULL);
	if (error) {
		BHND_NV_LOG("unexpected error in read_ptr(): %d\n", error);
		return (NULL);
	}

	/* Return the name pointer */
	return (nptr);
}

static int
bhnd_nvram_btxt_getvar_order(struct bhnd_nvram_data *nv, void *cookiep1,
    void *cookiep2)
{
	if (cookiep1 < cookiep2)
		return (-1);

	if (cookiep1 > cookiep2)
		return (1);

	return (0);
}

static int
bhnd_nvram_btxt_getvar(struct bhnd_nvram_data *nv, void *cookiep, void *buf,
    size_t *len, bhnd_nvram_type type)
{
	return (bhnd_nvram_data_generic_rp_getvar(nv, cookiep, buf, len, type));
}

static int
bhnd_nvram_btxt_copy_val(struct bhnd_nvram_data *nv, void *cookiep,
    bhnd_nvram_val **value)
{
	return (bhnd_nvram_data_generic_rp_copy_val(nv, cookiep, value));
}

const void *
bhnd_nvram_btxt_getvar_ptr(struct bhnd_nvram_data *nv, void *cookiep,
    size_t *len, bhnd_nvram_type *type)
{
	struct bhnd_nvram_btxt	*btxt;
	const void		*eptr;
	const char		*vptr;
	size_t			 io_offset, io_size;
	size_t			 line_len, env_len;
	int			 error;
	
	btxt = (struct bhnd_nvram_btxt *)nv;
	
	io_size = bhnd_nvram_io_getsize(btxt->data);
	io_offset = bhnd_nvram_btxt_cookiep_to_offset(btxt, cookiep);

	/* At EOF? */
	if (io_offset == io_size)
		return (NULL);

	/* Determine the entry length */
	error = bhnd_nvram_btxt_entry_len(btxt->data, io_offset, &line_len,
	    &env_len);
	if (error) {
		BHND_NV_LOG("unexpected error in entry_len(): %d\n", error);
		return (NULL);
	}

	/* Fetch the entry's value pointer and length */
	error = bhnd_nvram_io_read_ptr(btxt->data, io_offset, &eptr, env_len,
	    NULL);
	if (error) {
		BHND_NV_LOG("unexpected error in read_ptr(): %d\n", error);
		return (NULL);
	}

	error = bhnd_nvram_parse_env(eptr, env_len, '\0', NULL, NULL, &vptr,
	    len);
	if (error) {
		BHND_NV_LOG("unexpected error in parse_env(): %d\n", error);
		return (NULL);
	}

	/* Type is always CSTR */
	*type = BHND_NVRAM_TYPE_STRING;

	return (vptr);
}

static const char *
bhnd_nvram_btxt_getvar_name(struct bhnd_nvram_data *nv, void *cookiep)
{
	struct bhnd_nvram_btxt	*btxt;
	const void		*ptr;
	size_t			 io_offset, io_size;
	int			 error;
	
	btxt = (struct bhnd_nvram_btxt *)nv;
	
	io_size = bhnd_nvram_io_getsize(btxt->data);
	io_offset = bhnd_nvram_btxt_cookiep_to_offset(btxt, cookiep);

	/* At EOF? */
	if (io_offset == io_size)
		BHND_NV_PANIC("invalid cookiep: %p", cookiep);

	/* Variable name is found directly at the given offset; trailing
	 * NUL means we can assume that it's at least 1 byte long */
	error = bhnd_nvram_io_read_ptr(btxt->data, io_offset, &ptr, 1, NULL);
	if (error)
		BHND_NV_PANIC("unexpected error in read_ptr(): %d\n", error);

	return (ptr);
}

/**
 * Return a cookiep for the given I/O offset.
 */
static void *
bhnd_nvram_btxt_offset_to_cookiep(struct bhnd_nvram_btxt *btxt,
    size_t io_offset)
{
	const void	*ptr;
	int		 error;

	BHND_NV_ASSERT(io_offset < bhnd_nvram_io_getsize(btxt->data),
	    ("io_offset %zu out-of-range", io_offset));
	BHND_NV_ASSERT(io_offset < UINTPTR_MAX,
	    ("io_offset %#zx exceeds UINTPTR_MAX", io_offset));

	error = bhnd_nvram_io_read_ptr(btxt->data, 0x0, &ptr, io_offset, NULL);
	if (error)
		BHND_NV_PANIC("error mapping offset %zu: %d", io_offset, error);

	ptr = (const uint8_t *)ptr + io_offset;
	return (__DECONST(void *, ptr));
}

/* Convert a cookiep back to an I/O offset */
static size_t
bhnd_nvram_btxt_cookiep_to_offset(struct bhnd_nvram_btxt *btxt, void *cookiep)
{
	const void	*ptr;
	intptr_t	 offset;
	size_t		 io_size;
	int		 error;

	BHND_NV_ASSERT(cookiep != NULL, ("null cookiep"));

	io_size = bhnd_nvram_io_getsize(btxt->data);
	error = bhnd_nvram_io_read_ptr(btxt->data, 0x0, &ptr, io_size, NULL);
	if (error)
		BHND_NV_PANIC("error mapping offset %zu: %d", io_size, error);

	offset = (const uint8_t *)cookiep - (const uint8_t *)ptr;
	BHND_NV_ASSERT(offset >= 0, ("invalid cookiep"));
	BHND_NV_ASSERT((uintptr_t)offset < SIZE_MAX, ("cookiep > SIZE_MAX)"));
	BHND_NV_ASSERT((uintptr_t)offset <= io_size, ("cookiep > io_size)"));

	return ((size_t)offset);
}

/* Determine the entry length and env 'key=value' string length of the entry
 * at @p offset */
static int
bhnd_nvram_btxt_entry_len(struct bhnd_nvram_io *io, size_t offset,
    size_t *line_len, size_t *env_len)
{
	const uint8_t	*baseptr, *p;
	const void	*rbuf;
	size_t		 nbytes;
	int		 error;

	/* Fetch read buffer */
	if ((error = bhnd_nvram_io_read_ptr(io, offset, &rbuf, 0, &nbytes)))
		return (error);

	/* Find record termination (EOL, or '#') */
	p = rbuf;
	baseptr = rbuf;
	while ((size_t)(p - baseptr) < nbytes) {
		if (*p == '#' || *p == '\n' || *p == '\r')
			break;

		p++;
	}

	/* Got line length, now trim any trailing whitespace to determine
	 * actual env length */
	*line_len = p - baseptr;
	*env_len = *line_len;

	for (size_t i = 0; i < *line_len; i++) {
		char c = baseptr[*line_len - i - 1];
		if (!bhnd_nv_isspace(c))
			break;

		*env_len -= 1;
	}

	return (0);
}

/* Seek past the next line ending (\r, \r\n, or \n) */
static int
bhnd_nvram_btxt_seek_eol(struct bhnd_nvram_io *io, size_t *offset)
{
	const uint8_t	*baseptr, *p;
	const void	*rbuf;
	size_t		 nbytes;
	int		 error;

	/* Fetch read buffer */
	if ((error = bhnd_nvram_io_read_ptr(io, *offset, &rbuf, 0, &nbytes)))
		return (error);

	baseptr = rbuf;
	p = rbuf;
	while ((size_t)(p - baseptr) < nbytes) {
		char c = *p;

		/* Advance to next char. The next position may be EOF, in which
		 * case a read will be invalid */
		p++;

		if (c == '\r') {
			/* CR, check for optional LF */
			if ((size_t)(p - baseptr) < nbytes) {
				if (*p == '\n')
					p++;
			}

			break;
		} else if (c == '\n') {
			break;
		}
	}

	/* Hit newline or EOF */
	*offset += (p - baseptr);
	return (0);
}

/* Seek to the next valid non-comment line (or EOF) */
static int
bhnd_nvram_btxt_seek_next(struct bhnd_nvram_io *io, size_t *offset)
{
	const uint8_t	*baseptr, *p;
	const void	*rbuf;
	size_t		 nbytes;
	int		 error;

	/* Fetch read buffer */
	if ((error = bhnd_nvram_io_read_ptr(io, *offset, &rbuf, 0, &nbytes)))
		return (error);

	/* Skip leading whitespace and comments */
	baseptr = rbuf;
	p = rbuf;
	while ((size_t)(p - baseptr) < nbytes) {
		char c = *p;

		/* Skip whitespace */
		if (bhnd_nv_isspace(c)) {
			p++;
			continue;
		}

		/* Skip entire comment line */
		if (c == '#') {
			size_t line_off = *offset + (p - baseptr);
	
			if ((error = bhnd_nvram_btxt_seek_eol(io, &line_off)))
				return (error);

			p = baseptr + (line_off - *offset);
			continue;
		}

		/* Non-whitespace, non-comment */
		break;
	}

	*offset += (p - baseptr);
	return (0);
}

static int
bhnd_nvram_btxt_filter_setvar(struct bhnd_nvram_data *nv, const char *name,
    bhnd_nvram_val *value, bhnd_nvram_val **result)
{
	bhnd_nvram_val	*str;
	const char	*inp;
	bhnd_nvram_type	 itype;
	size_t		 ilen;
	int		 error;

	/* Name (trimmed of any path prefix) must be valid */
	if (!bhnd_nvram_validate_name(bhnd_nvram_trim_path_name(name)))
		return (EINVAL);

	/* Value must be bcm-formatted string */
	error = bhnd_nvram_val_convert_new(&str, &bhnd_nvram_val_bcm_string_fmt,
	    value, BHND_NVRAM_VAL_DYNAMIC);
	if (error)
		return (error);

	/* Value string must not contain our record delimiter character ('\n'),
	 * or our comment character ('#') */
	inp = bhnd_nvram_val_bytes(str, &ilen, &itype);
	BHND_NV_ASSERT(itype == BHND_NVRAM_TYPE_STRING, ("non-string value"));
	for (size_t i = 0; i < ilen; i++) {
		switch (inp[i]) {
		case '\n':
		case '#':
			BHND_NV_LOG("invalid character (%#hhx) in value\n",
			    inp[i]);
			bhnd_nvram_val_release(str);
			return (EINVAL);
		}
	}

	/* Success. Transfer result ownership to the caller. */
	*result = str;
	return (0);
}

static int
bhnd_nvram_btxt_filter_unsetvar(struct bhnd_nvram_data *nv, const char *name)
{
	/* We permit deletion of any variable */
	return (0);
}
