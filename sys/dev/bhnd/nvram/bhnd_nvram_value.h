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

#ifndef _BHND_NVRAM_BHND_NVRAM_VALUE_H_
#define _BHND_NVRAM_BHND_NVRAM_VALUE_H_


#include <sys/refcount.h>

#ifdef _KERNEL
#include <machine/stdarg.h>
#else /* !_KERNEL */
#include <stdarg.h>
#endif /* _KERNEL */

#include "bhnd_nvram.h"

typedef struct bhnd_nvram_val_fmt	bhnd_nvram_val_fmt;
typedef struct bhnd_nvram_val		bhnd_nvram_val;

const char			*bhnd_nvram_val_fmt_name(
				     const bhnd_nvram_val_fmt *fmt);

const bhnd_nvram_val_fmt	*bhnd_nvram_val_default_fmt(
				      bhnd_nvram_type type);

int				 bhnd_nvram_val_init(bhnd_nvram_val *value,
				     const bhnd_nvram_val_fmt *fmt,
				     const void *inp, size_t ilen,
				     bhnd_nvram_type itype, uint32_t flags);

int				 bhnd_nvram_val_convert_init(
				     bhnd_nvram_val *value,
				     const bhnd_nvram_val_fmt *fmt,
				     bhnd_nvram_val *src, uint32_t flags);

int				 bhnd_nvram_val_new(bhnd_nvram_val **value,
				     const bhnd_nvram_val_fmt *fmt,
				     const void *inp, size_t ilen,
				     bhnd_nvram_type itype, uint32_t flags);

int				 bhnd_nvram_val_convert_new(
				     bhnd_nvram_val **value,
				     const bhnd_nvram_val_fmt *fmt,
				     bhnd_nvram_val *src, uint32_t flags);

bhnd_nvram_val			*bhnd_nvram_val_copy(bhnd_nvram_val *value);

void				 bhnd_nvram_val_release(
				     bhnd_nvram_val *value);

int				 bhnd_nvram_val_encode(bhnd_nvram_val *value,
				     void *outp, size_t *olen,
				     bhnd_nvram_type otype);

int				 bhnd_nvram_val_encode_elem(
				     bhnd_nvram_val *value, const void *inp,
				     size_t ilen, void *outp, size_t *olen,
				     bhnd_nvram_type otype);

int				 bhnd_nvram_val_printf(bhnd_nvram_val *value,
				     const char *fmt, char *outp, size_t *olen,
				     ...);
int				 bhnd_nvram_val_vprintf(bhnd_nvram_val *value,
				     const char *fmt, char *outp, size_t *olen,
				     va_list ap);


const void			*bhnd_nvram_val_bytes(bhnd_nvram_val *value,
				     size_t *olen, bhnd_nvram_type *otype);

bhnd_nvram_type			 bhnd_nvram_val_type(bhnd_nvram_val *value);
bhnd_nvram_type			 bhnd_nvram_val_elem_type(
				     bhnd_nvram_val *value);

const void			*bhnd_nvram_val_next(bhnd_nvram_val *value,
				     const void *prev, size_t *olen);

size_t				 bhnd_nvram_val_nelem(bhnd_nvram_val *value);

/**
 * NVRAM value flags
 */
enum {
	/**
	 * Do not allocate additional space for value data; all data must be
	 * represented inline within the value structure (default).
	 */
	BHND_NVRAM_VAL_FIXED		= (0<<0),

	/**
	 * Automatically allocate additional space for value data if it cannot
	 * be represented within the value structure.
	 */
	BHND_NVRAM_VAL_DYNAMIC		= (1<<0),

	/** 
	 * Copy the value data upon initialization. (default).
	 */
	BHND_NVRAM_VAL_COPY_DATA	= (0<<1),

	/**
	 * Do not perform an initial copy of the value data; the data must
	 * remain valid for the lifetime of the NVRAM value.
	 * 
	 * Value data will still be copied if the value itself is copied to the
	 * heap.
	 */
	BHND_NVRAM_VAL_BORROW_DATA	= (1<<1),

	/**
	 * Do not copy the value data when copying the value to the heap; the
	 * vlaue data is assumed to be statically allocated and must remain
	 * valid for the lifetime of the process.
	 * 
	 * Implies BHND_NVRAM_VAL_BORROW_DATA.
	 */
	BHND_NVRAM_VAL_STATIC_DATA	= (1<<2),
};

/**
 * @internal
 *
 * NVRAM value storage types.
 */
typedef enum {
	/**
	 * The value structure has an automatic storage duration
	 * (e.g. it is stack allocated, or is otherwise externally managed),
	 * and no destructors will be run prior to deallocation of the value.
	 *
	 * When performing copy/retain, the existing structure must be copied
	 * to a new heap allocation.
	 */
	BHND_NVRAM_VAL_STORAGE_AUTO	= 0,
	
	/**
	 * The value structure was heap allocated and is fully managed by the
	 * the NVRAM value API.
	 *
	 * When performing copy/retain, the existing structure may be retained
	 * as-is.
	 */
	BHND_NVRAM_VAL_STORAGE_DYNAMIC	= 2,

	/**
	 * The value structure has a static storage duration, and will never
	 * be deallocated.
	 *
	 * When performing copy/retain, the existing structure may be referenced
	 * without modification.
	 */
	BHND_NVRAM_VAL_STORAGE_STATIC	= 3,
} bhnd_nvram_val_storage;

