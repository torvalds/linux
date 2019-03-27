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

#include <machine/_inttypes.h>
#else /* !_KERNEL */
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif /* _KERNEL */

#include "bhnd_nvram_map.h"

#include "bhnd_nvram_private.h"
#include "bhnd_nvram_datavar.h"

#include "bhnd_nvram_data_spromvar.h"

/*
 * BHND SPROM NVRAM data class
 *
 * The SPROM data format is a fixed-layout, non-self-descriptive binary format,
 * used on Broadcom wireless and wired adapters, that provides a subset of the
 * variables defined by Broadcom SoC NVRAM formats.
 */

static const bhnd_sprom_layout  *bhnd_nvram_sprom_get_layout(uint8_t sromrev);

static int			 bhnd_nvram_sprom_ident(
				     struct bhnd_nvram_io *io,
				     const bhnd_sprom_layout **ident);

static int			 bhnd_nvram_sprom_write_var(
				     bhnd_sprom_opcode_state *state,
				     bhnd_sprom_opcode_idx_entry *entry,
				     bhnd_nvram_val *value,
				     struct bhnd_nvram_io *io);

static int			 bhnd_nvram_sprom_read_var(
				     struct bhnd_sprom_opcode_state *state,
				     struct bhnd_sprom_opcode_idx_entry *entry,
				     struct bhnd_nvram_io *io,
				     union bhnd_nvram_sprom_storage *storage,
				     bhnd_nvram_val *val);

static int			 bhnd_nvram_sprom_write_offset(
				     const struct bhnd_nvram_vardefn *var,
				     struct bhnd_nvram_io *data,
				     bhnd_nvram_type type, size_t offset,
				     uint32_t mask, int8_t shift,
				     uint32_t value);

static int			 bhnd_nvram_sprom_read_offset(
				     const struct bhnd_nvram_vardefn *var,
				     struct bhnd_nvram_io *data,
				     bhnd_nvram_type type, size_t offset,
				     uint32_t mask, int8_t shift,
				     uint32_t *value);

static bool			 bhnd_sprom_is_external_immutable(
				     const char *name);

BHND_NVRAM_DATA_CLASS_DEFN(sprom, "Broadcom SPROM",
    BHND_NVRAM_DATA_CAP_DEVPATHS, sizeof(struct bhnd_nvram_sprom))

#define	SPROM_COOKIE_TO_VID(_cookie)	\
	(((struct bhnd_sprom_opcode_idx_entry *)(_cookie))->vid)

#define	SPROM_COOKIE_TO_NVRAM_VAR(_cookie)	\
	bhnd_nvram_get_vardefn(SPROM_COOKIE_TO_VID(_cookie))

/**
 * Read the magic value from @p io, and verify that it matches
 * the @p layout's expected magic value.
 * 
 * If @p layout does not defined a magic value, @p magic is set to 0x0
 * and success is returned.
 * 
 * @param	io	An I/O context mapping the SPROM data to be identified.
 * @param	layout	The SPROM layout against which @p io should be verified.
 * @param[out]	magic	On success, the SPROM magic value.
 * 
 * @retval 0		success
 * @retval non-zero	If checking @p io otherwise fails, a regular unix
 *			error code will be returned.
 */
static int
bhnd_nvram_sprom_check_magic(struct bhnd_nvram_io *io,
    const bhnd_sprom_layout *layout, uint16_t *magic)
{
	int error;

	/* Skip if layout does not define a magic value */
	if (layout->flags & SPROM_LAYOUT_MAGIC_NONE)
		return (0);

	/* Read the magic value */
	error = bhnd_nvram_io_read(io, layout->magic_offset, magic,
	    sizeof(*magic));
	if (error)
		return (error);

	*magic = le16toh(*magic);

	/* If the signature does not match, skip to next layout */
	if (*magic != layout->magic_value)
		return (ENXIO);

	return (0);
}

/**
 * Attempt to identify the format of the SPROM data mapped by @p io.
 *
 * The SPROM data format does not provide any identifying information at a
 * known offset, instead requiring that we iterate over the known SPROM image
 * sizes until we are able to compute a valid checksum (and, for later
 * revisions, validate a signature at a revision-specific offset).
 *
 * @param	io	An I/O context mapping the SPROM data to be identified.
 * @param[out]	ident	On success, the identified SPROM layout.
 *
 * @retval 0		success
 * @retval non-zero	If identifying @p io otherwise fails, a regular unix
 *			error code will be returned.
 */
