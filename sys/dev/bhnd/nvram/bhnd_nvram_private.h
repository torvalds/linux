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
 * 
 * $FreeBSD$
 */

#ifndef _BHND_NVRAM_BHND_NVRAM_PRIVATE_H_
#define _BHND_NVRAM_BHND_NVRAM_PRIVATE_H_

/*
 * Private BHND NVRAM definitions.
 */

#include <sys/param.h>

#ifdef _KERNEL
#include <sys/malloc.h>

#include <machine/stdarg.h>
#else
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#endif

#include "bhnd_nvram.h"
#include "bhnd_nvram_value.h"

/*
 * bhnd_nvram_crc8() lookup table.
 */
extern const uint8_t bhnd_nvram_crc8_tab[];

/* Forward declarations */
struct bhnd_nvram_vardefn;

#ifdef _KERNEL

MALLOC_DECLARE(M_BHND_NVRAM);

#define	bhnd_nv_isupper(c)	isupper(c)
#define	bhnd_nv_islower(c)	islower(c)
#define	bhnd_nv_isalpha(c)	isalpha(c)
#define	bhnd_nv_isprint(c)	isprint(c)
#define	bhnd_nv_isspace(c)	isspace(c)
#define	bhnd_nv_isdigit(c)	isdigit(c)
#define	bhnd_nv_isxdigit(c)	isxdigit(c)
#define	bhnd_nv_toupper(c)	toupper(c)

#define	bhnd_nv_malloc(size)		malloc((size), M_BHND_NVRAM, M_NOWAIT)
#define	bhnd_nv_calloc(n, size)		mallocarray((n), (size), M_BHND_NVRAM, \
					    M_NOWAIT | M_ZERO)
#define	bhnd_nv_reallocf(buf, size)	reallocf((buf), (size), M_BHND_NVRAM, \
					    M_NOWAIT)
#define	bhnd_nv_free(buf)		free((buf), M_BHND_NVRAM)
#define	bhnd_nv_asprintf(buf, fmt, ...)	asprintf((buf), M_BHND_NVRAM,	\
					    fmt, ## __VA_ARGS__)

/* We need our own strdup() implementation to pass required M_NOWAIT */
static inline char *
bhnd_nv_strdup(const char *str)
{
	char	*dest;
	size_t	 len;

	len = strlen(str);
	dest = malloc(len + 1, M_BHND_NVRAM, M_NOWAIT);
	if (dest == NULL)
		return (NULL);

	memcpy(dest, str, len);
	dest[len] = '\0';

	return (dest);
}

/* We need our own strndup() implementation to pass required M_NOWAIT */
static inline char *
bhnd_nv_strndup(const char *str, size_t len)
{
	char	*dest;

	len = strnlen(str, len);
	dest = malloc(len + 1, M_BHND_NVRAM, M_NOWAIT);
	if (dest == NULL)
		return (NULL);

	memcpy(dest, str, len);
	dest[len] = '\0';

	return (dest);
}

#ifdef INVARIANTS
#define	BHND_NV_INVARIANTS
#endif

#define	BHND_NV_ASSERT(expr, ...)	KASSERT(expr, __VA_ARGS__)

#define	BHND_NV_VERBOSE			(bootverbose)
#define	BHND_NV_PANIC(...)		panic(__VA_ARGS__)
#define	BHND_NV_LOG(fmt, ...)		\
	printf("%s: " fmt, __FUNCTION__, ##__VA_ARGS__)

#define	bhnd_nv_ummax(a, b)		ummax((a), (b))
#define	bhnd_nv_ummin(a, b)		ummin((a), (b))

#else /* !_KERNEL */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* ASCII-specific ctype variants that work consistently regardless
 * of current locale */
#define	bhnd_nv_isupper(c)	((c) >= 'A' && (c) <= 'Z')
#define	bhnd_nv_islower(c)	((c) >= 'a' && (c) <= 'z')
#define	bhnd_nv_isalpha(c)	(bhnd_nv_isupper(c) || bhnd_nv_islower(c))
#define	bhnd_nv_isprint(c)	((c) >= ' ' && (c) <= '~')
#define	bhnd_nv_isspace(c)	((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))
#define	bhnd_nv_isdigit(c)	isdigit(c)
#define	bhnd_nv_isxdigit(c)	isxdigit(c)
#define	bhnd_nv_toupper(c)	((c) -	\
    (('a' - 'A') * ((c) >= 'a' && (c) <= 'z')))

#define	bhnd_nv_malloc(size)		malloc((size))
#define	bhnd_nv_calloc(n, size)		calloc((n), (size))
#define	bhnd_nv_reallocf(buf, size)	reallocf((buf), (size))
#define	bhnd_nv_free(buf)		free((buf))
#define	bhnd_nv_strdup(str)		strdup(str)
#define	bhnd_nv_strndup(str, len)	strndup(str, len)
#define	bhnd_nv_asprintf(buf, fmt, ...)	asprintf((buf), fmt, ## __VA_ARGS__)

