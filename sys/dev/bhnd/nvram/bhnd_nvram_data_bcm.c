/*-
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/endian.h>

#ifdef _KERNEL

#include <sys/bus.h>
#include <sys/ctype.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#else /* !_KERNEL */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#endif /* _KERNEL */

#include "bhnd_nvram_private.h"

#include "bhnd_nvram_datavar.h"

#include "bhnd_nvram_data_bcmreg.h"
#include "bhnd_nvram_data_bcmvar.h"

/*
 * Broadcom NVRAM data class.
 * 
 * The Broadcom NVRAM NUL-delimited ASCII format is used by most
 * Broadcom SoCs.
 * 
 * The NVRAM data is encoded as a standard header, followed by series of
 * NUL-terminated 'key=value' strings; the end of the stream is denoted
 * by a single extra NUL character.
 */

struct bhnd_nvram_bcm;

static struct bhnd_nvram_bcm_hvar	*bhnd_nvram_bcm_gethdrvar(
					     struct bhnd_nvram_bcm *bcm,
					     const char *name);
static struct bhnd_nvram_bcm_hvar	*bhnd_nvram_bcm_to_hdrvar(
					     struct bhnd_nvram_bcm *bcm,
					     void *cookiep);
static size_t				 bhnd_nvram_bcm_hdrvar_index(
					     struct bhnd_nvram_bcm *bcm,
					     struct bhnd_nvram_bcm_hvar *hvar);
/*
 * Set of BCM NVRAM header values that are required to be mirrored in the
 * NVRAM data itself.
 *
 * If they're not included in the parsed NVRAM data, we need to vend the
 * header-parsed values with their appropriate keys, and add them in any
 * updates to the NVRAM data.
 *
 * If they're modified in NVRAM, we need to sync the changes with the
 * the NVRAM header values.
 */
static const struct bhnd_nvram_bcm_hvar bhnd_nvram_bcm_hvars[] = {
	{
		.name	= BCM_NVRAM_CFG0_SDRAM_INIT_VAR,
		.type	= BHND_NVRAM_TYPE_UINT16,
		.len	= sizeof(uint16_t),
		.nelem	= 1,
	},
	{
		.name	= BCM_NVRAM_CFG1_SDRAM_CFG_VAR,
		.type	= BHND_NVRAM_TYPE_UINT16,
		.len	= sizeof(uint16_t),
		.nelem	= 1,
	},
	{
		.name	= BCM_NVRAM_CFG1_SDRAM_REFRESH_VAR,
		.type	= BHND_NVRAM_TYPE_UINT16,
		.len	= sizeof(uint16_t),
		.nelem	= 1,
	},
	{
		.name	= BCM_NVRAM_SDRAM_NCDL_VAR,
		.type	= BHND_NVRAM_TYPE_UINT32,
		.len	= sizeof(uint32_t),
		.nelem	= 1,
	},
};

/** BCM NVRAM data class instance */
struct bhnd_nvram_bcm {
	struct bhnd_nvram_data		 nv;	/**< common instance state */
	struct bhnd_nvram_io		*data;	/**< backing buffer */
	bhnd_nvram_plist		*opts;	/**< serialization options */

	/** BCM header values */
	struct bhnd_nvram_bcm_hvar	 hvars[nitems(bhnd_nvram_bcm_hvars)];

	size_t				 count;	/**< total variable count */
};

BHND_NVRAM_DATA_CLASS_DEFN(bcm, "Broadcom", BHND_NVRAM_DATA_CAP_DEVPATHS,
    sizeof(struct bhnd_nvram_bcm))

static int
bhnd_nvram_bcm_probe(struct bhnd_nvram_io *io)
{
	struct bhnd_nvram_bcmhdr	hdr;
	int				error;

	if ((error = bhnd_nvram_io_read(io, 0x0, &hdr, sizeof(hdr))))
		return (error);

	if (le32toh(hdr.magic) != BCM_NVRAM_MAGIC)
		return (ENXIO);

	if (le32toh(hdr.size) > bhnd_nvram_io_getsize(io))
		return (ENXIO);

	return (BHND_NVRAM_DATA_PROBE_DEFAULT);
}

/**
 * Parser states for bhnd_nvram_bcm_getvar_direct_common().
 */
typedef enum {
	BCM_PARSE_KEY_START,
	BCM_PARSE_KEY_CONT,
	BCM_PARSE_KEY,
	BCM_PARSE_NEXT_KEY,
	BCM_PARSE_VALUE_START,
	BCM_PARSE_VALUE
} bcm_parse_state;

