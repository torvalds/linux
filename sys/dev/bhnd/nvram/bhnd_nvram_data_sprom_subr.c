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

#include <sys/param.h>
#include <sys/endian.h>

#ifdef _KERNEL
#include <sys/systm.h>
#include <machine/_inttypes.h>
#else /* !_KERNEL */
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#endif /* _KERNEL */

#include "bhnd_nvram_private.h"
#include "bhnd_nvram_data_spromvar.h"

static int	bhnd_sprom_opcode_sort_idx(const void *lhs, const void *rhs);
static int	bhnd_nvram_opcode_idx_vid_compare(const void *key,
		    const void *rhs);

static int	bhnd_sprom_opcode_reset(bhnd_sprom_opcode_state *state);

static int	bhnd_sprom_opcode_set_type(bhnd_sprom_opcode_state *state,
		    bhnd_nvram_type type);

static int	bhnd_sprom_opcode_set_var(bhnd_sprom_opcode_state *state,
		    size_t vid);
static int	bhnd_sprom_opcode_clear_var(bhnd_sprom_opcode_state *state);

static int	bhnd_sprom_opcode_flush_bind(bhnd_sprom_opcode_state *state);

static int	bhnd_sprom_opcode_read_opval32(bhnd_sprom_opcode_state *state,
		    uint8_t type, uint32_t *opval);

static int	bhnd_sprom_opcode_step(bhnd_sprom_opcode_state *state,
		    uint8_t *opcode);

