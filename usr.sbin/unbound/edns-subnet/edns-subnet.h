/*
 * edns-subnet/edns-subnet.h - Subnet option related constants 
 *
 * Copyright (c) 2013, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * \file
 * Subnet option related constants. 
 */

#include "util/net_help.h"

#ifndef EDNSSUBNET_EDNSSUBNET_H
#define EDNSSUBNET_EDNSSUBNET_H

/** In use by the edns subnet option code, as assigned by IANA */
#define EDNSSUBNET_ADDRFAM_IP4 1
#define EDNSSUBNET_ADDRFAM_IP6 2

/**
 * ECS option
 */
struct ecs_data {
	uint16_t subnet_addr_fam;
	uint8_t subnet_source_mask;
	uint8_t subnet_scope_mask;
	uint8_t subnet_addr[INET6_SIZE];
	int subnet_validdata;
};

/** 
 * copy the first n BITS from src to dst iff both src and dst 
 * are large enough, return 0 on success
 */
int
copy_clear(uint8_t* dst, size_t dstlen, uint8_t* src, size_t srclen, size_t n);

#endif /* EDNSSUBNET_EDNSSUBNET_H */
