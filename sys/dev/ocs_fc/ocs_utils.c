/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 *
 */

#include "ocs.h"
#include "ocs_os.h"

#define DEFAULT_SLAB_LEN		(64*1024)

struct ocs_array_s {
	ocs_os_handle_t os;

	uint32_t size;
	uint32_t count;

	uint32_t n_rows;
	uint32_t elems_per_row;
	uint32_t bytes_per_row;

	void **array_rows;
	uint32_t array_rows_len;
};

static uint32_t slab_len = DEFAULT_SLAB_LEN;

/**
 * @brief Set array slab allocation length
 *
 * The slab length is the maximum allocation length that the array uses.
 * The default 64k slab length may be overridden using this function.
 *
 * @param len new slab length.
 *
 * @return none
 */
void
ocs_array_set_slablen(uint32_t len)
{
	slab_len = len;
}

/**
 * @brief Allocate an array object
 *
 * An array object of size and number of elements is allocated
 *
 * @param os OS handle
 * @param size size of array elements in bytes
 * @param count number of elements in array
 *
 * @return pointer to array object or NULL
 */
ocs_array_t *
ocs_array_alloc(ocs_os_handle_t os, uint32_t size, uint32_t count)
{
	ocs_array_t *array = NULL;
	uint32_t i;

	/* Fail if the item size exceeds slab_len - caller should increase slab_size,
	 * or not use this API.
	 */
	if (size > slab_len) {
		ocs_log_err(NULL, "Error: size exceeds slab length\n");
		return NULL;
	}

	array = ocs_malloc(os, sizeof(*array), OCS_M_ZERO | OCS_M_NOWAIT);
	if (array == NULL) {
		return NULL;
	}

	array->os = os;
	array->size = size;
	array->count = count;
	array->elems_per_row = slab_len / size;
	array->n_rows = (count + array->elems_per_row - 1) / array->elems_per_row;
	array->bytes_per_row = array->elems_per_row * array->size;

	array->array_rows_len = array->n_rows * sizeof(*array->array_rows);
	array->array_rows = ocs_malloc(os, array->array_rows_len, OCS_M_ZERO | OCS_M_NOWAIT);
	if (array->array_rows == NULL) {
		ocs_array_free(array);
		return NULL;
	}
	for (i = 0; i < array->n_rows; i++) {
		array->array_rows[i] = ocs_malloc(os, array->bytes_per_row, OCS_M_ZERO | OCS_M_NOWAIT);
		if (array->array_rows[i] == NULL) {
			ocs_array_free(array);
			return NULL;
		}
	}

	return array;
}

/**
 * @brief Free an array object
 *
 * Frees a prevously allocated array object
 *
 * @param array pointer to array object
 *
 * @return none
 */
void
ocs_array_free(ocs_array_t *array)
{
	uint32_t i;

	if (array != NULL) {
		if (array->array_rows != NULL) {
			for (i = 0; i < array->n_rows; i++) {
				if (array->array_rows[i] != NULL) {
					ocs_free(array->os, array->array_rows[i], array->bytes_per_row);
				}
			}
			ocs_free(array->os, array->array_rows, array->array_rows_len);
		}
		ocs_free(array->os, array, sizeof(*array));
	}
}

/**
 * @brief Return reference to an element of an array object
 *
 * Return the address of an array element given an index
 *
 * @param array pointer to array object
 * @param idx array element index
 *
 * @return rointer to array element, or NULL if index out of range
 */
void *ocs_array_get(ocs_array_t *array, uint32_t idx)
{
	void *entry = NULL;

	if (idx < array->count) {
		uint32_t row = idx / array->elems_per_row;
		uint32_t offset = idx % array->elems_per_row;
		entry = ((uint8_t*)array->array_rows[row]) + (offset * array->size);
	}
	return entry;
}

/**
 * @brief Return number of elements in an array
 *
 * Return the number of elements in an array
 *
 * @param array pointer to array object
 *
 * @return returns count of elements in an array
 */
uint32_t
ocs_array_get_count(ocs_array_t *array)
{
	return array->count;
}

/**
 * @brief Return size of array elements in bytes
 *
 * Returns the size in bytes of each array element
 *
 * @param array pointer to array object
 *
 * @return size of array element
 */
uint32_t
ocs_array_get_size(ocs_array_t *array)
{
	return array->size;
}

/**
 * @brief Void pointer array structure
 *
 * This structure describes an object consisting of an array of void
 * pointers.   The object is allocated with a maximum array size, entries
 * are then added to the array with while maintaining an entry count.   A set of
 * iterator APIs are included to allow facilitate cycling through the array
 * entries in a circular fashion.
 *
 */
struct ocs_varray_s {
	ocs_os_handle_t os;
	uint32_t array_count;			/*>> maximum entry count in array */
	void **array;				/*>> pointer to allocated array memory */
	uint32_t entry_count;			/*>> number of entries added to the array */
	uint32_t next_index;			/*>> iterator next index */
	ocs_lock_t lock;			/*>> iterator lock */
};

/**
 * @brief Allocate a void pointer array
 *
 * A void pointer array of given length is allocated.
 *
 * @param os OS handle
 * @param array_count Array size
 *
 * @return returns a pointer to the ocs_varray_t object, other NULL on error
 */
ocs_varray_t *
ocs_varray_alloc(ocs_os_handle_t os, uint32_t array_count)
{
	ocs_varray_t *va;

	va = ocs_malloc(os, sizeof(*va), OCS_M_ZERO | OCS_M_NOWAIT);
	if (va != NULL) {
		va->os = os;
		va->array_count = array_count;
		va->array = ocs_malloc(os, sizeof(*va->array) * va->array_count, OCS_M_ZERO | OCS_M_NOWAIT);
		if (va->array != NULL) {
			va->next_index = 0;
			ocs_lock_init(os, &va->lock, "varray:%p", va);
		} else {
			ocs_free(os, va, sizeof(*va));
			va = NULL;
		}
	}
	return va;
}

/**
 * @brief Free a void pointer array
 *
 * The void pointer array object is free'd
 *
 * @param va Pointer to void pointer array
 *
 * @return none
 */
void
ocs_varray_free(ocs_varray_t *va)
{
	if (va != NULL) {
		ocs_lock_free(&va->lock);
		if (va->array != NULL) {
			ocs_free(va->os, va->array, sizeof(*va->array) * va->array_count);
		}
		ocs_free(va->os, va, sizeof(*va));
	}
}

/**
 * @brief Add an entry to a void pointer array
 *
 * An entry is added to the void pointer array
 *
 * @param va Pointer to void pointer array
 * @param entry Pointer to entry to add
 *
 * @return returns 0 if entry was added, -1 if there is no more space in the array
 */
int32_t
ocs_varray_add(ocs_varray_t *va, void *entry)
{
	uint32_t rc = -1;

	ocs_lock(&va->lock);
		if (va->entry_count < va->array_count) {
			va->array[va->entry_count++] = entry;
			rc = 0;
		}
	ocs_unlock(&va->lock);

	return rc;
}

/**
 * @brief Reset the void pointer array iterator
 *
 * The next index value of the void pointer array iterator is cleared.
 *
 * @param va Pointer to void pointer array
 *
 * @return none
 */
void
ocs_varray_iter_reset(ocs_varray_t *va)
{
	ocs_lock(&va->lock);
		va->next_index = 0;
	ocs_unlock(&va->lock);
}

/**
 * @brief Return next entry from a void pointer array
 *
 * The next entry in the void pointer array is returned.
 *
 * @param va Pointer to void point array
 *
 * Note: takes the void pointer array lock
 *
 * @return returns next void pointer entry
 */
void *
ocs_varray_iter_next(ocs_varray_t *va)
{
	void *rval = NULL;

	if (va != NULL) {
		ocs_lock(&va->lock);
			rval = _ocs_varray_iter_next(va);
		ocs_unlock(&va->lock);
	}
	return rval;
}

/**
 * @brief Return next entry from a void pointer array
 *
 * The next entry in the void pointer array is returned.
 *
 * @param va Pointer to void point array
 *
 * Note: doesn't take the void pointer array lock
 *
 * @return returns next void pointer entry
 */
void *
_ocs_varray_iter_next(ocs_varray_t *va)
{
	void *rval;

	rval = va->array[va->next_index];
	if (++va->next_index >= va->entry_count) {
		va->next_index = 0;
	}
	return rval;
}

/**
 * @brief Take void pointer array lock
 *
 * Takes the lock for the given void pointer array
 *
 * @param va Pointer to void pointer array
 *
 * @return none
 */
void
ocs_varray_lock(ocs_varray_t *va)
{
	ocs_lock(&va->lock);
}

/**
 * @brief Release void pointer array lock
 *
 * Releases the lock for the given void pointer array
 *
 * @param va Pointer to void pointer array
 *
 * @return none
 */
void
ocs_varray_unlock(ocs_varray_t *va)
{
	ocs_unlock(&va->lock);
}

/**
 * @brief Return entry count for a void pointer array
 *
 * The entry count for a void pointer array is returned
 *
 * @param va Pointer to void pointer array
 *
 * @return returns entry count
 */
uint32_t
ocs_varray_get_count(ocs_varray_t *va)
{
	uint32_t rc;

	ocs_lock(&va->lock);
		rc = va->entry_count;
	ocs_unlock(&va->lock);
	return rc;
}


struct ocs_cbuf_s {
	ocs_os_handle_t os;		/*<< OS handle */
	uint32_t entry_count;		/*<< entry count */
	void **array;			/*<< pointer to array of cbuf pointers */
	uint32_t pidx;			/*<< producer index */
	uint32_t cidx;			/*<< consumer index */
	ocs_lock_t cbuf_plock;		/*<< idx lock */
	ocs_lock_t cbuf_clock;		/*<< idx lock */
	ocs_sem_t cbuf_psem;		/*<< cbuf producer counting semaphore */
	ocs_sem_t cbuf_csem;		/*<< cbuf consumer counting semaphore */
};

/**
 * @brief Initialize a circular buffer queue
 *
 * A circular buffer with producer/consumer API is allocated
 *
 * @param os OS handle
 * @param entry_count count of entries
 *
 * @return returns pointer to circular buffer, or NULL
 */
ocs_cbuf_t*
ocs_cbuf_alloc(ocs_os_handle_t os, uint32_t entry_count)
{
	ocs_cbuf_t *cbuf;

	cbuf = ocs_malloc(os, sizeof(*cbuf), OCS_M_NOWAIT | OCS_M_ZERO);
	if (cbuf == NULL) {
		return NULL;
	}

	cbuf->os = os;
	cbuf->entry_count = entry_count;
	cbuf->pidx = 0;
	cbuf->cidx = 0;

	ocs_lock_init(NULL, &cbuf->cbuf_clock, "cbuf_c:%p", cbuf);
	ocs_lock_init(NULL, &cbuf->cbuf_plock, "cbuf_p:%p", cbuf);
	ocs_sem_init(&cbuf->cbuf_csem, 0, "cbuf:%p", cbuf);
	ocs_sem_init(&cbuf->cbuf_psem, cbuf->entry_count, "cbuf:%p", cbuf);

	cbuf->array = ocs_malloc(os, entry_count * sizeof(*cbuf->array), OCS_M_NOWAIT | OCS_M_ZERO);
	if (cbuf->array == NULL) {
		ocs_cbuf_free(cbuf);
		return NULL;
	}

	return cbuf;
}