#define	SPROM_OP_BAD(_state, _fmt, ...)					\
	BHND_NV_LOG("bad encoding at %td: " _fmt,			\
	    (_state)->input - (_state)->layout->bindings, ##__VA_ARGS__)

/**
 * Initialize SPROM opcode evaluation state.
 * 
 * @param state The opcode state to be initialized.
 * @param layout The SPROM layout to be parsed by this instance.
 * 
 * 
 * @retval 0 success
 * @retval non-zero If initialization fails, a regular unix error code will be
 * returned.
 */
int
bhnd_sprom_opcode_init(bhnd_sprom_opcode_state *state,
    const struct bhnd_sprom_layout *layout)
{
	bhnd_sprom_opcode_idx_entry	*idx;
	size_t				 num_vars, num_idx;
	int				 error;

	idx = NULL;

	state->layout = layout;
	state->idx = NULL;
	state->num_idx = 0;

	/* Initialize interpretation state */
	if ((error = bhnd_sprom_opcode_reset(state)))
		return (error);

	/* Allocate and populate our opcode index */
	num_idx = state->layout->num_vars;
	idx = bhnd_nv_calloc(num_idx, sizeof(*idx));
	if (idx == NULL)
		return (ENOMEM);

	for (num_vars = 0; num_vars < num_idx; num_vars++) {
		/* Seek to next entry */
		if ((error = bhnd_sprom_opcode_next_var(state))) {
			SPROM_OP_BAD(state, "error reading expected variable "
			    "entry: %d\n", error);
			bhnd_nv_free(idx);
			return (error);
		}

		/* Record entry state in our index */
		error = bhnd_sprom_opcode_init_entry(state, &idx[num_vars]);
		if (error) {
			SPROM_OP_BAD(state, "error initializing index for "
			    "entry: %d\n", error);
			bhnd_nv_free(idx);
			return (error);
		}
	}

	/* Should have reached end of binding table; next read must return
	 * ENOENT */
	if ((error = bhnd_sprom_opcode_next_var(state)) != ENOENT) {
		BHND_NV_LOG("expected EOF parsing binding table: %d\n", error);
		bhnd_nv_free(idx);
		return (ENXIO);
	}

	/* Reset interpretation state */
	if ((error = bhnd_sprom_opcode_reset(state))) {
		bhnd_nv_free(idx);
		return (error);
	}

	/* Make index available to opcode state evaluation */
        qsort(idx, num_idx, sizeof(idx[0]), bhnd_sprom_opcode_sort_idx);

	state->idx = idx;
	state->num_idx = num_idx;

	return (0);
}

/**
 * Reset SPROM opcode evaluation state; future evaluation will be performed
 * starting at the first opcode.
 * 
 * @param state The opcode state to be reset.
 *
 * @retval 0 success
 * @retval non-zero If reset fails, a regular unix error code will be returned.
 */
static int
bhnd_sprom_opcode_reset(bhnd_sprom_opcode_state *state)
{
	memset(&state->var, 0, sizeof(state->var));

	state->input = state->layout->bindings;
	state->offset = 0;
	state->vid = 0;
	state->var_state = SPROM_OPCODE_VAR_STATE_NONE;
	bit_set(state->revs, state->layout->rev);

	return (0);
}

/**
 * Free any resources associated with @p state.
 * 
 * @param state An opcode state previously successfully initialized with
 * bhnd_sprom_opcode_init().
 */
void
bhnd_sprom_opcode_fini(bhnd_sprom_opcode_state *state)
{
	bhnd_nv_free(state->idx);
}


/**
 * Sort function used to prepare our index for querying; sorts
 * bhnd_sprom_opcode_idx_entry values by variable ID, ascending.
 */
static int
bhnd_sprom_opcode_sort_idx(const void *lhs, const void *rhs)
{
	const bhnd_sprom_opcode_idx_entry *l, *r;

	l = lhs;
	r = rhs;

	if (l->vid < r->vid)
		return (-1);
	if (l->vid > r->vid)
		return (1);
	return (0);
}

/**
 * Binary search comparison function used by bhnd_sprom_opcode_index_find();
 * searches bhnd_sprom_opcode_idx_entry values by variable ID, ascending.
 */
static int
bhnd_nvram_opcode_idx_vid_compare(const void *key, const void *rhs)
{
	const bhnd_sprom_opcode_idx_entry	*entry;
	size_t				 	 vid;

	vid = *(const size_t *)key;
	entry = rhs;

	if (vid < entry->vid)
		return (-1);
	if (vid > entry->vid)
		return (1);

	return (0);
}

/**
 * Locate an index entry for the variable with @p name, or NULL if not found.
 * 
 * @param state The opcode state to be queried.
 * @param name	The name to search for.
 *
 * @retval non-NULL	If @p name is found, its index entry value will be
 *			returned.
 * @retval NULL		If @p name is not found.
 */
bhnd_sprom_opcode_idx_entry *
bhnd_sprom_opcode_index_find(bhnd_sprom_opcode_state *state, const char *name)
{
	const struct bhnd_nvram_vardefn	*var;
	size_t				 vid;

	/* Determine the variable ID for the given name */
	if ((var = bhnd_nvram_find_vardefn(name)) == NULL)
		return (NULL);

	vid = bhnd_nvram_get_vardefn_id(var);

	/* Search our index for the variable ID */
	return (bsearch(&vid, state->idx, state->num_idx, sizeof(state->idx[0]),
	    bhnd_nvram_opcode_idx_vid_compare));
}


/**
 * Iterate over all index entries in @p state.
 * 
 * @param		state	The opcode state to be iterated.
 * @param[in,out]	prev	An entry previously returned by
 *				bhnd_sprom_opcode_index_next(), or a NULL value
 *				to begin iteration.
 * 
 * @return Returns the next index entry name, or NULL if all entries have
 * been iterated.
 */
bhnd_sprom_opcode_idx_entry *
bhnd_sprom_opcode_index_next(bhnd_sprom_opcode_state *state,
    bhnd_sprom_opcode_idx_entry *prev)
{
	size_t idxpos;

	/* Get next index position */
	if (prev == NULL) {
		idxpos = 0;
	} else {
		/* Determine current position */
		idxpos = (size_t)(prev - state->idx);
		BHND_NV_ASSERT(idxpos < state->num_idx,
		    ("invalid index %zu", idxpos));

		/* Advance to next entry */
		idxpos++;
	}

	/* Check for EOF */
	if (idxpos == state->num_idx)
		return (NULL);

	return (&state->idx[idxpos]);
}


/**
 * Initialize @p entry with the current variable's opcode state.
 * 
 * @param	state	The opcode state to be saved.
 * @param[out]	entry	The opcode index entry to be initialized from @p state.
 * 
 * @retval 0		success
 * @retval ENXIO	if @p state cannot be serialized as an index entry.
 */
int
bhnd_sprom_opcode_init_entry(bhnd_sprom_opcode_state *state,
    bhnd_sprom_opcode_idx_entry *entry)
{
	size_t opcodes;

	/* We limit the SPROM index representations to the minimal type widths
	 * capable of covering all known layouts */

	/* Save SPROM image offset */
	if (state->offset > UINT16_MAX) {
		SPROM_OP_BAD(state, "cannot index large offset %u\n",
		    state->offset);
		return (ENXIO);
	}

	entry->offset = state->offset;

	/* Save current variable ID */
	if (state->vid > UINT16_MAX) {
		SPROM_OP_BAD(state, "cannot index large vid %zu\n",
		    state->vid);
		return (ENXIO);
	}
	entry->vid = state->vid;

	/* Save opcode position */
	opcodes = (state->input - state->layout->bindings);
	if (opcodes > UINT16_MAX) {
		SPROM_OP_BAD(state, "cannot index large opcode offset "
		    "%zu\n", opcodes);
		return (ENXIO);
	}
	entry->opcodes = opcodes;

	return (0);
}

/**
 * Reset SPROM opcode evaluation state and seek to the @p entry's position.
 * 
 * @param state The opcode state to be reset.
 * @param entry The indexed entry to which we'll seek the opcode state.
 */
int
bhnd_sprom_opcode_seek(bhnd_sprom_opcode_state *state,
    bhnd_sprom_opcode_idx_entry *entry)
{
	int error;

	BHND_NV_ASSERT(entry->opcodes < state->layout->bindings_size,
	    ("index entry references invalid opcode position"));

	/* Reset state */
	if ((error = bhnd_sprom_opcode_reset(state)))
		return (error);

	/* Seek to the indexed sprom opcode offset */
	state->input = state->layout->bindings + entry->opcodes;

	/* Restore the indexed sprom data offset and VID */
	state->offset = entry->offset;

	/* Restore the indexed sprom variable ID */
	if ((error = bhnd_sprom_opcode_set_var(state, entry->vid)))
		return (error);

	return (0);
}

/**
 * Set the current revision range for @p state. This also resets
 * variable state.
 * 
 * @param state The opcode state to update
 * @param start The first revision in the range.
 * @param end The last revision in the range.
 *
 * @retval 0 success
 * @retval non-zero If updating @p state fails, a regular unix error code will
 * be returned.
 */
static inline int
bhnd_sprom_opcode_set_revs(bhnd_sprom_opcode_state *state, uint8_t start,
    uint8_t end)
{
	int error;

	/* Validate the revision range */
	if (start > SPROM_OP_REV_MAX ||
	    end > SPROM_OP_REV_MAX ||
	    end < start)
	{
		SPROM_OP_BAD(state, "invalid revision range: %hhu-%hhu\n",
		    start, end);
		return (EINVAL);
	}

	/* Clear variable state */
	if ((error = bhnd_sprom_opcode_clear_var(state)))
		return (error);

	/* Reset revision mask */
	memset(state->revs, 0x0, sizeof(state->revs));
	bit_nset(state->revs, start, end);

	return (0);
}

/**
 * Set the current variable's value mask for @p state.
 * 
 * @param state The opcode state to update
 * @param mask The mask to be set
 *
 * @retval 0 success
 * @retval non-zero If updating @p state fails, a regular unix error code will
 * be returned.
 */
static inline int
bhnd_sprom_opcode_set_mask(bhnd_sprom_opcode_state *state, uint32_t mask)
{
	if (state->var_state != SPROM_OPCODE_VAR_STATE_OPEN) {
		SPROM_OP_BAD(state, "no open variable definition\n");
		return (EINVAL);
	}

	state->var.mask = mask;
	return (0);
}

/**
 * Set the current variable's value shift for @p state.
 * 
 * @param state The opcode state to update
 * @param shift The shift to be set
 *
 * @retval 0 success
 * @retval non-zero If updating @p state fails, a regular unix error code will
 * be returned.
 */
static inline int
bhnd_sprom_opcode_set_shift(bhnd_sprom_opcode_state *state, int8_t shift)
{
	if (state->var_state != SPROM_OPCODE_VAR_STATE_OPEN) {
		SPROM_OP_BAD(state, "no open variable definition\n");
		return (EINVAL);
	}

	state->var.shift = shift;
	return (0);
}

/**
 * Register a new BIND/BINDN operation with @p state.
 * 
 * @param state The opcode state to update.
 * @param count The number of elements to be bound.
 * @param skip_in The number of input elements to skip after each bind.
 * @param skip_in_negative If true, the input skip should be subtracted from
 * the current offset after each bind. If false, the input skip should be
 * added.
 * @param skip_out The number of output elements to skip after each bind.
 * 
 * @retval 0 success
 * @retval EINVAL if a variable definition is not open.
 * @retval EINVAL if @p skip_in and @p count would trigger an overflow or
 * underflow when applied to the current input offset.
 * @retval ERANGE if @p skip_in would overflow uint32_t when multiplied by
 * @p count and the scale value.
 * @retval ERANGE if @p skip_out would overflow uint32_t when multiplied by
 * @p count and the scale value.
 * @retval non-zero If updating @p state otherwise fails, a regular unix error
 * code will be returned.
 */
static inline int
bhnd_sprom_opcode_set_bind(bhnd_sprom_opcode_state *state, uint8_t count,
    uint8_t skip_in, bool skip_in_negative, uint8_t skip_out)
{
	uint32_t	iskip_total;
	uint32_t	iskip_scaled;
	int		error;

	/* Must have an open variable */
	if (state->var_state != SPROM_OPCODE_VAR_STATE_OPEN) {
		SPROM_OP_BAD(state, "no open variable definition\n");
		SPROM_OP_BAD(state, "BIND outside of variable definition\n");
		return (EINVAL);
	}

	/* Cannot overwite an existing bind definition */
	if (state->var.have_bind) {
		SPROM_OP_BAD(state, "BIND overwrites existing definition\n");
		return (EINVAL);
	}

	/* Must have a count of at least 1 */
	if (count == 0) {
		SPROM_OP_BAD(state, "BIND with zero count\n");
		return (EINVAL);
	}

	/* Scale skip_in by the current type width */
	iskip_scaled = skip_in;
	if ((error = bhnd_sprom_opcode_apply_scale(state, &iskip_scaled)))
		return (error);

	/* Calculate total input bytes skipped: iskip_scaled * count) */
	if (iskip_scaled > 0 && UINT32_MAX / iskip_scaled < count) {
		SPROM_OP_BAD(state, "skip_in %hhu would overflow", skip_in);
		return (EINVAL);
	}

	iskip_total = iskip_scaled * count;

	/* Verify that the skip_in value won't under/overflow the current
	 * input offset. */
	if (skip_in_negative) {
		if (iskip_total > state->offset) {
			SPROM_OP_BAD(state, "skip_in %hhu would underflow "
			    "offset %u\n", skip_in, state->offset);
			return (EINVAL);
		}
	} else {
		if (UINT32_MAX - iskip_total < state->offset) {
			SPROM_OP_BAD(state, "skip_in %hhu would overflow "
			    "offset %u\n", skip_in, state->offset);
			return (EINVAL);
		}
	}

	/* Set the actual count and skip values */
	state->var.have_bind = true;
	state->var.bind.count = count;
	state->var.bind.skip_in = skip_in;
	state->var.bind.skip_out = skip_out;

	state->var.bind.skip_in_negative = skip_in_negative;

	/* Update total bind count for the current variable */
	state->var.bind_total++;

	return (0);
}


/**
 * Apply and clear the current opcode bind state, if any.
 * 
 * @param state The opcode state to update.
 * 
 * @retval 0 success
 * @retval non-zero If updating @p state otherwise fails, a regular unix error
 * code will be returned.
 */
static int
bhnd_sprom_opcode_flush_bind(bhnd_sprom_opcode_state *state)
{
	int		error;
	uint32_t	skip;

	/* Nothing to do? */
	if (state->var_state != SPROM_OPCODE_VAR_STATE_OPEN ||
	    !state->var.have_bind)
		return (0);

	/* Apply SPROM offset adjustment */
	if (state->var.bind.count > 0) {
		skip = state->var.bind.skip_in * state->var.bind.count;
		if ((error = bhnd_sprom_opcode_apply_scale(state, &skip)))
			return (error);

		if (state->var.bind.skip_in_negative) {
			state->offset -= skip;
		} else {
			state->offset += skip;
		}
	}

	/* Clear bind state */
	memset(&state->var.bind, 0, sizeof(state->var.bind));
	state->var.have_bind = false;

	return (0);
}

/**
 * Set the current type to @p type, and reset type-specific
 * stream state.
 *
 * @param state The opcode state to update.
 * @param type The new type.
 * 
 * @retval 0 success
 * @retval EINVAL if @p vid is not a valid variable ID.
 */
static int
bhnd_sprom_opcode_set_type(bhnd_sprom_opcode_state *state, bhnd_nvram_type type)
{
	bhnd_nvram_type	base_type;
	size_t		width;
	uint32_t	mask;

	/* Must have an open variable definition */
	if (state->var_state != SPROM_OPCODE_VAR_STATE_OPEN) {
		SPROM_OP_BAD(state, "type set outside variable definition\n");
		return (EINVAL);
	}

	/* Fetch type width for use as our scale value */
	width = bhnd_nvram_type_width(type);
	if (width == 0) {
		SPROM_OP_BAD(state, "unsupported variable-width type: %d\n",
		    type);
		return (EINVAL);
	} else if (width > UINT32_MAX) {
		SPROM_OP_BAD(state, "invalid type width %zu for type: %d\n",
		    width, type);
		return (EINVAL);
	}

	/* Determine default mask value for the element type */
	base_type = bhnd_nvram_base_type(type);
	switch (base_type) {
	case BHND_NVRAM_TYPE_UINT8:
	case BHND_NVRAM_TYPE_INT8:
	case BHND_NVRAM_TYPE_CHAR:
		mask = UINT8_MAX;
		break;
	case BHND_NVRAM_TYPE_UINT16:
	case BHND_NVRAM_TYPE_INT16:
		mask = UINT16_MAX;
		break;
	case BHND_NVRAM_TYPE_UINT32:
	case BHND_NVRAM_TYPE_INT32:
		mask = UINT32_MAX;
		break;
	case BHND_NVRAM_TYPE_STRING:
		/* fallthrough (unused by SPROM) */
	default:
		SPROM_OP_BAD(state, "unsupported type: %d\n", type);
		return (EINVAL);
	}
	
	/* Update state */
	state->var.base_type = base_type;
	state->var.mask = mask;
	state->var.scale = (uint32_t)width;

	return (0);
}

/**
 * Clear current variable state, if any.
 * 
 * @param state The opcode state to update.
 */
static int
bhnd_sprom_opcode_clear_var(bhnd_sprom_opcode_state *state)
{
	if (state->var_state == SPROM_OPCODE_VAR_STATE_NONE)
		return (0);

	BHND_NV_ASSERT(state->var_state == SPROM_OPCODE_VAR_STATE_DONE,
	    ("incomplete variable definition"));
	BHND_NV_ASSERT(!state->var.have_bind, ("stale bind state"));

	memset(&state->var, 0, sizeof(state->var));
	state->var_state = SPROM_OPCODE_VAR_STATE_NONE;

	return (0);
}

/**
 * Set the current variable's array element count to @p nelem.
 *
 * @param state The opcode state to update.
 * @param nelem The new array length.
 * 
 * @retval 0 success
 * @retval EINVAL if no open variable definition exists.
 * @retval EINVAL if @p nelem is zero.
 * @retval ENXIO if @p nelem is greater than one, and the current variable does
 * not have an array type.
 * @retval ENXIO if @p nelem exceeds the array length of the NVRAM variable
 * definition.
 */
static int
bhnd_sprom_opcode_set_nelem(bhnd_sprom_opcode_state *state, uint8_t nelem)
{
	const struct bhnd_nvram_vardefn	*var;

	/* Must have a defined variable */
	if (state->var_state != SPROM_OPCODE_VAR_STATE_OPEN) {
		SPROM_OP_BAD(state, "array length set without open variable "
		    "state");
		return (EINVAL);
	}

	/* Locate the actual variable definition */
	if ((var = bhnd_nvram_get_vardefn(state->vid)) == NULL) {
		SPROM_OP_BAD(state, "unknown variable ID: %zu\n", state->vid);
		return (EINVAL);
	}

	/* Must be greater than zero */
	if (nelem == 0) {
		SPROM_OP_BAD(state, "invalid nelem: %hhu\n", nelem);
		return (EINVAL);
	}

	/* If the variable is not an array-typed value, the array length
	 * must be 1 */
	if (!bhnd_nvram_is_array_type(var->type) && nelem != 1) {
		SPROM_OP_BAD(state, "nelem %hhu on non-array %zu\n", nelem,
		    state->vid);
		return (ENXIO);
	}
	
	/* Cannot exceed the variable's defined array length */
	if (nelem > var->nelem) {
		SPROM_OP_BAD(state, "nelem %hhu exceeds %zu length %hhu\n",
		    nelem, state->vid, var->nelem);
		return (ENXIO);
	}

	/* Valid length; update state */
	state->var.nelem = nelem;

	return (0);
}

/**
 * Set the current variable ID to @p vid, and reset variable-specific
 * stream state.
 *
 * @param state The opcode state to update.
 * @param vid The new variable ID.
 * 
 * @retval 0 success
 * @retval EINVAL if @p vid is not a valid variable ID.
 */
static int
bhnd_sprom_opcode_set_var(bhnd_sprom_opcode_state *state, size_t vid)
{
	const struct bhnd_nvram_vardefn	*var;
	int				 error;

	BHND_NV_ASSERT(state->var_state == SPROM_OPCODE_VAR_STATE_NONE,
	    ("overwrite of open variable definition"));

	/* Locate the variable definition */
	if ((var = bhnd_nvram_get_vardefn(vid)) == NULL) {
		SPROM_OP_BAD(state, "unknown variable ID: %zu\n", vid);
		return (EINVAL);
	}

	/* Update vid and var state */
	state->vid = vid;
	state->var_state = SPROM_OPCODE_VAR_STATE_OPEN;

	/* Initialize default variable record values */
	memset(&state->var, 0x0, sizeof(state->var));

	/* Set initial base type */
	if ((error = bhnd_sprom_opcode_set_type(state, var->type)))
		return (error);

	/* Set default array length */
	if ((error = bhnd_sprom_opcode_set_nelem(state, var->nelem)))
		return (error);

	return (0);
}

/**
 * Mark the currently open variable definition as complete.
 * 
 * @param state The opcode state to update.
 *
 * @retval 0 success
 * @retval EINVAL if no incomplete open variable definition exists.
 */
static int
bhnd_sprom_opcode_end_var(bhnd_sprom_opcode_state *state)
{
	if (state->var_state != SPROM_OPCODE_VAR_STATE_OPEN) {
		SPROM_OP_BAD(state, "no open variable definition\n");
		return (EINVAL);
	}

	state->var_state = SPROM_OPCODE_VAR_STATE_DONE;
	return (0);
}

/**
 * Apply the current scale to @p value.
 * 
 * @param state The SPROM opcode state.
 * @param[in,out] value The value to scale
 * 
 * @retval 0 success
 * @retval EINVAL if no open variable definition exists.
 * @retval EINVAL if applying the current scale would overflow.
 */
int
bhnd_sprom_opcode_apply_scale(bhnd_sprom_opcode_state *state, uint32_t *value)
{
	/* Must have a defined variable (and thus, scale) */
	if (state->var_state != SPROM_OPCODE_VAR_STATE_OPEN) {
		SPROM_OP_BAD(state, "scaled value encoded without open "
		    "variable state");
		return (EINVAL);
	}

	/* Applying the scale value must not overflow */
	if (UINT32_MAX / state->var.scale < *value) {
		SPROM_OP_BAD(state, "cannot represent %" PRIu32 " * %" PRIu32
		    "\n", *value, state->var.scale);
		return (EINVAL);
	}

	*value = (*value) * state->var.scale;
	return (0);
}

/**
 * Read a SPROM_OP_DATA_* value from @p opcodes.
 * 
 * @param state The SPROM opcode state.
 * @param type The SROM_OP_DATA_* type to be read.
 * @param opval On success, the 32bit data representation. If @p type is signed,
 * the value will be appropriately sign extended and may be directly cast to
 * int32_t.
 * 
 * @retval 0 success
 * @retval non-zero If reading the value otherwise fails, a regular unix error
 * code will be returned.
 */
static int
bhnd_sprom_opcode_read_opval32(bhnd_sprom_opcode_state *state, uint8_t type,
   uint32_t *opval)
{
	const uint8_t	*p;
	int		 error;

	p = state->input;
	switch (type) {
	case SPROM_OP_DATA_I8:
		/* Convert to signed value first, then sign extend */
		*opval = (int32_t)(int8_t)(*p);
		p += 1;
		break;
	case SPROM_OP_DATA_U8:
		*opval = *p;
		p += 1;
		break;
	case SPROM_OP_DATA_U8_SCALED:
		*opval = *p;

		if ((error = bhnd_sprom_opcode_apply_scale(state, opval)))
			return (error);

		p += 1;
		break;
	case SPROM_OP_DATA_U16:
		*opval = le16dec(p);
		p += 2;
		break;
	case SPROM_OP_DATA_U32:
		*opval = le32dec(p);
		p += 4;
		break;
	default:
		SPROM_OP_BAD(state, "unsupported data type: %hhu\n", type);
		return (EINVAL);
	}

	/* Update read address */
	state->input = p;

	return (0);
}

/**
 * Return true if our layout revision is currently defined by the SPROM
 * opcode state.
 * 
 * This may be used to test whether the current opcode stream state applies
 * to the layout that we are actually parsing.
 * 
 * A given opcode stream may cover multiple layout revisions, switching
 * between them prior to defining a set of variables.
 */
static inline bool
bhnd_sprom_opcode_matches_layout_rev(bhnd_sprom_opcode_state *state)
{
	return (bit_test(state->revs, state->layout->rev));
}

/**
 * When evaluating @p state and @p opcode, rewrite @p opcode based on the
 * current evaluation state.
 * 
 * This allows the insertion of implicit opcodes into interpretation of the
 * opcode stream.
 * 
 * If @p opcode is rewritten, it should be returned from
 * bhnd_sprom_opcode_step() instead of the opcode parsed from @p state's opcode
 * stream.
 * 
 * If @p opcode remains unmodified, then bhnd_sprom_opcode_step() should
 * proceed to standard evaluation.
 */
static int
bhnd_sprom_opcode_rewrite_opcode(bhnd_sprom_opcode_state *state,
    uint8_t *opcode)
{
	uint8_t	op;
	int	error;

	op = SPROM_OPCODE_OP(*opcode);
	switch (state->var_state) {
	case SPROM_OPCODE_VAR_STATE_NONE:
		/* No open variable definition */
		return (0);

	case SPROM_OPCODE_VAR_STATE_OPEN:
		/* Open variable definition; check for implicit closure. */

		/*
		 * If a variable definition contains no explicit bind
		 * instructions prior to closure, we must generate a DO_BIND
		 * instruction with count and skip values of 1.
		 */
		if (SPROM_OP_IS_VAR_END(op) &&
		    state->var.bind_total == 0)
		{
			uint8_t	count, skip_in, skip_out;
			bool	skip_in_negative;

			/* Create bind with skip_in/skip_out of 1, count of 1 */
			count = 1;
			skip_in = 1;
			skip_out = 1;
			skip_in_negative = false;

			error = bhnd_sprom_opcode_set_bind(state, count,
			    skip_in, skip_in_negative, skip_out);
			if (error)
				return (error);

			/* Return DO_BIND */
			*opcode = SPROM_OPCODE_DO_BIND |
			    (0 << SPROM_OP_BIND_SKIP_IN_SIGN) |
			    (1 << SPROM_OP_BIND_SKIP_IN_SHIFT) |
			    (1 << SPROM_OP_BIND_SKIP_OUT_SHIFT);

			return (0);
		}

		/*
		 * If a variable is implicitly closed (e.g. by a new variable
		 * definition), we must generate a VAR_END instruction.
		 */
		if (SPROM_OP_IS_IMPLICIT_VAR_END(op)) {
			/* Mark as complete */
			if ((error = bhnd_sprom_opcode_end_var(state)))
				return (error);

			/* Return VAR_END */
			*opcode = SPROM_OPCODE_VAR_END;
			return (0);
		}
		break;


	case SPROM_OPCODE_VAR_STATE_DONE:
		/* Previously completed variable definition. Discard variable
		 * state */
		return (bhnd_sprom_opcode_clear_var(state));
	}

	/* Nothing to do */
	return (0);
}

/**
 * Evaluate one opcode from @p state.
 *
 * @param state The opcode state to be evaluated.
 * @param[out] opcode On success, the evaluated opcode
 * 
 * @retval 0 success
 * @retval ENOENT if EOF is reached
 * @retval non-zero if evaluation otherwise fails, a regular unix error
 * code will be returned.
 */
static int
bhnd_sprom_opcode_step(bhnd_sprom_opcode_state *state, uint8_t *opcode)
{
	int error;

	while (*state->input != SPROM_OPCODE_EOF) {
		uint32_t	val;
		uint8_t		op, rewrite, immd;

		/* Fetch opcode */
		*opcode = *state->input;
		op = SPROM_OPCODE_OP(*opcode);
		immd = SPROM_OPCODE_IMM(*opcode);

		/* Clear any existing bind state */
		if ((error = bhnd_sprom_opcode_flush_bind(state)))
			return (error);

		/* Insert local opcode based on current state? */
		rewrite = *opcode;
		if ((error = bhnd_sprom_opcode_rewrite_opcode(state, &rewrite)))
			return (error);

		if (rewrite != *opcode) {
			/* Provide rewritten opcode */
			*opcode = rewrite;

			/* We must keep evaluating until we hit a state
			 * applicable to the SPROM revision we're parsing */
			if (!bhnd_sprom_opcode_matches_layout_rev(state))
				continue;

			return (0);
		}

		/* Advance input */
		state->input++;

		switch (op) {
		case SPROM_OPCODE_VAR_IMM:
			if ((error = bhnd_sprom_opcode_set_var(state, immd)))
				return (error);
			break;

		case SPROM_OPCODE_VAR_REL_IMM:
			error = bhnd_sprom_opcode_set_var(state,
			    state->vid + immd);
			if (error)
				return (error);
			break;

		case SPROM_OPCODE_VAR:
			error = bhnd_sprom_opcode_read_opval32(state, immd,
			    &val);
			if (error)
				return (error);

			if ((error = bhnd_sprom_opcode_set_var(state, val)))
				return (error);

			break;

		case SPROM_OPCODE_VAR_END:
			if ((error = bhnd_sprom_opcode_end_var(state)))
				return (error);
			break;

		case SPROM_OPCODE_NELEM:
			immd = *state->input;
			if ((error = bhnd_sprom_opcode_set_nelem(state, immd)))
				return (error);

			state->input++;
			break;

		case SPROM_OPCODE_DO_BIND:
		case SPROM_OPCODE_DO_BINDN: {
			uint8_t	count, skip_in, skip_out;
			bool	skip_in_negative;

			/* Fetch skip arguments */
			skip_in = (immd & SPROM_OP_BIND_SKIP_IN_MASK) >>
			    SPROM_OP_BIND_SKIP_IN_SHIFT;

			skip_in_negative =
			    ((immd & SPROM_OP_BIND_SKIP_IN_SIGN) != 0);

			skip_out = (immd & SPROM_OP_BIND_SKIP_OUT_MASK) >>
			      SPROM_OP_BIND_SKIP_OUT_SHIFT;

			/* Fetch count argument (if any) */
			if (op == SPROM_OPCODE_DO_BINDN) {
				/* Count is provided as trailing U8 */
				count = *state->input;
				state->input++;
			} else {
				count = 1;
			}

			/* Set BIND state */
			error = bhnd_sprom_opcode_set_bind(state, count,
			    skip_in, skip_in_negative, skip_out);
			if (error)
				return (error);

			break;
		}
		case SPROM_OPCODE_DO_BINDN_IMM: {
			uint8_t	count, skip_in, skip_out;
			bool	skip_in_negative;

			/* Implicit skip_in/skip_out of 1, count encoded as immd
			 * value */
			count = immd;
			skip_in = 1;
			skip_out = 1;
			skip_in_negative = false;

			error = bhnd_sprom_opcode_set_bind(state, count,
			    skip_in, skip_in_negative, skip_out);
			if (error)
				return (error);
			break;
		}

		case SPROM_OPCODE_REV_IMM:
			error = bhnd_sprom_opcode_set_revs(state, immd, immd);
			if (error)
				return (error);
			break;

		case SPROM_OPCODE_REV_RANGE: {
			uint8_t range;
			uint8_t rstart, rend;

			/* Revision range is encoded in next byte, as
			 * { uint8_t start:4, uint8_t end:4 } */
			range = *state->input;
			rstart = (range & SPROM_OP_REV_START_MASK) >>
			    SPROM_OP_REV_START_SHIFT;
			rend = (range & SPROM_OP_REV_END_MASK) >>
			    SPROM_OP_REV_END_SHIFT;

			/* Update revision bitmask */
			error = bhnd_sprom_opcode_set_revs(state, rstart, rend);
			if (error)
				return (error);

			/* Advance input */
			state->input++;
			break;
		}
		case SPROM_OPCODE_MASK_IMM:
			if ((error = bhnd_sprom_opcode_set_mask(state, immd)))
				return (error);
			break;

		case SPROM_OPCODE_MASK:
			error = bhnd_sprom_opcode_read_opval32(state, immd,
			    &val);
			if (error)
				return (error);

			if ((error = bhnd_sprom_opcode_set_mask(state, val)))
				return (error);
			break;

		case SPROM_OPCODE_SHIFT_IMM:
			error = bhnd_sprom_opcode_set_shift(state, immd * 2);
			if (error)
				return (error);
			break;

		case SPROM_OPCODE_SHIFT: {
			int8_t shift;

			if (immd == SPROM_OP_DATA_I8) {
				shift = (int8_t)(*state->input);
			} else if (immd == SPROM_OP_DATA_U8) {
				val = *state->input;
				if (val > INT8_MAX) {
					SPROM_OP_BAD(state, "invalid shift "
					    "value: %#x\n", val);
				}

				shift = val;
			} else {
				SPROM_OP_BAD(state, "unsupported shift data "
				    "type: %#hhx\n", immd);
				return (EINVAL);
			}

			if ((error = bhnd_sprom_opcode_set_shift(state, shift)))
				return (error);

			state->input++;
			break;
		}
		case SPROM_OPCODE_OFFSET_REL_IMM:
			/* Fetch unscaled relative offset */
			val = immd;

			/* Apply scale */
			error = bhnd_sprom_opcode_apply_scale(state, &val);
			if (error)
				return (error);
	
			/* Adding val must not overflow our offset */
			if (UINT32_MAX - state->offset < val) {
				BHND_NV_LOG("offset out of range\n");
				return (EINVAL);
			}

			/* Adjust offset */
			state->offset += val;
			break;
		case SPROM_OPCODE_OFFSET:
			error = bhnd_sprom_opcode_read_opval32(state, immd,
			    &val);
			if (error)
				return (error);

			state->offset = val;
			break;

		case SPROM_OPCODE_TYPE:
			/* Type follows as U8 */
			immd = *state->input;
			state->input++;

			/* fall through */
		case SPROM_OPCODE_TYPE_IMM:
			switch (immd) {
			case BHND_NVRAM_TYPE_UINT8:
			case BHND_NVRAM_TYPE_UINT16:
			case BHND_NVRAM_TYPE_UINT32:
			case BHND_NVRAM_TYPE_UINT64:
			case BHND_NVRAM_TYPE_INT8:
			case BHND_NVRAM_TYPE_INT16:
			case BHND_NVRAM_TYPE_INT32:
			case BHND_NVRAM_TYPE_INT64:
			case BHND_NVRAM_TYPE_CHAR:
			case BHND_NVRAM_TYPE_STRING:
				error = bhnd_sprom_opcode_set_type(state,
				    (bhnd_nvram_type)immd);
				if (error)
					return (error);
				break;
			default:
				BHND_NV_LOG("unrecognized type %#hhx\n", immd);
				return (EINVAL);
			}
			break;

		default:
			BHND_NV_LOG("unrecognized opcode %#hhx\n", *opcode);
			return (EINVAL);
		}

		/* We must keep evaluating until we hit a state applicable to
		 * the SPROM revision we're parsing */
		if (bhnd_sprom_opcode_matches_layout_rev(state))
			return (0);
	}

	/* End of opcode stream */
	return (ENOENT);
}

/**
 * Reset SPROM opcode evaluation state, seek to the @p entry's position,
 * and perform complete evaluation of the variable's opcodes.
 * 
 * @param state The opcode state to be to be evaluated.
 * @param entry The indexed variable location.
 *
 * @retval 0 success
 * @retval non-zero If evaluation fails, a regular unix error code will be
 * returned.
 */
int
bhnd_sprom_opcode_eval_var(bhnd_sprom_opcode_state *state,
    bhnd_sprom_opcode_idx_entry *entry)
{
	uint8_t	opcode;
	int	error;

	/* Seek to entry */
	if ((error = bhnd_sprom_opcode_seek(state, entry)))
		return (error);

	/* Parse full variable definition */
	while ((error = bhnd_sprom_opcode_step(state, &opcode)) == 0) {
		/* Iterate until VAR_END */
		if (SPROM_OPCODE_OP(opcode) != SPROM_OPCODE_VAR_END)
			continue;

		BHND_NV_ASSERT(state->var_state == SPROM_OPCODE_VAR_STATE_DONE,
		    ("incomplete variable definition"));

		return (0);
	}

	/* Error parsing definition */
	return (error);
}

/**
 * Evaluate @p state until the next variable definition is found.
 * 
 * @param state The opcode state to be evaluated.
 * 
 * @retval 0 success
 * @retval ENOENT if no additional variable definitions are available.
 * @retval non-zero if evaluation otherwise fails, a regular unix error
 * code will be returned.
 */
int
bhnd_sprom_opcode_next_var(bhnd_sprom_opcode_state *state)
{
	uint8_t	opcode;
	int	error;

	/* Step until we hit a variable opcode */
	while ((error = bhnd_sprom_opcode_step(state, &opcode)) == 0) {
		switch (SPROM_OPCODE_OP(opcode)) {
		case SPROM_OPCODE_VAR:
		case SPROM_OPCODE_VAR_IMM:
		case SPROM_OPCODE_VAR_REL_IMM:
			BHND_NV_ASSERT(
			    state->var_state == SPROM_OPCODE_VAR_STATE_OPEN,
			    ("missing variable definition"));

			return (0);
		default:
			continue;
		}
	}

	/* Reached EOF, or evaluation failed */
	return (error);
}

/**
 * Evaluate @p state until the next binding for the current variable definition
 * is found.
 * 
 * @param state The opcode state to be evaluated.
 * 
 * @retval 0 success
 * @retval ENOENT if no additional binding opcodes are found prior to reaching
 * a new variable definition, or the end of @p state's binding opcodes.
 * @retval non-zero if evaluation otherwise fails, a regular unix error
 * code will be returned.
 */
int
bhnd_sprom_opcode_next_binding(bhnd_sprom_opcode_state *state)
{
	uint8_t	opcode;
	int	error;

	if (state->var_state != SPROM_OPCODE_VAR_STATE_OPEN)
		return (EINVAL);

	/* Step until we hit a bind opcode, or a new variable */
	while ((error = bhnd_sprom_opcode_step(state, &opcode)) == 0) {
		switch (SPROM_OPCODE_OP(opcode)) {
		case SPROM_OPCODE_DO_BIND:
		case SPROM_OPCODE_DO_BINDN:
		case SPROM_OPCODE_DO_BINDN_IMM:
			/* Found next bind */
			BHND_NV_ASSERT(
			    state->var_state == SPROM_OPCODE_VAR_STATE_OPEN,
			    ("missing variable definition"));
			BHND_NV_ASSERT(state->var.have_bind, ("missing bind"));

			return (0);

		case SPROM_OPCODE_VAR_END:
			/* No further binding opcodes */ 
			BHND_NV_ASSERT(
			    state->var_state == SPROM_OPCODE_VAR_STATE_DONE,
			    ("variable definition still available"));
			return (ENOENT);
		}
	}

	/* Not found, or evaluation failed */
	return (error);
}
