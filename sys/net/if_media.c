/*	$NetBSD: if_media.c,v 1.1 1997/03/17 02:55:15 thorpej Exp $	*/
/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997
 *	Jonathan Stone and Jason R. Thorpe.  All rights reserved.
 *
 * This software is derived from information provided by Matt Thomas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Jonathan Stone
 *	and Jason R. Thorpe for the NetBSD Project.
 * 4. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * BSD/OS-compatible network interface media selection.
 *
 * Where it is safe to do so, this code strays slightly from the BSD/OS
 * design.  Software which uses the API (device drivers, basically)
 * shouldn't notice any difference.
 *
 * Many thanks to Matt Thomas for providing the information necessary
 * to implement this interface.
 */

#include "opt_ifmedia.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_media.h>

/*
 * Compile-time options:
 * IFMEDIA_DEBUG:
 *	turn on implementation-level debug printfs.
 * 	Useful for debugging newly-ported  drivers.
 */

static struct ifmedia_entry *ifmedia_match(struct ifmedia *ifm,
    int flags, int mask);

#ifdef IFMEDIA_DEBUG
#include <net/if_var.h>
int	ifmedia_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, ifmedia, CTLFLAG_RW, &ifmedia_debug,
	    0, "if_media debugging msgs");
static	void ifmedia_printword(int);
#endif

/*
 * Initialize if_media struct for a specific interface instance.
 */
void
ifmedia_init(ifm, dontcare_mask, change_callback, status_callback)
	struct ifmedia *ifm;
	int dontcare_mask;
	ifm_change_cb_t change_callback;
	ifm_stat_cb_t status_callback;
{

	LIST_INIT(&ifm->ifm_list);
	ifm->ifm_cur = NULL;
	ifm->ifm_media = 0;
	ifm->ifm_mask = dontcare_mask;		/* IF don't-care bits */
	ifm->ifm_change = change_callback;
	ifm->ifm_status = status_callback;
}

void
ifmedia_removeall(ifm)
	struct ifmedia *ifm;
{
	struct ifmedia_entry *entry;

	for (entry = LIST_FIRST(&ifm->ifm_list); entry;
	     entry = LIST_FIRST(&ifm->ifm_list)) {
		LIST_REMOVE(entry, ifm_list);
		free(entry, M_IFADDR);
	}
	ifm->ifm_cur = NULL;
}

/*
 * Add a media configuration to the list of supported media
 * for a specific interface instance.
 */
void
ifmedia_add(ifm, mword, data, aux)
	struct ifmedia *ifm;
	int mword;
	int data;
	void *aux;
{
	struct ifmedia_entry *entry;

#ifdef IFMEDIA_DEBUG
	if (ifmedia_debug) {
		if (ifm == NULL) {
			printf("ifmedia_add: null ifm\n");
			return;
		}
		printf("Adding entry for ");
		ifmedia_printword(mword);
	}
#endif

	entry = malloc(sizeof(*entry), M_IFADDR, M_NOWAIT);
	if (entry == NULL)
		panic("ifmedia_add: can't malloc entry");

	entry->ifm_media = mword;
	entry->ifm_data = data;
	entry->ifm_aux = aux;

	LIST_INSERT_HEAD(&ifm->ifm_list, entry, ifm_list);
}

/*
 * Add an array of media configurations to the list of
 * supported media for a specific interface instance.
 */
void
ifmedia_list_add(ifm, lp, count)
	struct ifmedia *ifm;
	struct ifmedia_entry *lp;
	int count;
{
	int i;

	for (i = 0; i < count; i++)
		ifmedia_add(ifm, lp[i].ifm_media, lp[i].ifm_data,
		    lp[i].ifm_aux);
}

/*
 * Set the default active media. 
 *
 * Called by device-specific code which is assumed to have already
 * selected the default media in hardware.  We do _not_ call the
 * media-change callback.
 */
void
ifmedia_set(ifm, target)
	struct ifmedia *ifm; 
	int target;

{
	struct ifmedia_entry *match;

	match = ifmedia_match(ifm, target, ifm->ifm_mask);

	if (match == NULL) {
		printf("ifmedia_set: no match for 0x%x/0x%x\n",
		    target, ~ifm->ifm_mask);
		panic("ifmedia_set");
	}
	ifm->ifm_cur = match;

#ifdef IFMEDIA_DEBUG
	if (ifmedia_debug) {
		printf("ifmedia_set: target ");
		ifmedia_printword(target);
		printf("ifmedia_set: setting to ");
		ifmedia_printword(ifm->ifm_cur->ifm_media);
	}
#endif
}

/*
 * Given a media word, return one suitable for an application
 * using the original encoding.
 */
static int
compat_media(int media)
{

	if (IFM_TYPE(media) == IFM_ETHER && IFM_SUBTYPE(media) > IFM_OTHER) {
		media &= ~(IFM_ETH_XTYPE|IFM_TMASK);
		media |= IFM_OTHER;
	}
	return (media);
}

/*
 * Device-independent media ioctl support function.
 */