static int
bhnd_nvram_sprom_ident(struct bhnd_nvram_io *io,
    const bhnd_sprom_layout **ident)
{
	uint8_t	crc;
	size_t	crc_errors;
	size_t	nbytes;
	int	error;

	crc = BHND_NVRAM_CRC8_INITIAL;
	crc_errors = 0;
	nbytes = 0;

	/* We iterate the SPROM layouts smallest to largest, allowing us to
	 * perform incremental checksum calculation */
	for (size_t i = 0; i < bhnd_sprom_num_layouts; i++) {
		const bhnd_sprom_layout	*layout;
		u_char			 buf[512];
		size_t			 nread;
		uint16_t		 magic;
		uint8_t			 srevcrc[2];
		uint8_t			 srev;
		bool			 crc_valid;
		bool			 have_magic;

		layout = &bhnd_sprom_layouts[i];
		crc_valid = true;

		have_magic = true;
		if ((layout->flags & SPROM_LAYOUT_MAGIC_NONE))
			have_magic = false;

		/*
		 * Read image data and update CRC (errors are reported
		 * after the signature check)
		 * 
		 * Layout instances must be ordered from smallest to largest by
		 * the nvram_map compiler, allowing us to incrementally update
		 * our CRC.
		 */
		if (nbytes > layout->size)
			BHND_NV_PANIC("SPROM layout defined out-of-order");

		nread = layout->size - nbytes;

		while (nread > 0) {
			size_t nr;

			nr = bhnd_nv_ummin(nread, sizeof(buf));

			if ((error = bhnd_nvram_io_read(io, nbytes, buf, nr)))
				return (error);

			crc = bhnd_nvram_crc8(buf, nr, crc);
			crc_valid = (crc == BHND_NVRAM_CRC8_VALID);
			if (!crc_valid)
				crc_errors++;

			nread -= nr;
			nbytes += nr;
		}

		/* Read 8-bit SPROM revision, maintaining 16-bit size alignment
		 * required by some OTP/SPROM chipsets. */
		error = bhnd_nvram_io_read(io, layout->srev_offset, &srevcrc,
		    sizeof(srevcrc));
		if (error)
			return (error);

		srev = srevcrc[0];

		/* Early sromrev 1 devices (specifically some BCM440x enet
		 * cards) are reported to have been incorrectly programmed
		 * with a revision of 0x10. */
		if (layout->rev == 1 && srev == 0x10)
			srev = 0x1;
		
		/* Check revision against the layout definition */
		if (srev != layout->rev)
			continue;

		/* Check the magic value, skipping to the next layout on
		 * failure. */
		error = bhnd_nvram_sprom_check_magic(io, layout, &magic);
		if (error) {
			/* If the CRC is was valid, log the mismatch */
			if (crc_valid || BHND_NV_VERBOSE) {
				BHND_NV_LOG("invalid sprom %hhu signature: "
					    "0x%hx (expected 0x%hx)\n", srev,
					    magic, layout->magic_value);

					return (ENXIO);
			}
	
			continue;
		}

		/* Check for an earlier CRC error */
		if (!crc_valid) {
			/* If the magic check succeeded, then we may just have
			 * data corruption -- log the CRC error */
			if (have_magic || BHND_NV_VERBOSE) {
				BHND_NV_LOG("sprom %hhu CRC error (crc=%#hhx, "
					    "expected=%#x)\n", srev, crc,
					    BHND_NVRAM_CRC8_VALID);
			}

			continue;
		}

		/* Identified */
		*ident = layout;
		return (0);
	}

	/* No match */
	if (crc_errors > 0 && BHND_NV_VERBOSE) {
		BHND_NV_LOG("sprom parsing failed with %zu CRC errors\n",
		    crc_errors);
	}

	return (ENXIO);
}

static int
bhnd_nvram_sprom_probe(struct bhnd_nvram_io *io)
{
	const bhnd_sprom_layout	*layout;
	int			 error;

	/* Try to parse the input */
	if ((error = bhnd_nvram_sprom_ident(io, &layout)))
		return (error);

	return (BHND_NVRAM_DATA_PROBE_DEFAULT);
}

static int
bhnd_nvram_sprom_getvar_direct(struct bhnd_nvram_io *io, const char *name,
    void *buf, size_t *len, bhnd_nvram_type type)
{
	const bhnd_sprom_layout		*layout;
	bhnd_sprom_opcode_state		 state;
	const struct bhnd_nvram_vardefn	*var;
	size_t				 vid;
	int				 error;

	/* Look up the variable definition and ID */
	if ((var = bhnd_nvram_find_vardefn(name)) == NULL)
		return (ENOENT);
	
	vid = bhnd_nvram_get_vardefn_id(var);

	/* Identify the SPROM image layout */
	if ((error = bhnd_nvram_sprom_ident(io, &layout)))
		return (error);

	/* Initialize SPROM layout interpreter */
	if ((error = bhnd_sprom_opcode_init(&state, layout))) {
		BHND_NV_LOG("error initializing opcode state: %d\n", error);
		return (ENXIO);
	}

	/* Find SPROM layout entry for the requested variable */
	while ((error = bhnd_sprom_opcode_next_var(&state)) == 0) {
		bhnd_sprom_opcode_idx_entry	entry;
		union bhnd_nvram_sprom_storage	storage;
		bhnd_nvram_val			val;
	
		/* Fetch the variable's entry state */
		if ((error = bhnd_sprom_opcode_init_entry(&state, &entry)))
			return (error);

		/* Match against expected VID */
		if (entry.vid != vid)
			continue;

		/* Decode variable to a new value instance */
		error = bhnd_nvram_sprom_read_var(&state, &entry, io, &storage,
		    &val);
		if (error)
			return (error);

		/* Perform value coercion */
		error = bhnd_nvram_val_encode(&val, buf, len, type);

		/* Clean up */
		bhnd_nvram_val_release(&val);
		return (error);
	}

	/* Hit EOF without matching the requested variable? */
	if (error == ENOENT)
		return (ENOENT);

	/* Some other parse error occured */
	return (error);
}

/**
 * Return the SPROM layout definition for the given @p sromrev, or NULL if
 * not found.
 */
static const bhnd_sprom_layout *
bhnd_nvram_sprom_get_layout(uint8_t sromrev)
{
	/* Find matching SPROM layout definition */
	for (size_t i = 0; i < bhnd_sprom_num_layouts; i++) {
		if (bhnd_sprom_layouts[i].rev == sromrev)
			return (&bhnd_sprom_layouts[i]);
	}

	/* Not found */
	return (NULL);
}

/**
 * Serialize a SPROM variable.
 *
 * @param state	The SPROM opcode state describing the layout of @p io.
 * @param entry	The variable's SPROM opcode index entry.
 * @param value	The value to encode to @p io as per @p entry.
 * @param io	I/O context to which @p value should be written, or NULL
 *		if no output should be produced. This may be used to validate
 *		values prior to write.
 *
 * @retval 0		success
 * @retval EFTYPE	If value coercion from @p value to the type required by
 *			@p entry is unsupported.
 * @retval ERANGE	If value coercion from @p value would overflow
 *			(or underflow) the type required by @p entry.
 * @retval non-zero	If serialization otherwise fails, a regular unix error
 *			code will be returned.
 */
static int
bhnd_nvram_sprom_write_var(bhnd_sprom_opcode_state *state,
    bhnd_sprom_opcode_idx_entry *entry, bhnd_nvram_val *value,
    struct bhnd_nvram_io *io)
{
	const struct bhnd_nvram_vardefn	*var;
	uint32_t			 u32[BHND_SPROM_ARRAY_MAXLEN];
	bhnd_nvram_type			 itype, var_base_type;
	size_t				 ipos, ilen, nelem;
	int				 error;

