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

#ifdef _KERNEL

#include <sys/param.h>
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
#include "bhnd_nvram_data_bcmvar.h"

/*
 * Broadcom-RAW NVRAM data class.
 * 
 * The Broadcom NVRAM NUL-delimited ASCII format is used by most
 * Broadcom SoCs.
 * 
 * The NVRAM data is encoded as a stream of of NUL-terminated 'key=value'
 * strings; the end of the stream is denoted by a single extra NUL character.
 */

struct bhnd_nvram_bcmraw;

/** BCM-RAW NVRAM data class instance */
struct bhnd_nvram_bcmraw {
	struct bhnd_nvram_data		 nv;	/**< common instance state */
	char				*data;	/**< backing buffer */
	size_t				 size;	/**< buffer size */
	size_t				 count;	/**< variable count */
};

BHND_NVRAM_DATA_CLASS_DEFN(bcmraw, "Broadcom (RAW)",
    BHND_NVRAM_DATA_CAP_DEVPATHS, sizeof(struct bhnd_nvram_bcmraw))

static int
bhnd_nvram_bcmraw_probe(struct bhnd_nvram_io *io)
{
	char	 envp[16];
	size_t	 envp_len;
	size_t	 io_size;
	int	 error;

	io_size = bhnd_nvram_io_getsize(io);

	/*
	 * Fetch initial bytes
	 */
	envp_len = bhnd_nv_ummin(sizeof(envp), io_size);
	if ((error = bhnd_nvram_io_read(io, 0x0, envp, envp_len)))
		return (error);

	/* An empty BCM-RAW buffer should still contain a single terminating
	 * NUL */
	if (envp_len == 0)
		return (ENXIO);

	if (envp_len == 1) {
		if (envp[0] != '\0')
			return (ENXIO);

		return (BHND_NVRAM_DATA_PROBE_MAYBE);
	}

	/* Must contain only printable ASCII characters delimited
	 * by NUL record delimiters */
	for (size_t i = 0; i < envp_len; i++) {
		char c = envp[i];

		/* If we hit a newline, this is probably BCM-TXT */
		if (c == '\n')
			return (ENXIO);

		if (c == '\0' && !bhnd_nv_isprint(c))
			continue;
	}

	/* A valid BCM-RAW buffer should contain a terminating NUL for
	 * the last record, followed by a final empty record terminated by
	 * NUL */
	envp_len = 2;
	if (io_size < envp_len)
		return (ENXIO);

	if ((error = bhnd_nvram_io_read(io, io_size-envp_len, envp, envp_len)))
		return (error);

	if (envp[0] != '\0' || envp[1] != '\0')
		return (ENXIO);

	return (BHND_NVRAM_DATA_PROBE_MAYBE + 1);
}

static int
bhnd_nvram_bcmraw_getvar_direct(struct bhnd_nvram_io *io, const char *name,
    void *buf, size_t *len, bhnd_nvram_type type)
{
	return (bhnd_nvram_bcm_getvar_direct_common(io, name, buf, len, type,
	    false));
}