/**
 * @brief Free a circular buffer
 *
 * The memory resources of a circular buffer are free'd
 *
 * @param cbuf pointer to circular buffer
 *
 * @return none
 */
void
ocs_cbuf_free(ocs_cbuf_t *cbuf)
{
	if (cbuf != NULL) {
		if (cbuf->array != NULL) {
			ocs_free(cbuf->os, cbuf->array, sizeof(*cbuf->array) * cbuf->entry_count);
		}
		ocs_lock_free(&cbuf->cbuf_clock);
		ocs_lock_free(&cbuf->cbuf_plock);
		ocs_free(cbuf->os, cbuf, sizeof(*cbuf));
	}
}

/**
 * @brief Get pointer to buffer
 *
 * Wait for a buffer to become available, and return a pointer to the buffer.
 *
 * @param cbuf pointer to circular buffer
 * @param timeout_usec timeout in microseconds
 *
 * @return pointer to buffer, or NULL if timeout
 */
void*
ocs_cbuf_get(ocs_cbuf_t *cbuf, int32_t timeout_usec)
{
	void *ret = NULL;

	if (likely(ocs_sem_p(&cbuf->cbuf_csem, timeout_usec) == 0)) {
		ocs_lock(&cbuf->cbuf_clock);
			ret = cbuf->array[cbuf->cidx];
			if (unlikely(++cbuf->cidx >= cbuf->entry_count)) {
				cbuf->cidx = 0;
			}
		ocs_unlock(&cbuf->cbuf_clock);
		ocs_sem_v(&cbuf->cbuf_psem);
	}
	return ret;
}

/**
 * @brief write a buffer
 *
 * The buffer is written to the circular buffer.
 *
 * @param cbuf pointer to circular buffer
 * @param elem pointer to entry
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_cbuf_put(ocs_cbuf_t *cbuf, void *elem)
{
	int32_t rc = 0;

	if (likely(ocs_sem_p(&cbuf->cbuf_psem, -1) == 0)) {
		ocs_lock(&cbuf->cbuf_plock);
			cbuf->array[cbuf->pidx] = elem;
			if (unlikely(++cbuf->pidx >= cbuf->entry_count)) {
				cbuf->pidx = 0;
			}
		ocs_unlock(&cbuf->cbuf_plock);
		ocs_sem_v(&cbuf->cbuf_csem);
	} else {
		rc = -1;
	}
	return rc;
}

/**
 * @brief Prime a circular buffer data
 *
 * Post array buffers to a circular buffer
 *
 * @param cbuf pointer to circular buffer
 * @param array pointer to buffer array
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_cbuf_prime(ocs_cbuf_t *cbuf, ocs_array_t *array)
{
	uint32_t i;
	uint32_t count = MIN(ocs_array_get_count(array), cbuf->entry_count);

	for (i = 0; i < count; i++) {
		ocs_cbuf_put(cbuf, ocs_array_get(array, i));
	}
	return 0;
}

/**
 * @brief Generate driver dump start of file information
 *
 * The start of file information is added to 'textbuf'
 *
 * @param textbuf pointer to driver dump text buffer
 *
 * @return none
 */

void
ocs_ddump_startfile(ocs_textbuf_t *textbuf)
{
	ocs_textbuf_printf(textbuf, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\" ?>\n");
}

/**
 * @brief Generate driver dump end of file information
 *
 * The end of file information is added to 'textbuf'
 *
 * @param textbuf pointer to driver dump text buffer
 *
 * @return none
 */

void
ocs_ddump_endfile(ocs_textbuf_t *textbuf)
{
}

/**
 * @brief Generate driver dump section start data
 *
 * The driver section start information is added to textbuf
 *
 * @param textbuf pointer to text buffer
 * @param name name of section
 * @param instance instance number of this section
 *
 * @return none
 */

void
ocs_ddump_section(ocs_textbuf_t *textbuf, const char *name, uint32_t instance)
{
	ocs_textbuf_printf(textbuf, "<%s type=\"section\" instance=\"%d\">\n", name, instance);
}

/**
 * @brief Generate driver dump section end data
 *
 * The driver section end information is added to textbuf
 *
 * @param textbuf pointer to text buffer
 * @param name name of section
 * @param instance instance number of this section
 *
 * @return none
 */

void
ocs_ddump_endsection(ocs_textbuf_t *textbuf, const char *name, uint32_t instance)
{
	ocs_textbuf_printf(textbuf, "</%s>\n", name);
}

/**
 * @brief Generate driver dump data for a given value
 *
 * A value is added to textbuf
 *
 * @param textbuf pointer to text buffer
 * @param name name of variable
 * @param fmt snprintf format specifier
 *
 * @return none
 */

void
ocs_ddump_value(ocs_textbuf_t *textbuf, const char *name, const char *fmt, ...)
{
	va_list ap;
	char valuebuf[64];

	va_start(ap, fmt);
	vsnprintf(valuebuf, sizeof(valuebuf), fmt, ap);
	va_end(ap);

	ocs_textbuf_printf(textbuf, "<%s>%s</%s>\n", name, valuebuf, name);
}


/**
 * @brief Generate driver dump data for an arbitrary buffer of DWORDS
 *
 * A status value is added to textbuf
 *
 * @param textbuf pointer to text buffer
 * @param name name of status variable
 * @param instance instance number of this section
 * @param buffer buffer to print
 * @param size size of buffer in bytes
 *
 * @return none
 */

void
ocs_ddump_buffer(ocs_textbuf_t *textbuf, const char *name, uint32_t instance, void *buffer, uint32_t size)
{
	uint32_t *dword;
	uint32_t i;
	uint32_t count;

	count = size / sizeof(uint32_t);

	if (count == 0) {
		return;
	}

	ocs_textbuf_printf(textbuf, "<%s type=\"buffer\" instance=\"%d\">\n", name, instance);

	dword = buffer;
	for (i = 0; i < count; i++) {
#define OCS_NEWLINE_MOD	8
		ocs_textbuf_printf(textbuf, "%08x ", *dword++);
		if ((i % OCS_NEWLINE_MOD) == (OCS_NEWLINE_MOD - 1)) {
			ocs_textbuf_printf(textbuf, "\n");
		}
	}

	ocs_textbuf_printf(textbuf, "</%s>\n", name);
}

/**
 * @brief Generate driver dump for queue
 *
 * Add queue elements to text buffer
 *
 * @param textbuf pointer to driver dump text buffer
 * @param q_addr address of start of queue
 * @param size size of each queue entry
 * @param length number of queue entries in the queue
 * @param index current index of queue
 * @param qentries number of most recent queue entries to dump
 *
 * @return none
 */

void
ocs_ddump_queue_entries(ocs_textbuf_t *textbuf, void *q_addr, uint32_t size,
			uint32_t length, int32_t index, uint32_t qentries)
{
	uint32_t i;
	uint32_t j;
	uint8_t *entry;
	uint32_t *dword;
	uint32_t entry_count = 0;
	uint32_t entry_words = size / sizeof(uint32_t);

	if ((qentries == (uint32_t)-1) || (qentries > length)) {
		/* if qentries is -1 or larger than queue size, dump entire queue */
		entry_count = length;
		index = 0;
	} else {
		entry_count = qentries;

		index -= (qentries - 1);
		if (index < 0) {
			index += length;
		}

	}
#define OCS_NEWLINE_MOD	8
	ocs_textbuf_printf(textbuf, "<qentries>\n");
	for (i = 0; i < entry_count; i++){
		entry = q_addr;
		entry += index * size;
		dword = (uint32_t *)entry;

		ocs_textbuf_printf(textbuf, "[%04x] ", index);
		for (j = 0; j < entry_words; j++) {
			ocs_textbuf_printf(textbuf, "%08x ", *dword++);
			if (((j+1) == entry_words) ||
			    ((j % OCS_NEWLINE_MOD) == (OCS_NEWLINE_MOD - 1))) {
				ocs_textbuf_printf(textbuf, "\n");
				if ((j+1) < entry_words) {
					ocs_textbuf_printf(textbuf, "       ");
				}
			}
		}

		index++;
		if ((uint32_t)index >= length) {
			index = 0;
		}
	}
	ocs_textbuf_printf(textbuf, "</qentries>\n");
}


#define OCS_DEBUG_ENABLE(x)	(x ? ~0 : 0)

#define OCS_DEBUG_MASK \
	(OCS_DEBUG_ENABLE(1)	& OCS_DEBUG_ALWAYS)  | \
	(OCS_DEBUG_ENABLE(0)	& OCS_DEBUG_ENABLE_MQ_DUMP) | \
	(OCS_DEBUG_ENABLE(0)	& OCS_DEBUG_ENABLE_CQ_DUMP) | \
	(OCS_DEBUG_ENABLE(0)	& OCS_DEBUG_ENABLE_WQ_DUMP) | \
	(OCS_DEBUG_ENABLE(0)	& OCS_DEBUG_ENABLE_EQ_DUMP) | \
	(OCS_DEBUG_ENABLE(0)	& OCS_DEBUG_ENABLE_SPARAM_DUMP)

static uint32_t ocs_debug_mask = OCS_DEBUG_MASK;

static int
_isprint(int c) {
	return ((c > 32) && (c < 127));
}

/**
 * @ingroup debug
 * @brief enable debug options
 *
 * Enables debug options by or-ing in <b>mask</b> into the currently enabled
 * debug mask.
 *
 * @param mask mask bits to enable
 *
 * @return none
 */

void ocs_debug_enable(uint32_t mask) {
	ocs_debug_mask |= mask;
}

/**
 * @ingroup debug
 * @brief disable debug options
 *
 * Disables debug options by clearing bits in <b>mask</b> into the currently enabled
 * debug mask.
 *
 * @param mask mask bits to enable
 *
 * @return none
 */

void ocs_debug_disable(uint32_t mask) {
	ocs_debug_mask &= ~mask;
}

/**
 * @ingroup debug
 * @brief return true if debug bits are enabled
 *
 * Returns true if the request debug bits are set.
 *
 * @param mask debug bit mask
 *
 * @return true if corresponding bits are set
 *
 * @note Passing in a mask value of zero always returns true
 */

int ocs_debug_is_enabled(uint32_t mask) {
	return (ocs_debug_mask & mask) == mask;
}


/**
 * @ingroup debug
 * @brief Dump 32 bit hex/ascii data
 *
 * Dumps using ocs_log a buffer of data as 32 bit hex and ascii
 *
 * @param mask debug enable bits
 * @param os os handle
 * @param label text label for the display (may be NULL)
 * @param buf pointer to data buffer
 * @param buf_length length of data buffer
 *
 * @return none
 *
 */

