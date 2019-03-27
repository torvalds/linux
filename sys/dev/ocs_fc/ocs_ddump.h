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

#if !defined(__OCS_DDUMP_H__)
#define __OCS_DDUMP_H__

#include "ocs_utils.h"

#define OCS_DDUMP_FLAGS_WQES	(1U << 0)
#define OCS_DDUMP_FLAGS_CQES	(1U << 1)
#define OCS_DDUMP_FLAGS_MQES	(1U << 2)
#define OCS_DDUMP_FLAGS_RQES	(1U << 3)
#define OCS_DDUMP_FLAGS_EQES	(1U << 4)

extern int ocs_ddump(ocs_t *ocs, ocs_textbuf_t *textbuf, uint32_t flags, uint32_t qentries);

extern void ocs_ddump_startfile(ocs_textbuf_t *textbuf);
extern void ocs_ddump_endfile(ocs_textbuf_t *textbuf);
extern void ocs_ddump_section(ocs_textbuf_t *textbuf, const char *name, uint32_t instance);
extern void ocs_ddump_endsection(ocs_textbuf_t *textbuf, const char *name, uint32_t instance);
__attribute__((format(printf,3,4)))
extern void ocs_ddump_value(ocs_textbuf_t *textbuf, const char *name, const char *fmt, ...);
extern void ocs_ddump_buffer(ocs_textbuf_t *textbuf, const char *name, uint32_t instance, void *buffer, uint32_t size);
extern int32_t ocs_save_ddump(ocs_t *ocs, uint32_t flags, uint32_t qentries);
extern int32_t ocs_get_saved_ddump(ocs_t *ocs, ocs_textbuf_t *textbuf);
extern int32_t ocs_save_ddump_all(uint32_t flags, uint32_t qentries, uint32_t alloc_flag);
extern int32_t ocs_clear_saved_ddump(ocs_t *ocs);
extern void ocs_ddump_queue_entries(ocs_textbuf_t *textbuf, void *q_addr, uint32_t size, uint32_t length, int32_t index, uint32_t qentries);

#endif 
