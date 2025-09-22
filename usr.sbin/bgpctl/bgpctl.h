/*	$OpenBSD: bgpctl.h,v 1.25 2025/02/20 19:48:14 claudio Exp $ */

/*
 * Copyright (c) 2019 Claudio Jeker <claudio@openbsd.org>
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

struct parse_result;

struct output {
	void	(*head)(struct parse_result *);
	void	(*neighbor)(struct peer *, struct parse_result *);
	void	(*timer)(struct ctl_timer *);
	void	(*fib)(struct kroute_full *);
	void	(*fib_table)(struct ktable *);
	void	(*flowspec)(struct flowspec *);
	void	(*nexthop)(struct ctl_show_nexthop *);
	void	(*interface)(struct ctl_show_interface *);
	void	(*attr)(struct ibuf *, int, int);
	void	(*communities)(struct ibuf *, struct parse_result *);
	void	(*rib)(struct ctl_show_rib *, struct ibuf *,
		    struct parse_result *);
	void	(*rib_mem)(struct rde_memstats *);
	void	(*set)(struct ctl_show_set *);
	void	(*rtr)(struct ctl_show_rtr *);
	void	(*result)(u_int);
	void	(*tail)(void);
};

extern const struct output show_output, json_output, ometric_output;

#define EOL0(flag)	((flag & F_CTL_SSV) ? ';' : '\n')

time_t		 get_rel_monotime(monotime_t);
char		*fmt_peer(const char *, const struct bgpd_addr *, int);
const char	*fmt_monotime(monotime_t);
const char	*fmt_fib_flags(uint16_t);
const char	*fmt_origin(uint8_t, int);
const char	*fmt_flags(uint32_t, int);
const char	*fmt_ovs(uint8_t, int);
const char	*fmt_avs(uint8_t, int);
const char	*fmt_auth_method(enum auth_method);
const char	*fmt_mem(long long);
const char	*fmt_errstr(uint8_t, uint8_t);
const char	*fmt_attr(uint8_t, int);
const char	*fmt_community(uint16_t, uint16_t);
const char	*fmt_large_community(uint32_t, uint32_t, uint32_t);
const char	*fmt_ext_community(uint64_t);
const char	*fmt_set_type(struct ctl_show_set *);

#define MPLS_LABEL_OFFSET 12