void
ocs_dump32(uint32_t mask, ocs_os_handle_t os, const char *label, void *buf, uint32_t buf_length)
{
	uint32_t word_count = buf_length / sizeof(uint32_t);
	uint32_t i;
	uint32_t columns = 8;
	uint32_t n;
	uint32_t *wbuf;
	char *cbuf;
	uint32_t addr = 0;
	char linebuf[200];
	char *pbuf = linebuf;

	if (!ocs_debug_is_enabled(mask))
		return;

	if (label)
		ocs_log_debug(os, "%s\n", label);

	wbuf = buf;
	while (word_count > 0) {
		pbuf = linebuf;
		pbuf += ocs_snprintf(pbuf, sizeof(linebuf) - (pbuf-linebuf), "%08X:  ", addr);

		n = word_count;
		if (n > columns)
			n = columns;

		for (i = 0; i < n; i ++)
			pbuf += ocs_snprintf(pbuf, sizeof(linebuf) - (pbuf-linebuf), "%08X ", wbuf[i]);

		for (; i < columns; i ++)
			pbuf += ocs_snprintf(pbuf, sizeof(linebuf) - (pbuf-linebuf), "%8s ", "");

		pbuf += ocs_snprintf(pbuf, sizeof(linebuf) - (pbuf-linebuf), "    ");
		cbuf = (char*)wbuf;
		for (i = 0; i < n*sizeof(uint32_t); i ++)
			pbuf += ocs_snprintf(pbuf, sizeof(linebuf) - (pbuf-linebuf), "%c", _isprint(cbuf[i]) ? cbuf[i] : '.');
		pbuf += ocs_snprintf(pbuf, sizeof(linebuf) - (pbuf-linebuf), "\n");

		ocs_log_debug(os, "%s", linebuf);

		wbuf += n;
		word_count -= n;
		addr += n*sizeof(uint32_t);
	}
}


#if defined(OCS_DEBUG_QUEUE_HISTORY)

/* each bit corresponds to word to capture */
#define OCS_Q_HIST_WQE_WORD_MASK_DEFAULT	(BIT(4) | BIT(6) | BIT(7) | BIT(9) | BIT(12))
#define OCS_Q_HIST_TRECV_CONT_WQE_WORD_MASK	(BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(9) | BIT(12))
#define OCS_Q_HIST_IWRITE_WQE_WORD_MASK		(BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(9))
#define OCS_Q_HIST_IREAD_WQE_WORD_MASK		(BIT(4) | BIT(6) | BIT(7) | BIT(9))
#define OCS_Q_HIST_ABORT_WQE_WORD_MASK		(BIT(3) | BIT(7) | BIT(8) | BIT(9))
#define OCS_Q_HIST_WCQE_WORD_MASK		(BIT(0) | BIT(3))
#define OCS_Q_HIST_WCQE_WORD_MASK_ERR		(BIT(0) | BIT(1) | BIT(2) | BIT(3))
#define OCS_Q_HIST_CQXABT_WORD_MASK		(BIT(0) | BIT(1) | BIT(2) | BIT(3))

/* if set, will provide extra queue information in each entry */
#define OCS_Q_HIST_ENABLE_Q_INFO	0
uint8_t ocs_queue_history_q_info_enabled(void)
{
	return OCS_Q_HIST_ENABLE_Q_INFO;
}

/* if set, will provide timestamps in each entry */
#define OCS_Q_HIST_ENABLE_TIMESTAMPS	0
uint8_t ocs_queue_history_timestamp_enabled(void)
{
	return OCS_Q_HIST_ENABLE_TIMESTAMPS;
}

/* Add WQEs and masks to override default WQE mask */
ocs_q_hist_wqe_mask_t ocs_q_hist_wqe_masks[] = {
	/* WQE command   Word mask */
	{SLI4_WQE_ABORT, OCS_Q_HIST_ABORT_WQE_WORD_MASK},
	{SLI4_WQE_FCP_IREAD64, OCS_Q_HIST_IREAD_WQE_WORD_MASK},
	{SLI4_WQE_FCP_IWRITE64, OCS_Q_HIST_IWRITE_WQE_WORD_MASK},
	{SLI4_WQE_FCP_CONT_TRECEIVE64, OCS_Q_HIST_TRECV_CONT_WQE_WORD_MASK},
};

/* CQE masks */
ocs_q_hist_cqe_mask_t ocs_q_hist_cqe_masks[] = {
	/* CQE type     Q_hist_type		mask (success) 	mask (non-success) */
	{SLI_QENTRY_WQ, OCS_Q_HIST_TYPE_CWQE, 	OCS_Q_HIST_WCQE_WORD_MASK, OCS_Q_HIST_WCQE_WORD_MASK_ERR},
	{SLI_QENTRY_XABT, OCS_Q_HIST_TYPE_CXABT, OCS_Q_HIST_CQXABT_WORD_MASK, OCS_Q_HIST_WCQE_WORD_MASK},
};

static uint32_t ocs_q_hist_get_wqe_mask(sli4_generic_wqe_t *wqe)
{
	uint32_t i;
	for (i = 0; i < ARRAY_SIZE(ocs_q_hist_wqe_masks); i++) {
		if (ocs_q_hist_wqe_masks[i].command == wqe->command) {
			return ocs_q_hist_wqe_masks[i].mask;
		}
	}
	/* return default WQE mask */
	return OCS_Q_HIST_WQE_WORD_MASK_DEFAULT;
}

/**
 * @ingroup debug
 * @brief Initialize resources for queue history
 *
 * @param os os handle
 * @param q_hist Pointer to the queue history object.
 *
 * @return none
 */
void
ocs_queue_history_init(ocs_t *ocs, ocs_hw_q_hist_t *q_hist)
{
	q_hist->ocs = ocs;
	if (q_hist->q_hist != NULL) {
		/* Setup is already done */
		ocs_log_debug(ocs, "q_hist not NULL, skipping init\n");
		return;
	}

	q_hist->q_hist = ocs_malloc(ocs, sizeof(*q_hist->q_hist)*OCS_Q_HIST_SIZE, OCS_M_ZERO | OCS_M_NOWAIT);

	if (q_hist->q_hist == NULL) {
		ocs_log_err(ocs, "Could not allocate queue history buffer\n");
	} else {
		ocs_lock_init(ocs, &q_hist->q_hist_lock, "queue history lock[%d]", ocs_instance(ocs));
	}

	q_hist->q_hist_index = 0;
}

/**
 * @ingroup debug
 * @brief Free resources for queue history
 *
 * @param q_hist Pointer to the queue history object.
 *
 * @return none
 */
void
ocs_queue_history_free(ocs_hw_q_hist_t *q_hist)
{
	ocs_t *ocs = q_hist->ocs;

	if (q_hist->q_hist != NULL) {
		ocs_free(ocs, q_hist->q_hist, sizeof(*q_hist->q_hist)*OCS_Q_HIST_SIZE);
		ocs_lock_free(&q_hist->q_hist_lock);
		q_hist->q_hist = NULL;
	}
}

static void
ocs_queue_history_add_q_info(ocs_hw_q_hist_t *q_hist, uint32_t qid, uint32_t qindex)
{
	if (ocs_queue_history_q_info_enabled()) {
		/* write qid, index */
		q_hist->q_hist[q_hist->q_hist_index] = (qid << 16) | qindex;
		q_hist->q_hist_index++;
		q_hist->q_hist_index = q_hist->q_hist_index % OCS_Q_HIST_SIZE;
	}
}

static void
ocs_queue_history_add_timestamp(ocs_hw_q_hist_t *q_hist)
{
	if (ocs_queue_history_timestamp_enabled()) {
		/* write tsc */
		uint64_t tsc_value;
		tsc_value = get_cyclecount();
		q_hist->q_hist[q_hist->q_hist_index] = ((tsc_value >> 32 ) & 0xFFFFFFFF);
		q_hist->q_hist_index++;
		q_hist->q_hist_index = q_hist->q_hist_index % OCS_Q_HIST_SIZE;
		q_hist->q_hist[q_hist->q_hist_index] = (tsc_value & 0xFFFFFFFF);
		q_hist->q_hist_index++;
		q_hist->q_hist_index = q_hist->q_hist_index % OCS_Q_HIST_SIZE;
	}
}

/**
 * @ingroup debug
 * @brief Log work queue entry (WQE) into history array
 *
 * @param q_hist Pointer to the queue history object.
 * @param entryw Work queue entry in words
 * @param qid Queue ID
 * @param qindex Queue index
 *
 * @return none
 */
void
ocs_queue_history_wq(ocs_hw_q_hist_t *q_hist, uint32_t *entryw, uint32_t qid, uint32_t qindex)
{
	int i;
	ocs_q_hist_ftr_t ftr;
	uint32_t wqe_word_mask = ocs_q_hist_get_wqe_mask((sli4_generic_wqe_t *)entryw);

	if (q_hist->q_hist == NULL) {
		/* Can't save anything */
		return;
	}

	ftr.word = 0;
	ftr.s.type = OCS_Q_HIST_TYPE_WQE;
	ocs_lock(&q_hist->q_hist_lock);
		/* Capture words in reverse order since we'll be interpretting them LIFO */
		for (i = ((sizeof(wqe_word_mask)*8) - 1); i >= 0; i--){
			if ((wqe_word_mask >> i) & 1) {
				q_hist->q_hist[q_hist->q_hist_index] = entryw[i];
				q_hist->q_hist_index++;
				q_hist->q_hist_index = q_hist->q_hist_index % OCS_Q_HIST_SIZE;
			}
		}

		ocs_queue_history_add_q_info(q_hist, qid, qindex);
		ocs_queue_history_add_timestamp(q_hist);

		/* write footer */
		if (wqe_word_mask) {
			ftr.s.mask = wqe_word_mask;
			q_hist->q_hist[q_hist->q_hist_index] = ftr.word;
			q_hist->q_hist_index++;
			q_hist->q_hist_index = q_hist->q_hist_index % OCS_Q_HIST_SIZE;
		}

	ocs_unlock(&q_hist->q_hist_lock);
}

/**
 * @ingroup debug
 * @brief Log misc words
 *
 * @param q_hist Pointer to the queue history object.
 * @param entryw array of words
 * @param num_words number of words in entryw
 *
 * @return none
 */
void
ocs_queue_history_misc(ocs_hw_q_hist_t *q_hist, uint32_t *entryw, uint32_t num_words)
{
	int i;
	ocs_q_hist_ftr_t ftr;
	uint32_t mask = 0;

	if (q_hist->q_hist == NULL) {
		/* Can't save anything */
		return;
	}

	ftr.word = 0;
	ftr.s.type = OCS_Q_HIST_TYPE_MISC;
	ocs_lock(&q_hist->q_hist_lock);
		/* Capture words in reverse order since we'll be interpretting them LIFO */
		for (i = num_words-1; i >= 0; i--) {
			q_hist->q_hist[q_hist->q_hist_index] = entryw[i];
			q_hist->q_hist_index++;
			q_hist->q_hist_index = q_hist->q_hist_index % OCS_Q_HIST_SIZE;
			mask |= BIT(i);
		}

		ocs_queue_history_add_timestamp(q_hist);

		/* write footer */
		if (num_words) {
			ftr.s.mask = mask;
			q_hist->q_hist[q_hist->q_hist_index] = ftr.word;
			q_hist->q_hist_index++;
			q_hist->q_hist_index = q_hist->q_hist_index % OCS_Q_HIST_SIZE;
		}

	ocs_unlock(&q_hist->q_hist_lock);
}