#ifndef NDEBUG
#define	BHND_NV_INVARIANTS
#endif

#ifdef BHND_NV_INVARIANTS

#define	BHND_NV_ASSERT(expr, msg)	do {				\
	if (!(expr)) {							\
		fprintf(stderr, "Assertion failed: %s, function %s, "	\
		    "file %s, line %u\n", __STRING(expr), __FUNCTION__,	\
		    __FILE__, __LINE__);				\
		BHND_NV_PANIC msg;					\
	}								\
} while(0)

#else /* !BHND_NV_INVARIANTS */

#define	BHND_NV_ASSERT(expr, msg)

#endif /* BHND_NV_INVARIANTS */

#define	BHND_NV_VERBOSE			(0)
#define	BHND_NV_PANIC(fmt, ...)		do {			\
	fprintf(stderr, "panic: " fmt "\n", ##__VA_ARGS__);	\
	abort();						\
} while(0)
#define	BHND_NV_LOG(fmt, ...)					\
	fprintf(stderr, "%s: " fmt, __FUNCTION__, ##__VA_ARGS__)

static inline uintmax_t
bhnd_nv_ummax(uintmax_t a, uintmax_t b)
{
        return (a > b ? a : b);
}

static inline uintmax_t
bhnd_nv_ummin(uintmax_t a, uintmax_t b)
{

        return (a < b ? a : b);
}

#endif /* _KERNEL */

#ifdef BHND_NV_VERBOSE
#define	BHND_NV_DEBUG(...)	BHND_NV_LOG(__VA_ARGS__)
#else /* !BHND_NV_VERBOSE */
#define	BHND_NV_DEBUG(...)
#endif /* BHND_NV_VERBOSE */

/* Limit a size_t value to a suitable range for use as a printf string field
 * width */
#define	BHND_NV_PRINT_WIDTH(_len)	\
	((_len) > (INT_MAX) ? (INT_MAX) : (int)(_len))

int				 bhnd_nvram_value_coerce(const void *inp,
				     size_t ilen, bhnd_nvram_type itype,
				     void *outp, size_t *olen,
				     bhnd_nvram_type otype);

int				 bhnd_nvram_value_check_aligned(const void *inp,
				     size_t ilen, bhnd_nvram_type itype);

int				 bhnd_nvram_value_nelem(const void *inp,
				     size_t ilen, bhnd_nvram_type itype,
				     size_t *nelem);

size_t				 bhnd_nvram_value_size(const void *inp,
				     size_t ilen, bhnd_nvram_type itype,
				     size_t nelem);

int				 bhnd_nvram_value_printf(const char *fmt,
				     const void *inp, size_t ilen,
				     bhnd_nvram_type itype, char *outp,
				     size_t *olen, ...);
int				 bhnd_nvram_value_vprintf(const char *fmt,
				     const void *inp, size_t ilen,
				     bhnd_nvram_type itype, char *outp,
				     size_t *olen, va_list ap);

const void			*bhnd_nvram_value_array_next(const void *inp,
				     size_t ilen, bhnd_nvram_type itype,
				     const void *prev, size_t *olen);

const struct bhnd_nvram_vardefn	*bhnd_nvram_find_vardefn(const char *varname);
const struct bhnd_nvram_vardefn	*bhnd_nvram_get_vardefn(size_t id);
size_t				 bhnd_nvram_get_vardefn_id(
				     const struct bhnd_nvram_vardefn *defn);

int				 bhnd_nvram_parse_int(const char *s,
				     size_t maxlen,  u_int base, size_t *nbytes,
				     void *outp, size_t *olen,
				     bhnd_nvram_type otype);

int				 bhnd_nvram_parse_env(const char *env,
				     size_t env_len, char delim,
				     const char **name, size_t *name_len,
				     const char **value, size_t *value_len);

size_t				 bhnd_nvram_parse_field(const char **inp,
				     size_t ilen, char delim);
size_t				 bhnd_nvram_trim_field(const char **inp,
				     size_t ilen, char delim);

const char			*bhnd_nvram_trim_path_name(const char *name);

bool				 bhnd_nvram_validate_name(const char *name);

/**
 * Calculate CRC-8 over @p buf using the Broadcom SPROM/NVRAM CRC-8
 * polynomial.
 * 
 * @param buf input buffer
 * @param size buffer size
 * @param crc last computed crc, or BHND_NVRAM_CRC8_INITIAL
 */
static inline uint8_t
bhnd_nvram_crc8(const void *buf, size_t size, uint8_t crc)
{
	const uint8_t *p = (const uint8_t *)buf;
	while (size--)
		crc = bhnd_nvram_crc8_tab[(crc ^ *p++)];

	return (crc);
}

#define	BHND_NVRAM_CRC8_INITIAL	0xFF	/**< Initial bhnd_nvram_crc8 value */
#define	BHND_NVRAM_CRC8_VALID	0x9F	/**< Valid CRC-8 checksum */

/** NVRAM variable flags */
enum {
	BHND_NVRAM_VF_MFGINT	= 1<<0,	/**< mfg-internal variable; should not
					     be externally visible */
	BHND_NVRAM_VF_IGNALL1	= 1<<1	/**< hide variable if its value has all
					     bits set. */
};

/**
 * SPROM layout flags
 */
enum {
	/**
	 * SPROM layout does not have magic identification value.
	 *
	 * This applies to SPROM revisions 1-3, where the actual
	 * layout must be determined by looking for a matching sromrev
	 * at the expected offset, and then verifying the CRC to ensure
	 * that the match was not a false positive.
	 */
	SPROM_LAYOUT_MAGIC_NONE	= (1<<0),	
};

/** NVRAM variable definition */
struct bhnd_nvram_vardefn {
	const char			*name;	/**< variable name */
	const char			*desc;	/**< human readable description,
						     or NULL */
	const char			*help;	/**< human readable help text,
						     or NULL */
	bhnd_nvram_type			 type;	/**< variable type */
	uint8_t				 nelem;	/**< element count, or 1 if not
						     an array-typed variable */
	const bhnd_nvram_val_fmt	*fmt;	/**< value format */
	uint32_t			 flags;	/**< flags (BHND_NVRAM_VF_*) */
};

/*
 * NVRAM variable definitions generated from nvram_map.
 */
extern const struct bhnd_nvram_vardefn bhnd_nvram_vardefns[];
extern const size_t bhnd_nvram_num_vardefns;

/**
 * SPROM layout descriptor.
 */
typedef struct bhnd_sprom_layout {
	size_t		 size;		/**< SPROM image size, in bytes */
	uint8_t		 rev;		/**< SPROM revision */
	uint8_t		 flags;		/**< layout flags (SPROM_LAYOUT_*) */
	size_t		 srev_offset;	/**< offset to SROM revision */
	size_t		 magic_offset;	/**< offset to magic value */
	uint16_t	 magic_value;	/**< expected magic value */
	size_t		 crc_offset;	/**< offset to crc8 value */
	const uint8_t	*bindings;	/**< SPROM binding opcode table */
	size_t		 bindings_size;	/**< SPROM binding opcode table size */
	uint16_t	 num_vars;	/**< total number of variables defined
					     for this layout by the binding
					     table */
} bhnd_sprom_layout;

/*
 * SPROM layout descriptions generated from nvram_map.
 */
extern const struct bhnd_sprom_layout bhnd_sprom_layouts[];
extern const size_t bhnd_sprom_num_layouts;

/*
 * SPROM binding opcodes.
 * 
 * Most opcodes are provided with two variants:
 *
 * - Standard:	The opcode's data directly follows the opcode. The data type
 * 		(SPROM_OPCODE_DATA_*) is encoded in the opcode immediate (IMM).
 * - Immediate:	The opcode's data is encoded directly in the opcode immediate
 *		(IMM).
 */ 
#define	SPROM_OPC_MASK			0xF0	/**< operation mask */
#define	SPROM_IMM_MASK			0x0F	/**< immediate value mask */
#define	SPROM_IMM_MAX			SPROM_IMM_MASK
#define	  SPROM_OP_DATA_U8		  0x00	/**< data is u8 */
#define	  SPROM_OP_DATA_U8_SCALED	  0x01	/**< data is u8; multiply by
						     type width */
#define	  SPROM_OP_DATA_U16		  0x02	/**< data is u16-le */
#define	  SPROM_OP_DATA_U32		  0x03	/**< data is u32-le */
#define	  SPROM_OP_DATA_I8		  0x04	/**< data is i8 */
#define	SPROM_OPCODE_EXT		0x00	/**< extended opcodes defined
						     in IMM */
#define	SPROM_OPCODE_EOF		0x00	/**< marks end of opcode
						     stream */
#define	SPROM_OPCODE_NELEM		0x01	/**< variable array element
						     count follows as U8 */
#define	SPROM_OPCODE_VAR_END		0x02	/**< marks end of variable
						     definition */
#define	SPROM_OPCODE_TYPE		0x03	/**< input type follows as U8
						     (see BHND_NVRAM_TYPE_*) */
#define	SPROM_OPCODE_VAR_IMM		0x10	/**< variable ID (imm) */
#define	SPROM_OPCODE_VAR_REL_IMM	0x20	/**< relative variable ID
						     (last ID + imm) */
#define	SPROM_OPCODE_VAR		0x30	/**< variable ID */
#define	SPROM_OPCODE_REV_IMM		0x40	/**< revision range (imm) */
#define	SPROM_OPCODE_REV_RANGE		0x50	/**< revision range (8-bit range)*/
#define	  SPROM_OP_REV_RANGE_MAX	  0x0F	/**< maximum representable SROM
						     revision */
#define	  SPROM_OP_REV_START_MASK	  0xF0
#define	  SPROM_OP_REV_START_SHIFT	  4
#define	  SPROM_OP_REV_END_MASK	 	  0x0F
#define	  SPROM_OP_REV_END_SHIFT	  0
#define	SPROM_OPCODE_MASK_IMM		0x60	/**< value mask (imm) */
#define	SPROM_OPCODE_MASK		0x70	/**< value mask */
#define	SPROM_OPCODE_SHIFT_IMM		0x80	/**< value shift (unsigned
						     imm, multipled by 2) */