	/* Fetch variable definition and the native element type */
	var = bhnd_nvram_get_vardefn(entry->vid);
	BHND_NV_ASSERT(var != NULL, ("missing variable definition"));

	var_base_type = bhnd_nvram_base_type(var->type);

	/* Fetch the element count from the SPROM variable layout definition */
	if ((error = bhnd_sprom_opcode_eval_var(state, entry)))
		return (error);

	nelem = state->var.nelem;
	BHND_NV_ASSERT(nelem <= var->nelem, ("SPROM nelem=%zu exceeds maximum "
	     "NVRAM nelem=%hhu", nelem, var->nelem));

	/* Promote the data to a common 32-bit representation */
	if (bhnd_nvram_is_signed_type(var_base_type))
		itype = BHND_NVRAM_TYPE_INT32_ARRAY;
	else
		itype = BHND_NVRAM_TYPE_UINT32_ARRAY;

	/* Calculate total size of the 32-bit promoted representation */
	if ((ilen = bhnd_nvram_value_size(NULL, 0, itype, nelem)) == 0) {
		/* Variable-width types are unsupported */
		BHND_NV_LOG("invalid %s SPROM variable type %d\n",
			    var->name, var->type);
		return (EFTYPE);
	}

	/* The native representation must fit within our scratch array */
	if (ilen > sizeof(u32)) {
		BHND_NV_LOG("error encoding '%s', SPROM_ARRAY_MAXLEN "
			    "incorrect\n", var->name);
		return (EFTYPE);
	}

	/* Initialize our common 32-bit value representation */
	if (bhnd_nvram_val_type(value) == BHND_NVRAM_TYPE_NULL) {
		/* No value provided; can this variable be encoded as missing
		 * by setting all bits to one? */
		if (!(var->flags & BHND_NVRAM_VF_IGNALL1)) {
			BHND_NV_LOG("missing required property: %s\n",
			    var->name);
			return (EINVAL);
		}

		/* Set all bits */
		memset(u32, 0xFF, ilen);
	} else {
		bhnd_nvram_val	 bcm_val;
		const void	*var_ptr;
		bhnd_nvram_type	 var_type, raw_type;
		size_t		 var_len, enc_nelem;

		/* Try to coerce the value to the native variable format. */
		error = bhnd_nvram_val_convert_init(&bcm_val, var->fmt, value,
		    BHND_NVRAM_VAL_DYNAMIC|BHND_NVRAM_VAL_BORROW_DATA);
		if (error) {
			BHND_NV_LOG("error converting input type %s to %s "
			    "format\n",
			    bhnd_nvram_type_name(bhnd_nvram_val_type(value)),
			    bhnd_nvram_val_fmt_name(var->fmt));
			return (error);
		}

		var_ptr = bhnd_nvram_val_bytes(&bcm_val, &var_len, &var_type);

		/*
		 * Promote to a common 32-bit representation. 
		 *
		 * We must use the raw type to interpret the input data as its
		 * underlying integer representation -- otherwise, coercion
		 * would attempt to parse the input as its complex
		 * representation.
		 *
		 * For example, direct CHAR -> UINT32 coercion would attempt to
		 * parse the character as a decimal integer, rather than
		 * promoting the raw UTF8 byte value to a 32-bit value.
		 */
		raw_type = bhnd_nvram_raw_type(var_type);
		error = bhnd_nvram_value_coerce(var_ptr, var_len, raw_type,
		     u32, &ilen, itype);

		/* Clean up temporary value representation */
		bhnd_nvram_val_release(&bcm_val);

		/* Report coercion failure */
		if (error) {
			BHND_NV_LOG("error promoting %s to %s: %d\n",
			    bhnd_nvram_type_name(var_type),
			    bhnd_nvram_type_name(itype), error);
			return (error);
		}

		/* Encoded element count must match SPROM's definition */
		error = bhnd_nvram_value_nelem(u32, ilen, itype, &enc_nelem);
		if (error)
			return (error);

		if (enc_nelem != nelem) {
			const char *type_name;
	
			type_name = bhnd_nvram_type_name(var_base_type);
			BHND_NV_LOG("invalid %s property value '%s[%zu]': "
			    "required %s[%zu]", var->name, type_name,
			    enc_nelem, type_name, nelem);
			return (EFTYPE);
		}
	}

	/*
	 * Seek to the start of the variable's SPROM layout definition and
	 * iterate over all bindings.
	 */
	if ((error = bhnd_sprom_opcode_seek(state, entry))) {
		BHND_NV_LOG("variable seek failed: %d\n", error);
		return (error);
	}
	
	ipos = 0;
	while ((error = bhnd_sprom_opcode_next_binding(state)) == 0) {
		bhnd_sprom_opcode_bind	*binding;
		bhnd_sprom_opcode_var	*binding_var;
		size_t			 offset;
		uint32_t		 skip_out_bytes;

		BHND_NV_ASSERT(
		    state->var_state >= SPROM_OPCODE_VAR_STATE_OPEN,
		    ("invalid var state"));
		BHND_NV_ASSERT(state->var.have_bind, ("invalid bind state"));

		binding_var = &state->var;
		binding = &state->var.bind;

		/* Calculate output skip bytes for this binding.
		 * 
		 * Skip directions are defined in terms of decoding, and
		 * reversed when encoding. */
		skip_out_bytes = binding->skip_in;
		error = bhnd_sprom_opcode_apply_scale(state, &skip_out_bytes);
		if (error)
			return (error);

		/* Bind */
		offset = state->offset;
		for (size_t i = 0; i < binding->count; i++) {
			if (ipos >= nelem) {
				BHND_NV_LOG("input skip %u positioned %zu "
				    "beyond nelem %zu\n", binding->skip_out,
				    ipos, nelem);
				return (EINVAL);
			}

			/* Write next offset */
			if (io != NULL) {
				error = bhnd_nvram_sprom_write_offset(var, io,
				    binding_var->base_type,
				    offset,
				    binding_var->mask,
				    binding_var->shift,
				    u32[ipos]);
				if (error)
					return (error);
			}

			/* Adjust output position; this was already verified to
			 * not overflow/underflow during SPROM opcode
			 * evaluation */
			if (binding->skip_in_negative) {
				offset -= skip_out_bytes;
			} else {
				offset += skip_out_bytes;
			}

			/* Skip advancing input if additional bindings are
			 * required to fully encode intv */
			if (binding->skip_out == 0)
				continue;

			/* Advance input position */
			if (SIZE_MAX - binding->skip_out < ipos) {
				BHND_NV_LOG("output skip %u would overflow "
				    "%zu\n", binding->skip_out, ipos);
				return (EINVAL);
			}

			ipos += binding->skip_out;
		}
	}