/**
 * @ingroup debug
 * @brief Log work queue completion (CQE) entry into history
 *        array
 *
 * @param q_hist Pointer to the queue history object.
 * @param ctype Type of completion entry
 * @param entryw Completion queue entry in words
 * @param status Completion queue status
 * @param qid Queue ID
 * @param qindex Queue index
 *
 * @return none
 */
void
ocs_queue_history_cqe(ocs_hw_q_hist_t *q_hist, uint8_t ctype, uint32_t *entryw, uint8_t status, uint32_t qid, uint32_t qindex)
{
	int i;
	unsigned j;
	uint32_t cqe_word_mask = 0;
	ocs_q_hist_ftr_t ftr;

	if (q_hist->q_hist == NULL) {
		/* Can't save anything */
		return;
	}

	ftr.word = 0;
	for (j = 0; j < ARRAY_SIZE(ocs_q_hist_cqe_masks); j++) {
		if (ocs_q_hist_cqe_masks[j].ctype == ctype) {
			ftr.s.type = ocs_q_hist_cqe_masks[j].type;
			if (status != 0) {
				cqe_word_mask = ocs_q_hist_cqe_masks[j].mask_err;
			} else {
				cqe_word_mask = ocs_q_hist_cqe_masks[j].mask;
			}
		}
	}
	ocs_lock(&q_hist->q_hist_lock);
		/* Capture words in reverse order since we'll be interpretting them LIFO */
		for (i = ((sizeof(cqe_word_mask)*8) - 1); i >= 0; i--){
			if ((cqe_word_mask >> i) & 1) {
				q_hist->q_hist[q_hist->q_hist_index] = entryw[i];
				q_hist->q_hist_index++;
				q_hist->q_hist_index = q_hist->q_hist_index % OCS_Q_HIST_SIZE;
			}
		}
		ocs_queue_history_add_q_info(q_hist, qid, qindex);
		ocs_queue_history_add_timestamp(q_hist);

		/* write footer */
		if (cqe_word_mask) {
			ftr.s.mask = cqe_word_mask;
			q_hist->q_hist[q_hist->q_hist_index] = ftr.word;
			q_hist->q_hist_index++;
			q_hist->q_hist_index = q_hist->q_hist_index % OCS_Q_HIST_SIZE;
		}

	ocs_unlock(&q_hist->q_hist_lock);
}

/**
 * @brief Get previous index
 *
 * @param index Index from which previous index is derived.
 */
uint32_t
ocs_queue_history_prev_index(uint32_t index)
{
	if (index == 0) {
		return OCS_Q_HIST_SIZE - 1;
	} else {
		return index - 1;
	}
}

#endif /* OCS_DEBUG_QUEUE_HISTORY */

/**
 * @brief Display service parameters
 *
 * <description>
 *
 * @param prelabel leading display label
 * @param reqlabel display label
 * @param dest destination 0=ocs_log, 1=textbuf
 * @param textbuf text buffer destination (if dest==1)
 * @param sparams pointer to service parameter
 *
 * @return none
 */

void
ocs_display_sparams(const char *prelabel, const char *reqlabel, int dest, void *textbuf, void *sparams)
{
	char label[64];

	if (sparams == NULL) {
		return;
	}

	switch(dest) {
	case 0:
		if (prelabel != NULL) {
			ocs_snprintf(label, sizeof(label), "[%s] sparam: %s", prelabel, reqlabel);
		} else {
			ocs_snprintf(label, sizeof(label), "sparam: %s", reqlabel);
		}

		ocs_dump32(OCS_DEBUG_ENABLE_SPARAM_DUMP, NULL, label, sparams, sizeof(fc_plogi_payload_t));
		break;
	case 1:
		ocs_ddump_buffer((ocs_textbuf_t*) textbuf, reqlabel, 0, sparams, sizeof(fc_plogi_payload_t));
		break;
	}
}

/**
 * @brief Calculate the T10 PI CRC guard value for a block.
 *
 * @param buffer Pointer to the data buffer.
 * @param size Number of bytes.
 * @param crc Previously-calculated CRC, or 0 for a new block.
 *
 * @return Returns the calculated CRC, which may be passed back in for partial blocks.
 *
 */

uint16_t
ocs_scsi_dif_calc_crc(const uint8_t *buffer, uint32_t size, uint16_t crc)
{
	return t10crc16(buffer, size, crc);
}

/**
 * @brief Calculate the IP-checksum guard value for a block.
 *
 * @param addrlen array of address length pairs
 * @param addrlen_count number of entries in the addrlen[] array
 *
 * Algorithm:
 *    Sum all all the 16-byte words in the block
 *    Add in the "carry", which is everything in excess of 16-bits
 *    Flip all the bits
 *
 * @return Returns the calculated checksum
 */

uint16_t
ocs_scsi_dif_calc_checksum(ocs_scsi_vaddr_len_t addrlen[], uint32_t addrlen_count)
{
	uint32_t i, j;
	uint16_t checksum;
	uint32_t intermediate; /* Use an intermediate to hold more than 16 bits during calculations */
	uint32_t count;
	uint16_t *buffer;

	intermediate = 0;
	for (j = 0; j < addrlen_count; j++) {
		buffer = addrlen[j].vaddr;
		count = addrlen[j].length / 2;
		for (i=0; i < count; i++) {
			intermediate += buffer[i];
		}
	}

	/* Carry is everything over 16 bits */
	intermediate += ((intermediate & 0xffff0000) >> 16);

	/* Flip all the bits */
	intermediate = ~intermediate;

	checksum = intermediate;

	return checksum;
}

/**
 * @brief Return blocksize given SCSI API DIF block size
 *
 * Given the DIF block size enumerated value, return the block size value. (e.g.
 * OCS_SCSI_DIF_BLK_SIZE_512 returns 512)
 *
 * @param dif_info Pointer to SCSI API DIF info block
 *
 * @return returns block size, or 0 if SCSI API DIF blocksize is invalid
 */

uint32_t
ocs_scsi_dif_blocksize(ocs_scsi_dif_info_t *dif_info)
{
	uint32_t blocksize = 0;

	switch(dif_info->blk_size) {
	case OCS_SCSI_DIF_BK_SIZE_512:	blocksize = 512; break;
	case OCS_SCSI_DIF_BK_SIZE_1024:	blocksize = 1024; break;
	case OCS_SCSI_DIF_BK_SIZE_2048:	blocksize = 2048; break;
	case OCS_SCSI_DIF_BK_SIZE_4096:	blocksize = 4096; break;
	case OCS_SCSI_DIF_BK_SIZE_520:	blocksize = 520; break;
	case OCS_SCSI_DIF_BK_SIZE_4104:	blocksize = 4104; break;
	default:
		break;
	}

	return blocksize;
}

/**
 * @brief Set SCSI API DIF blocksize
 *
 * Given a blocksize value (512, 1024, etc.), set the SCSI API DIF blocksize
 * in the DIF info block
 *
 * @param dif_info Pointer to the SCSI API DIF info block
 * @param blocksize Block size
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

int32_t
ocs_scsi_dif_set_blocksize(ocs_scsi_dif_info_t *dif_info, uint32_t blocksize)
{
	int32_t rc = 0;

	switch(blocksize) {
	case 512:	dif_info->blk_size = OCS_SCSI_DIF_BK_SIZE_512; break;
	case 1024:	dif_info->blk_size = OCS_SCSI_DIF_BK_SIZE_1024; break;
	case 2048:	dif_info->blk_size = OCS_SCSI_DIF_BK_SIZE_2048; break;
	case 4096:	dif_info->blk_size = OCS_SCSI_DIF_BK_SIZE_4096; break;
	case 520:	dif_info->blk_size = OCS_SCSI_DIF_BK_SIZE_520; break;
	case 4104:	dif_info->blk_size = OCS_SCSI_DIF_BK_SIZE_4104; break;
	default:
		rc = -1;
		break;
	}
	return rc;

}

/**
 * @brief Return memory block size given SCSI DIF API
 *
 * The blocksize in memory for the DIF transfer is returned, given the SCSI DIF info
 * block and the direction of transfer.
 *
 * @param dif_info Pointer to DIF info block
 * @param wiretomem Transfer direction, 1 is wire to memory, 0 is memory to wire
 *
 * @return Memory blocksize, or negative error value
 *
 * WARNING: the order of initialization of the adj[] arrays MUST match the declarations
 * of OCS_SCSI_DIF_OPER_*
 */

int32_t
ocs_scsi_dif_mem_blocksize(ocs_scsi_dif_info_t *dif_info, int wiretomem)
{
	uint32_t blocksize;
	uint8_t wiretomem_adj[] = {
		0,		/* OCS_SCSI_DIF_OPER_DISABLED, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CRC, */
		0,		/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_NODIF, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CHKSUM, */
		0,		/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_NODIF, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_CRC, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CRC, */
		DIF_SIZE};	/* OCS_SCSI_DIF_OPER_IN_RAW_OUT_RAW, */
	uint8_t memtowire_adj[] = {
		0,		/* OCS_SCSI_DIF_OPER_DISABLED, */
		0,		/* OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CRC, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_NODIF, */
		0,		/* OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_NODIF, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_CRC, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CRC, */
		DIF_SIZE};	/* OCS_SCSI_DIF_OPER_IN_RAW_OUT_RAW, */

	blocksize = ocs_scsi_dif_blocksize(dif_info);
	if (blocksize == 0) {
		return -1;
	}

	if (wiretomem) {
		ocs_assert(dif_info->dif_oper < ARRAY_SIZE(wiretomem_adj), 0);
		blocksize += wiretomem_adj[dif_info->dif_oper];
	} else {	/* mem to wire */
		ocs_assert(dif_info->dif_oper < ARRAY_SIZE(memtowire_adj), 0);
		blocksize += memtowire_adj[dif_info->dif_oper];
	}
	return blocksize;
}

/**
 * @brief Return wire block size given SCSI DIF API
 *
 * The blocksize on the wire for the DIF transfer is returned, given the SCSI DIF info
 * block and the direction of transfer.
 *
 * @param dif_info Pointer to DIF info block
 * @param wiretomem Transfer direction, 1 is wire to memory, 0 is memory to wire
 *
 * @return Wire blocksize or negative error value
 *
 * WARNING: the order of initialization of the adj[] arrays MUST match the declarations
 * of OCS_SCSI_DIF_OPER_*
 */