#define	SPROM_OPCODE_SHIFT		0x90	/**< value shift */
#define	SPROM_OPCODE_OFFSET_REL_IMM	0xA0	/**< relative input offset
						     (last offset +
						      (imm * type width)) */
#define	SPROM_OPCODE_OFFSET		0xB0	/**< input offset */
#define	SPROM_OPCODE_TYPE_IMM		0xC0	/**< input type (imm,
						     see BHND_NVRAM_TYPE_*) */
#define	SPROM_OPCODE_DO_BIND		0xD0	/**< bind current value,
						     advance input/output
						     offsets as per IMM */
#define	  SPROM_OP_BIND_SKIP_IN_MASK	  0x03	/**< the number of input
						     elements to advance after
						     the bind */
#define	  SPROM_OP_BIND_SKIP_IN_SHIFT	  0
#define	  SPROM_OP_BIND_SKIP_IN_SIGN	 (1<<2)	/**< SKIP_IN sign bit */
#define	  SPROM_OP_BIND_SKIP_OUT_MASK	  0x08	/**< the number of output
						     elements to advance after
						     the bind */
#define	  SPROM_OP_BIND_SKIP_OUT_SHIFT	  3
#define	SPROM_OPCODE_DO_BINDN_IMM	0xE0	/**< bind IMM times, advancing
						     input/output offsets by one
						     element each time */
