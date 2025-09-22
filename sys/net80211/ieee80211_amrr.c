/*	$OpenBSD: ieee80211_amrr.c,v 1.12 2019/02/24 09:36:28 stsp Exp $	*/

/*-
 * Copyright (c) 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_amrr.h>

#define is_success(amn)	\
	((amn)->amn_retrycnt < (amn)->amn_txcnt / 10)
#define is_failure(amn)	\
	((amn)->amn_retrycnt > (amn)->amn_txcnt / 3)
#define is_enough(amn)		\
	((amn)->amn_txcnt > 10)
#define reset_cnt(amn)		\
	do { (amn)->amn_txcnt = (amn)->amn_retrycnt = 0; } while (0)

static inline int
is_min_rate(struct ieee80211_node *ni)
{
	return (ni->ni_txrate == 0);
}

static inline int
is_max_rate(struct ieee80211_node *ni)
{
	return (ni->ni_txrate == ni->ni_rates.rs_nrates - 1);
}

static inline void
increase_rate(struct ieee80211_node *ni)
{
	ni->ni_txrate++;
}

static inline void
decrease_rate(struct ieee80211_node *ni)
{
	ni->ni_txrate--;
}

void
ieee80211_amrr_node_init(const struct ieee80211_amrr *amrr,
    struct ieee80211_amrr_node *amn)
{
	amn->amn_success = 0;
	amn->amn_recovery = 0;
	amn->amn_txcnt = amn->amn_retrycnt = 0;
	amn->amn_success_threshold = amrr->amrr_min_success_threshold;
}

/*
 * Update ni->ni_txrate.
 */
void
ieee80211_amrr_choose(struct ieee80211_amrr *amrr, struct ieee80211_node *ni,
    struct ieee80211_amrr_node *amn)
{
#define RV(rate)	((rate) & IEEE80211_RATE_VAL)
	int need_change = 0;

	if (is_success(amn) && is_enough(amn)) {
		amn->amn_success++;
		if (amn->amn_success >= amn->amn_success_threshold &&
		    !is_max_rate(ni)) {
			amn->amn_recovery = 1;
			amn->amn_success = 0;
			increase_rate(ni);
			DPRINTF(("increase rate=%d,#tx=%d,#retries=%d\n",
			    RV(ni->ni_rates.rs_rates[ni->ni_txrate]),
			    amn->amn_txcnt, amn->amn_retrycnt));
			need_change = 1;
		} else {
			amn->amn_recovery = 0;
		}
	} else if (is_failure(amn)) {
		amn->amn_success = 0;
		if (!is_min_rate(ni)) {
			if (amn->amn_recovery) {
				amn->amn_success_threshold *= 2;
				if (amn->amn_success_threshold >
				    amrr->amrr_max_success_threshold)
					amn->amn_success_threshold =
					    amrr->amrr_max_success_threshold;
			} else {
				amn->amn_success_threshold =
				    amrr->amrr_min_success_threshold;
			}
			decrease_rate(ni);
			DPRINTF(("decrease rate=%d,#tx=%d,#retries=%d\n",
			    RV(ni->ni_rates.rs_rates[ni->ni_txrate]),
			    amn->amn_txcnt, amn->amn_retrycnt));
			need_change = 1;
		}
		amn->amn_recovery = 0;
	}

	if (is_enough(amn) || need_change)
		reset_cnt(amn);
#undef RV
}
