/*
 * Copyright (c) 2014, Ericsson AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_TIPC_NETLINK_H_
#define _LINUX_TIPC_NETLINK_H_

#define TIPC_GENL_V2_NAME      "TIPCv2"
#define TIPC_GENL_V2_VERSION   0x1

/* Netlink commands */
enum {
	TIPC_NL_UNSPEC,
	TIPC_NL_LEGACY,
	TIPC_NL_BEARER_DISABLE,
	TIPC_NL_BEARER_ENABLE,
	TIPC_NL_BEARER_GET,
	TIPC_NL_BEARER_SET,
	TIPC_NL_SOCK_GET,
	TIPC_NL_PUBL_GET,

	__TIPC_NL_CMD_MAX,
	TIPC_NL_CMD_MAX = __TIPC_NL_CMD_MAX - 1
};

/* Top level netlink attributes */
enum {
	TIPC_NLA_UNSPEC,
	TIPC_NLA_BEARER,		/* nest */
	TIPC_NLA_SOCK,			/* nest */
	TIPC_NLA_PUBL,			/* nest */

	__TIPC_NLA_MAX,
	TIPC_NLA_MAX = __TIPC_NLA_MAX - 1
};

/* Bearer info */
enum {
	TIPC_NLA_BEARER_UNSPEC,
	TIPC_NLA_BEARER_NAME,		/* string */
	TIPC_NLA_BEARER_PROP,		/* nest */
	TIPC_NLA_BEARER_DOMAIN,		/* u32 */

	__TIPC_NLA_BEARER_MAX,
	TIPC_NLA_BEARER_MAX = __TIPC_NLA_BEARER_MAX - 1
};

/* Socket info */
enum {
	TIPC_NLA_SOCK_UNSPEC,
	TIPC_NLA_SOCK_ADDR,		/* u32 */
	TIPC_NLA_SOCK_REF,		/* u32 */
	TIPC_NLA_SOCK_CON,		/* nest */
	TIPC_NLA_SOCK_HAS_PUBL,		/* flag */

	__TIPC_NLA_SOCK_MAX,
	TIPC_NLA_SOCK_MAX = __TIPC_NLA_SOCK_MAX - 1
};

/* Publication info */
enum {
	TIPC_NLA_PUBL_UNSPEC,

	TIPC_NLA_PUBL_TYPE,		/* u32 */
	TIPC_NLA_PUBL_LOWER,		/* u32 */
	TIPC_NLA_PUBL_UPPER,		/* u32 */
	TIPC_NLA_PUBL_SCOPE,		/* u32 */
	TIPC_NLA_PUBL_NODE,		/* u32 */
	TIPC_NLA_PUBL_REF,		/* u32 */
	TIPC_NLA_PUBL_KEY,		/* u32 */

	__TIPC_NLA_PUBL_MAX,
	TIPC_NLA_PUBL_MAX = __TIPC_NLA_PUBL_MAX - 1
};

/* Nest, connection info */
enum {
	TIPC_NLA_CON_UNSPEC,

	TIPC_NLA_CON_FLAG,		/* flag */
	TIPC_NLA_CON_NODE,		/* u32 */
	TIPC_NLA_CON_SOCK,		/* u32 */
	TIPC_NLA_CON_TYPE,		/* u32 */
	TIPC_NLA_CON_INST,		/* u32 */

	__TIPC_NLA_CON_MAX,
	TIPC_NLA_CON_MAX = __TIPC_NLA_CON_MAX - 1
};

/* Nest, link propreties. Valid for link, media and bearer */
enum {
	TIPC_NLA_PROP_UNSPEC,

	TIPC_NLA_PROP_PRIO,		/* u32 */
	TIPC_NLA_PROP_TOL,		/* u32 */
	TIPC_NLA_PROP_WIN,		/* u32 */

	__TIPC_NLA_PROP_MAX,
	TIPC_NLA_PROP_MAX = __TIPC_NLA_PROP_MAX - 1
};

#endif
