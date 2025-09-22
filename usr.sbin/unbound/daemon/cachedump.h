/*
 * daemon/cachedump.h - dump the cache to text format.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
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
 *
 * This file contains functions to read and write the cache(s)
 * to text format.
 *
 * The format of the file is as follows:
 * [RRset cache]
 * [Message cache]
 * EOF		-- fixed string "EOF" before end of the file.
 *
 * The RRset cache is:
 * START_RRSET_CACHE
 * [rrset]*
 * END_RRSET_CACHE
 *
 * rrset is:
 * ;rrset [nsec_apex] TTL rr_count rrsig_count trust security
 * resource records, one per line, in zonefile format
 * rrsig records, one per line, in zonefile format
 * If the text conversion fails, BADRR is printed on the line.
 *
 * The Message cache is:
 * START_MSG_CACHE
 * [msg]*
 * END_MSG_CACHE
 *
 * msg is:
 * msg name class type flags qdcount ttl security an ns ar
 * list of rrset references, one per line. If conversion fails, BADREF
 * reference is:
 * name class type flags
 *
 * Expired cache entries are not printed.
 */

#ifndef DAEMON_DUMPCACHE_H
#define DAEMON_DUMPCACHE_H
struct worker;
#include "daemon/remote.h"

/**
 * Dump cache(s) to text
 * @param ssl: to print to
 * @param worker: worker that is available (buffers, etc) and has 
 * 	ptrs to the caches.
 * @return false on ssl print error.
 */
int dump_cache(RES* ssl, struct worker* worker);

/**
 * Load cache(s) from text 
 * @param ssl: to read from 
 * @param worker: worker that is available (buffers, etc) and has 
 * 	ptrs to the caches.
 * @return false on ssl error.
 */
int load_cache(RES* ssl, struct worker* worker);

/**
 * Print the delegation used to lookup for this name.
 * @param ssl: to read from 
 * @param worker: worker that is available (buffers, etc) and has 
 * 	ptrs to the caches.
 * @param nm: name to lookup
 * @param nmlen: length of name.
 * @param nmlabs: labels in name.
 * @return false on ssl error.
 */
int print_deleg_lookup(RES* ssl, struct worker* worker, uint8_t* nm,
	size_t nmlen, int nmlabs);

#endif /* DAEMON_DUMPCACHE_H */
