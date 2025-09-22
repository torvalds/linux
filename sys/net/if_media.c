/*	$OpenBSD: if_media.c,v 1.40 2025/07/07 02:28:50 jsg Exp $	*/
/*	$NetBSD: if_media.c,v 1.10 2000/03/13 23:52:39 soren Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <net/if.h>
#ifdef IFMEDIA_DEBUG
#include <net/if_var.h>
#endif
#include <net/if_media.h>

/*
 * Compile-time options:
 * IFMEDIA_DEBUG:
 *	turn on implementation-level debug printfs.
 *	Useful for debugging newly-ported  drivers.
 */

#ifdef IFMEDIA_DEBUG
int	ifmedia_debug = 0;
static	void ifmedia_printword(uint64_t);
#endif

struct mutex ifmedia_mtx = MUTEX_INITIALIZER(IPL_NET);

struct	ifmedia_entry *ifmedia_get(struct ifmedia *, uint64_t, uint64_t);

/*
 * Initialize if_media struct for a specific interface instance.
 */
void
ifmedia_init(struct ifmedia *ifm, uint64_t dontcare_mask,
    ifm_change_cb_t change_callback, ifm_stat_cb_t status_callback)
{
	TAILQ_INIT(&ifm->ifm_list);
	ifm->ifm_nwords = 0;
	ifm->ifm_cur = NULL;
	ifm->ifm_media = 0;
	ifm->ifm_mask = dontcare_mask;		/* IF don't-care bits */
	ifm->ifm_change_cb = change_callback;
	ifm->ifm_status_cb = status_callback;
}

/*
 * Add a media configuration to the list of supported media
 * for a specific interface instance.
 */
void
ifmedia_add(struct ifmedia *ifm, uint64_t mword, int data, void *aux)
{
	struct ifmedia_entry *entry;

#ifdef IFMEDIA_DEBUG
	if (ifmedia_debug) {
		if (ifm == NULL) {
			printf("%s: null ifm\n", __func__);
			return;
		}
		printf("%s: adding entry for ", __func__);
		ifmedia_printword(mword);
	}
#endif

	entry = malloc(sizeof(*entry), M_IFADDR, M_NOWAIT);
	if (entry == NULL)
		panic("ifmedia_add: can't malloc entry");

	entry->ifm_media = mword;
	entry->ifm_data = data;
	entry->ifm_aux = aux;

	mtx_enter(&ifmedia_mtx);
	TAILQ_INSERT_TAIL(&ifm->ifm_list, entry, ifm_list);
	ifm->ifm_nwords++;
	mtx_leave(&ifmedia_mtx);
}

/*
 * Set the default active media.
 *
 * Called by device-specific code which is assumed to have already
 * selected the default media in hardware.  We do _not_ call the
 * media-change callback.
 */
void
ifmedia_set(struct ifmedia *ifm, uint64_t target)
{
	struct ifmedia_entry *match;

	mtx_enter(&ifmedia_mtx);
	match = ifmedia_get(ifm, target, ifm->ifm_mask);

	/*
	 * If we didn't find the requested media, then we try to fall
	 * back to target-type (IFM_ETHER, e.g.) | IFM_NONE.  If that's
	 * not on the list, then we add it and set the media to it.
	 *
	 * Since ifmedia_set is almost always called with IFM_AUTO or
	 * with a known-good media, this really should only occur if we:
	 *
	 * a) didn't find any PHYs, or
	 * b) didn't find an autoselect option on the PHY when the
	 *    parent ethernet driver expected to.
	 *
	 * In either case, it makes sense to select no media.
	 */
	if (match == NULL) {
		printf("%s: no match for 0x%llx/0x%llx\n", __func__,
		    target, ~ifm->ifm_mask);
		target = (target & IFM_NMASK) | IFM_NONE;
		match = ifmedia_get(ifm, target, ifm->ifm_mask);
		if (match == NULL) {
			mtx_leave(&ifmedia_mtx);
			ifmedia_add(ifm, target, 0, NULL);
			mtx_enter(&ifmedia_mtx);
			match = ifmedia_get(ifm, target, ifm->ifm_mask);
			if (match == NULL) {
				mtx_leave(&ifmedia_mtx);
				panic("ifmedia_set failed");
			}
		}
	}
	ifm->ifm_cur = match;
	mtx_leave(&ifmedia_mtx);

#ifdef IFMEDIA_DEBUG
	if (ifmedia_debug) {
		printf("%s: target ", __func__);
		ifmedia_printword(target);
		printf("%s: setting to ", __func__);
		ifmedia_printword(ifm->ifm_cur->ifm_media);
	}
#endif
}

/*
 * Device-independent media ioctl support function.
 */