	/* Did we iterate all bindings until hitting end of the variable
	 * definition? */
	BHND_NV_ASSERT(error != 0, ("loop terminated early"));
	if (error != ENOENT)
		return (error);

	return (0);
}

static int
bhnd_nvram_sprom_serialize(bhnd_nvram_data_class *cls, bhnd_nvram_plist *props,
    bhnd_nvram_plist *options, void *outp, size_t *olen)
{
	bhnd_sprom_opcode_state		 state;
	struct bhnd_nvram_io		*io;
	bhnd_nvram_prop			*prop;
	bhnd_sprom_opcode_idx_entry	*entry;
	const bhnd_sprom_layout		*layout;
	size_t				 limit;
	uint8_t				 crc;
	uint8_t				 sromrev;
	int				 error;

	limit = *olen;
	layout = NULL;
	io = NULL;

	/* Fetch sromrev property */
	if (!bhnd_nvram_plist_contains(props, BHND_NVAR_SROMREV)) {
		BHND_NV_LOG("missing required property: %s\n",
		    BHND_NVAR_SROMREV);
		return (EINVAL);
	}

	error = bhnd_nvram_plist_get_uint8(props, BHND_NVAR_SROMREV, &sromrev);
	if (error) {
		BHND_NV_LOG("error reading sromrev property: %d\n", error);
		return (EFTYPE);
	}

	/* Find SPROM layout definition */
	if ((layout = bhnd_nvram_sprom_get_layout(sromrev)) == NULL) {
		BHND_NV_LOG("unsupported sromrev: %hhu\n", sromrev);
		return (EFTYPE);
	}

	/* Provide required size to caller */
	*olen = layout->size;
	if (outp == NULL)
		return (0);
	else if (limit < *olen)
		return (ENOMEM);

	/* Initialize SPROM layout interpreter */
	if ((error = bhnd_sprom_opcode_init(&state, layout))) {
		BHND_NV_LOG("error initializing opcode state: %d\n", error);
		return (ENXIO);
	}

	/* Check for unsupported properties */
	prop = NULL;
	while ((prop = bhnd_nvram_plist_next(props, prop)) != NULL) {
		const char *name;

		/* Fetch the corresponding SPROM layout index entry */
		name = bhnd_nvram_prop_name(prop);
		entry = bhnd_sprom_opcode_index_find(&state, name);
		if (entry == NULL) {
			BHND_NV_LOG("property '%s' unsupported by sromrev "
			    "%hhu\n", name, layout->rev);
			error = EINVAL;
			goto finished;
		}
	}

	/* Zero-initialize output */
	memset(outp, 0, *olen);

	/* Allocate wrapping I/O context for output buffer */
	io = bhnd_nvram_ioptr_new(outp, *olen, *olen, BHND_NVRAM_IOPTR_RDWR);
	if (io == NULL) {
		error = ENOMEM;
		goto finished;
	}

	/*
	 * Serialize all SPROM variable data.
	 */
	entry = NULL;
	while ((entry = bhnd_sprom_opcode_index_next(&state, entry)) != NULL) {
		const struct bhnd_nvram_vardefn	*var;
		bhnd_nvram_val			*val;

		var = bhnd_nvram_get_vardefn(entry->vid);
		BHND_NV_ASSERT(var != NULL, ("missing variable definition"));

		/* Fetch prop; will be NULL if unavailable */
		prop = bhnd_nvram_plist_get_prop(props, var->name);
		if (prop != NULL) {
			val = bhnd_nvram_prop_val(prop);
		} else {
			val = BHND_NVRAM_VAL_NULL;
		}

		/* Attempt to serialize the property value to the appropriate
		 * offset within the output buffer */
		error = bhnd_nvram_sprom_write_var(&state, entry, val, io);
		if (error) {
			BHND_NV_LOG("error serializing %s to required type "
			    "%s: %d\n", var->name,
			    bhnd_nvram_type_name(var->type), error);

			/* ENOMEM is reserved for signaling that the output
			 * buffer capacity is insufficient */
			if (error == ENOMEM)
				error = EINVAL;

			goto finished;
		}
	}

	/*
	 * Write magic value, if any.
	 */
	if (!(layout->flags & SPROM_LAYOUT_MAGIC_NONE)) {
		uint16_t magic;

		magic = htole16(layout->magic_value);
		error = bhnd_nvram_io_write(io, layout->magic_offset, &magic,
		    sizeof(magic));
		if (error) {
			BHND_NV_LOG("error writing magic value: %d\n", error);
			goto finished;
		}
	}

	/* Calculate the CRC over all SPROM data, not including the CRC byte. */
	crc = ~bhnd_nvram_crc8(outp, layout->crc_offset,
	    BHND_NVRAM_CRC8_INITIAL);

	/* Write the checksum. */
	error = bhnd_nvram_io_write(io, layout->crc_offset, &crc, sizeof(crc));
	if (error) {
		BHND_NV_LOG("error writing CRC value: %d\n", error);
		goto finished;
	}
	
	/*
	 * Success!
	 */
	error = 0;

finished:
	bhnd_sprom_opcode_fini(&state);
	
	if (io != NULL)
		bhnd_nvram_io_free(io);

	return (error);
}