int
ifmedia_ioctl(ifp, ifr, ifm, cmd)
	struct ifnet *ifp;
	struct ifreq *ifr;
	struct ifmedia *ifm;
	u_long cmd;
{
	struct ifmedia_entry *match;
	struct ifmediareq *ifmr = (struct ifmediareq *) ifr;
	int error = 0;

	if (ifp == NULL || ifr == NULL || ifm == NULL)
		return(EINVAL);

	switch (cmd) {

	/*
	 * Set the current media.
	 */
	case  SIOCSIFMEDIA:
	{
		struct ifmedia_entry *oldentry;
		int oldmedia;
		int newmedia = ifr->ifr_media;

		match = ifmedia_match(ifm, newmedia, ifm->ifm_mask);
		if (match == NULL) {
#ifdef IFMEDIA_DEBUG
			if (ifmedia_debug) {
				printf(
				    "ifmedia_ioctl: no media found for 0x%x\n", 
				    newmedia);
			}
#endif
			return (ENXIO);
		}

		/*
		 * If no change, we're done.
		 * XXX Automedia may invole software intervention.
		 *     Keep going in case the connected media changed.
		 *     Similarly, if best match changed (kernel debugger?).
		 */
		if ((IFM_SUBTYPE(newmedia) != IFM_AUTO) &&
		    (newmedia == ifm->ifm_media) &&
		    (match == ifm->ifm_cur))
			return 0;

		/*
		 * We found a match, now make the driver switch to it.
		 * Make sure to preserve our old media type in case the
		 * driver can't switch.
		 */
#ifdef IFMEDIA_DEBUG
		if (ifmedia_debug) {
			printf("ifmedia_ioctl: switching %s to ",
			    ifp->if_xname);
			ifmedia_printword(match->ifm_media);
		}
#endif
		oldentry = ifm->ifm_cur;
		oldmedia = ifm->ifm_media;
		ifm->ifm_cur = match;
		ifm->ifm_media = newmedia;
		error = (*ifm->ifm_change)(ifp);
		if (error) {
			ifm->ifm_cur = oldentry;
			ifm->ifm_media = oldmedia;
		}
		break;
	}

	/*
	 * Get list of available media and current media on interface.
	 */
	case  SIOCGIFMEDIA: 
	case  SIOCGIFXMEDIA: 
	{
		struct ifmedia_entry *ep;
		int i;

		if (ifmr->ifm_count < 0)
			return (EINVAL);

		if (cmd == SIOCGIFMEDIA) {
			ifmr->ifm_active = ifmr->ifm_current = ifm->ifm_cur ?
			    compat_media(ifm->ifm_cur->ifm_media) : IFM_NONE;
		} else {
			ifmr->ifm_active = ifmr->ifm_current = ifm->ifm_cur ?
			    ifm->ifm_cur->ifm_media : IFM_NONE;
		}
		ifmr->ifm_mask = ifm->ifm_mask;
		ifmr->ifm_status = 0;
		(*ifm->ifm_status)(ifp, ifmr);

		/*
		 * If there are more interfaces on the list, count
		 * them.  This allows the caller to set ifmr->ifm_count
		 * to 0 on the first call to know how much space to
		 * allocate.
		 */
		i = 0;
		LIST_FOREACH(ep, &ifm->ifm_list, ifm_list)
			if (i++ < ifmr->ifm_count) {
				error = copyout(&ep->ifm_media,
				    ifmr->ifm_ulist + i - 1, sizeof(int));
				if (error)
					break;
			}
		if (error == 0 && i > ifmr->ifm_count)
			error = ifmr->ifm_count ? E2BIG : 0;
		ifmr->ifm_count = i;
		break;
	}

	default:
		return (EINVAL);
	}

	return (error);
}

/*
 * Find media entry matching a given ifm word.
 *
 */
static struct ifmedia_entry *
ifmedia_match(ifm, target, mask)
	struct ifmedia *ifm; 
	int target;
	int mask;
{
	struct ifmedia_entry *match, *next;

	match = NULL;
	mask = ~mask;

	LIST_FOREACH(next, &ifm->ifm_list, ifm_list) {
		if ((next->ifm_media & mask) == (target & mask)) {
#if defined(IFMEDIA_DEBUG) || defined(DIAGNOSTIC)
			if (match) {
				printf("ifmedia_match: multiple match for "
				    "0x%x/0x%x\n", target, mask);
			}
#endif
			match = next;
		}
	}

	return match;
}

/*
 * Compute the interface `baudrate' from the media, for the interface
 * metrics (used by routing daemons).
 */
static const struct ifmedia_baudrate ifmedia_baudrate_descriptions[] =   
    IFM_BAUDRATE_DESCRIPTIONS;

uint64_t
ifmedia_baudrate(int mword)
{
	int i;

	for (i = 0; ifmedia_baudrate_descriptions[i].ifmb_word != 0; i++) {
		if (IFM_TYPE_MATCH(mword, ifmedia_baudrate_descriptions[i].ifmb_word))
			return (ifmedia_baudrate_descriptions[i].ifmb_baudrate);
	}

	/* Not known. */
	return (0);
}
 
