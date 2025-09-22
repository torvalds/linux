/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
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
	void	(*interface)(struct ctl_iface *, int);
	void	(*summary)(struct ctl_sum *);
	void	(*summary_area)(struct ctl_sum_area *);
	void	(*neighbor)(struct ctl_nbr *, int);
	void	(*rib)(struct ctl_rt *, int);
	void	(*fib)(struct kroute *);
	void	(*fib_interface)(struct kif *);
	void	(*db)(struct lsa *, struct in_addr, u_int8_t,
		    char *);
	void	(*db_simple)(struct lsa_hdr *, struct in_addr, u_int8_t,
		    char *);
	void	(*tail)(void);
};

extern const struct output show_output;

#define EOL0(flag)	((flag & F_CTL_SSV) ? ';' : '\n')

const char	*fmt_timeframe_core(time_t);
const char	*get_linkstate(uint8_t, int);
const char	*print_ospf_rtr_flags(u_int8_t);
const char	*print_ospf_options(u_int8_t);
uint64_t	 get_ifms_type(uint8_t);
const char	*get_media_descr(uint64_t);
const char	*print_baudrate(u_int64_t);
const char	*print_link(int);
char		*print_ls_type(u_int8_t);
const char	*log_id(u_int32_t );
const char	*log_adv_rtr(u_int32_t);
const char	*print_ospf_flags(u_int8_t);
char		*print_rtr_link_type(u_int8_t);