int
ifmedia_ioctl(struct ifnet *ifp, struct ifreq *ifr, struct ifmedia *ifm,
    u_long cmd)
{
	struct ifmedia_entry *match;
	int error = 0;

	if (ifp == NULL || ifr == NULL || ifm == NULL)
		return (EINVAL);

	switch (cmd) {

	/*
	 * Set the current media.
	 */
	case SIOCSIFMEDIA:
	{
		struct ifmedia_entry *oldentry;
		uint64_t oldmedia;
		uint64_t newmedia = ifr->ifr_media;

		mtx_enter(&ifmedia_mtx);
		match = ifmedia_get(ifm, newmedia, ifm->ifm_mask);
		if (match == NULL) {
			mtx_leave(&ifmedia_mtx);
#ifdef IFMEDIA_DEBUG
			if (ifmedia_debug) {
				printf("%s: no media found for 0x%llx\n",
				    __func__, newmedia);
			}
#endif
			return (EINVAL);
		}

		/*
		 * If no change, we're done.
		 * XXX Automedia may involve software intervention.
		 *     Keep going in case the connected media changed.
		 *     Similarly, if best match changed (kernel debugger?).
		 */
		if ((IFM_SUBTYPE(newmedia) != IFM_AUTO) &&
		    (newmedia == ifm->ifm_media) &&
		    (match == ifm->ifm_cur)) {
			mtx_leave(&ifmedia_mtx);
			return (0);
		}

		/*
		 * We found a match, now make the driver switch to it.
		 * Make sure to preserve our old media type in case the
		 * driver can't switch.
		 */
#ifdef IFMEDIA_DEBUG
		if (ifmedia_debug) {
			printf("%s: switching %s to ", __func__,
			    ifp->if_xname);
			ifmedia_printword(match->ifm_media);
		}
#endif
		oldentry = ifm->ifm_cur;
		oldmedia = ifm->ifm_media;
		ifm->ifm_cur = match;
		ifm->ifm_media = newmedia;
		mtx_leave(&ifmedia_mtx);

		error = (*ifm->ifm_change_cb)(ifp);
		if (error && error != ENETRESET) {
			mtx_enter(&ifmedia_mtx);
			if (ifm->ifm_cur == match) {
				ifm->ifm_cur = oldentry;
				ifm->ifm_media = oldmedia;
			}
			mtx_leave(&ifmedia_mtx);
		}
		break;
	}

	/*
	 * Get list of available media and current media on interface.
	 */
	case SIOCGIFMEDIA:
	{
		struct ifmediareq *ifmr = (struct ifmediareq *) ifr;
		size_t nwords;

		if (ifmr->ifm_count < 0)
			return (EINVAL);

		mtx_enter(&ifmedia_mtx);
		ifmr->ifm_active = ifmr->ifm_current = ifm->ifm_cur ?
		    ifm->ifm_cur->ifm_media : IFM_NONE;
		ifmr->ifm_mask = ifm->ifm_mask;
		ifmr->ifm_status = 0;
		mtx_leave(&ifmedia_mtx);

		(*ifm->ifm_status_cb)(ifp, ifmr);

		mtx_enter(&ifmedia_mtx);
		nwords = ifm->ifm_nwords;
		mtx_leave(&ifmedia_mtx);

		if (ifmr->ifm_count == 0) {
			ifmr->ifm_count = nwords;
			return (0);
		}

		while (1) {
			struct ifmedia_entry *ife;
			uint64_t *kptr;
			size_t ksiz;

			kptr = mallocarray(nwords, sizeof(*kptr), M_TEMP,
			    M_WAITOK | M_ZERO);
			ksiz = nwords * sizeof(*kptr);

			mtx_enter(&ifmedia_mtx);
			/* Media list might grow during malloc(). */
			if (nwords < ifm->ifm_nwords) {
				nwords = ifm->ifm_nwords;
				mtx_leave(&ifmedia_mtx);
				free(kptr, M_TEMP, ksiz);
				continue;
			}
			/* Request memory too small, set error and ifm_count. */
			if (ifmr->ifm_count < ifm->ifm_nwords) {
				nwords = ifm->ifm_nwords;
				mtx_leave(&ifmedia_mtx);
				free(kptr, M_TEMP, ksiz);
				error = E2BIG;
				break;
			}
			/*
			 * Get the media words from the interface's list.
			 */
			nwords = 0;
			TAILQ_FOREACH(ife, &ifm->ifm_list, ifm_list) {
				kptr[nwords++] = ife->ifm_media;
			}
			KASSERT(nwords == ifm->ifm_nwords);
			mtx_leave(&ifmedia_mtx);

			error = copyout(kptr, ifmr->ifm_ulist,
			    nwords * sizeof(*kptr));
			free(kptr, M_TEMP, ksiz);
			break;
		}
		ifmr->ifm_count = nwords;
		break;
	}

	default:
		return (ENOTTY);
	}

	return (error);
}

/*
 * Find media entry matching a given ifm word.  Return 1 if found.
 */
int
ifmedia_match(struct ifmedia *ifm, uint64_t target, uint64_t mask)
{
	struct ifmedia_entry *match;

	mtx_enter(&ifmedia_mtx);
	match = ifmedia_get(ifm, target, mask);
	mtx_leave(&ifmedia_mtx);

	return (match != NULL);
}