/**
 * @internal
 *
 * NVRAM data storage types.
 */
typedef enum {
	/** Value has no active representation. This is the default for
	*  zero-initialized value structures. */
	BHND_NVRAM_VAL_DATA_NONE	= 0,

	/** Value data is represented inline */
	BHND_NVRAM_VAL_DATA_INLINE	= 1,

	/**
	 * Value represented by an external reference to data with a static
	 * storage location. The data need not be copied if copying the value.
	 */
	BHND_NVRAM_VAL_DATA_EXT_STATIC	= 2,

	/**
	 * Value represented by weak external reference, which must be copied
	 * if copying the value.
	 */
	BHND_NVRAM_VAL_DATA_EXT_WEAK	= 3,

	/**
	 * Value represented by an external reference that must be deallocated
	 * when deallocating the value.
	 */
	BHND_NVRAM_VAL_DATA_EXT_ALLOC	= 4,
} bhnd_nvram_val_data_storage;

/**
 * NVRAM value
 */
struct bhnd_nvram_val {
	volatile u_int			 refs;		/**< reference count */
	bhnd_nvram_val_storage		 val_storage;	/**< value structure storage */
	const bhnd_nvram_val_fmt	*fmt;		/**< value format */
	bhnd_nvram_val_data_storage	 data_storage;	/**< data storage */
	bhnd_nvram_type			 data_type;	/**< data type */
	size_t				 data_len;	/**< data size */

	/** data representation */
	union {
		uint8_t			 u8[8];		/**< 8-bit unsigned data */
		uint16_t		 u16[4];	/**< 16-bit unsigned data */
		uint32_t		 u32[2];	/**< 32-bit unsigned data */
		uint32_t		 u64[1];	/**< 64-bit unsigned data */
		int8_t			 i8[8];		/**< 8-bit signed data */
		int16_t			 i16[4];	/**< 16-bit signed data */
		int32_t			 i32[2];	/**< 32-bit signed data */
		int64_t			 i64[1];	/**< 64-bit signed data */
		unsigned char		 ch[8];		/**< 8-bit character data */
		bhnd_nvram_bool_t	 b[8];		/**< 8-bit boolean data */
		const void		*ptr;		/**< external data */
	} data;
};

/** Declare a bhnd_nvram_val_fmt with name @p _n */
#define	BHND_NVRAM_VAL_FMT_DECL(_n)	\
	extern const bhnd_nvram_val_fmt bhnd_nvram_val_ ## _n ## _fmt;

BHND_NVRAM_VAL_FMT_DECL(bcm_decimal);
BHND_NVRAM_VAL_FMT_DECL(bcm_hex);
BHND_NVRAM_VAL_FMT_DECL(bcm_leddc);
BHND_NVRAM_VAL_FMT_DECL(bcm_macaddr);
BHND_NVRAM_VAL_FMT_DECL(bcm_string);

BHND_NVRAM_VAL_FMT_DECL(uint8);
BHND_NVRAM_VAL_FMT_DECL(uint16);
BHND_NVRAM_VAL_FMT_DECL(uint32);
BHND_NVRAM_VAL_FMT_DECL(uint64);
BHND_NVRAM_VAL_FMT_DECL(int8);
BHND_NVRAM_VAL_FMT_DECL(int16);
BHND_NVRAM_VAL_FMT_DECL(int32);
BHND_NVRAM_VAL_FMT_DECL(int64);
BHND_NVRAM_VAL_FMT_DECL(char);
BHND_NVRAM_VAL_FMT_DECL(bool);
BHND_NVRAM_VAL_FMT_DECL(string);
BHND_NVRAM_VAL_FMT_DECL(data);
BHND_NVRAM_VAL_FMT_DECL(null);

BHND_NVRAM_VAL_FMT_DECL(uint8_array);
BHND_NVRAM_VAL_FMT_DECL(uint16_array);
BHND_NVRAM_VAL_FMT_DECL(uint32_array);
BHND_NVRAM_VAL_FMT_DECL(uint64_array);
BHND_NVRAM_VAL_FMT_DECL(int8_array);
BHND_NVRAM_VAL_FMT_DECL(int16_array);
BHND_NVRAM_VAL_FMT_DECL(int32_array);
BHND_NVRAM_VAL_FMT_DECL(int64_array);
BHND_NVRAM_VAL_FMT_DECL(char_array);
BHND_NVRAM_VAL_FMT_DECL(bool_array);
BHND_NVRAM_VAL_FMT_DECL(string_array);

/** Shared NULL value instance */
#define	BHND_NVRAM_VAL_NULL	(&bhnd_nvram_val_null)
extern bhnd_nvram_val bhnd_nvram_val_null;

#endif /* _BHND_NVRAM_BHND_NVRAM_VALUE_H_ */