static int
bhnd_nvram_sprom_new(struct bhnd_nvram_data *nv, struct bhnd_nvram_io *io)
{
	struct bhnd_nvram_sprom	*sp;
	int			 error;

	sp = (struct bhnd_nvram_sprom *)nv;

	/* Identify the SPROM input data */
	if ((error = bhnd_nvram_sprom_ident(io, &sp->layout)))
		return (error);

	/* Copy SPROM image to our shadow buffer */
	sp->data = bhnd_nvram_iobuf_copy_range(io, 0, sp->layout->size);
	if (sp->data == NULL)
		goto failed;

	/* Initialize SPROM binding eval state */
	if ((error = bhnd_sprom_opcode_init(&sp->state, sp->layout)))
		goto failed;

	return (0);

failed:
	if (sp->data != NULL)
		bhnd_nvram_io_free(sp->data);

	return (error);
}

static void
bhnd_nvram_sprom_free(struct bhnd_nvram_data *nv)
{
	struct bhnd_nvram_sprom *sp = (struct bhnd_nvram_sprom *)nv;

	bhnd_sprom_opcode_fini(&sp->state);
	bhnd_nvram_io_free(sp->data);
}

size_t
bhnd_nvram_sprom_count(struct bhnd_nvram_data *nv)
{
	struct bhnd_nvram_sprom *sprom = (struct bhnd_nvram_sprom *)nv;
	return (sprom->layout->num_vars);
}

static bhnd_nvram_plist *
bhnd_nvram_sprom_options(struct bhnd_nvram_data *nv)
{
	return (NULL);
}

static uint32_t
bhnd_nvram_sprom_caps(struct bhnd_nvram_data *nv)
{
	return (BHND_NVRAM_DATA_CAP_INDEXED);
}

static const char *
bhnd_nvram_sprom_next(struct bhnd_nvram_data *nv, void **cookiep)
{
	struct bhnd_nvram_sprom		*sp;
	bhnd_sprom_opcode_idx_entry	*entry;
	const struct bhnd_nvram_vardefn	*var;

	sp = (struct bhnd_nvram_sprom *)nv;

	/* Find next index entry that is not disabled by virtue of IGNALL1 */
	entry = *cookiep;
	while ((entry = bhnd_sprom_opcode_index_next(&sp->state, entry))) {
		/* Update cookiep and fetch variable definition */
		*cookiep = entry;
		var = SPROM_COOKIE_TO_NVRAM_VAR(*cookiep);
		BHND_NV_ASSERT(var != NULL, ("invalid cookiep %p", cookiep));

		/* We might need to parse the variable's value to determine
		 * whether it should be treated as unset */
		if (var->flags & BHND_NVRAM_VF_IGNALL1) {
			int     error;
			size_t  len;

			error = bhnd_nvram_sprom_getvar(nv, *cookiep, NULL,
			    &len, var->type);
			if (error) {
				BHND_NV_ASSERT(error == ENOENT, ("unexpected "
				    "error parsing variable: %d", error));
				continue;
			}
		}

		/* Found! */
		return (var->name);
	}

	/* Reached end of index entries */
	return (NULL);
}

static void *
bhnd_nvram_sprom_find(struct bhnd_nvram_data *nv, const char *name)
{
	struct bhnd_nvram_sprom		*sp;
	bhnd_sprom_opcode_idx_entry	*entry;

	sp = (struct bhnd_nvram_sprom *)nv;

	entry = bhnd_sprom_opcode_index_find(&sp->state, name);
	return (entry);
}

/**
 * Write @p value of @p type to the SPROM @p data at @p offset, applying
 * @p mask and @p shift, and OR with the existing data.
 *
 * @param var The NVRAM variable definition.
 * @param data The SPROM data to be modified.
 * @param type The type to write at @p offset.
 * @param offset The data offset to be written.
 * @param mask The mask to be applied to @p value after shifting.
 * @param shift The shift to be applied to @p value; if positive, a left
 * shift will be applied, if negative, a right shift (this is the reverse of the
 * decoding behavior)
 * @param value The value to be written. The parsed value will be OR'd with the
 * current contents of @p data at @p offset.
 */
static int
bhnd_nvram_sprom_write_offset(const struct bhnd_nvram_vardefn *var,
    struct bhnd_nvram_io *data, bhnd_nvram_type type, size_t offset,
    uint32_t mask, int8_t shift, uint32_t value)
{
	union bhnd_nvram_sprom_storage	scratch;
	int				error;

#define	NV_WRITE_INT(_widen, _repr, _swap)	do {		\
	/* Narrow the 32-bit representation */			\
	scratch._repr[1] = (_widen)value;			\
								\
	/* Shift and mask the new value */			\
	if (shift > 0)						\
		scratch._repr[1] <<= shift;			\
	else if (shift < 0)					\
		scratch._repr[1] >>= -shift;			\
	scratch._repr[1] &= mask;				\
								\
	/* Swap to output byte order */				\
	scratch._repr[1] = _swap(scratch._repr[1]);		\
								\
	/* Fetch the current value */				\
	error = bhnd_nvram_io_read(data, offset,		\
	    &scratch._repr[0], sizeof(scratch._repr[0]));	\
	if (error) {						\
		BHND_NV_LOG("error reading %s SPROM offset "	\
		    "%#zx: %d\n", var->name, offset, error);	\
		return (EFTYPE);				\
	}							\
								\
	/* Mask and set our new value's bits in the current	\
	 * value */						\
	if (shift >= 0)						\
		scratch._repr[0] &= ~_swap(mask << shift);	\
	else if (shift < 0)					\
		scratch._repr[0] &= ~_swap(mask >> (-shift));	\
	scratch._repr[0] |= scratch._repr[1];			\
								\
	/* Perform write */					\
	error = bhnd_nvram_io_write(data, offset,		\
	    &scratch._repr[0], sizeof(scratch._repr[0]));	\
	if (error) {						\
		BHND_NV_LOG("error writing %s SPROM offset "	\
		    "%#zx: %d\n", var->name, offset, error);	\
		return (EFTYPE);				\
	}							\
} while(0)

	/* Apply mask/shift and widen to a common 32bit representation */
	switch (type) {
	case BHND_NVRAM_TYPE_UINT8:
		NV_WRITE_INT(uint32_t,	u8,	);
		break;
	case BHND_NVRAM_TYPE_UINT16:
		NV_WRITE_INT(uint32_t,	u16,	htole16);
		break;
	case BHND_NVRAM_TYPE_UINT32:
		NV_WRITE_INT(uint32_t,	u32,	htole32);
		break;
	case BHND_NVRAM_TYPE_INT8:
		NV_WRITE_INT(int32_t,	i8,	);
		break;
	case BHND_NVRAM_TYPE_INT16:
		NV_WRITE_INT(int32_t,	i16,	htole16);
		break;
	case BHND_NVRAM_TYPE_INT32:
		NV_WRITE_INT(int32_t,	i32,	htole32);
		break;
	case BHND_NVRAM_TYPE_CHAR:
		NV_WRITE_INT(uint32_t,	u8,	);
		break;
	default:
		BHND_NV_LOG("unhandled %s offset type: %d\n", var->name, type);
		return (EFTYPE);
	}
