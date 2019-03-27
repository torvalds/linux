/*	$NetBSD: bnep.h,v 1.1 2008/08/17 13:20:57 plunky Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2008 Iain Hibbert
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $FreeBSD$ */

/*
 * Constants defined in the Bluetooth Network Encapsulation
 * Protocol (BNEP) specification v1.0
 */

#define	BNEP_MTU_MIN		1691

#define BNEP_EXT		0x80
#define	BNEP_TYPE(x)		((x) & 0x7f)
#define BNEP_TYPE_EXT(x)	(((x) & BNEP_EXT) == BNEP_EXT)

/* BNEP packet types */
#define	BNEP_GENERAL_ETHERNET			0x00
#define	BNEP_CONTROL				0x01
#define	BNEP_COMPRESSED_ETHERNET		0x02
#define	BNEP_COMPRESSED_ETHERNET_SRC_ONLY	0x03
#define	BNEP_COMPRESSED_ETHERNET_DST_ONLY	0x04

/* BNEP extension header types */
#define	BNEP_EXTENSION_CONTROL			0x00

/* BNEP control types */
#define	BNEP_CONTROL_COMMAND_NOT_UNDERSTOOD	0x00
#define	BNEP_SETUP_CONNECTION_REQUEST		0x01
#define	BNEP_SETUP_CONNECTION_RESPONSE		0x02
#define	BNEP_FILTER_NET_TYPE_SET		0x03
#define	BNEP_FILTER_NET_TYPE_RESPONSE		0x04
#define	BNEP_FILTER_MULTI_ADDR_SET		0x05
#define	BNEP_FILTER_MULTI_ADDR_RESPONSE		0x06

/* BNEP setup response codes */
#define	BNEP_SETUP_SUCCESS		0x0000
#define	BNEP_SETUP_INVALID_SRC_UUID	0x0001
#define	BNEP_SETUP_INVALID_DST_UUID	0x0002
#define	BNEP_SETUP_INVALID_UUID_SIZE	0x0003
#define	BNEP_SETUP_NOT_ALLOWED		0x0004

/* BNEP filter return codes */
#define	BNEP_FILTER_SUCCESS		0x0000
#define	BNEP_FILTER_UNSUPPORTED_REQUEST	0x0001
#define	BNEP_FILTER_INVALID_RANGE	0x0002
#define	BNEP_FILTER_TOO_MANY_FILTERS	0x0003
#define	BNEP_FILTER_SECURITY_FAILURE	0x0004
