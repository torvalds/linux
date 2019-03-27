/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006-2007, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _NETINET_SCTP_OS_H_
#define _NETINET_SCTP_OS_H_

/*
 * General kernel memory allocation:
 *  SCTP_MALLOC(element, type, size, name)
 *  SCTP_FREE(element)
 * Kernel memory allocation for "soname"- memory must be zeroed.
 *  SCTP_MALLOC_SONAME(name, type, size)
 *  SCTP_FREE_SONAME(name)
 */

/*
 * Zone(pool) allocation routines: MUST be defined for each OS.
 *  zone = zone/pool pointer.
 *  name = string name of the zone/pool.
 *  size = size of each zone/pool element.
 *  number = number of elements in zone/pool.
 *  type = structure type to allocate
 *
 * sctp_zone_t
 * SCTP_ZONE_INIT(zone, name, size, number)
 * SCTP_ZONE_GET(zone, type)
 * SCTP_ZONE_FREE(zone, element)
 * SCTP_ZONE_DESTROY(zone)
 */

#include <netinet/sctp_os_bsd.h>





/* All os's must implement this address gatherer. If
 * no VRF's exist, then vrf 0 is the only one and all
 * addresses and ifn's live here.
 */
#define SCTP_DEFAULT_VRF 0
void sctp_init_vrf_list(int vrfid);

#endif
