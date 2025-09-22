/*	$OpenBSD: ifconfig.h,v 1.6 2025/01/06 17:49:29 denis Exp $	*/

/*
 * Copyright (c) 2009 Claudio Jeker <claudio@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

extern int aflag;
extern int ifaliases;
extern int sock;
extern char ifname[IFNAMSIZ];

void printb(char *, unsigned int, unsigned char *);

void setdiscover(const char *, int);
void unsetdiscover(const char *, int);
void setblocknonip(const char *, int);
void unsetblocknonip(const char *, int);
void setlearn(const char *, int);
void unsetlearn(const char *, int);
void setstp(const char *, int);
void unsetstp(const char *, int);
void setedge(const char *, int);
void unsetedge(const char *, int);
void setautoedge(const char *, int);
void unsetautoedge(const char *, int);
void setptp(const char *, int);
void unsetptp(const char *, int);
void setautoptp(const char *, int);
void unsetautoptp(const char *, int);
void addlocal(const char *, int);

void bridge_add(const char *, int);
void bridge_delete(const char *, int);
void bridge_addspan(const char *, int);
void bridge_delspan(const char *, int);
void bridge_flush(const char *, int);
void bridge_flushall(const char *, int);
void bridge_addaddr(const char *, const char *);
void bridge_addendpoint(const char *, const char *);
void bridge_delendpoint(const char *, int);
void bridge_deladdr(const char *, int);
void bridge_maxaddr(const char *, int);
void bridge_addrs(const char *, int);
void bridge_hellotime(const char *, int);
void bridge_fwddelay(const char *, int);
void bridge_maxage(const char *, int);
void bridge_protect(const char *, const char *);
void bridge_unprotect(const char *, int);
void bridge_proto(const char *, int);
void bridge_ifprio(const char *, const char *);
void bridge_ifcost(const char *, const char *);
void bridge_noifcost(const char *, int);
void bridge_timeout(const char *, int);
void bridge_holdcnt(const char *, int);
void bridge_priority(const char *, int);
void bridge_rules(const char *, int);
void bridge_rulefile(const char *, int);
void bridge_flushrule(const char *, int);
int is_bridge(void);
void bridge_status(void);
int bridge_rule(int, char **, int);

int if_sff_info(int);
