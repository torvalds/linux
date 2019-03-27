/* $FreeBSD$ */
/* $NetBSD: citrus_namespace.h,v 1.8 2009/01/11 02:46:24 christos Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003 Citrus Project,
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "citrus_bcs.h"

#ifndef _CITRUS_NAMESPACE_H_
#define _CITRUS_NAMESPACE_H_

/* citrus_alias */
#ifndef _CITRUS_ALIAS_NO_NAMESPACE
#define _alias_lookup		_citrus_alias_lookup
#endif /* _CITRUS_ALIAS_NO_NAMESPACE */

/* citrus_bcs */
#ifndef _CITRUS_BCS_NO_NAMESPACE
#define _bcs_isalnum		_citrus_bcs_isalnum
#define _bcs_isalpha		_citrus_bcs_isalpha
#define _bcs_isblank		_citrus_bcs_isblank
#define _bcs_isdigit		_citrus_bcs_isdigit
#define _bcs_islower		_citrus_bcs_islower
#define _bcs_iseol		_citrus_bcs_iseol
#define _bcs_isspace		_citrus_bcs_isspace
#define _bcs_isupper		_citrus_bcs_isupper
#define _bcs_isxdigit		_citrus_bcs_isxdigit
#define _bcs_skip_nonws		_citrus_bcs_skip_nonws
#define _bcs_skip_nonws_len	_citrus_bcs_skip_nonws_len
#define _bcs_skip_ws		_citrus_bcs_skip_ws
#define _bcs_skip_ws_len	_citrus_bcs_skip_ws_len
#define _bcs_strcasecmp		_citrus_bcs_strcasecmp
#define _bcs_strncasecmp	_citrus_bcs_strncasecmp
#define _bcs_tolower		_citrus_bcs_tolower
#define _bcs_toupper		_citrus_bcs_toupper
#define _bcs_trunc_rws_len	_citrus_bcs_trunc_rws_len
#define _bcs_convert_to_lower	_citrus_bcs_convert_to_lower
#define _bcs_convert_to_upper	_citrus_bcs_convert_to_upper
#define _bcs_strtol		_citrus_bcs_strtol
#define _bcs_strtoul		_citrus_bcs_strtoul
#endif /* _CITRUS_BCS_NO_NAMESPACE */

/* citrus_csmapper */
#ifndef _CITRUS_CSMAPPER_NO_NAMESPACE
#define _csmapper		_citrus_csmapper
#define _csmapper_open		_citrus_csmapper_open
#define _csmapper_close		_citrus_csmapper_close
#define _csmapper_convert	_citrus_csmapper_convert
#define _csmapper_init_state	_citrus_csmapper_init_state
#define _csmapper_get_state_size _citrus_csmapper_get_state_size
#define _csmapper_get_src_max	_citrus_csmapper_get_src_max
#define _csmapper_get_dst_max	_citrus_csmapper_get_dst_max
#define _CSMAPPER_F_PREVENT_PIVOT _CITRUS_CSMAPPER_F_PREVENT_PIVOT
#endif /* _CITRUS_CSMAPPER_NO_NAMESPACE */

/* citrus_db */
#ifndef _CITRUS_DB_NO_NAMESPACE
#define _db_open		_citrus_db_open
#define _db_close		_citrus_db_close
#define _db_lookup		_citrus_db_lookup
#define _db_lookup_by_s		_citrus_db_lookup_by_string
#define _db_lookup8_by_s	_citrus_db_lookup8_by_string
#define _db_lookup16_by_s	_citrus_db_lookup16_by_string
#define _db_lookup32_by_s	_citrus_db_lookup32_by_string
#define _db_lookupstr_by_s	_citrus_db_lookup_string_by_string
#define _db_hash_std		_citrus_db_hash_std
#define _db_get_num_entries	_citrus_db_get_number_of_entries
#define _db_get_entry		_citrus_db_get_entry
#define _db_locator		_citrus_db_locator
#define _db_locator_init	_citrus_db_locator_init
#endif /* _CITRUS_DB_NO_NAMESPACE */