int32_t
ocs_scsi_dif_wire_blocksize(ocs_scsi_dif_info_t *dif_info, int wiretomem)
{
	uint32_t blocksize;
	uint8_t wiretomem_adj[] = {
		0,		/* OCS_SCSI_DIF_OPER_DISABLED, */
		0,		/* OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CRC, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_NODIF, */
		0,		/* OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_NODIF, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_CRC, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CRC, */
		DIF_SIZE};	/* OCS_SCSI_DIF_OPER_IN_RAW_OUT_RAW, */
	uint8_t memtowire_adj[] = {
		0,		/* OCS_SCSI_DIF_OPER_DISABLED, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CRC, */
		0,		/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_NODIF, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_NODIF_OUT_CHKSUM, */
		0,		/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_NODIF, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_CRC, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CRC_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_SCSI_DIF_OPER_IN_CHKSUM_OUT_CRC, */
		DIF_SIZE};	/* OCS_SCSI_DIF_OPER_IN_RAW_OUT_RAW, */


	blocksize = ocs_scsi_dif_blocksize(dif_info);
	if (blocksize == 0) {
		return -1;
	}

	if (wiretomem) {
		ocs_assert(dif_info->dif_oper < ARRAY_SIZE(wiretomem_adj), 0);
		blocksize += wiretomem_adj[dif_info->dif_oper];
	} else {	/* mem to wire */
		ocs_assert(dif_info->dif_oper < ARRAY_SIZE(memtowire_adj), 0);
		blocksize += memtowire_adj[dif_info->dif_oper];
	}

	return blocksize;
}
/**
 * @brief Return blocksize given HW API DIF block size
 *
 * Given the DIF block size enumerated value, return the block size value. (e.g.
 * OCS_SCSI_DIF_BLK_SIZE_512 returns 512)
 *
 * @param dif_info Pointer to HW API DIF info block
 *
 * @return returns block size, or 0 if HW API DIF blocksize is invalid
 */

uint32_t
ocs_hw_dif_blocksize(ocs_hw_dif_info_t *dif_info)
{
	uint32_t blocksize = 0;

	switch(dif_info->blk_size) {
	case OCS_HW_DIF_BK_SIZE_512:	blocksize = 512; break;
	case OCS_HW_DIF_BK_SIZE_1024:	blocksize = 1024; break;
	case OCS_HW_DIF_BK_SIZE_2048:	blocksize = 2048; break;
	case OCS_HW_DIF_BK_SIZE_4096:	blocksize = 4096; break;
	case OCS_HW_DIF_BK_SIZE_520:	blocksize = 520; break;
	case OCS_HW_DIF_BK_SIZE_4104:	blocksize = 4104; break;
	default:
		break;
	}

	return blocksize;
}

/**
 * @brief Return memory block size given HW DIF API
 *
 * The blocksize in memory for the DIF transfer is returned, given the HW DIF info
 * block and the direction of transfer.
 *
 * @param dif_info Pointer to DIF info block
 * @param wiretomem Transfer direction, 1 is wire to memory, 0 is memory to wire
 *
 * @return Memory blocksize, or negative error value
 *
 * WARNING: the order of initialization of the adj[] arrays MUST match the declarations
 * of OCS_HW_DIF_OPER_*
 */

int32_t
ocs_hw_dif_mem_blocksize(ocs_hw_dif_info_t *dif_info, int wiretomem)
{
	uint32_t blocksize;
	uint8_t wiretomem_adj[] = {
		0,		/* OCS_HW_DIF_OPER_DISABLED, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_NODIF_OUT_CRC, */
		0,		/* OCS_HW_DIF_OPER_IN_CRC_OUT_NODIF, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_NODIF_OUT_CHKSUM, */
		0,		/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_NODIF, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_CRC, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_CRC, */
		DIF_SIZE};	/* OCS_HW_DIF_OPER_IN_RAW_OUT_RAW, */
	uint8_t memtowire_adj[] = {
		0,		/* OCS_HW_DIF_OPER_DISABLED, */
		0,		/* OCS_HW_DIF_OPER_IN_NODIF_OUT_CRC, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_NODIF, */
		0,		/* OCS_HW_DIF_OPER_IN_NODIF_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_NODIF, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_CRC, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_CRC, */
		DIF_SIZE};	/* OCS_HW_DIF_OPER_IN_RAW_OUT_RAW, */

	blocksize = ocs_hw_dif_blocksize(dif_info);
	if (blocksize == 0) {
		return -1;
	}

	if (wiretomem) {
		ocs_assert(dif_info->dif_oper < ARRAY_SIZE(wiretomem_adj), 0);
		blocksize += wiretomem_adj[dif_info->dif_oper];
	} else {	/* mem to wire */
		ocs_assert(dif_info->dif_oper < ARRAY_SIZE(memtowire_adj), 0);
		blocksize += memtowire_adj[dif_info->dif_oper];
	}
	return blocksize;
}

/**
 * @brief Return wire block size given HW DIF API
 *
 * The blocksize on the wire for the DIF transfer is returned, given the HW DIF info
 * block and the direction of transfer.
 *
 * @param dif_info Pointer to DIF info block
 * @param wiretomem Transfer direction, 1 is wire to memory, 0 is memory to wire
 *
 * @return Wire blocksize or negative error value
 *
 * WARNING: the order of initialization of the adj[] arrays MUST match the declarations
 * of OCS_HW_DIF_OPER_*
 */

int32_t
ocs_hw_dif_wire_blocksize(ocs_hw_dif_info_t *dif_info, int wiretomem)
{
	uint32_t blocksize;
	uint8_t wiretomem_adj[] = {
		0,		/* OCS_HW_DIF_OPER_DISABLED, */
		0,		/* OCS_HW_DIF_OPER_IN_NODIF_OUT_CRC, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_NODIF, */
		0,		/* OCS_HW_DIF_OPER_IN_NODIF_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_NODIF, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_CRC, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_CRC, */
		DIF_SIZE};	/* OCS_HW_DIF_OPER_IN_RAW_OUT_RAW, */
	uint8_t memtowire_adj[] = {
		0,		/* OCS_HW_DIF_OPER_DISABLED, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_NODIF_OUT_CRC, */
		0,		/* OCS_HW_DIF_OPER_IN_CRC_OUT_NODIF, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_NODIF_OUT_CHKSUM, */
		0,		/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_NODIF, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_CRC, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CRC_OUT_CHKSUM, */
		DIF_SIZE,	/* OCS_HW_DIF_OPER_IN_CHKSUM_OUT_CRC, */
		DIF_SIZE};	/* OCS_HW_DIF_OPER_IN_RAW_OUT_RAW, */


	blocksize = ocs_hw_dif_blocksize(dif_info);
	if (blocksize == 0) {
		return -1;
	}

	if (wiretomem) {
		ocs_assert(dif_info->dif_oper < ARRAY_SIZE(wiretomem_adj), 0);
		blocksize += wiretomem_adj[dif_info->dif_oper];
	} else {	/* mem to wire */
		ocs_assert(dif_info->dif_oper < ARRAY_SIZE(memtowire_adj), 0);
		blocksize += memtowire_adj[dif_info->dif_oper];
	}

	return blocksize;
}

static int32_t ocs_segment_remaining(ocs_textbuf_segment_t *segment);
static ocs_textbuf_segment_t *ocs_textbuf_segment_alloc(ocs_textbuf_t *textbuf);
static void ocs_textbuf_segment_free(ocs_t *ocs, ocs_textbuf_segment_t *segment);
static ocs_textbuf_segment_t *ocs_textbuf_get_segment(ocs_textbuf_t *textbuf, uint32_t idx);

uint8_t *
ocs_textbuf_get_buffer(ocs_textbuf_t *textbuf)
{
	return ocs_textbuf_ext_get_buffer(textbuf, 0);
}

int32_t
ocs_textbuf_get_length(ocs_textbuf_t *textbuf)
{
	return ocs_textbuf_ext_get_length(textbuf, 0);
}

int32_t
ocs_textbuf_get_written(ocs_textbuf_t *textbuf)
{
	uint32_t idx;
	int32_t n;
	int32_t total = 0;

	for (idx = 0; (n = ocs_textbuf_ext_get_written(textbuf, idx)) >= 0; idx++) {
		total += n;
	}
	return total;
}

uint8_t *ocs_textbuf_ext_get_buffer(ocs_textbuf_t *textbuf, uint32_t idx)
{
	ocs_textbuf_segment_t *segment = ocs_textbuf_get_segment(textbuf, idx);
	if (segment == NULL) {
		return NULL;
	}
	return segment->buffer;
}

int32_t ocs_textbuf_ext_get_length(ocs_textbuf_t *textbuf, uint32_t idx)
{
	ocs_textbuf_segment_t *segment = ocs_textbuf_get_segment(textbuf, idx);
	if (segment == NULL) {
		return -1;
	}
	return segment->buffer_length;
}

int32_t ocs_textbuf_ext_get_written(ocs_textbuf_t *textbuf, uint32_t idx)
{
	ocs_textbuf_segment_t *segment = ocs_textbuf_get_segment(textbuf, idx);
	if (segment == NULL) {
		return -1;
	}
	return segment->buffer_written;
}

uint32_t
ocs_textbuf_initialized(ocs_textbuf_t *textbuf)
{
	return (textbuf->ocs != NULL);
}

int32_t
ocs_textbuf_alloc(ocs_t *ocs, ocs_textbuf_t *textbuf, uint32_t length)
{
	ocs_memset(textbuf, 0, sizeof(*textbuf));

	textbuf->ocs = ocs;
	ocs_list_init(&textbuf->segment_list, ocs_textbuf_segment_t, link);

	if (length > OCS_TEXTBUF_MAX_ALLOC_LEN) {
		textbuf->allocation_length = OCS_TEXTBUF_MAX_ALLOC_LEN;
	} else {
		textbuf->allocation_length = length;
	}

	/* mark as extendable */
	textbuf->extendable = TRUE;

	/* save maximum allocation length */
	textbuf->max_allocation_length = length;

	/* Add first segment */
	return (ocs_textbuf_segment_alloc(textbuf) == NULL) ? -1 : 0;
}

static ocs_textbuf_segment_t *
ocs_textbuf_segment_alloc(ocs_textbuf_t *textbuf)
{
	ocs_textbuf_segment_t *segment = NULL;

	if (textbuf->extendable) {
		segment = ocs_malloc(textbuf->ocs, sizeof(*segment), OCS_M_ZERO | OCS_M_NOWAIT);
		if (segment != NULL) {
			segment->buffer = ocs_malloc(textbuf->ocs, textbuf->allocation_length, OCS_M_ZERO | OCS_M_NOWAIT);
			if (segment->buffer != NULL) {
				segment->buffer_length = textbuf->allocation_length;
				segment->buffer_written = 0;
				ocs_list_add_tail(&textbuf->segment_list, segment);
				textbuf->total_allocation_length += textbuf->allocation_length;

				/* If we've allocated our limit, then mark as not extendable */
				if (textbuf->total_allocation_length >= textbuf->max_allocation_length) {
					textbuf->extendable = 0;
				}

			} else {
				ocs_textbuf_segment_free(textbuf->ocs, segment);
				segment = NULL;
			}
		}
	}
	return segment;
}

static void
ocs_textbuf_segment_free(ocs_t *ocs, ocs_textbuf_segment_t *segment)
{
	if (segment) {
		if (segment->buffer && !segment->user_allocated) {
			ocs_free(ocs, segment->buffer, segment->buffer_length);
		}
		ocs_free(ocs, segment, sizeof(*segment));
	}
}

static ocs_textbuf_segment_t *
ocs_textbuf_get_segment(ocs_textbuf_t *textbuf, uint32_t idx)
{
	uint32_t i;
	ocs_textbuf_segment_t *segment;

	if (ocs_textbuf_initialized(textbuf)) {
		i = 0;
		ocs_list_foreach(&textbuf->segment_list, segment) {
			if (i == idx) {
				return segment;
			}
			i++;
		}
	}
	return NULL;
}