#ifdef IFMEDIA_DEBUG
struct ifmedia_description ifm_type_descriptions[] =
    IFM_TYPE_DESCRIPTIONS;

struct ifmedia_description ifm_subtype_ethernet_descriptions[] =
    IFM_SUBTYPE_ETHERNET_DESCRIPTIONS;

struct ifmedia_description ifm_subtype_ethernet_option_descriptions[] =
    IFM_SUBTYPE_ETHERNET_OPTION_DESCRIPTIONS;

struct ifmedia_description ifm_subtype_ieee80211_descriptions[] =
    IFM_SUBTYPE_IEEE80211_DESCRIPTIONS;

struct ifmedia_description ifm_subtype_ieee80211_option_descriptions[] =
    IFM_SUBTYPE_IEEE80211_OPTION_DESCRIPTIONS;

struct ifmedia_description ifm_subtype_ieee80211_mode_descriptions[] =
    IFM_SUBTYPE_IEEE80211_MODE_DESCRIPTIONS;

struct ifmedia_description ifm_subtype_atm_descriptions[] =
    IFM_SUBTYPE_ATM_DESCRIPTIONS;

struct ifmedia_description ifm_subtype_atm_option_descriptions[] =
    IFM_SUBTYPE_ATM_OPTION_DESCRIPTIONS;

struct ifmedia_description ifm_subtype_shared_descriptions[] =
    IFM_SUBTYPE_SHARED_DESCRIPTIONS;

struct ifmedia_description ifm_shared_option_descriptions[] =
    IFM_SHARED_OPTION_DESCRIPTIONS;

struct ifmedia_type_to_subtype {
	struct ifmedia_description *subtypes;
	struct ifmedia_description *options;
	struct ifmedia_description *modes;
};

/* must be in the same order as IFM_TYPE_DESCRIPTIONS */
struct ifmedia_type_to_subtype ifmedia_types_to_subtypes[] = {
	{
	  &ifm_subtype_ethernet_descriptions[0],
	  &ifm_subtype_ethernet_option_descriptions[0],
	  NULL,
	},
	{
	  &ifm_subtype_ieee80211_descriptions[0],
	  &ifm_subtype_ieee80211_option_descriptions[0],
	  &ifm_subtype_ieee80211_mode_descriptions[0]
	},
	{
	  &ifm_subtype_atm_descriptions[0],
	  &ifm_subtype_atm_option_descriptions[0],
	  NULL,
	},
};

/*
 * print a media word.
 */
static void
ifmedia_printword(ifmw)
	int ifmw;
{
	struct ifmedia_description *desc;
	struct ifmedia_type_to_subtype *ttos;
	int seen_option = 0;

	/* Find the top-level interface type. */
	for (desc = ifm_type_descriptions, ttos = ifmedia_types_to_subtypes;
	    desc->ifmt_string != NULL; desc++, ttos++)
		if (IFM_TYPE(ifmw) == desc->ifmt_word)
			break;
	if (desc->ifmt_string == NULL) {
		printf("<unknown type>\n");
		return;
	}
	printf("%s", desc->ifmt_string);

	/* Any mode. */
	for (desc = ttos->modes; desc && desc->ifmt_string != NULL; desc++)
		if (IFM_MODE(ifmw) == desc->ifmt_word) {
			if (desc->ifmt_string != NULL)
				printf(" mode %s", desc->ifmt_string);
			break;
		}

	/*
	 * Check for the shared subtype descriptions first, then the
	 * type-specific ones.
	 */
	for (desc = ifm_subtype_shared_descriptions;
	    desc->ifmt_string != NULL; desc++)
		if (IFM_SUBTYPE(ifmw) == desc->ifmt_word)
			goto got_subtype;

	for (desc = ttos->subtypes; desc->ifmt_string != NULL; desc++)
		if (IFM_SUBTYPE(ifmw) == desc->ifmt_word)
			break;
	if (desc->ifmt_string == NULL) {
		printf(" <unknown subtype>\n");
		return;
	}

 got_subtype:
	printf(" %s", desc->ifmt_string);

	/*
	 * Look for shared options.
	 */
	for (desc = ifm_shared_option_descriptions;
	    desc->ifmt_string != NULL; desc++) {
		if (ifmw & desc->ifmt_word) {
			if (seen_option == 0)
				printf(" <");
			printf("%s%s", seen_option++ ? "," : "",
			    desc->ifmt_string);
		}
	}

	/*
	 * Look for subtype-specific options.
	 */
	for (desc = ttos->options; desc->ifmt_string != NULL; desc++) {
		if (ifmw & desc->ifmt_word) {
			if (seen_option == 0)
				printf(" <");
			printf("%s%s", seen_option++ ? "," : "",
			    desc->ifmt_string); 
		}
	}
	printf("%s\n", seen_option ? ">" : "");
}
#endif /* IFMEDIA_DEBUG */