/* citrus_db_factory */
#ifndef _CITRUS_DB_FACTORY_NO_NAMESPACE
#define _db_factory		_citrus_db_factory
#define _db_factory_create	_citrus_db_factory_create
#define _db_factory_free	_citrus_db_factory_free
#define _db_factory_add		_citrus_db_factory_add
#define _db_factory_add_by_s	_citrus_db_factory_add_by_string
#define _db_factory_add8_by_s	_citrus_db_factory_add8_by_string
#define _db_factory_add16_by_s	_citrus_db_factory_add16_by_string
#define _db_factory_add32_by_s	_citrus_db_factory_add32_by_string
#define _db_factory_addstr_by_s	_citrus_db_factory_add_string_by_string
#define _db_factory_calc_size	_citrus_db_factory_calc_size
#define _db_factory_serialize	_citrus_db_factory_serialize
#endif /* _CITRUS_DB_FACTORY_NO_NAMESPACE */

/* citrus_lookup */
#ifndef _CITRUS_DB_NO_NAMESPACE
#define _LOOKUP_CASE_SENSITIVE	_CITRUS_LOOKUP_CASE_SENSITIVE
#define _LOOKUP_CASE_IGNORE	_CITRUS_LOOKUP_CASE_IGNORE
#define _lookup			_citrus_lookup
#define _lookup_simple		_citrus_lookup_simple
#define _lookup_alias		_citrus_lookup_alias
#define _lookup_seq_open	_citrus_lookup_seq_open
#define _lookup_seq_rewind	_citrus_lookup_seq_rewind
#define _lookup_seq_next	_citrus_lookup_seq_next
#define _lookup_seq_lookup	_citrus_lookup_seq_lookup
#define _lookup_get_num_entries	_citrus_lookup_get_number_of_entries
#define _lookup_seq_close	_citrus_lookup_seq_close
#define _lookup_factory_convert	_citrus_lookup_factory_convert
#endif /* _CITRUS_DB_NO_NAMESPACE */

/* citrus_esdb */
#ifndef _CITRUS_ESDB_NO_NAMESPACE
#define _esdb			_citrus_esdb
#define _esdb_charset		_citrus_esdb_charset
#define _esdb_open		_citrus_esdb_open
#define _esdb_close		_citrus_esdb_close
#define _esdb_get_list		_citrus_esdb_get_list
#define _esdb_free_list		_citrus_esdb_free_list
#endif /* _CITRUS_ESDB_NO_NAMESPACE */

/* citrus_hash */
#ifndef _CITRUS_HASH_NO_NAMESPACE
#define _citrus_string_hash_func _string_hash_func
#endif /* _CITRUS_HASH_NO_NAMESPACE */

/* citrus_mapper */
#ifndef _CITRUS_MAPPER_NO_NAMESPACE
#define _mapper			_citrus_mapper
#define _mapper_ops		_citrus_mapper_ops
#define _mapper_traits		_citrus_mapper_traits
#define _mapper_open		_citrus_mapper_open
#define _mapper_open_direct	_citrus_mapper_open_direct
#define _mapper_close		_citrus_mapper_close
#define _MAPPER_CONVERT_SUCCESS	_CITRUS_MAPPER_CONVERT_SUCCESS
#define _MAPPER_CONVERT_NONIDENTICAL _CITRUS_MAPPER_CONVERT_NONIDENTICAL
#define _MAPPER_CONVERT_SRC_MORE _CITRUS_MAPPER_CONVERT_SRC_MORE
#define _MAPPER_CONVERT_DST_MORE _CITRUS_MAPPER_CONVERT_DST_MORE
#define _MAPPER_CONVERT_ILSEQ	_CITRUS_MAPPER_CONVERT_ILSEQ
#define _MAPPER_CONVERT_FATAL	_CITRUS_MAPPER_CONVERT_FATAL
#define _mapper_convert		_citrus_mapper_convert
#define _mapper_init_state	_citrus_mapper_init_state
#define _mapper_get_state_size	_citrus_mapper_get_state_size
#define _mapper_get_src_max	_citrus_mapper_get_src_max
#define _mapper_get_dst_max	_citrus_mapper_get_dst_max
#define _mapper_set_persistent	_citrus_mapper_set_persistent
#endif /* _CITRUS_MAPPER_NO_NAMESPACE */