int32_t
ocs_textbuf_init(ocs_t *ocs, ocs_textbuf_t *textbuf, void *buffer, uint32_t length)
{
	int32_t rc = -1;
	ocs_textbuf_segment_t *segment;

	ocs_memset(textbuf, 0, sizeof(*textbuf));

	textbuf->ocs = ocs;
	ocs_list_init(&textbuf->segment_list, ocs_textbuf_segment_t, link);
	segment = ocs_malloc(ocs, sizeof(*segment), OCS_M_ZERO | OCS_M_NOWAIT);
	if (segment) {
		segment->buffer = buffer;
		segment->buffer_length = length;
		segment->buffer_written = 0;
		segment->user_allocated = 1;
		ocs_list_add_tail(&textbuf->segment_list, segment);
		rc = 0;
	}

	return rc;
}

void
ocs_textbuf_free(ocs_t *ocs, ocs_textbuf_t *textbuf)
{
	ocs_textbuf_segment_t *segment;
	ocs_textbuf_segment_t *n;

	if (ocs_textbuf_initialized(textbuf)) {
		ocs_list_foreach_safe(&textbuf->segment_list, segment, n) {
			ocs_list_remove(&textbuf->segment_list, segment);
			ocs_textbuf_segment_free(ocs, segment);
		}

		ocs_memset(textbuf, 0, sizeof(*textbuf));
	}
}

void
ocs_textbuf_printf(ocs_textbuf_t *textbuf, const char *fmt, ...)
{
	va_list ap;

	if (ocs_textbuf_initialized(textbuf)) {
		va_start(ap, fmt);
		ocs_textbuf_vprintf(textbuf, fmt, ap);
		va_end(ap);
	}
}

void
ocs_textbuf_vprintf(ocs_textbuf_t *textbuf, const char *fmt, va_list ap)
{
	int avail;
	int written;
	ocs_textbuf_segment_t *segment;
	va_list save_ap;

	if (!ocs_textbuf_initialized(textbuf)) {
		return;
	}

	va_copy(save_ap, ap);

	/* fetch last segment */
	segment = ocs_list_get_tail(&textbuf->segment_list);

	avail = ocs_segment_remaining(segment);
	if (avail == 0) {
		if ((segment = ocs_textbuf_segment_alloc(textbuf)) == NULL) {
			goto out;
		}
		avail = ocs_segment_remaining(segment);
	}

	written = ocs_vsnprintf(segment->buffer + segment->buffer_written, avail, fmt, ap);

	/* See if data was truncated */
	if (written >= avail) {

		written = avail;

		if (textbuf->extendable) {

			/* revert the partially written data */
			*(segment->buffer + segment->buffer_written) = 0;

			/* Allocate a new segment */
			if ((segment = ocs_textbuf_segment_alloc(textbuf)) == NULL) {
				ocs_log_err(textbuf->ocs, "alloc segment failed\n");
				goto out;
			}
			avail = ocs_segment_remaining(segment);

			/* Retry the write */
			written = ocs_vsnprintf(segment->buffer + segment->buffer_written, avail, fmt, save_ap);
		}
	}
	segment->buffer_written += written;

out:
	va_end(save_ap);
}

void
ocs_textbuf_putc(ocs_textbuf_t *textbuf, uint8_t c)
{
	ocs_textbuf_segment_t *segment;

	if (ocs_textbuf_initialized(textbuf)) {
		segment = ocs_list_get_tail(&textbuf->segment_list);

		if (ocs_segment_remaining(segment)) {
			*(segment->buffer + segment->buffer_written++) = c;
		}
		if (ocs_segment_remaining(segment) == 0) {
			ocs_textbuf_segment_alloc(textbuf);
		}
	}
}

void
ocs_textbuf_puts(ocs_textbuf_t *textbuf, char *s)
{
	if (ocs_textbuf_initialized(textbuf)) {
		while(*s) {
			ocs_textbuf_putc(textbuf, *s++);
		}
	}
}

void
ocs_textbuf_buffer(ocs_textbuf_t *textbuf, uint8_t *buffer, uint32_t buffer_length)
{
	char *s;

	if (!ocs_textbuf_initialized(textbuf)) {
		return;
	}

	s = (char*) buffer;
	while(*s) {

		/*
		 * XML escapes
		 *
		 * "   &quot;
		 * '   &apos;
		 * <   &lt;
		 * >   &gt;
		 * &   &amp;
		 */

		switch(*s) {
		case '"':	ocs_textbuf_puts(textbuf, "&quot;"); break;
		case '\'':	ocs_textbuf_puts(textbuf, "&apos;"); break;
		case '<':	ocs_textbuf_puts(textbuf, "&lt;"); break;
		case '>':	ocs_textbuf_puts(textbuf, "&gt;"); break;
		case '&':	ocs_textbuf_puts(textbuf, "&amp;"); break;
		default:	ocs_textbuf_putc(textbuf, *s); break;
		}
		s++;
	}

}

void
ocs_textbuf_copy(ocs_textbuf_t *textbuf, uint8_t *buffer, uint32_t buffer_length)
{
	char *s;

	if (!ocs_textbuf_initialized(textbuf)) {
		return;
	}

	s = (char*) buffer;
	while(*s) {
		ocs_textbuf_putc(textbuf, *s++);
	}

}

int32_t
ocs_textbuf_remaining(ocs_textbuf_t *textbuf)
{
	if (ocs_textbuf_initialized(textbuf)) {
		return ocs_segment_remaining(ocs_list_get_head(&textbuf->segment_list));
	} else {
		return 0;
	}
}

static int32_t
ocs_segment_remaining(ocs_textbuf_segment_t *segment)
{
	return segment->buffer_length - segment->buffer_written;
}

void
ocs_textbuf_reset(ocs_textbuf_t *textbuf)
{
	uint32_t i = 0;
	ocs_textbuf_segment_t *segment;
	ocs_textbuf_segment_t *n;

	if (ocs_textbuf_initialized(textbuf)) {
		/* zero written on the first segment, free the rest */
		ocs_list_foreach_safe(&textbuf->segment_list, segment, n) {
			if (i++ == 0) {
				segment->buffer_written = 0;
			} else {
				ocs_list_remove(&textbuf->segment_list, segment);
				ocs_textbuf_segment_free(textbuf->ocs, segment);
			}
		}
	}
}

/**
 * @brief Sparse Vector API.
 *
 * This is a trimmed down sparse vector implementation tuned to the problem of
 * 24-bit FC_IDs. In this case, the 24-bit index value is broken down in three
 * 8-bit values. These values are used to index up to three 256 element arrays.
 * Arrays are allocated, only when needed. @n @n
 * The lookup can complete in constant time (3 indexed array references). @n @n
 * A typical use case would be that the fabric/directory FC_IDs would cause two rows to be
 * allocated, and the fabric assigned remote nodes would cause two rows to be allocated, with
 * the root row always allocated. This gives five rows of 256 x sizeof(void*),
 * resulting in 10k.
 */



/**
 * @ingroup spv
 * @brief Allocate a new sparse vector row.
 *
 * @param os OS handle
 * @param rowcount Count of rows.
 *
 * @par Description
 * A new sparse vector row is allocated.
 *
 * @param rowcount Number of elements in a row.
 *
 * @return Returns the pointer to a row.
 */
static void
**spv_new_row(ocs_os_handle_t os, uint32_t rowcount)
{
	return ocs_malloc(os, sizeof(void*) * rowcount, OCS_M_ZERO | OCS_M_NOWAIT);
}



/**
 * @ingroup spv
 * @brief Delete row recursively.
 *
 * @par Description
 * This function recursively deletes the rows in this sparse vector
 *
 * @param os OS handle
 * @param a Pointer to the row.
 * @param n Number of elements in the row.
 * @param depth Depth of deleting.
 *
 * @return None.
 */
static void
_spv_del(ocs_os_handle_t os, void **a, uint32_t n, uint32_t depth)
{
	if (a) {
		if (depth) {
			uint32_t i;

			for (i = 0; i < n; i ++) {
				_spv_del(os, a[i], n, depth-1);
			}

			ocs_free(os, a, SPV_ROWLEN*sizeof(*a));
		}
	}
}

/**
 * @ingroup spv
 * @brief Delete a sparse vector.
 *
 * @par Description
 * The sparse vector is freed.
 *
 * @param spv Pointer to the sparse vector object.
 */
void
spv_del(sparse_vector_t spv)
{
	if (spv) {
		_spv_del(spv->os, spv->array, SPV_ROWLEN, SPV_DIM);
		ocs_free(spv->os, spv, sizeof(*spv));
	}
}

/**
 * @ingroup spv
 * @brief Instantiate a new sparse vector object.
 *
 * @par Description
 * A new sparse vector is allocated.
 *
 * @param os OS handle
 *
 * @return Returns the pointer to the sparse vector, or NULL.
 */
sparse_vector_t
spv_new(ocs_os_handle_t os)
{
	sparse_vector_t spv;
	uint32_t i;

	spv = ocs_malloc(os, sizeof(*spv), OCS_M_ZERO | OCS_M_NOWAIT);
	if (!spv) {
		return NULL;
	}

	spv->os = os;
	spv->max_idx = 1;
	for (i = 0; i < SPV_DIM; i ++) {
		spv->max_idx *= SPV_ROWLEN;
	}

	return spv;
}

/**
 * @ingroup spv
 * @brief Return the address of a cell.
 *
 * @par Description
 * Returns the address of a cell, allocates sparse rows as needed if the
 *         alloc_new_rows parameter is set.
 *
 * @param sv Pointer to the sparse vector.
 * @param idx Index of which to return the address.
 * @param alloc_new_rows If TRUE, then new rows may be allocated to set values,
 *                       Set to FALSE for retrieving values.
 *
 * @return Returns the pointer to the cell, or NULL.
 */
static void
*spv_new_cell(sparse_vector_t sv, uint32_t idx, uint8_t alloc_new_rows)
{
	uint32_t a = (idx >> 16) & 0xff;
	uint32_t b = (idx >>  8) & 0xff;
	uint32_t c = (idx >>  0) & 0xff;
	void **p;

	if (idx >= sv->max_idx) {
		return NULL;
	}

	if (sv->array == NULL) {
		sv->array = (alloc_new_rows ? spv_new_row(sv->os, SPV_ROWLEN) : NULL);
		if (sv->array == NULL) {
			return NULL;
		}
	}
	p = sv->array;
	if (p[a] == NULL) {
		p[a] = (alloc_new_rows ? spv_new_row(sv->os, SPV_ROWLEN) : NULL);
		if (p[a] == NULL) {
			return NULL;
		}
	}
	p = p[a];
	if (p[b] == NULL) {
		p[b] = (alloc_new_rows ? spv_new_row(sv->os, SPV_ROWLEN) : NULL);
		if (p[b] == NULL) {
			return NULL;
		}
	}
	p = p[b];

	return &p[c];
}

/**
 * @ingroup spv
 * @brief Set the sparse vector cell value.
 *
 * @par Description
 * Sets the sparse vector at @c idx to @c value.
 *
 * @param sv Pointer to the sparse vector.
 * @param idx Index of which to store.
 * @param value Value to store.
 *
 * @return None.
 */
