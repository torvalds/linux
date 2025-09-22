/*	$OpenBSD: pfctl.h,v 1.65 2024/11/20 13:57:29 kirill Exp $ */

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _PFCTL_H_
#define _PFCTL_H_

#ifdef PFCTL_DEBUG
#define DBGPRINT(...)	fprintf(stderr, __VA_ARGS__)
#else
#define DBGPRINT(...)	(void)(0)
#endif

enum pfctl_show { PFCTL_SHOW_RULES, PFCTL_SHOW_LABELS, PFCTL_SHOW_NOTHING };

enum {	PFRB_TABLES = 1, PFRB_TSTATS, PFRB_ADDRS, PFRB_ASTATS,
	PFRB_IFACES, PFRB_TRANS, PFRB_MAX };
struct pfr_buffer {
	int	 pfrb_type;	/* type of content, see enum above */
	int	 pfrb_size;	/* number of objects in buffer */
	int	 pfrb_msize;	/* maximum number of objects in buffer */
	void	*pfrb_caddr;	/* malloc'ated memory area */
};
#define PFRB_FOREACH(var, buf)				\
	for ((var) = pfr_buf_next((buf), NULL);		\
	    (var) != NULL;				\
	    (var) = pfr_buf_next((buf), (var)))


struct pfr_anchoritem {
	SLIST_ENTRY(pfr_anchoritem)	pfra_sle;
	char	*pfra_anchorname;
};

struct pfr_uktable {
	struct pfr_ktable	pfrukt_kt;
	struct pfr_buffer	pfrukt_addrs;
	int			pfrukt_init_addr;
	SLIST_ENTRY(pfr_uktable)
				pfrukt_entry;
};

#define pfrukt_t	pfrukt_kt.pfrkt_ts.pfrts_t
#define pfrukt_name	pfrukt_kt.pfrkt_t.pfrt_name
#define pfrukt_anchor	pfrukt_kt.pfrkt_t.pfrt_anchor

extern struct pfr_ktablehead pfr_ktables;

SLIST_HEAD(pfr_anchors, pfr_anchoritem);

int	 pfr_clr_tables(struct pfr_table *, int *, int);
int	 pfr_add_tables(struct pfr_table *, int, int *, int);
int	 pfr_del_tables(struct pfr_table *, int, int *, int);
int	 pfr_get_tables(struct pfr_table *, struct pfr_table *, int *, int);
int	 pfr_get_tstats(struct pfr_table *, struct pfr_tstats *, int *, int);
int	 pfr_clr_tstats(struct pfr_table *, int, int *, int);
int	 pfr_clr_astats(struct pfr_table *, struct pfr_addr *, int, int *, int);
int	 pfr_clr_addrs(struct pfr_table *, int *, int);
int	 pfr_add_addrs(struct pfr_table *, struct pfr_addr *, int, int *, int);
int	 pfr_del_addrs(struct pfr_table *, struct pfr_addr *, int, int *, int);
int	 pfr_set_addrs(struct pfr_table *, struct pfr_addr *, int, int *,
	    int *, int *, int *, int);
int	 pfr_get_addrs(struct pfr_table *, struct pfr_addr *, int *, int);
int	 pfr_get_astats(struct pfr_table *, struct pfr_astats *, int *, int);
int	 pfr_tst_addrs(struct pfr_table *, struct pfr_addr *, int, int *, int);
int	 pfr_ina_define(struct pfr_table *, struct pfr_addr *, int, int *,
	    int *, int, int);
void	 pfr_buf_clear(struct pfr_buffer *);
int	 pfr_buf_add(struct pfr_buffer *, const void *);
void	*pfr_buf_next(struct pfr_buffer *, const void *);
int	 pfr_buf_grow(struct pfr_buffer *, int);
int	 pfr_buf_load(struct pfr_buffer *, char *, int, int);
char	*pf_strerror(int);
int	 pfi_get_ifaces(const char *, struct pfi_kif *, int *);

void	 pfctl_print_title(char *);
int	 pfctl_clear_tables(const char *, int);
void	 pfctl_show_tables(const char *, int);
int	 pfctl_table(int, char *[], char *, const char *, char *,
	    const char *, int);
void	 warn_duplicate_tables(const char *, const char *);
void	 pfctl_show_ifaces(const char *, int);
FILE	*pfctl_fopen(const char *, const char *);

void	 print_addr(struct pf_addr_wrap *, sa_family_t, int);
void	 print_addr_str(sa_family_t, struct pf_addr *);
void	 print_host(struct pf_addr *, u_int16_t p, sa_family_t, u_int16_t, const char *, int);
void	 print_seq(struct pfsync_state_peer *);
void	 print_state(struct pfsync_state *, int);

int	 pfctl_cmdline_symset(char *);
int	 pfctl_add_trans(struct pfr_buffer *, int, const char *);
u_int32_t
	 pfctl_get_ticket(struct pfr_buffer *, int, const char *);
int	 pfctl_trans(int, struct pfr_buffer *, u_long, int);

int	 pfctl_show_queues(int, const char *, int, int);

void	 pfctl_err(int, int, const char *, ...);
void	 pfctl_errx(int, int, const char *, ...);

#endif /* _PFCTL_H_ */