/* citrus_memstream */
#ifndef _CITRUS_MEMSTREAM_NO_NAMESPACE
#define _memstream		_citrus_memory_stream
#define _memstream_getln	_citrus_memory_stream_getln
#define _memstream_matchline	_citrus_memory_stream_matchline
#define _memstream_chr		_citrus_memory_stream_chr
#define _memstream_skip_ws	_citrus_memory_stream_skip_ws
#define _memstream_iseof	_citrus_memory_stream_iseof
#define _memstream_bind		_citrus_memory_stream_bind
#define _memstream_bind_ptr	_citrus_memory_stream_bind_ptr
#define _memstream_seek		_citrus_memory_stream_seek
#define _memstream_rewind	_citrus_memory_stream_rewind
#define _memstream_tell		_citrus_memory_stream_tell
#define _memstream_remainder	_citrus_memory_stream_remainder
#define _memstream_getc		_citrus_memory_stream_getc
#define _memstream_ungetc	_citrus_memory_stream_ungetc
#define _memstream_peek		_citrus_memory_stream_peek
#define _memstream_getregion	_citrus_memory_stream_getregion
#define _memstream_getln_region	_citrus_memory_stream_getln_region
#endif /* _CITRUS_MEMSTREAM_NO_NAMESPACE */

/* citrus_mmap */
#ifndef _CITRUS_MMAP_NO_NAMESPACE
#define _map_file		_citrus_map_file
#define _unmap_file		_citrus_unmap_file
#endif /* _CITRUS_MMAP_NO_NAMESPACE */

#ifndef _CITRUS_PIVOT_NO_NAMESPACE
#define _pivot_factory_convert	_citrus_pivot_factory_convert
#endif /* _CITRUS_PIVOT_NO_NAMESPACE */

/* citrus_region.h */
#ifndef _CITRUS_REGION_NO_NAMESPACE
#define _region			_citrus_region
#define _region_init		_citrus_region_init
#define _region_head		_citrus_region_head
#define _region_size		_citrus_region_size
#define _region_check		_citrus_region_check
#define _region_offset		_citrus_region_offset
#define _region_peek8		_citrus_region_peek8
#define _region_peek16		_citrus_region_peek16
#define _region_peek32		_citrus_region_peek32
#define _region_get_subregion	_citrus_region_get_subregion
#endif /* _CITRUS_REGION_NO_NAMESPACE */

/* citrus_stdenc.h */
#ifndef _CITRUS_STDENC_NO_NAMESPACE
#define _stdenc			_citrus_stdenc
#define _stdenc_ops		_citrus_stdenc_ops
#define _stdenc_traits		_citrus_stdenc_traits
#define _stdenc_state_desc	_citrus_stdenc_state_desc
#define _stdenc_open		_citrus_stdenc_open
#define _stdenc_close		_citrus_stdenc_close
#define _stdenc_init_state	_citrus_stdenc_init_state
#define _stdenc_mbtocs		_citrus_stdenc_mbtocs
#define _stdenc_cstomb		_citrus_stdenc_cstomb
#define _stdenc_mbtowc		_citrus_stdenc_mbtowc
#define _stdenc_wctomb		_citrus_stdenc_wctomb
#define _stdenc_put_state_reset	_citrus_stdenc_put_state_reset
#define _stdenc_get_state_size	_citrus_stdenc_get_state_size
#define _stdenc_get_mb_cur_max	_citrus_stdenc_get_mb_cur_max
#define _stdenc_get_state_desc	_citrus_stdenc_get_state_desc
#define _STDENC_SDID_GENERIC	_CITRUS_STDENC_SDID_GENERIC
#define _STDENC_SDGEN_UNKNOWN	_CITRUS_STDENC_SDGEN_UNKNOWN
#define _STDENC_SDGEN_INITIAL	_CITRUS_STDENC_SDGEN_INITIAL
#define _STDENC_SDGEN_STABLE	_CITRUS_STDENC_SDGEN_STABLE
#define _STDENC_SDGEN_INCOMPLETE_CHAR _CITRUS_STDENC_SDGEN_INCOMPLETE_CHAR
#define _STDENC_SDGEN_INCOMPLETE_SHIFT _CITRUS_STDENC_SDGEN_INCOMPLETE_SHIFT
#endif /* _CITRUS_STDENC_NO_NAMESPACE */

/* citrus_types.h */
#ifndef _CITRUS_TYPES_NO_NAMESPACE
#define _index_t		_citrus_index_t
#define _csid_t			_citrus_csid_t
#define _wc_t			_citrus_wc_t
#endif /* _CITRUS_TYPES_NO_NAMESPACE */

#endif