void
spv_set(sparse_vector_t sv, uint32_t idx, void *value)
{
	void **ref = spv_new_cell(sv, idx, TRUE);
	if (ref) {
		*ref = value;
	}
}

/**
 * @ingroup spv
 * @brief Return the sparse vector cell value.
 *
 * @par Description
 * Returns the value at @c idx.
 *
 * @param sv Pointer to the sparse vector.
 * @param idx Index of which to return the value.
 *
 * @return Returns the cell value, or NULL.
 */
void
*spv_get(sparse_vector_t sv, uint32_t idx)
{
	void **ref = spv_new_cell(sv, idx, FALSE);
	if (ref) {
		return *ref;
	}
	return NULL;
}

/*****************************************************************/
/*                                                               */
/* CRC LOOKUP TABLE                                              */
/* ================                                              */
/* The following CRC lookup table was generated automagically    */
/* by the Rocksoft^tm Model CRC Algorithm Table Generation       */
/* Program V1.0 using the following model parameters:            */
/*                                                               */
/*    Width   : 2 bytes.                                         */
/*    Poly    : 0x8BB7                                           */
/*    Reverse : FALSE.                                           */
/*                                                               */
/* For more information on the Rocksoft^tm Model CRC Algorithm,  */
/* see the document titled "A Painless Guide to CRC Error        */
/* Detection Algorithms" by Ross Williams                        */
/* (ross@guest.adelaide.edu.au.). This document is likely to be  */
/* in the FTP archive "ftp.adelaide.edu.au/pub/rocksoft".        */
/*                                                               */
/*****************************************************************/
/*
 * Emulex Inc, changes:
 * - minor syntax changes for successful compilation with contemporary
 *   C compilers, and OCS SDK API
 * - crctable[] generated using Rocksoft public domain code
 *
 * Used in the Emulex SDK, the generated file crctable.out is cut and pasted into
 * applicable SDK sources.
 */


static unsigned short crctable[256] =
{
 0x0000, 0x8BB7, 0x9CD9, 0x176E, 0xB205, 0x39B2, 0x2EDC, 0xA56B,
 0xEFBD, 0x640A, 0x7364, 0xF8D3, 0x5DB8, 0xD60F, 0xC161, 0x4AD6,
 0x54CD, 0xDF7A, 0xC814, 0x43A3, 0xE6C8, 0x6D7F, 0x7A11, 0xF1A6,
 0xBB70, 0x30C7, 0x27A9, 0xAC1E, 0x0975, 0x82C2, 0x95AC, 0x1E1B,
 0xA99A, 0x222D, 0x3543, 0xBEF4, 0x1B9F, 0x9028, 0x8746, 0x0CF1,
 0x4627, 0xCD90, 0xDAFE, 0x5149, 0xF422, 0x7F95, 0x68FB, 0xE34C,
 0xFD57, 0x76E0, 0x618E, 0xEA39, 0x4F52, 0xC4E5, 0xD38B, 0x583C,
 0x12EA, 0x995D, 0x8E33, 0x0584, 0xA0EF, 0x2B58, 0x3C36, 0xB781,
 0xD883, 0x5334, 0x445A, 0xCFED, 0x6A86, 0xE131, 0xF65F, 0x7DE8,
 0x373E, 0xBC89, 0xABE7, 0x2050, 0x853B, 0x0E8C, 0x19E2, 0x9255,
 0x8C4E, 0x07F9, 0x1097, 0x9B20, 0x3E4B, 0xB5FC, 0xA292, 0x2925,
 0x63F3, 0xE844, 0xFF2A, 0x749D, 0xD1F6, 0x5A41, 0x4D2F, 0xC698,
 0x7119, 0xFAAE, 0xEDC0, 0x6677, 0xC31C, 0x48AB, 0x5FC5, 0xD472,
 0x9EA4, 0x1513, 0x027D, 0x89CA, 0x2CA1, 0xA716, 0xB078, 0x3BCF,
 0x25D4, 0xAE63, 0xB90D, 0x32BA, 0x97D1, 0x1C66, 0x0B08, 0x80BF,
 0xCA69, 0x41DE, 0x56B0, 0xDD07, 0x786C, 0xF3DB, 0xE4B5, 0x6F02,
 0x3AB1, 0xB106, 0xA668, 0x2DDF, 0x88B4, 0x0303, 0x146D, 0x9FDA,
 0xD50C, 0x5EBB, 0x49D5, 0xC262, 0x6709, 0xECBE, 0xFBD0, 0x7067,
 0x6E7C, 0xE5CB, 0xF2A5, 0x7912, 0xDC79, 0x57CE, 0x40A0, 0xCB17,
 0x81C1, 0x0A76, 0x1D18, 0x96AF, 0x33C4, 0xB873, 0xAF1D, 0x24AA,
 0x932B, 0x189C, 0x0FF2, 0x8445, 0x212E, 0xAA99, 0xBDF7, 0x3640,
 0x7C96, 0xF721, 0xE04F, 0x6BF8, 0xCE93, 0x4524, 0x524A, 0xD9FD,
 0xC7E6, 0x4C51, 0x5B3F, 0xD088, 0x75E3, 0xFE54, 0xE93A, 0x628D,
 0x285B, 0xA3EC, 0xB482, 0x3F35, 0x9A5E, 0x11E9, 0x0687, 0x8D30,
 0xE232, 0x6985, 0x7EEB, 0xF55C, 0x5037, 0xDB80, 0xCCEE, 0x4759,
 0x0D8F, 0x8638, 0x9156, 0x1AE1, 0xBF8A, 0x343D, 0x2353, 0xA8E4,
 0xB6FF, 0x3D48, 0x2A26, 0xA191, 0x04FA, 0x8F4D, 0x9823, 0x1394,
 0x5942, 0xD2F5, 0xC59B, 0x4E2C, 0xEB47, 0x60F0, 0x779E, 0xFC29,
 0x4BA8, 0xC01F, 0xD771, 0x5CC6, 0xF9AD, 0x721A, 0x6574, 0xEEC3,
 0xA415, 0x2FA2, 0x38CC, 0xB37B, 0x1610, 0x9DA7, 0x8AC9, 0x017E,
 0x1F65, 0x94D2, 0x83BC, 0x080B, 0xAD60, 0x26D7, 0x31B9, 0xBA0E,
 0xF0D8, 0x7B6F, 0x6C01, 0xE7B6, 0x42DD, 0xC96A, 0xDE04, 0x55B3
};

/*****************************************************************/
/*                   End of CRC Lookup Table                     */
/*****************************************************************/

/**
 * @brief Calculate the T10 PI CRC guard value for a block.
 *
 * Code based on Rocksoft's public domain CRC code, refer to
 * http://www.ross.net/crc/download/crc_v3.txt.  Minimally altered
 * to work with the ocs_dif API.
 *
 * @param blk_adr Pointer to the data buffer.
 * @param blk_len Number of bytes.
 * @param crc Previously-calculated CRC, or crcseed for a new block.
 *
 * @return Returns the calculated CRC, which may be passed back in for partial blocks.
 *
 */

unsigned short
t10crc16(const unsigned char *blk_adr, unsigned long blk_len, unsigned short crc)
{
	if (blk_len > 0) {
		while (blk_len--) {
			crc = crctable[((crc>>8) ^ *blk_adr++) & 0xFFL] ^ (crc << 8);
		}
	}
	return crc;
}

struct ocs_ramlog_s {
	uint32_t initialized;
	uint32_t textbuf_count;
	uint32_t textbuf_base;
	ocs_textbuf_t *textbufs;
	uint32_t cur_textbuf_idx;
	ocs_textbuf_t *cur_textbuf;
	ocs_lock_t lock;
};

static uint32_t ocs_ramlog_next_idx(ocs_ramlog_t *ramlog, uint32_t idx);

/**
 * @brief Allocate a ramlog buffer.
 *
 * Initialize a RAM logging buffer with text buffers totalling buffer_len.
 *
 * @param ocs Pointer to driver structure.
 * @param buffer_len Total length of RAM log buffers.
 * @param buffer_count Number of text buffers to allocate (totalling buffer-len).
 *
 * @return Returns pointer to ocs_ramlog_t instance, or NULL.
 */
ocs_ramlog_t *
ocs_ramlog_init(ocs_t *ocs, uint32_t buffer_len, uint32_t buffer_count)
{
	uint32_t i;
	uint32_t rc;
	ocs_ramlog_t *ramlog;

	ramlog = ocs_malloc(ocs, sizeof(*ramlog), OCS_M_ZERO | OCS_M_NOWAIT);
	if (ramlog == NULL) {
		ocs_log_err(ocs, "ocs_malloc ramlog failed\n");
		return NULL;
	}

	ramlog->textbuf_count = buffer_count;

	ramlog->textbufs = ocs_malloc(ocs, sizeof(*ramlog->textbufs)*buffer_count, OCS_M_ZERO | OCS_M_NOWAIT);
	if (ramlog->textbufs == NULL) {
		ocs_log_err(ocs, "ocs_malloc textbufs failed\n");
		ocs_ramlog_free(ocs, ramlog);
		return NULL;
	}

	for (i = 0; i < buffer_count; i ++) {
		rc = ocs_textbuf_alloc(ocs, &ramlog->textbufs[i], buffer_len);
		if (rc) {
			ocs_log_err(ocs, "ocs_textbuf_alloc failed\n");
			ocs_ramlog_free(ocs, ramlog);
			return NULL;
		}
	}

	ramlog->cur_textbuf_idx = 0;
	ramlog->textbuf_base = 1;
	ramlog->cur_textbuf = &ramlog->textbufs[0];
	ramlog->initialized = TRUE;
	ocs_lock_init(ocs, &ramlog->lock, "ramlog_lock[%d]", ocs_instance(ocs));
	return ramlog;
}

/**
 * @brief Free a ramlog buffer.
 *
 * A previously allocated RAM logging buffer is freed.
 *
 * @param ocs Pointer to driver structure.
 * @param ramlog Pointer to RAM logging buffer structure.
 *
 * @return None.
 */

void
ocs_ramlog_free(ocs_t *ocs, ocs_ramlog_t *ramlog)
{
	uint32_t i;

	if (ramlog != NULL) {
		ocs_lock_free(&ramlog->lock);
		if (ramlog->textbufs) {
			for (i = 0; i < ramlog->textbuf_count; i ++) {
				ocs_textbuf_free(ocs, &ramlog->textbufs[i]);
			}

			ocs_free(ocs, ramlog->textbufs, ramlog->textbuf_count*sizeof(*ramlog->textbufs));
			ramlog->textbufs = NULL;
		}
		ocs_free(ocs, ramlog, sizeof(*ramlog));
	}
}

/**
 * @brief Clear a ramlog buffer.
 *
 * The text in the start of day and/or recent ramlog text buffers is cleared.
 *
 * @param ocs Pointer to driver structure.
 * @param ramlog Pointer to RAM logging buffer structure.
 * @param clear_start_of_day Clear the start of day (driver init) portion of the ramlog.
 * @param clear_recent Clear the recent messages portion of the ramlog.
 *
 * @return None.
 */

