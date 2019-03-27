/* -
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * Author: George V. Neville-Neil
 *
 */

/* Organizationally Unique Identifier assigned by IEEE 14 Nov 2013 */
#define OUI_FREEBSD_BASE 0x589cfc000000
#define OUI_FREEBSD(nic) (OUI_FREEBSD_BASE | (nic))

/* 
 * OUIs are most often used to uniquely identify network interfaces
 * and occupy the first 3 bytes of both destination and source MAC
 * addresses.  The following allocations exist so that various
 * software systems associated with FreeBSD can have unique IDs in the
 * absence of hardware.  The use of OUIs for this purpose is not fully
 * fleshed out but is now in common use in virtualization technology.
 * 
 * Allocations from this range are expected to be made using COMMON
 * SENSE by developers.  Do NOT take a large range just because
 * they're currently wide open.  Take the smallest useful range for
 * your system.  We have (2^24 - 2) available addresses (see Reserved
 * Values below) but that is far from infinite.
 *
 * In the event of a conflict arbitration of allocation in this file
 * is subject to core@ approval.
 * 
 * Applications are differentiated based on the high order bit(s) of
 * the remaining three bytes.  Our first allocation has all 0s, the
 * next allocation has the highest bit set.  Allocating in this way
 * gives us 254 allocations of 64K addresses.  Address blocks can be
 * concatenated if necessary.
 *
 * Reserved Values: 0x000000 and 0xffffff are reserved and MUST NOT BE
 * allocated for any reason.
 */

/* Allocate 20 bits to bhyve */
#define OUI_FREEBSD_BHYVE_LOW	OUI_FREEBSD(0x000001)
#define OUI_FREEBSD_BHYVE_HIGH	OUI_FREEBSD(0x0fffff)