#undef	NV_WRITE_INT

	return (0);
}

/**
 * Read the value of @p type from the SPROM @p data at @p offset, apply @p mask
 * and @p shift, and OR with the existing @p value.
 * 
 * @param var The NVRAM variable definition.
 * @param data The SPROM data to be decoded.
 * @param type The type to read at @p offset
 * @param offset The data offset to be read.
 * @param mask The mask to be applied to the value read at @p offset.
 * @param shift The shift to be applied after masking; if positive, a right
 * shift will be applied, if negative, a left shift.
 * @param value The read destination; the parsed value will be OR'd with the
 * current contents of @p value.
 */
static int
bhnd_nvram_sprom_read_offset(const struct bhnd_nvram_vardefn *var,
    struct bhnd_nvram_io *data, bhnd_nvram_type type, size_t offset,
    uint32_t mask, int8_t shift, uint32_t *value)
{
	union bhnd_nvram_sprom_storage	scratch;
	int				error;

#define	NV_PARSE_INT(_widen, _repr, _swap)		do {	\
	/* Perform read */					\
	error = bhnd_nvram_io_read(data, offset,		\
	    &scratch._repr[0], sizeof(scratch._repr[0]));	\
	if (error) {						\
		BHND_NV_LOG("error reading %s SPROM offset "	\
		    "%#zx: %d\n", var->name, offset, error);	\
		return (EFTYPE);				\
	}							\
								\
	/* Swap to host byte order */				\
	scratch._repr[0] = _swap(scratch._repr[0]);		\
								\
	/* Mask and shift the value */				\
	scratch._repr[0] &= mask;				\
	if (shift > 0) {					\
		scratch. _repr[0] >>= shift;			\
	} else if (shift < 0) {					\
		scratch. _repr[0] <<= -shift;			\
	}							\
								\
	/* Widen to 32-bit representation and OR with current	\
	 * value */						\
	(*value) |= (_widen)scratch._repr[0];			\
} while(0)

	/* Apply mask/shift and widen to a common 32bit representation */
	switch (type) {
	case BHND_NVRAM_TYPE_UINT8:
		NV_PARSE_INT(uint32_t,	u8,	);
		break;
	case BHND_NVRAM_TYPE_UINT16:
		NV_PARSE_INT(uint32_t,	u16,	le16toh);
		break;
	case BHND_NVRAM_TYPE_UINT32:
		NV_PARSE_INT(uint32_t,	u32,	le32toh);
		break;
	case BHND_NVRAM_TYPE_INT8:
		NV_PARSE_INT(int32_t,	i8,	);
		break;
	case BHND_NVRAM_TYPE_INT16:
		NV_PARSE_INT(int32_t,	i16,	le16toh);
		break;
	case BHND_NVRAM_TYPE_INT32:
		NV_PARSE_INT(int32_t,	i32,	le32toh);
		break;
	case BHND_NVRAM_TYPE_CHAR:
		NV_PARSE_INT(uint32_t,	u8,	);
		break;
	default:
		BHND_NV_LOG("unhandled %s offset type: %d\n", var->name, type);
		return (EFTYPE);
	}
#undef	NV_PARSE_INT

	return (0);
}

/**
 * Read a SPROM variable value from @p io.
 * 
 * @param	state		The SPROM opcode state describing the layout of @p io.
 * @param	entry		The variable's SPROM opcode index entry.
 * @param	io		The input I/O context.
 * @param	storage		Storage to be used with @p val.
 * @param[out]	val		Value instance to be initialized with the
 *				parsed variable data.
 *
 * The returned @p val instance will hold a borrowed reference to @p storage,
 * and must be copied via bhnd_nvram_val_copy() if it will be referenced beyond
 * the lifetime of @p storage.
 *
 * The caller is responsible for releasing any allocated value state
 * via bhnd_nvram_val_release().
 */
static int
bhnd_nvram_sprom_read_var(struct bhnd_sprom_opcode_state *state,
    struct bhnd_sprom_opcode_idx_entry *entry, struct bhnd_nvram_io *io,
    union bhnd_nvram_sprom_storage *storage, bhnd_nvram_val *val)
{
	union bhnd_nvram_sprom_storage	*inp;
	const struct bhnd_nvram_vardefn	*var;
	bhnd_nvram_type			 var_btype;
	uint32_t			 intv;
	size_t				 ilen, ipos, iwidth;
	size_t				 nelem;
	bool				 all_bits_set;
	int				 error;

	/* Fetch canonical variable definition */
	var = bhnd_nvram_get_vardefn(entry->vid);
	BHND_NV_ASSERT(var != NULL, ("invalid entry"));

	/*
	 * Fetch the array length from the SPROM variable definition.
	 *
	 * This generally be identical to the array length provided by the
	 * canonical NVRAM variable definition, but some SPROM layouts may
	 * define a smaller element count.
	 */
	if ((error = bhnd_sprom_opcode_eval_var(state, entry))) {
		BHND_NV_LOG("variable evaluation failed: %d\n", error);
		return (error);
	}

	nelem = state->var.nelem;
	if (nelem > var->nelem) {
		BHND_NV_LOG("SPROM array element count %zu cannot be "
		    "represented by '%s' element count of %hhu\n", nelem,
		    var->name, var->nelem);
		return (EFTYPE);
	}

