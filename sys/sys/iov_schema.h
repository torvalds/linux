/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014-2015 Sandvine Inc.
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_IOV_SCHEMA_H_
#define _SYS_IOV_SCHEMA_H_

#define	IOV_SCHEMA_HASDEFAULT	(1 << 0)
#define	IOV_SCHEMA_REQUIRED	(1 << 1)

nvlist_t	*pci_iov_schema_alloc_node(void);

void	pci_iov_schema_add_bool(nvlist_t *schema, const char *name,
	    uint32_t flags,  int defaultVal);
void	pci_iov_schema_add_string(nvlist_t *schema, const char *name,
	    uint32_t flags, const char *defaultVal);
void	pci_iov_schema_add_uint8(nvlist_t *schema, const char *name,
	    uint32_t flags, uint8_t defaultVal);
void	pci_iov_schema_add_uint16(nvlist_t *schema, const char *name,
	    uint32_t flags, uint16_t defaultVal);
void	pci_iov_schema_add_uint32(nvlist_t *schema, const char *name,
	    uint32_t flags, uint32_t defaultVal);
void	pci_iov_schema_add_uint64(nvlist_t *schema, const char *name,
	    uint32_t flags, uint64_t defaultVal);
void	pci_iov_schema_add_unicast_mac(nvlist_t *schema, const char *name,
	    uint32_t flags, const uint8_t * defaultVal);

#endif
