/******************************************************************************

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#ifndef _IXGBE_RSS_H_
#define _IXGBE_RSS_H_

#ifdef RSS

#include <net/rss_config.h>
#include <netinet/in_rss.h>

#else

#define RSS_HASHTYPE_RSS_IPV4          (1 << 1)
#define RSS_HASHTYPE_RSS_TCP_IPV4      (1 << 2)
#define RSS_HASHTYPE_RSS_IPV6          (1 << 3)
#define RSS_HASHTYPE_RSS_TCP_IPV6      (1 << 4)
#define RSS_HASHTYPE_RSS_IPV6_EX       (1 << 5)
#define RSS_HASHTYPE_RSS_TCP_IPV6_EX   (1 << 6)
#define RSS_HASHTYPE_RSS_UDP_IPV4      (1 << 7)
#define RSS_HASHTYPE_RSS_UDP_IPV6      (1 << 9)
#define RSS_HASHTYPE_RSS_UDP_IPV6_EX   (1 << 10)

#define rss_getcpu(_a) 0
#define rss_getnumbuckets() 1
#define rss_getkey(_a)
#define rss_get_indirection_to_bucket(_a) 0
#define rss_gethashconfig() 0x7E
#define rss_hash2bucket(_a,_b,_c) -1

#endif
#endif /* _IXGBE_RSS_H_ */