struct ifmedia_entry *
ifmedia_get(struct ifmedia *ifm, uint64_t target, uint64_t mask)
{
	struct ifmedia_entry *match, *next;

	MUTEX_ASSERT_LOCKED(&ifmedia_mtx);

	match = NULL;
	mask = ~mask;

	TAILQ_FOREACH(next, &ifm->ifm_list, ifm_list) {
		if ((next->ifm_media & mask) == (target & mask)) {
			if (match) {
#if defined(IFMEDIA_DEBUG) || defined(DIAGNOSTIC)
				printf("%s: multiple match for 0x%llx/0x%llx, "
				    "selected instance %lld\n", __func__,
				    target, mask, IFM_INST(match->ifm_media));
#endif
				break;
			}
			match = next;
		}
	}

	return (match);
}

/*
 * Delete all media for a given instance.
 */
void
ifmedia_delete_instance(struct ifmedia *ifm, uint64_t inst)
{
	struct ifmedia_entry *ife, *nife;
	struct ifmedia_list ifmlist;

	TAILQ_INIT(&ifmlist);

	mtx_enter(&ifmedia_mtx);
	TAILQ_FOREACH_SAFE(ife, &ifm->ifm_list, ifm_list, nife) {
		if (inst == IFM_INST_ANY ||
		    inst == IFM_INST(ife->ifm_media)) {
			TAILQ_REMOVE(&ifm->ifm_list, ife, ifm_list);
			ifm->ifm_nwords--;
			TAILQ_INSERT_TAIL(&ifmlist, ife, ifm_list);
		}
	}
	ifm->ifm_cur = NULL;
	mtx_leave(&ifmedia_mtx);

	/* Do not hold mutex longer than necessary, call free() without. */
	while((ife = TAILQ_FIRST(&ifmlist)) != NULL) {
		TAILQ_REMOVE(&ifmlist, ife, ifm_list);
		free(ife, M_IFADDR, sizeof *ife);
	}
}

/*
 * Compute the interface `baudrate' from the media, for the interface
 * metrics (used by routing daemons).
 */
const struct ifmedia_baudrate ifmedia_baudrate_descriptions[] =
    IFM_BAUDRATE_DESCRIPTIONS;

uint64_t
ifmedia_baudrate(uint64_t mword)
{
	int i;

	for (i = 0; ifmedia_baudrate_descriptions[i].ifmb_word != 0; i++) {
		if ((mword & (IFM_NMASK|IFM_TMASK)) ==
		    ifmedia_baudrate_descriptions[i].ifmb_word)
			return (ifmedia_baudrate_descriptions[i].ifmb_baudrate);
	}

	/* Not known. */
	return (0);
}

#ifdef IFMEDIA_DEBUG

const struct ifmedia_description ifm_type_descriptions[] =
    IFM_TYPE_DESCRIPTIONS;

const struct ifmedia_description ifm_subtype_descriptions[] =
    IFM_SUBTYPE_DESCRIPTIONS;

const struct ifmedia_description ifm_option_descriptions[] =
    IFM_OPTION_DESCRIPTIONS;

/*
 * print a media word.
 */
static void
ifmedia_printword(uint64_t ifmw)
{
	const struct ifmedia_description *desc;
	uint64_t seen_option = 0;

	/* Print the top-level interface type. */
	for (desc = ifm_type_descriptions; desc->ifmt_string != NULL;
	     desc++) {
		if (IFM_TYPE(ifmw) == desc->ifmt_word)
			break;
	}
	if (desc->ifmt_string == NULL)
		printf("<unknown type> ");
	else
		printf("%s ", desc->ifmt_string);

	/* Print the subtype. */
	for (desc = ifm_subtype_descriptions; desc->ifmt_string != NULL;
	     desc++) {
		if (IFM_TYPE_MATCH(desc->ifmt_word, ifmw) &&
		    IFM_SUBTYPE(desc->ifmt_word) == IFM_SUBTYPE(ifmw))
			break;
	}
	if (desc->ifmt_string == NULL)
		printf("<unknown subtype>");
	else
		printf("%s", desc->ifmt_string);

	/* Print any options. */
	for (desc = ifm_option_descriptions; desc->ifmt_string != NULL;
	     desc++) {
		if (IFM_TYPE_MATCH(desc->ifmt_word, ifmw) &&
		    (ifmw & desc->ifmt_word) != 0 &&
		    (seen_option & IFM_OPTIONS(desc->ifmt_word)) == 0) {
			if (seen_option == 0)
				printf(" <");
			printf("%s%s", seen_option ? "," : "",
			    desc->ifmt_string);
			seen_option |= IFM_OPTIONS(desc->ifmt_word);
		}
	}
	printf("%s\n", seen_option ? ">" : "");
}

#endif /* IFMEDIA_DEBUG */