static int
bhnd_nvram_bcm_getvar_direct(struct bhnd_nvram_io *io, const char *name,
    void *outp, size_t *olen, bhnd_nvram_type otype)
{
	return (bhnd_nvram_bcm_getvar_direct_common(io, name, outp, olen, otype,
	    true));
}

/**
 * Common BCM/BCMRAW implementation of bhnd_nvram_getvar_direct().
 */
int
bhnd_nvram_bcm_getvar_direct_common(struct bhnd_nvram_io *io, const char *name,
    void *outp, size_t *olen, bhnd_nvram_type otype, bool have_header)
{
	struct bhnd_nvram_bcmhdr	 hdr;
	char				 buf[512];
	bcm_parse_state			 pstate;
	size_t				 limit, offset;
	size_t				 buflen, bufpos;
	size_t				 namelen, namepos;
	size_t				 vlen;
	int				 error;

	limit = bhnd_nvram_io_getsize(io);
	offset = 0;

	/* Fetch and validate the header */
	if (have_header) {
		if ((error = bhnd_nvram_io_read(io, offset, &hdr, sizeof(hdr))))
			return (error);

		if (le32toh(hdr.magic) != BCM_NVRAM_MAGIC)
			return (ENXIO);

		offset += sizeof(hdr);
		limit = bhnd_nv_ummin(le32toh(hdr.size), limit);
	}

	/* Loop our parser until we find the requested variable, or hit EOF */
	pstate = BCM_PARSE_KEY_START;
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
		case BCM_PARSE_KEY_START:
			BHND_NV_ASSERT(buflen - bufpos > 0, ("empty buffer!"));

			/* An extra '\0' denotes NVRAM EOF */
			if (buf[bufpos] == '\0')
				return (ENOENT);

			/* Reset name matching position */
			namepos = 0;

			/* Start name matching */
			pstate = BCM_PARSE_KEY_CONT;
			break;

		case BCM_PARSE_KEY_CONT: {
			size_t navail, nleft;

			nleft = namelen - namepos;
			navail = bhnd_nv_ummin(buflen - bufpos, nleft);

			if (strncmp(name+namepos, buf+bufpos, navail) == 0) {
				/* Matched */
				namepos += navail;
				bufpos += navail;

				/* If we've matched the full variable name,
				 * look for its trailing delimiter */
				if (namepos == namelen)
					pstate = BCM_PARSE_KEY;
			} else {
				/* No match; advance to next entry and restart
				 * name matching */
				pstate = BCM_PARSE_NEXT_KEY;
			}

			break;
		}

		case BCM_PARSE_KEY:
			BHND_NV_ASSERT(buflen - bufpos > 0, ("empty buffer!"));

			if (buf[bufpos] == '=') {
				/* Key fully matched; advance past '=' and
				 * parse the value */
				bufpos++;
				pstate = BCM_PARSE_VALUE_START;
			} else {
				/* No match; advance to next entry and restart
				 * name matching */
				pstate = BCM_PARSE_NEXT_KEY;
			}

			break;

		case BCM_PARSE_NEXT_KEY: {
			const char *p;

			/* Scan for a '\0' terminator */
			p = memchr(buf+bufpos, '\0', buflen - bufpos);

			if (p != NULL) {
				/* Found entry terminator; restart name
				 * matching at next entry */
				pstate = BCM_PARSE_KEY_START;
				bufpos = (p - buf) + 1 /* skip '\0' */;
			} else {
				/* Consumed full buffer looking for '\0'; 
				 * force repopulation of the buffer and
				 * retry */
				bufpos = buflen;
			}

			break;
		}

		case BCM_PARSE_VALUE_START: {
			const char *p;

			/* Scan for a '\0' terminator */
			p = memchr(buf+bufpos, '\0', buflen - bufpos);

			if (p != NULL) {
				/* Found entry terminator; parse the value */
				vlen = p - &buf[bufpos];
				pstate = BCM_PARSE_VALUE;

			} else if (p == NULL && offset == limit) {
				/* Hit EOF without a terminating '\0';
				 * treat the entry as implicitly terminated */
				vlen = buflen - bufpos;
				pstate = BCM_PARSE_VALUE;

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

		case BCM_PARSE_VALUE:
			BHND_NV_ASSERT(vlen <= buflen, ("value buf overrun"));

			return (bhnd_nvram_value_coerce(buf+bufpos, vlen,
			    BHND_NVRAM_TYPE_STRING, outp, olen, otype));
		}
	}

	/* Variable not found */
	return (ENOENT);
}