void
ocs_ramlog_clear(ocs_t *ocs, ocs_ramlog_t *ramlog, int clear_start_of_day, int clear_recent)
{
	uint32_t i;

	if (clear_recent) {
		for (i = ramlog->textbuf_base; i < ramlog->textbuf_count; i ++) {
			ocs_textbuf_reset(&ramlog->textbufs[i]);
		}
		ramlog->cur_textbuf_idx = 1;
	}
	if (clear_start_of_day && ramlog->textbuf_base) {
		ocs_textbuf_reset(&ramlog->textbufs[0]);
		/* Set textbuf_base to 0, so that all buffers are available for
		 * recent logs
		 */
		ramlog->textbuf_base = 0;
	}
}

/**
 * @brief Append formatted printf data to a ramlog buffer.
 *
 * Formatted data is appended to a RAM logging buffer.
 *
 * @param os Pointer to driver structure.
 * @param fmt Pointer to printf style format specifier.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

int32_t
ocs_ramlog_printf(void *os, const char *fmt, ...)
{
	ocs_t *ocs = os;
	va_list ap;
	int32_t res;

	if (ocs == NULL || ocs->ramlog == NULL) {
		return -1;
	}

	va_start(ap, fmt);
	res = ocs_ramlog_vprintf(ocs->ramlog, fmt, ap);
	va_end(ap);

	return res;
}

/**
 * @brief Append formatted text to a ramlog using variable arguments.
 *
 * Formatted data is appended to the RAM logging buffer, using variable arguments.
 *
 * @param ramlog Pointer to RAM logging buffer.
 * @param fmt Pointer to printf style formatting string.
 * @param ap Variable argument pointer.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

int32_t
ocs_ramlog_vprintf(ocs_ramlog_t *ramlog, const char *fmt, va_list ap)
{
	if (ramlog == NULL || !ramlog->initialized) {
		return -1;
	}

	/* check the current text buffer, if it is almost full (less than 120 characaters), then
	 * roll to the next one.
	 */
	ocs_lock(&ramlog->lock);
	if (ocs_textbuf_remaining(ramlog->cur_textbuf) < 120) {
		ramlog->cur_textbuf_idx = ocs_ramlog_next_idx(ramlog, ramlog->cur_textbuf_idx);
		ramlog->cur_textbuf = &ramlog->textbufs[ramlog->cur_textbuf_idx];
		ocs_textbuf_reset(ramlog->cur_textbuf);
	}

	ocs_textbuf_vprintf(ramlog->cur_textbuf, fmt, ap);
	ocs_unlock(&ramlog->lock);

	return 0;
}

/**
 * @brief Return next ramlog buffer index.
 *
 * Given a RAM logging buffer index, return the next index.
 *
 * @param ramlog Pointer to RAM logging buffer.
 * @param idx Index value.
 *
 * @return Returns next index value.
 */

static uint32_t
ocs_ramlog_next_idx(ocs_ramlog_t *ramlog, uint32_t idx)
{
	idx = idx + 1;

	if (idx >= ramlog->textbuf_count) {
		idx = ramlog->textbuf_base;
	}

	return idx;
}

/**
 * @brief Perform ramlog buffer driver dump.
 *
 * The RAM logging buffer is appended to the driver dump data.
 *
 * @param textbuf Pointer to the driver dump text buffer.
 * @param ramlog Pointer to the RAM logging buffer.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

int32_t
ocs_ddump_ramlog(ocs_textbuf_t *textbuf, ocs_ramlog_t *ramlog)
{
	uint32_t i;
	ocs_textbuf_t *rltextbuf;
	int idx;

	if ((ramlog == NULL) || (ramlog->textbufs == NULL)) {
		return -1;
	}

	ocs_ddump_section(textbuf, "driver-log", 0);

	/* Dump the start of day buffer */
	ocs_ddump_section(textbuf, "startofday", 0);
	/* If textbuf_base is 0, then all buffers are used for recent */
	if (ramlog->textbuf_base) {
		rltextbuf = &ramlog->textbufs[0];
		ocs_textbuf_buffer(textbuf, ocs_textbuf_get_buffer(rltextbuf), ocs_textbuf_get_written(rltextbuf));
	}
	ocs_ddump_endsection(textbuf, "startofday", 0);

	/* Dump the most recent buffers */
	ocs_ddump_section(textbuf, "recent", 0);

	/* start with the next textbuf */
	idx = ocs_ramlog_next_idx(ramlog, ramlog->textbuf_count);

	for (i = ramlog->textbuf_base; i < ramlog->textbuf_count; i ++) {
		rltextbuf = &ramlog->textbufs[idx];
		ocs_textbuf_buffer(textbuf, ocs_textbuf_get_buffer(rltextbuf), ocs_textbuf_get_written(rltextbuf));
		idx = ocs_ramlog_next_idx(ramlog, idx);
	}
	ocs_ddump_endsection(textbuf, "recent", 0);
	ocs_ddump_endsection(textbuf, "driver-log", 0);

	return 0;
}

struct ocs_pool_s {
	ocs_os_handle_t os;
	ocs_array_t *a;
	ocs_list_t freelist;
	uint32_t use_lock:1;
	ocs_lock_t lock;
};

typedef struct {
	ocs_list_link_t link;
} pool_hdr_t;


/**
 * @brief Allocate a memory pool.
 *
 * A memory pool of given size and item count is allocated.
 *
 * @param os OS handle.
 * @param size Size in bytes of item.
 * @param count Number of items in a memory pool.
 * @param use_lock TRUE to enable locking of pool.
 *
 * @return Returns pointer to allocated memory pool, or NULL.
 */
ocs_pool_t *
ocs_pool_alloc(ocs_os_handle_t os, uint32_t size, uint32_t count, uint32_t use_lock)
{
	ocs_pool_t *pool;
	uint32_t i;

	pool = ocs_malloc(os, sizeof(*pool), OCS_M_ZERO | OCS_M_NOWAIT);
	if (pool == NULL) {
		return NULL;
	}

	pool->os = os;
	pool->use_lock = use_lock;

	/* Allocate an array where each array item is the size of a pool_hdr_t plus
	 * the requested memory item size (size)
	 */
	pool->a = ocs_array_alloc(os, size + sizeof(pool_hdr_t), count);
	if (pool->a == NULL) {
		ocs_pool_free(pool);
		return NULL;
	}

	ocs_list_init(&pool->freelist, pool_hdr_t, link);
	for (i = 0; i < count; i++) {
		ocs_list_add_tail(&pool->freelist, ocs_array_get(pool->a, i));
	}

	if (pool->use_lock) {
		ocs_lock_init(os, &pool->lock, "ocs_pool:%p", pool);
	}

	return pool;
}

/**
 * @brief Reset a memory pool.
 *
 * Place all pool elements on the free list, and zero them.
 *
 * @param pool Pointer to the pool object.
 *
 * @return None.
 */
void
ocs_pool_reset(ocs_pool_t *pool)
{
	uint32_t i;
	uint32_t count = ocs_array_get_count(pool->a);
	uint32_t size = ocs_array_get_size(pool->a);

	if (pool->use_lock) {
		ocs_lock(&pool->lock);
	}

	/*
	 * Remove all the entries from the free list, otherwise we will
	 * encountered linked list asserts when they are re-added.
	 */
	while (!ocs_list_empty(&pool->freelist)) {
		ocs_list_remove_head(&pool->freelist);
	}

	/* Reset the free list */
	ocs_list_init(&pool->freelist, pool_hdr_t, link);

	/* Return all elements to the free list and zero the elements */
	for (i = 0; i < count; i++) {
		ocs_memset(ocs_pool_get_instance(pool, i), 0, size - sizeof(pool_hdr_t));
		ocs_list_add_tail(&pool->freelist, ocs_array_get(pool->a, i));
	}
	if (pool->use_lock) {
		ocs_unlock(&pool->lock);
	}

}

/**
 * @brief Free a previously allocated memory pool.
 *
 * The memory pool is freed.
 *
 * @param pool Pointer to memory pool.
 *
 * @return None.
 */
void
ocs_pool_free(ocs_pool_t *pool)
{
	if (pool != NULL) {
		if (pool->a != NULL) {
			ocs_array_free(pool->a);
		}
		if (pool->use_lock) {
			ocs_lock_free(&pool->lock);
		}
		ocs_free(pool->os, pool, sizeof(*pool));
	}
}

/**
 * @brief Allocate a memory pool item
 *
 * A memory pool item is taken from the free list and returned.
 *
 * @param pool Pointer to memory pool.
 *
 * @return Pointer to allocated item, otherwise NULL if there are no unallocated
 *	   items.
 */
void *
ocs_pool_get(ocs_pool_t *pool)
{
	pool_hdr_t *h;
	void *item = NULL;

	if (pool->use_lock) {
		ocs_lock(&pool->lock);
	}

	h = ocs_list_remove_head(&pool->freelist);

	if (h != NULL) {
		/* Return the array item address offset by the size of pool_hdr_t */
		item = &h[1];
	}

	if (pool->use_lock) {
		ocs_unlock(&pool->lock);
	}
	return item;
}

/**
 * @brief free memory pool item
 *
 * A memory pool item is freed.
 *
 * @param pool Pointer to memory pool.
 * @param item Pointer to item to free.
 *
 * @return None.
 */
void
ocs_pool_put(ocs_pool_t *pool, void *item)
{
	pool_hdr_t *h;

	if (pool->use_lock) {
		ocs_lock(&pool->lock);
	}

	/* Fetch the address of the array item, which is the item address negatively offset
	 * by size of pool_hdr_t (note the index of [-1]
	 */
	h = &((pool_hdr_t*)item)[-1];

	ocs_list_add_tail(&pool->freelist, h);

	if (pool->use_lock) {
		ocs_unlock(&pool->lock);
	}

}

/**
 * @brief Return memory pool item count.
 *
 * Returns the allocated number of items.
 *
 * @param pool Pointer to memory pool.
 *
 * @return Returns count of allocated items.
 */
uint32_t
ocs_pool_get_count(ocs_pool_t *pool)
{
	uint32_t count;
	if (pool->use_lock) {
		ocs_lock(&pool->lock);
	}
	count = ocs_array_get_count(pool->a);
	if (pool->use_lock) {
		ocs_unlock(&pool->lock);
	}
	return count;
}

/**
 * @brief Return item given an index.
 *
 * A pointer to a memory pool item is returned given an index.
 *
 * @param pool Pointer to memory pool.
 * @param idx Index.
 *
 * @return Returns pointer to item, or NULL if index is invalid.
 */
void *
ocs_pool_get_instance(ocs_pool_t *pool, uint32_t idx)
{
	pool_hdr_t *h = ocs_array_get(pool->a, idx);

	if (h == NULL) {
		return NULL;
	}
	return &h[1];
}

/**
 * @brief Return count of free objects in a pool.
 *
 * The number of objects on a pool's free list.
 *
 * @param pool Pointer to memory pool.
 *
 * @return Returns count of objects on free list.
 */
uint32_t
ocs_pool_get_freelist_count(ocs_pool_t *pool)
{
	uint32_t count = 0;
	void *item;

	if (pool->use_lock) {
		ocs_lock(&pool->lock);
	}

	ocs_list_foreach(&pool->freelist, item) {
		count++;
	}

	if (pool->use_lock) {
		ocs_unlock(&pool->lock);
	}
	return count;
}