	/* Fetch the var's base element type */
	var_btype = bhnd_nvram_base_type(var->type);

	/* Calculate total byte length of the native encoding */
	if ((iwidth = bhnd_nvram_value_size(NULL, 0, var_btype, 1)) == 0) {
		/* SPROM does not use (and we do not support) decoding of
		 * variable-width data types */
		BHND_NV_LOG("invalid SPROM data type: %d", var->type);
		return (EFTYPE);
	}
	ilen = nelem * iwidth;

	/* Decode into our caller's local storage */
	inp = storage;
	if (ilen > sizeof(*storage)) {
		BHND_NV_LOG("error decoding '%s', SPROM_ARRAY_MAXLEN "
		    "incorrect\n", var->name);
		return (EFTYPE);
	}

	/* Zero-initialize our decode buffer; any output elements skipped
	 * during decode should default to zero. */
	memset(inp, 0, ilen);

	/*
	 * Decode the SPROM data, iteratively decoding up to nelem values.
	 */
	if ((error = bhnd_sprom_opcode_seek(state, entry))) {
		BHND_NV_LOG("variable seek failed: %d\n", error);
		return (error);
	}

	ipos = 0;
	intv = 0x0;
	if (var->flags & BHND_NVRAM_VF_IGNALL1)
		all_bits_set = true;
	else
		all_bits_set = false;
	while ((error = bhnd_sprom_opcode_next_binding(state)) == 0) {
		bhnd_sprom_opcode_bind	*binding;
		bhnd_sprom_opcode_var	*binding_var;
		bhnd_nvram_type		 intv_type;
		size_t			 offset;
		size_t			 nbyte;
		uint32_t		 skip_in_bytes;
		void			*ptr;

		BHND_NV_ASSERT(
		    state->var_state >= SPROM_OPCODE_VAR_STATE_OPEN,
		    ("invalid var state"));
		BHND_NV_ASSERT(state->var.have_bind, ("invalid bind state"));

		binding_var = &state->var;
		binding = &state->var.bind;

		if (ipos >= nelem) {
			BHND_NV_LOG("output skip %u positioned "
			    "%zu beyond nelem %zu\n",
			    binding->skip_out, ipos, nelem);
			return (EINVAL);
		}

		/* Calculate input skip bytes for this binding */
		skip_in_bytes = binding->skip_in;
		error = bhnd_sprom_opcode_apply_scale(state, &skip_in_bytes);
		if (error)
			return (error);

		/* Bind */
		offset = state->offset;
		for (size_t i = 0; i < binding->count; i++) {
			/* Read the offset value, OR'ing with the current
			 * value of intv */
			error = bhnd_nvram_sprom_read_offset(var, io,
			    binding_var->base_type,
			    offset,
			    binding_var->mask,
			    binding_var->shift,
			    &intv);
			if (error)
				return (error);

			/* If IGNALL1, record whether value does not have
			 * all bits set. */
			if (var->flags & BHND_NVRAM_VF_IGNALL1 &&
			    all_bits_set)
			{
				uint32_t all1;

				all1 = binding_var->mask;
				if (binding_var->shift > 0)
					all1 >>= binding_var->shift;
				else if (binding_var->shift < 0)
					all1 <<= -binding_var->shift;

				if ((intv & all1) != all1)
					all_bits_set = false;
			}

			/* Adjust input position; this was already verified to
			 * not overflow/underflow during SPROM opcode
			 * evaluation */
			if (binding->skip_in_negative) {
				offset -= skip_in_bytes;
			} else {
				offset += skip_in_bytes;
			}

			/* Skip writing to inp if additional bindings are
			 * required to fully populate intv */
			if (binding->skip_out == 0)
				continue;

			/* We use bhnd_nvram_value_coerce() to perform
			 * overflow-checked coercion from the widened
			 * uint32/int32 intv value to the requested output
			 * type */
			if (bhnd_nvram_is_signed_type(var_btype))
				intv_type = BHND_NVRAM_TYPE_INT32;
			else
				intv_type = BHND_NVRAM_TYPE_UINT32;

			/* Calculate address of the current element output
			 * position */
			ptr = (uint8_t *)inp + (iwidth * ipos);

			/* Perform coercion of the array element */
			nbyte = iwidth;
			error = bhnd_nvram_value_coerce(&intv, sizeof(intv),
			    intv_type, ptr, &nbyte, var_btype);
			if (error)
				return (error);

			/* Clear temporary state */
			intv = 0x0;

			/* Advance output position */
			if (SIZE_MAX - binding->skip_out < ipos) {
				BHND_NV_LOG("output skip %u would overflow "
				    "%zu\n", binding->skip_out, ipos);
				return (EINVAL);
			}

			ipos += binding->skip_out;
		}
	}

	/* Did we iterate all bindings until hitting end of the variable
	 * definition? */
	BHND_NV_ASSERT(error != 0, ("loop terminated early"));
	if (error != ENOENT) {
		return (error);
	}

	/* If marked IGNALL1 and all bits are set, treat variable as
	 * unavailable */
	if ((var->flags & BHND_NVRAM_VF_IGNALL1) && all_bits_set)
		return (ENOENT);

	/* Provide value wrapper */
	return (bhnd_nvram_val_init(val, var->fmt, inp, ilen, var->type,
	    BHND_NVRAM_VAL_BORROW_DATA));
}


/**
 * Common variable decoding; fetches and decodes variable to @p val,
 * using @p storage for actual data storage.
 * 
 * The returned @p val instance will hold a borrowed reference to @p storage,
 * and must be copied via bhnd_nvram_val_copy() if it will be referenced beyond
 * the lifetime of @p storage.
 *
 * The caller is responsible for releasing any allocated value state
 * via bhnd_nvram_val_release().
 */
static int
bhnd_nvram_sprom_getvar_common(struct bhnd_nvram_data *nv, void *cookiep,
    union bhnd_nvram_sprom_storage *storage, bhnd_nvram_val *val)
{
	struct bhnd_nvram_sprom		*sp;
	bhnd_sprom_opcode_idx_entry	*entry;
	const struct bhnd_nvram_vardefn	*var;

