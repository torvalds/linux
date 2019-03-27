/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Brian Somers <brian@Awfulhak.org>
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

/*
 * These structures should be treated as opaque.
 */
struct ncprange {
  sa_family_t ncprange_family;
  union {
    struct {
      struct in_addr ipaddr;
      struct in_addr mask;
      int width;
    } ip4;
#ifndef NOINET6
    struct {
      struct in6_addr ipaddr;
      int width;
    } ip6;
#endif
  } u;
};

struct ncpaddr {
  sa_family_t ncpaddr_family;
  union {
    struct in_addr ip4addr;
#ifndef NOINET6
    struct in6_addr ip6addr;
#endif
  } u;
};

struct ncp;

extern void ncpaddr_init(struct ncpaddr *);
extern int ncpaddr_isset(const struct ncpaddr *);
extern int ncpaddr_isdefault(const struct ncpaddr *);
extern int ncpaddr_equal(const struct ncpaddr *, const struct ncpaddr *);
extern void ncpaddr_copy(struct ncpaddr *, const struct ncpaddr *);
extern void ncpaddr_setip4addr(struct ncpaddr *, u_int32_t);
extern int ncpaddr_getip4addr(const struct ncpaddr *, u_int32_t *);
extern void ncpaddr_setip4(struct ncpaddr *, struct in_addr);
extern int ncpaddr_getip4(const struct ncpaddr *, struct in_addr *);
#ifndef NOINET6
extern void ncpaddr_setip6(struct ncpaddr *, const struct in6_addr *);
extern int ncpaddr_getip6(const struct ncpaddr *, struct in6_addr *);
#endif
extern void ncpaddr_getsa(const struct ncpaddr *, struct sockaddr_storage *);
extern void ncpaddr_setsa(struct ncpaddr *, const struct sockaddr *);
extern const char *ncpaddr_ntoa(const struct ncpaddr *);
extern int ncpaddr_aton(struct ncpaddr *, struct ncp *, const char *);

extern void ncprange_init(struct ncprange *);
extern int ncprange_isset(const struct ncprange *);
extern int ncprange_equal(const struct ncprange *, const struct ncprange *);
extern int ncprange_isdefault(const struct ncprange *);
extern void ncprange_setdefault(struct ncprange *, int);
extern int ncprange_contains(const struct ncprange *, const struct ncpaddr *);
extern int ncprange_containsip4(const struct ncprange *, struct in_addr);
extern void ncprange_copy(struct ncprange *, const struct ncprange *);
extern void ncprange_set(struct ncprange *, const struct ncpaddr *, int);
extern void ncprange_sethost(struct ncprange *, const struct ncpaddr *);
extern int ncprange_ishost(const struct ncprange *);
extern int ncprange_setwidth(struct ncprange *, int);
extern void ncprange_setip4(struct ncprange *, struct in_addr, struct in_addr);
extern void ncprange_setip4host(struct ncprange *, struct in_addr);
extern int ncprange_setip4mask(struct ncprange *, struct in_addr);
extern void ncprange_setsa(struct ncprange *, const struct sockaddr *,
                           const struct sockaddr *);
extern void ncprange_getsa(const struct ncprange *, struct sockaddr_storage *,
                           struct sockaddr_storage *);
extern int ncprange_getaddr(const struct ncprange *, struct ncpaddr *);
extern int ncprange_getip4addr(const struct ncprange *, struct in_addr *);
extern int ncprange_getip4mask(const struct ncprange *, struct in_addr *);
extern int ncprange_getwidth(const struct ncprange *, int *);
extern const char *ncprange_ntoa(const struct ncprange *);
#ifndef NOINET6
extern int ncprange_scopeid(const struct ncprange *);
#endif
extern int ncprange_aton(struct ncprange *, struct ncp *, const char *);

#define ncpaddr_family(a) ((a)->ncpaddr_family)
#define ncprange_family(r) ((r)->ncprange_family)
