/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Brian Somers <brian@Awfulhak.org>
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

#ifdef __i386__		/* Do any other archs not care about alignment ? */

#  define ua_htonl(src, tgt) (*(u_int32_t *)(tgt) = htonl(*(u_int32_t *)(src)))
#  define ua_ntohl(src, tgt) (*(u_int32_t *)(tgt) = ntohl(*(u_int32_t *)(src)))
#  define ua_htons(src, tgt) (*(u_int16_t *)(tgt) = htons(*(u_int16_t *)(src)))
#  define ua_ntohs(src, tgt) (*(u_int16_t *)(tgt) = ntohs(*(u_int16_t *)(src)))

#else	/* We care about alignment (or else drop a core !) */

#  define ua_htonl(src, tgt)				\
    do {						\
      u_int32_t __oh;					\
      memcpy(&__oh, (src), sizeof __oh);		\
      *(u_char *)(tgt) = __oh >> 24;			\
      *((u_char *)(tgt) + 1) = (__oh >> 16) & 0xff;	\
      *((u_char *)(tgt) + 2) = (__oh >> 8) & 0xff;	\
      *((u_char *)(tgt) + 3) = __oh & 0xff;		\
    } while (0)

#  define ua_ntohl(src, tgt)				\
    do {						\
      u_int32_t __nh;					\
      __nh = ((u_int32_t)*(u_char *)(src) << 24) |	\
          ((u_int32_t)*((u_char *)(src) + 1) << 16) |	\
          ((u_int32_t)*((u_char *)(src) + 2) << 8) |	\
          (u_int32_t)*((u_char *)(src) + 3);		\
      memcpy((tgt), &__nh, sizeof __nh);		\
    } while (0)

#  define ua_htons(src, tgt)				\
    do {						\
      u_int16_t __oh;					\
      memcpy(&__oh, (src), sizeof __oh);		\
      *(u_char *)(tgt) = __oh >> 8;			\
      *((u_char *)(tgt) + 1) = __oh & 0xff;		\
    } while (0)

#  define ua_ntohs(src, tgt)				\
    do {						\
      u_int16_t __nh;					\
      __nh = ((u_int16_t)*(u_char *)(src) << 8) |	\
          (u_int16_t)*((u_char *)(src) + 1);		\
      memcpy((tgt), &__nh, sizeof __nh);		\
    } while (0)

#endif
