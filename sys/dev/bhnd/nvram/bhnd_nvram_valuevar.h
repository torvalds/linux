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

#ifndef _BHND_NVRAM_BHND_NVRAM_VALUEVAR_H_
#define _BHND_NVRAM_BHND_NVRAM_VALUEVAR_H_

#include "bhnd_nvram_value.h"

int		 bhnd_nvram_val_generic_encode(bhnd_nvram_val *value,
		     void *outp, size_t *olen, bhnd_nvram_type otype);
int		 bhnd_nvram_val_generic_encode_elem(bhnd_nvram_val *value,
		     const void *inp, size_t ilen, void *outp, size_t *olen,
		     bhnd_nvram_type otype);
const void	*bhnd_nvram_val_generic_next(bhnd_nvram_val *value,
		     const void *prev, size_t *olen);

/**
 * Filter input data prior to initialization.
 * 
 * This may be used to permit direct initialization from data types other than
 * the default native_type defined by @p fmt.
 *
 * @param[in,out]	fmt	Indirect pointer to the NVRAM value format. If
 *				modified by the caller, initialization will be
 *				restarted and performed using the provided
 *				format instance.
 * @param		inp	Input data.
 * @param		ilen	Input data length.
 * @param		itype	Input data type.
 *
 * @retval 0		If initialization from @p inp is supported.
 * @retval EFTYPE	If initialization from @p inp is unsupported.
 * @retval EFAULT	if @p ilen is not correctly aligned for elements of
 *			@p itype.
 */
typedef int (bhnd_nvram_val_op_filter)(const bhnd_nvram_val_fmt **fmt,
    const void *inp, size_t ilen, bhnd_nvram_type itype);

/** @see bhnd_nvram_val_encode() */
typedef int (bhnd_nvram_val_op_encode)(bhnd_nvram_val *value, void *outp,
    size_t *olen, bhnd_nvram_type otype);

/** @see bhnd_nvram_val_encode_elem() */
typedef int (bhnd_nvram_val_op_encode_elem)(bhnd_nvram_val *value,
    const void *inp, size_t ilen, void *outp, size_t *olen,
    bhnd_nvram_type otype);

/** @see bhnd_nvram_val_next() */
typedef const void *(bhnd_nvram_val_op_next)(bhnd_nvram_val *value,
    const void *prev, size_t *olen);

/** @see bhnd_nvram_val_nelem() */
typedef size_t (bhnd_nvram_val_op_nelem)(bhnd_nvram_val *value);

/**
 * NVRAM value format.
 * 
 * Provides a set of callbacks to support defining custom parsing
 * and encoding/conversion behavior when representing values as
 * instances of bhnd_nvram_val.
 */
struct bhnd_nvram_val_fmt {
	const char			*name;		/**< type name */
	bhnd_nvram_type			 native_type;	/**< native value representation */
	bhnd_nvram_val_op_filter	*op_filter;
	bhnd_nvram_val_op_encode	*op_encode;
	bhnd_nvram_val_op_encode_elem	*op_encode_elem;
	bhnd_nvram_val_op_nelem		*op_nelem;
	bhnd_nvram_val_op_next		*op_next;
};

#endif /* _BHND_NVRAM_BHND_NVRAM_VALUEVAR_H_ */
