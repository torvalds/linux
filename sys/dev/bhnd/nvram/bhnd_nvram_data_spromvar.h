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

#ifndef _BHND_NVRAM_BHND_NVRAM_SPROMVAR_H_
#define _BHND_NVRAM_BHND_NVRAM_SPROMVAR_H_

#ifdef _KERNEL
#include <sys/bitstring.h>
#else
#include <bitstring.h>
#endif

#include "bhnd_nvram_private.h"

#include "bhnd_nvram_datavar.h"
#include "bhnd_nvram_io.h"

/** The maximum number of array elements encoded in a single SPROM variable */
#define	BHND_SPROM_ARRAY_MAXLEN	12

typedef struct bhnd_sprom_opcode_state		bhnd_sprom_opcode_state;
typedef struct bhnd_sprom_opcode_bind		bhnd_sprom_opcode_bind;
typedef struct bhnd_sprom_opcode_var		bhnd_sprom_opcode_var;
typedef struct bhnd_sprom_opcode_idx_entry	bhnd_sprom_opcode_idx_entry;

int				 bhnd_sprom_opcode_init(
				     bhnd_sprom_opcode_state *state,
				     const bhnd_sprom_layout *layout);
void				 bhnd_sprom_opcode_fini(
				     bhnd_sprom_opcode_state *state);

bhnd_sprom_opcode_idx_entry	*bhnd_sprom_opcode_index_find(
				     bhnd_sprom_opcode_state *state,
				     const char *name);
bhnd_sprom_opcode_idx_entry	*bhnd_sprom_opcode_index_next(
				     bhnd_sprom_opcode_state *state,
				     bhnd_sprom_opcode_idx_entry *prev);

int				 bhnd_sprom_opcode_init_entry(
				     bhnd_sprom_opcode_state *state,
				     bhnd_sprom_opcode_idx_entry *entry);

int				 bhnd_sprom_opcode_eval_var(
				     bhnd_sprom_opcode_state *state,
				     bhnd_sprom_opcode_idx_entry *entry);

int				 bhnd_sprom_opcode_seek(
				     bhnd_sprom_opcode_state *state,
				     bhnd_sprom_opcode_idx_entry *entry);
int				 bhnd_sprom_opcode_next_var(
				     bhnd_sprom_opcode_state *state);
int				 bhnd_sprom_opcode_next_binding(
				     bhnd_sprom_opcode_state *state);
int				 bhnd_sprom_opcode_apply_scale(
				     bhnd_sprom_opcode_state *state,
				      uint32_t *value);

/**
 * SPROM opcode per-bind evaluation state.
 */
struct bhnd_sprom_opcode_bind {
	uint8_t		count;
	uint32_t	skip_in;		/**< input element skips */
	bool		skip_in_negative;	/**< skip_in should be subtracted */
	uint32_t	skip_out;		/**< output element skip */
};

/**
 * SPROM opcode per-variable evaluation state.
 */
struct bhnd_sprom_opcode_var {
	uint8_t			nelem;		/**< variable array length */
	uint32_t		mask;		/**< current bind input mask */
	int8_t			shift;		/**< current bind input shift */
	bhnd_nvram_type		base_type;	/**< current bind input type */
	uint32_t		scale;		/**< current scale to apply to scaled encodings */
	bhnd_sprom_opcode_bind	bind;		/**< current bind state */
	bool			have_bind;	/**< if bind state is defined */
	size_t			bind_total;	/**< total count of bind operations performed */
};

/**
 * SPROM opcode variable definition states.
 * 
 * Ordered to support inequality comparisons
 * (e.g. >= SPROM_OPCODE_VAR_STATE_OPEN)
 */
typedef enum {
	SPROM_OPCODE_VAR_STATE_NONE	= 1,	/**< no variable entry available */
	SPROM_OPCODE_VAR_STATE_OPEN	= 2,	/**< currently parsing a variable entry */
	SPROM_OPCODE_VAR_STATE_DONE	= 3	/**< full variable entry has been parsed */
} bhnd_sprom_opcode_var_state;

/**
 * SPROM opcode evaluation state
 */
struct bhnd_sprom_opcode_state {
	const bhnd_sprom_layout		*layout;	/**< SPROM layout */

	bhnd_sprom_opcode_idx_entry	*idx;		/**< variable index (NULL during initialization) */
	size_t				 num_idx;	/**< variable index entry count */

	/** Current SPROM revision range */
	bitstr_t			 bit_decl(revs, SPROM_OP_REV_MAX);
	
	const uint8_t			*input;		/**< opcode input position */

	/* State preserved across variable definitions */
	uint32_t			 offset;	/**< SPROM offset */
	size_t				 vid;		/**< Variable ID */

	/* State reset after end of each variable definition */
	bhnd_sprom_opcode_var		 var;		/**< variable record (if any) */
	bhnd_sprom_opcode_var_state	 var_state;	/**< variable record state */
};

/**
 * SPROM opcode variable index entry
 */
struct bhnd_sprom_opcode_idx_entry {
	uint16_t	vid;		/**< SPROM variable ID */
	uint16_t	offset;		/**< SPROM input offset */
	uint16_t	opcodes;	/**< SPROM opcode offset */
};

/**
 * SPROM value storage.
 *
 * Sufficient for representing the native encoding of any defined SPROM
 * variable.
 */
union bhnd_nvram_sprom_storage {
	uint8_t		u8[BHND_SPROM_ARRAY_MAXLEN];
	uint16_t	u16[BHND_SPROM_ARRAY_MAXLEN];
	uint32_t	u32[BHND_SPROM_ARRAY_MAXLEN];
	int8_t		i8[BHND_SPROM_ARRAY_MAXLEN];
	int16_t		i16[BHND_SPROM_ARRAY_MAXLEN];
	int32_t		i32[BHND_SPROM_ARRAY_MAXLEN];
	char		ch[BHND_SPROM_ARRAY_MAXLEN];
};

/**
 * SPROM data class instance state.
 */
struct bhnd_nvram_sprom {
	struct bhnd_nvram_data	 nv;		/**< common instance state */
	struct bhnd_nvram_io	*data;		/**< backing SPROM image */
	const bhnd_sprom_layout	*layout;	/**< layout definition */
	bhnd_sprom_opcode_state	 state;		/**< opcode eval state */
};

#endif /* _BHND_NVRAM_BHND_NVRAM_SPROMVAR_H_ */