	BHND_NV_ASSERT(cookiep != NULL, ("NULL variable cookiep"));

	sp = (struct bhnd_nvram_sprom *)nv;
	entry = cookiep;

	/* Fetch canonical variable definition */
	var = SPROM_COOKIE_TO_NVRAM_VAR(cookiep);
	BHND_NV_ASSERT(var != NULL, ("invalid cookiep %p", cookiep));

	return (bhnd_nvram_sprom_read_var(&sp->state, entry, sp->data, storage,
	    val));
}

static int
bhnd_nvram_sprom_getvar_order(struct bhnd_nvram_data *nv, void *cookiep1,
    void *cookiep2)
{
	struct bhnd_sprom_opcode_idx_entry *e1, *e2;

	e1 = cookiep1;
	e2 = cookiep2;

	/* Use the index entry order; this matches the order of variables
	 * returned via bhnd_nvram_sprom_next() */
	if (e1 < e2)
		return (-1);
	else if (e1 > e2)
		return (1);

	return (0);
}

static int
bhnd_nvram_sprom_getvar(struct bhnd_nvram_data *nv, void *cookiep, void *buf,
    size_t *len, bhnd_nvram_type otype)
{
	bhnd_nvram_val			val;
	union bhnd_nvram_sprom_storage	storage;
	int				error;

	/* Decode variable to a new value instance */
	error = bhnd_nvram_sprom_getvar_common(nv, cookiep, &storage, &val);
	if (error)
		return (error);

	/* Perform value coercion */
	error = bhnd_nvram_val_encode(&val, buf, len, otype);

	/* Clean up */
	bhnd_nvram_val_release(&val);
	return (error);
}

static int
bhnd_nvram_sprom_copy_val(struct bhnd_nvram_data *nv, void *cookiep,
    bhnd_nvram_val **value)
{
	bhnd_nvram_val			val;
	union bhnd_nvram_sprom_storage	storage;
	int				error;

	/* Decode variable to a new value instance */
	error = bhnd_nvram_sprom_getvar_common(nv, cookiep, &storage, &val);
	if (error)
		return (error);

	/* Attempt to copy to heap */
	*value = bhnd_nvram_val_copy(&val);
	bhnd_nvram_val_release(&val);

	if (*value == NULL)
		return (ENOMEM);

	return (0);
}

static const void *
bhnd_nvram_sprom_getvar_ptr(struct bhnd_nvram_data *nv, void *cookiep,
    size_t *len, bhnd_nvram_type *type)
{
	/* Unsupported */
	return (NULL);
}

static const char *
bhnd_nvram_sprom_getvar_name(struct bhnd_nvram_data *nv, void *cookiep)
{
	const struct bhnd_nvram_vardefn	*var;

	BHND_NV_ASSERT(cookiep != NULL, ("NULL variable cookiep"));

	var = SPROM_COOKIE_TO_NVRAM_VAR(cookiep);
	BHND_NV_ASSERT(var != NULL, ("invalid cookiep %p", cookiep));

	return (var->name);
}

static int
bhnd_nvram_sprom_filter_setvar(struct bhnd_nvram_data *nv, const char *name,
    bhnd_nvram_val *value, bhnd_nvram_val **result)
{
	struct bhnd_nvram_sprom		*sp;
	const struct bhnd_nvram_vardefn	*var;
	bhnd_sprom_opcode_idx_entry	*entry;
	bhnd_nvram_val			*spval;
	int				 error;

	sp = (struct bhnd_nvram_sprom *)nv;

	/* Is this an externally immutable variable name? */
	if (bhnd_sprom_is_external_immutable(name))
		return (EINVAL);

	/* Variable must be defined in our SPROM layout */
	if ((entry = bhnd_sprom_opcode_index_find(&sp->state, name)) == NULL)
		return (ENOENT);

	var = bhnd_nvram_get_vardefn(entry->vid);
	BHND_NV_ASSERT(var != NULL, ("missing variable definition"));

	/* Value must be convertible to the native variable type */
	error = bhnd_nvram_val_convert_new(&spval, var->fmt, value,
	    BHND_NVRAM_VAL_DYNAMIC);
	if (error)
		return (error);

	/* Value must be encodeable by our SPROM layout */
	error = bhnd_nvram_sprom_write_var(&sp->state, entry, spval, NULL);
	if (error) {
		bhnd_nvram_val_release(spval);
		return (error);
	}

	/* Success. Transfer our ownership of the converted value to the
	 * caller */
	*result = spval;
	return (0);
}

static int
bhnd_nvram_sprom_filter_unsetvar(struct bhnd_nvram_data *nv, const char *name)
{
	struct bhnd_nvram_sprom		*sp;
	const struct bhnd_nvram_vardefn	*var;
	bhnd_sprom_opcode_idx_entry	*entry;

	sp = (struct bhnd_nvram_sprom *)nv;

	/* Is this an externally immutable variable name? */
	if (bhnd_sprom_is_external_immutable(name))
		return (EINVAL);

	/* Variable must be defined in our SPROM layout */
	if ((entry = bhnd_sprom_opcode_index_find(&sp->state, name)) == NULL)
		return (ENOENT);

	var = bhnd_nvram_get_vardefn(entry->vid);
	BHND_NV_ASSERT(var != NULL, ("missing variable definition"));

	/* Variable must be capable of representing a NULL/deleted value.
	 * 
	 * Since SPROM's layout is fixed, this requires IGNALL -- if
	 * all bits are set, an IGNALL variable is treated as unset. */
	if (!(var->flags & BHND_NVRAM_VF_IGNALL1))
		return (EINVAL);

	return (0);
}

/**
 * Return true if @p name represents a special immutable variable name
 * (e.g. sromrev) that cannot be updated in an SPROM existing image.
 * 
 * @param name The name to check.
 */
static bool
bhnd_sprom_is_external_immutable(const char *name)
{
	/* The layout revision is immutable and cannot be changed */
	if (strcmp(name, BHND_NVAR_SROMREV) == 0)
		return (true);

	return (false);
}
