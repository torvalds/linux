/*
 * ng_gif_demux.h
 */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2001 The Aerospace Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions, and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of The Aerospace Corporation may not be used to endorse or
 *    promote products derived from this software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AEROSPACE CORPORATION ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AEROSPACE CORPORATION BE LIABLE
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

#ifndef _NETGRAPH_NG_GIF_DEMUX_H_
#define _NETGRAPH_NG_GIF_DEMUX_H_

/* Node type name and magic cookie */
#define NG_GIF_DEMUX_NODE_TYPE		"gif_demux"
#define NGM_GIF_DEMUX_COOKIE		995567329

/* Hook names */
#define NG_GIF_DEMUX_HOOK_GIF		"gif"
#define NG_GIF_DEMUX_HOOK_INET		"inet"
#define NG_GIF_DEMUX_HOOK_INET6		"inet6"
#define NG_GIF_DEMUX_HOOK_ATALK		"atalk"
#define NG_GIF_DEMUX_HOOK_IPX		"ipx"
#define NG_GIF_DEMUX_HOOK_ATM		"atm"
#define NG_GIF_DEMUX_HOOK_NATM		"natm"

#endif /* _NETGRAPH_NG_GIF_DEMUX_H_ */