static int
bhnd_nvram_bcm_serialize(bhnd_nvram_data_class *cls, bhnd_nvram_plist *props,
    bhnd_nvram_plist *options, void *outp, size_t *olen)
{
	struct bhnd_nvram_bcmhdr	 hdr;
	bhnd_nvram_prop			*prop;
	size_t				 limit, nbytes;
	uint32_t			 sdram_ncdl;
	uint16_t			 sdram_init, sdram_cfg, sdram_refresh;
	uint8_t				 bcm_ver, crc8;
	int				 error;

	/* Determine output byte limit */
	if (outp != NULL)
		limit = *olen;
	else
		limit = 0;

	/* Fetch required header variables */
#define	PROPS_GET_HDRVAR(_name, _dest, _type)	do {			\
		const char *name = BCM_NVRAM_ ## _name ## _VAR;	\
		if (!bhnd_nvram_plist_contains(props, name)) {		\
			BHND_NV_LOG("missing required property: %s\n",	\
			    name);					\
			return (EFTYPE);				\
		}							\
									\
		error = bhnd_nvram_plist_get_encoded(props, name,	\
		    (_dest), sizeof(*(_dest)),				\
		    BHND_NVRAM_TYPE_ ##_type);				\
		if (error) {						\
			BHND_NV_LOG("error reading required header "	\
			    "%s property: %d\n", name, error);		\
			return (EFTYPE);				\
		}							\
} while (0)

	PROPS_GET_HDRVAR(SDRAM_NCDL,		&sdram_ncdl,	UINT32);
	PROPS_GET_HDRVAR(CFG0_SDRAM_INIT,	&sdram_init,	UINT16);
	PROPS_GET_HDRVAR(CFG1_SDRAM_CFG,	&sdram_cfg,	UINT16);
	PROPS_GET_HDRVAR(CFG1_SDRAM_REFRESH,	&sdram_refresh,	UINT16);

#undef	PROPS_GET_HDRVAR

	/* Fetch BCM nvram version from options */
	if (options != NULL &&
	    bhnd_nvram_plist_contains(options, BCM_NVRAM_ENCODE_OPT_VERSION))
	{
		error = bhnd_nvram_plist_get_uint8(options,
		    BCM_NVRAM_ENCODE_OPT_VERSION, &bcm_ver);
		if (error) {
			BHND_NV_LOG("error reading %s uint8 option value: %d\n",
			    BCM_NVRAM_ENCODE_OPT_VERSION, error);
			return (EINVAL);
		}
	} else {
		bcm_ver = BCM_NVRAM_CFG0_VER_DEFAULT;
	}

	/* Construct our header */
	hdr = (struct bhnd_nvram_bcmhdr) {
		.magic = htole32(BCM_NVRAM_MAGIC),
		.size = 0,
		.cfg0 = 0,
		.cfg1 = 0,
		.sdram_ncdl = htole32(sdram_ncdl)
	};

	hdr.cfg0 = BCM_NVRAM_SET_BITS(hdr.cfg0, BCM_NVRAM_CFG0_CRC, 0x0);
	hdr.cfg0 = BCM_NVRAM_SET_BITS(hdr.cfg0, BCM_NVRAM_CFG0_VER, bcm_ver);
	hdr.cfg0 = BCM_NVRAM_SET_BITS(hdr.cfg0, BCM_NVRAM_CFG0_SDRAM_INIT,
	    htole16(sdram_init));
	
	hdr.cfg1 = BCM_NVRAM_SET_BITS(hdr.cfg1, BCM_NVRAM_CFG1_SDRAM_CFG,
	    htole16(sdram_cfg));
	hdr.cfg1 = BCM_NVRAM_SET_BITS(hdr.cfg1, BCM_NVRAM_CFG1_SDRAM_REFRESH,
	    htole16(sdram_refresh));

	/* Write the header */
	nbytes = sizeof(hdr);
	if (limit >= nbytes)
		memcpy(outp, &hdr, sizeof(hdr));

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

		/* Fetch and write name + '=' to output */
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

		/* Attempt to write NUL-terminated value to output */
		value_len = prop_limit;
		error = bhnd_nvram_prop_encode(prop, p, &value_len,
		    BHND_NVRAM_TYPE_STRING);

		/* If encoding failed for any reason other than ENOMEM (which
		 * we'll detect and report after encoding all properties),
		 * return immediately */
		if (error && error != ENOMEM) {
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

	/* Write terminating '\0' */
	if (limit > nbytes)
		*((char *)outp + nbytes) = '\0';

	if (nbytes == SIZE_MAX)
		return (EFTYPE); /* would overflow size_t */
	else
		nbytes++;

	/* Update header length; this must fit within the header's 32-bit size
	 * field */
	if (nbytes <= UINT32_MAX) {
		hdr.size = (uint32_t)nbytes;
	} else {
		BHND_NV_LOG("size %zu exceeds maximum supported size of %u "
		    "bytes\n", nbytes, UINT32_MAX);
		return (EFTYPE);
	}

	/* Provide required length */
	*olen = nbytes;
	if (limit < *olen) {
		if (outp == NULL)
			return (0);

		return (ENOMEM);
	}

	/* Calculate the CRC value */
	BHND_NV_ASSERT(nbytes >= BCM_NVRAM_CRC_SKIP, ("invalid output size"));
	crc8 = bhnd_nvram_crc8((uint8_t *)outp + BCM_NVRAM_CRC_SKIP,
	    nbytes - BCM_NVRAM_CRC_SKIP, BHND_NVRAM_CRC8_INITIAL);

	/* Update CRC and write the finalized header */
	BHND_NV_ASSERT(nbytes >= sizeof(hdr), ("invalid output size"));
	hdr.cfg0 = BCM_NVRAM_SET_BITS(hdr.cfg0, BCM_NVRAM_CFG0_CRC, crc8);
	memcpy(outp, &hdr, sizeof(hdr));

	return (0);
}

/**
 * Initialize @p bcm with the provided NVRAM data mapped by @p src.
 * 
 * @param bcm A newly allocated data instance.
 */
static int
bhnd_nvram_bcm_init(struct bhnd_nvram_bcm *bcm, struct bhnd_nvram_io *src)
{
	struct bhnd_nvram_bcmhdr	 hdr;
	uint8_t				*p;
	void				*ptr;
	size_t				 io_offset, io_size;
	uint8_t				 crc, valid, bcm_ver;
	int				 error;

	if ((error = bhnd_nvram_io_read(src, 0x0, &hdr, sizeof(hdr))))
		return (error);

	if (le32toh(hdr.magic) != BCM_NVRAM_MAGIC)
		return (ENXIO);

	/* Fetch the actual NVRAM image size */
	io_size = le32toh(hdr.size);
	if (io_size < sizeof(hdr)) {
		/* The header size must include the header itself */
		BHND_NV_LOG("corrupt header size: %zu\n", io_size);
		return (EINVAL);
	}

	if (io_size > bhnd_nvram_io_getsize(src)) {
		BHND_NV_LOG("header size %zu exceeds input size %zu\n",
		    io_size, bhnd_nvram_io_getsize(src));
		return (EINVAL);
	}

	/* Allocate a buffer large enough to hold the NVRAM image, and
	 * an extra EOF-signaling NUL (on the chance it's missing from the
	 * source data) */
	if (io_size == SIZE_MAX)
		return (ENOMEM);

	bcm->data = bhnd_nvram_iobuf_empty(io_size, io_size + 1);
	if (bcm->data == NULL)
		return (ENOMEM);

	/* Fetch a pointer into our backing buffer and copy in the
	 * NVRAM image. */
	error = bhnd_nvram_io_write_ptr(bcm->data, 0x0, &ptr, io_size, NULL);
	if (error)
		return (error);

	p = ptr;
	if ((error = bhnd_nvram_io_read(src, 0x0, p, io_size)))
		return (error);

	/* Verify the CRC */
	valid = BCM_NVRAM_GET_BITS(hdr.cfg0, BCM_NVRAM_CFG0_CRC);
	crc = bhnd_nvram_crc8(p + BCM_NVRAM_CRC_SKIP,
	    io_size - BCM_NVRAM_CRC_SKIP, BHND_NVRAM_CRC8_INITIAL);

	if (crc != valid) {
		BHND_NV_LOG("warning: NVRAM CRC error (crc=%#hhx, "
		    "expected=%hhx)\n", crc, valid);
	}

	/* Populate header variable definitions */
#define	BCM_READ_HDR_VAR(_name, _dest, _swap) do {		\
	struct bhnd_nvram_bcm_hvar *data;				\
	data = bhnd_nvram_bcm_gethdrvar(bcm, _name ##_VAR);		\
	BHND_NV_ASSERT(data != NULL,						\
	    ("no such header variable: " __STRING(_name)));		\
									\
									\
	data->value. _dest = _swap(BCM_NVRAM_GET_BITS(			\
	    hdr. _name ## _FIELD, _name));				\
} while(0)

	BCM_READ_HDR_VAR(BCM_NVRAM_CFG0_SDRAM_INIT,	u16, le16toh);
	BCM_READ_HDR_VAR(BCM_NVRAM_CFG1_SDRAM_CFG,	u16, le16toh);
	BCM_READ_HDR_VAR(BCM_NVRAM_CFG1_SDRAM_REFRESH,	u16, le16toh);
	BCM_READ_HDR_VAR(BCM_NVRAM_SDRAM_NCDL,		u32, le32toh);

	_Static_assert(nitems(bcm->hvars) == 4, "missing initialization for"
	    "NVRAM header variable(s)");

#undef BCM_READ_HDR_VAR

	/* Process the buffer */
	bcm->count = 0;
	io_offset = sizeof(hdr);
	while (io_offset < io_size) {
		char		*envp;
		const char	*name, *value;
		size_t		 envp_len;
		size_t		 name_len, value_len;

		/* Parse the key=value string */
		envp = (char *) (p + io_offset);
		envp_len = strnlen(envp, io_size - io_offset);
		error = bhnd_nvram_parse_env(envp, envp_len, '=', &name,
					     &name_len, &value, &value_len);
		if (error) {
			BHND_NV_LOG("error parsing envp at offset %#zx: %d\n",
			    io_offset, error);
			return (error);
		}

		/* Insert a '\0' character, replacing the '=' delimiter and
		 * allowing us to vend references directly to the variable
		 * name */
		*(envp + name_len) = '\0';

		/* Record any NVRAM variables that mirror our header variables.
		 * This is a brute-force search -- for the amount of data we're
		 * operating on, it shouldn't be an issue. */
		for (size_t i = 0; i < nitems(bcm->hvars); i++) {
			struct bhnd_nvram_bcm_hvar	*hvar;
			union bhnd_nvram_bcm_hvar_value	 hval;
			size_t				 hval_len;

			hvar = &bcm->hvars[i];

			/* Already matched? */
			if (hvar->envp != NULL)
				continue;

			/* Name matches? */
			if ((strcmp(name, hvar->name)) != 0)
				continue;

			/* Save pointer to mirrored envp */
			hvar->envp = envp;

			/* Check for stale value */
			hval_len = sizeof(hval);
			error = bhnd_nvram_value_coerce(value, value_len,
			    BHND_NVRAM_TYPE_STRING, &hval, &hval_len,
			    hvar->type);
			if (error) {
				/* If parsing fails, we can likely only make
				 * things worse by trying to synchronize the
				 * variables */
				BHND_NV_LOG("error parsing header variable "
				    "'%s=%s': %d\n", name, value, error);
			} else if (hval_len != hvar->len) {
				hvar->stale = true;
			} else if (memcmp(&hval, &hvar->value, hval_len) != 0) {
				hvar->stale = true;
			}
		}

		/* Seek past the value's terminating '\0' */
		io_offset += envp_len;
		if (io_offset == io_size) {
			BHND_NV_LOG("missing terminating NUL at offset %#zx\n",
			    io_offset);
			return (EINVAL);
		}

		if (*(p + io_offset) != '\0') {
			BHND_NV_LOG("invalid terminator '%#hhx' at offset "
			    "%#zx\n", *(p + io_offset), io_offset);
			return (EINVAL);
		}

		/* Update variable count */
		bcm->count++;

		/* Seek to the next record */
		if (++io_offset == io_size) {
			char ch;
	
			/* Hit EOF without finding a terminating NUL
			 * byte; we need to grow our buffer and append
			 * it */
			io_size++;
			if ((error = bhnd_nvram_io_setsize(bcm->data, io_size)))
				return (error);

			/* Write NUL byte */
			ch = '\0';
			error = bhnd_nvram_io_write(bcm->data, io_size-1, &ch,
			    sizeof(ch));
			if (error)
				return (error);
		}

		/* Check for explicit EOF (encoded as a single empty NUL
		 * terminated string) */
		if (*(p + io_offset) == '\0')
			break;
	}

	/* Add non-mirrored header variables to total count variable */
	for (size_t i = 0; i < nitems(bcm->hvars); i++) {
		if (bcm->hvars[i].envp == NULL)
			bcm->count++;
	}

	/* Populate serialization options from our header */
	bcm_ver = BCM_NVRAM_GET_BITS(hdr.cfg0, BCM_NVRAM_CFG0_VER);
	error = bhnd_nvram_plist_append_bytes(bcm->opts,
	    BCM_NVRAM_ENCODE_OPT_VERSION, &bcm_ver, sizeof(bcm_ver),
	    BHND_NVRAM_TYPE_UINT8);
	if (error)
		return (error);

	return (0);
}

static int
bhnd_nvram_bcm_new(struct bhnd_nvram_data *nv, struct bhnd_nvram_io *io)
{
	struct bhnd_nvram_bcm	*bcm;
	int			 error;

	bcm = (struct bhnd_nvram_bcm *)nv;

	/* Populate default BCM mirrored header variable set */
	_Static_assert(sizeof(bcm->hvars) == sizeof(bhnd_nvram_bcm_hvars),
	    "hvar declarations must match bhnd_nvram_bcm_hvars template");
	memcpy(bcm->hvars, bhnd_nvram_bcm_hvars, sizeof(bcm->hvars));

	/* Allocate (empty) option list, to be populated by
	 * bhnd_nvram_bcm_init() */
	bcm->opts = bhnd_nvram_plist_new();
	if (bcm->opts == NULL)
		return (ENOMEM);

	/* Parse the BCM input data and initialize our backing
	 * data representation */
	if ((error = bhnd_nvram_bcm_init(bcm, io))) {
		bhnd_nvram_bcm_free(nv);
		return (error);
	}

	return (0);
}

static void
bhnd_nvram_bcm_free(struct bhnd_nvram_data *nv)
{
	struct bhnd_nvram_bcm *bcm = (struct bhnd_nvram_bcm *)nv;

	if (bcm->data != NULL)
		bhnd_nvram_io_free(bcm->data);

	if (bcm->opts != NULL)
		bhnd_nvram_plist_release(bcm->opts);
}

size_t
bhnd_nvram_bcm_count(struct bhnd_nvram_data *nv)
{
	struct bhnd_nvram_bcm *bcm = (struct bhnd_nvram_bcm *)nv;
	return (bcm->count);
}

static bhnd_nvram_plist *
bhnd_nvram_bcm_options(struct bhnd_nvram_data *nv)
{
	struct bhnd_nvram_bcm *bcm = (struct bhnd_nvram_bcm *)nv;
	return (bcm->opts);
}

static uint32_t
bhnd_nvram_bcm_caps(struct bhnd_nvram_data *nv)
{
	return (BHND_NVRAM_DATA_CAP_READ_PTR|BHND_NVRAM_DATA_CAP_DEVPATHS);
}

static const char *
bhnd_nvram_bcm_next(struct bhnd_nvram_data *nv, void **cookiep)
{
	struct bhnd_nvram_bcm		*bcm;
	struct bhnd_nvram_bcm_hvar	*hvar, *hvar_next;
	const void			*ptr;
	const char			*envp, *basep;
	size_t				 io_size, io_offset;
	int				 error;

	bcm = (struct bhnd_nvram_bcm *)nv;
	
	io_offset = sizeof(struct bhnd_nvram_bcmhdr);
	io_size = bhnd_nvram_io_getsize(bcm->data) - io_offset;

	/* Map backing buffer */
	error = bhnd_nvram_io_read_ptr(bcm->data, io_offset, &ptr, io_size,
	    NULL);
	if (error) {
		BHND_NV_LOG("error mapping backing buffer: %d\n", error);
		return (NULL);
	}

	basep = ptr;

	/* If cookiep pointers into our header variable array, handle as header
	 * variable iteration. */
	hvar = bhnd_nvram_bcm_to_hdrvar(bcm, *cookiep);
	if (hvar != NULL) {
		size_t idx;

		/* Advance to next entry, if any */
		idx = bhnd_nvram_bcm_hdrvar_index(bcm, hvar) + 1;

		/* Find the next header-defined variable that isn't defined in
		 * the NVRAM data, start iteration there */
		for (size_t i = idx; i < nitems(bcm->hvars); i++) {
			hvar_next = &bcm->hvars[i];
			if (hvar_next->envp != NULL && !hvar_next->stale)
				continue;

			*cookiep = hvar_next;
			return (hvar_next->name);
		}

		/* No further header-defined variables; iteration
		 * complete */
		return (NULL);
	}

	/* Handle standard NVRAM data iteration */
	if (*cookiep == NULL) {
		/* Start at the first NVRAM data record */
		envp = basep;
	} else {
		/* Seek to next record */
		envp = *cookiep;
		envp += strlen(envp) + 1;	/* key + '\0' */
		envp += strlen(envp) + 1;	/* value + '\0' */
	}

	/*
	 * Skip entries that have an existing header variable entry that takes
	 * precedence over the NVRAM data value.
	 * 
	 * The header's value will be provided when performing header variable
	 * iteration
	 */
	 while ((size_t)(envp - basep) < io_size && *envp != '\0') {
		/* Locate corresponding header variable */
		hvar = NULL;
		for (size_t i = 0; i < nitems(bcm->hvars); i++) {
			if (bcm->hvars[i].envp != envp)
				continue;

			hvar = &bcm->hvars[i];
			break;
		}

		/* If no corresponding hvar entry, or the entry does not take
		 * precedence over this NVRAM value, we can safely return this
		 * value as-is. */
		if (hvar == NULL || !hvar->stale)
			break;

		/* Seek to next record */
		envp += strlen(envp) + 1;	/* key + '\0' */
		envp += strlen(envp) + 1;	/* value + '\0' */
	 }

	/* On NVRAM data EOF, try switching to header variables */
	if ((size_t)(envp - basep) == io_size || *envp == '\0') {
		/* Find first valid header variable */
		for (size_t i = 0; i < nitems(bcm->hvars); i++) {
			if (bcm->hvars[i].envp != NULL)
				continue;
			
			*cookiep = &bcm->hvars[i];
			return (bcm->hvars[i].name);
		}

		/* No header variables */
		return (NULL);
	}

	*cookiep = __DECONST(void *, envp);
	return (envp);
}

static void *
bhnd_nvram_bcm_find(struct bhnd_nvram_data *nv, const char *name)
{
	return (bhnd_nvram_data_generic_find(nv, name));
}

static int
bhnd_nvram_bcm_getvar_order(struct bhnd_nvram_data *nv, void *cookiep1,
    void *cookiep2)
{
	struct bhnd_nvram_bcm		*bcm;
	struct bhnd_nvram_bcm_hvar	*hvar1, *hvar2;

	bcm = (struct bhnd_nvram_bcm *)nv;

	hvar1 = bhnd_nvram_bcm_to_hdrvar(bcm, cookiep1);
	hvar2 = bhnd_nvram_bcm_to_hdrvar(bcm, cookiep2);

	/* Header variables are always ordered below any variables defined
	 * in the BCM data */
	if (hvar1 != NULL && hvar2 == NULL) {
		return (1);	/* hvar follows non-hvar */
	} else if (hvar1 == NULL && hvar2 != NULL) {
		return (-1);	/* non-hvar precedes hvar */
	}

	/* Otherwise, both cookies are either hvars or non-hvars. We can
	 * safely fall back on pointer order, which will provide a correct
	 * ordering matching the behavior of bhnd_nvram_data_next() for
	 * both cases */
	if (cookiep1 < cookiep2)
		return (-1);

	if (cookiep1 > cookiep2)
		return (1);

	return (0);
}

static int
bhnd_nvram_bcm_getvar(struct bhnd_nvram_data *nv, void *cookiep, void *buf,
    size_t *len, bhnd_nvram_type type)
{
	return (bhnd_nvram_data_generic_rp_getvar(nv, cookiep, buf, len, type));
}

static int
bhnd_nvram_bcm_copy_val(struct bhnd_nvram_data *nv, void *cookiep,
    bhnd_nvram_val **value)
{
	return (bhnd_nvram_data_generic_rp_copy_val(nv, cookiep, value));
}

static const void *
bhnd_nvram_bcm_getvar_ptr(struct bhnd_nvram_data *nv, void *cookiep,
    size_t *len, bhnd_nvram_type *type)
{
	struct bhnd_nvram_bcm		*bcm;
	struct bhnd_nvram_bcm_hvar	*hvar;
	const char			*envp;

	bcm = (struct bhnd_nvram_bcm *)nv;

	/* Handle header variables */
	if ((hvar = bhnd_nvram_bcm_to_hdrvar(bcm, cookiep)) != NULL) {
		BHND_NV_ASSERT(bhnd_nvram_value_check_aligned(&hvar->value,
		    hvar->len, hvar->type) == 0, ("value misaligned"));

		*type = hvar->type;
		*len = hvar->len;
		return (&hvar->value);
	}

	/* Cookie points to key\0value\0 -- get the value address */
	BHND_NV_ASSERT(cookiep != NULL, ("NULL cookiep"));

	envp = cookiep;
	envp += strlen(envp) + 1;	/* key + '\0' */
	*len = strlen(envp) + 1;	/* value + '\0' */
	*type = BHND_NVRAM_TYPE_STRING;

	return (envp);
}

static const char *
bhnd_nvram_bcm_getvar_name(struct bhnd_nvram_data *nv, void *cookiep)
{
	struct bhnd_nvram_bcm		*bcm;
	struct bhnd_nvram_bcm_hvar	*hvar;

	bcm = (struct bhnd_nvram_bcm *)nv;

	/* Handle header variables */
	if ((hvar = bhnd_nvram_bcm_to_hdrvar(bcm, cookiep)) != NULL) {
		return (hvar->name);
	}

	/* Cookie points to key\0value\0 */
	return (cookiep);
}

static int
bhnd_nvram_bcm_filter_setvar(struct bhnd_nvram_data *nv, const char *name,
    bhnd_nvram_val *value, bhnd_nvram_val **result)
{
	bhnd_nvram_val	*str;
	int		 error;

	/* Name (trimmed of any path prefix) must be valid */
	if (!bhnd_nvram_validate_name(bhnd_nvram_trim_path_name(name)))
		return (EINVAL);

	/* Value must be bcm-formatted string */
	error = bhnd_nvram_val_convert_new(&str, &bhnd_nvram_val_bcm_string_fmt,
	    value, BHND_NVRAM_VAL_DYNAMIC);
	if (error)
		return (error);

	/* Success. Transfer result ownership to the caller. */
	*result = str;
	return (0);
}

static int
bhnd_nvram_bcm_filter_unsetvar(struct bhnd_nvram_data *nv, const char *name)
{
	/* We permit deletion of any variable */
	return (0);
}

/**
 * Return the internal BCM data reference for a header-defined variable
 * with @p name, or NULL if none exists.
 */
static struct bhnd_nvram_bcm_hvar *
bhnd_nvram_bcm_gethdrvar(struct bhnd_nvram_bcm *bcm, const char *name)
{
	for (size_t i = 0; i < nitems(bcm->hvars); i++) {
		if (strcmp(bcm->hvars[i].name, name) == 0)
			return (&bcm->hvars[i]);
	}

	/* Not found */
	return (NULL);
}

/**
 * If @p cookiep references a header-defined variable, return the
 * internal BCM data reference. Otherwise, returns NULL.
 */
static struct bhnd_nvram_bcm_hvar *
bhnd_nvram_bcm_to_hdrvar(struct bhnd_nvram_bcm *bcm, void *cookiep)
{
#ifdef BHND_NVRAM_INVARIANTS                                                                                                                                                                                                                                
	uintptr_t base, ptr;
#endif

	/* If the cookie falls within the hvar array, it's a
	 * header variable cookie */
	if (nitems(bcm->hvars) == 0)
		return (NULL);

	if (cookiep < (void *)&bcm->hvars[0])
		return (NULL);

	if (cookiep > (void *)&bcm->hvars[nitems(bcm->hvars)-1])
		return (NULL);

#ifdef BHND_NVRAM_INVARIANTS
	base = (uintptr_t)bcm->hvars;
	ptr = (uintptr_t)cookiep;

	BHND_NV_ASSERT((ptr - base) % sizeof(bcm->hvars[0]) == 0,
	    ("misaligned hvar pointer %p/%p", cookiep, bcm->hvars));
#endif /* INVARIANTS */

	return ((struct bhnd_nvram_bcm_hvar *)cookiep);
}

/**
 * Return the index of @p hdrvar within @p bcm's backing hvars array.
 */
static size_t
bhnd_nvram_bcm_hdrvar_index(struct bhnd_nvram_bcm *bcm,
    struct bhnd_nvram_bcm_hvar *hdrvar)
{
	BHND_NV_ASSERT(bhnd_nvram_bcm_to_hdrvar(bcm, (void *)hdrvar) != NULL,
	    ("%p is not a valid hdrvar reference", hdrvar));

	return (hdrvar - &bcm->hvars[0]);
}