static int
bhnd_nvram_bcmraw_serialize(bhnd_nvram_data_class *cls, bhnd_nvram_plist *props,
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
 * Initialize @p bcm with the provided NVRAM data mapped by @p src.
 * 
 * @param bcm A newly allocated data instance.
 */
static int
bhnd_nvram_bcmraw_init(struct bhnd_nvram_bcmraw *bcm, struct bhnd_nvram_io *src)
{
	size_t	 io_size;
	size_t	 capacity, offset;
	int	 error;

	/* Fetch the input image size */
	io_size = bhnd_nvram_io_getsize(src);

	/* Allocate a buffer large enough to hold the NVRAM image, and
	 * an extra EOF-signaling NUL (on the chance it's missing from the
	 * source data) */
	if (io_size == SIZE_MAX)
		return (ENOMEM);

	capacity = io_size + 1 /* room for extra NUL */;
	bcm->size = io_size;
	if ((bcm->data = bhnd_nv_malloc(capacity)) == NULL)
		return (ENOMEM);

	/* Copy in the NVRAM image */
	if ((error = bhnd_nvram_io_read(src, 0x0, bcm->data, io_size)))
		return (error);

	/* Process the buffer */
	bcm->count = 0;
	for (offset = 0; offset < bcm->size; offset++) {
		char		*envp;
		const char	*name, *value;
		size_t		 envp_len;
		size_t		 name_len, value_len;

		/* Parse the key=value string */
		envp = (char *) (bcm->data + offset);
		envp_len = strnlen(envp, bcm->size - offset);
		error = bhnd_nvram_parse_env(envp, envp_len, '=', &name,
					     &name_len, &value, &value_len);
		if (error) {
			BHND_NV_LOG("error parsing envp at offset %#zx: %d\n",
			    offset, error);
			return (error);
		}

		/* Insert a '\0' character, replacing the '=' delimiter and
		 * allowing us to vend references directly to the variable
		 * name */
		*(envp + name_len) = '\0';

		/* Add to variable count */
		bcm->count++;

		/* Seek past the value's terminating '\0' */
		offset += envp_len;
		if (offset == io_size) {
			BHND_NV_LOG("missing terminating NUL at offset %#zx\n",
			    offset);
			return (EINVAL);
		}

		/* If we hit EOF without finding a terminating NUL
		 * byte, we need to append it */
		if (++offset == bcm->size) {
			BHND_NV_ASSERT(offset < capacity,
			    ("appending past end of buffer"));
			bcm->size++;
			*(bcm->data + offset) = '\0';
		}

		/* Check for explicit EOF (encoded as a single empty NUL
		 * terminated string) */
		if (*(bcm->data + offset) == '\0')
			break;
	}

	/* Reclaim any unused space in he backing buffer */
	if (offset < bcm->size) {
		bcm->data = bhnd_nv_reallocf(bcm->data, bcm->size);
		if (bcm->data == NULL)
			return (ENOMEM);
	}

	return (0);
}

static int
bhnd_nvram_bcmraw_new(struct bhnd_nvram_data *nv, struct bhnd_nvram_io *io)
{
	struct bhnd_nvram_bcmraw	*bcm;
	int				 error;

	bcm = (struct bhnd_nvram_bcmraw *)nv;

	/* Parse the BCM input data and initialize our backing
	 * data representation */
	if ((error = bhnd_nvram_bcmraw_init(bcm, io))) {
		bhnd_nvram_bcmraw_free(nv);
		return (error);
	}

	return (0);
}

static void
bhnd_nvram_bcmraw_free(struct bhnd_nvram_data *nv)
{
	struct bhnd_nvram_bcmraw *bcm = (struct bhnd_nvram_bcmraw *)nv;

	if (bcm->data != NULL)
		bhnd_nv_free(bcm->data);
}

static bhnd_nvram_plist *
bhnd_nvram_bcmraw_options(struct bhnd_nvram_data *nv)
{
	return (NULL);
}

static size_t
bhnd_nvram_bcmraw_count(struct bhnd_nvram_data *nv)
{
	struct bhnd_nvram_bcmraw *bcm = (struct bhnd_nvram_bcmraw *)nv;

	return (bcm->count);
}

static uint32_t
bhnd_nvram_bcmraw_caps(struct bhnd_nvram_data *nv)
{
	return (BHND_NVRAM_DATA_CAP_READ_PTR|BHND_NVRAM_DATA_CAP_DEVPATHS);
}

static const char *
bhnd_nvram_bcmraw_next(struct bhnd_nvram_data *nv, void **cookiep)
{
	struct bhnd_nvram_bcmraw	*bcm;
	const char			*envp;

	bcm = (struct bhnd_nvram_bcmraw *)nv;

	if (*cookiep == NULL) {
		/* Start at the first NVRAM data record */
		envp = bcm->data;
	} else {
		/* Seek to next record */
		envp = *cookiep;
		envp += strlen(envp) + 1;	/* key + '\0' */
		envp += strlen(envp) + 1;	/* value + '\0' */
	}

	/* EOF? */
	if (*envp == '\0')
		return (NULL);

	*cookiep = (void *)(uintptr_t)envp;
	return (envp);
}

static void *
bhnd_nvram_bcmraw_find(struct bhnd_nvram_data *nv, const char *name)
{
	return (bhnd_nvram_data_generic_find(nv, name));
}

static int
bhnd_nvram_bcmraw_getvar_order(struct bhnd_nvram_data *nv, void *cookiep1,
    void *cookiep2)
{
	if (cookiep1 < cookiep2)
		return (-1);

	if (cookiep1 > cookiep2)
		return (1);

	return (0);
}

static int
bhnd_nvram_bcmraw_getvar(struct bhnd_nvram_data *nv, void *cookiep, void *buf,
    size_t *len, bhnd_nvram_type type)
{
	return (bhnd_nvram_data_generic_rp_getvar(nv, cookiep, buf, len, type));
}

static int
bhnd_nvram_bcmraw_copy_val(struct bhnd_nvram_data *nv, void *cookiep,
    bhnd_nvram_val **value)
{
	return (bhnd_nvram_data_generic_rp_copy_val(nv, cookiep, value));
}

static const void *
bhnd_nvram_bcmraw_getvar_ptr(struct bhnd_nvram_data *nv, void *cookiep,
    size_t *len, bhnd_nvram_type *type)
{
	const char *envp;

	/* Cookie points to key\0value\0 -- get the value address */
	envp = cookiep;
	envp += strlen(envp) + 1;	/* key + '\0' */
	*len = strlen(envp) + 1;	/* value + '\0' */
	*type = BHND_NVRAM_TYPE_STRING;

	return (envp);
}

static const char *
bhnd_nvram_bcmraw_getvar_name(struct bhnd_nvram_data *nv, void *cookiep)
{
	/* Cookie points to key\0value\0 */
	return (cookiep);
}

static int
bhnd_nvram_bcmraw_filter_setvar(struct bhnd_nvram_data *nv, const char *name,
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
bhnd_nvram_bcmraw_filter_unsetvar(struct bhnd_nvram_data *nv, const char *name)
{
	/* We permit deletion of any variable */
	return (0);
}
