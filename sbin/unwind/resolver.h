/*	$OpenBSD: resolver.h,v 1.17 2019/12/18 09:18:27 florian Exp $	*/

/*
 * Copyright (c) 2018 Florian Obser <florian@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
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

enum uw_resolver_state {
	DEAD,
	UNKNOWN,
	RESOLVING,
	VALIDATING
};

static const char * const	uw_resolver_state_str[] = {
	"dead",
	"unknown",
	"resolving",
	"validating"
};

static const int64_t		histogram_limits[] = {
	10,
	20,
	40,
	60,
	80,
	100,
	200,
	400,
	600,
	800,
	1000,
	INT64_MAX,
};

struct ctl_resolver_info {
	enum uw_resolver_state	 state;
	enum uw_resolver_type	 type;
	int64_t			 median;
	int64_t			 histogram[nitems(histogram_limits)];
	int64_t			 latest_histogram[nitems(histogram_limits)];
};

struct ctl_forwarder_info {
	char		 ip[INET6_ADDRSTRLEN];
	uint32_t	 if_index;
	int		 src;
};

struct ctl_mem_info {
	size_t		 msg_cache_used;
	size_t		 msg_cache_max;
	size_t		 rrset_cache_used;
	size_t		 rrset_cache_max;
	size_t		 key_cache_used;
	size_t		 key_cache_max;
	size_t		 neg_cache_used;
	size_t		 neg_cache_max;
};

void	 resolver(int, int);
int	 resolver_imsg_compose_main(int, pid_t, void *, uint16_t);
int	 resolver_imsg_compose_frontend(int, pid_t, void *, uint16_t);