#define	SPROM_OPCODE_DO_BINDN		0xF0	/**< bind N times, advancing
						     input/output offsets as per
						     SPROM_OP_BIND_SKIP_IN/SPROM_OP_BIND_SKIP_OUT
						     IMM values. The U8 element
						     count follows. */

/** Evaluates to true if opcode is an extended opcode */
#define SPROM_OPCODE_IS_EXT(_opcode)	\
    (((_opcode) & SPROM_OPC_MASK) == SPROM_OPCODE_EXT)

/** Return the opcode constant for a simple or extended opcode */
#define SPROM_OPCODE_OP(_opcode)	\
    (SPROM_OPCODE_IS_EXT(_opcode) ? (_opcode) : ((_opcode) & SPROM_OPC_MASK))

/** Return the opcode immediate for a simple opcode, or zero if this is
  * an extended opcode  */
#define SPROM_OPCODE_IMM(_opcode)	\
    (SPROM_OPCODE_IS_EXT(_opcode) ? 0 : ((_opcode) & SPROM_IMM_MASK))

/** Evaluates to true if the given opcode produces an implicit
 *  SPROM_OPCODE_VAR_END instruction for any open variable */
#define	SPROM_OP_IS_IMPLICIT_VAR_END(_opcode)		\
    (((_opcode) == SPROM_OPCODE_VAR_IMM)	||	\
     ((_opcode) == SPROM_OPCODE_VAR_REL_IMM)	||	\
     ((_opcode) == SPROM_OPCODE_VAR)		||	\
     ((_opcode) == SPROM_OPCODE_REV_IMM)	||	\
     ((_opcode) == SPROM_OPCODE_REV_RANGE))

/** Evaluates to true if the given opcode is either an explicit
  * SPROM_OPCODE_VAR_END instruction, or is an opcode that produces an
  * implicit terminatation of any open variable */
#define	SPROM_OP_IS_VAR_END(_opcode)		\
     (((_opcode) == SPROM_OPCODE_VAR_END) ||	\
     SPROM_OP_IS_IMPLICIT_VAR_END(_opcode))

/** maximum representable immediate value */
#define	SPROM_OP_IMM_MAX	SPROM_IMM_MASK

/** maximum representable SROM revision */
#define	SPROM_OP_REV_MAX	MAX(SPROM_OP_REV_RANGE_MAX, SPROM_IMM_MAX)

#endif /* _BHND_NVRAM_BHND_NVRAM_PRIVATE_H_ */
