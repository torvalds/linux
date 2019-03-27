/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_cam.h>
#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_task.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>


void
rtwn_init_cam(struct rtwn_softc *sc)
{
	/* Invalidate all CAM entries. */
	rtwn_write_4(sc, R92C_CAMCMD,
	    R92C_CAMCMD_POLLING | R92C_CAMCMD_CLR);
}

static int
rtwn_cam_write(struct rtwn_softc *sc, uint32_t addr, uint32_t data)
{
	int error;

	error = rtwn_write_4(sc, R92C_CAMWRITE, data);
	if (error != 0)
		return (error);
	error = rtwn_write_4(sc, R92C_CAMCMD,
	    R92C_CAMCMD_POLLING | R92C_CAMCMD_WRITE |
	    SM(R92C_CAMCMD_ADDR, addr));

	return (error);
}

void
rtwn_init_seccfg(struct rtwn_softc *sc)
{
	uint16_t seccfg;

	/* Select decryption / encryption flags. */
	seccfg = 0;
	switch (sc->sc_hwcrypto) {
	case RTWN_CRYPTO_SW:
		break;	/* nothing to do */
	case RTWN_CRYPTO_PAIR:
		/* NB: TXUCKEY_DEF / RXUCKEY_DEF are required for RTL8192C */
		seccfg = R92C_SECCFG_TXUCKEY_DEF | R92C_SECCFG_RXUCKEY_DEF |
		    R92C_SECCFG_TXENC_ENA | R92C_SECCFG_RXDEC_ENA |
		    R92C_SECCFG_MC_SRCH_DIS;
		break;
	case RTWN_CRYPTO_FULL:
		seccfg = R92C_SECCFG_TXUCKEY_DEF | R92C_SECCFG_RXUCKEY_DEF |
		    R92C_SECCFG_TXENC_ENA | R92C_SECCFG_RXDEC_ENA |
		    R92C_SECCFG_TXBCKEY_DEF | R92C_SECCFG_RXBCKEY_DEF;
		break;
	default:
		KASSERT(0, ("%s: case %d was not handled\n", __func__,
		    sc->sc_hwcrypto));
		break;
	}

	RTWN_DPRINTF(sc, RTWN_DEBUG_KEY, "%s: seccfg %04X, hwcrypto %d\n",
	    __func__, seccfg, sc->sc_hwcrypto);

	rtwn_write_2(sc, R92C_SECCFG, seccfg);
}

int
rtwn_key_alloc(struct ieee80211vap *vap, struct ieee80211_key *k,
    ieee80211_keyix *keyix, ieee80211_keyix *rxkeyix)
{
	struct rtwn_softc *sc = vap->iv_ic->ic_softc;
	int i, start;

	if (&vap->iv_nw_keys[0] <= k &&
	    k < &vap->iv_nw_keys[IEEE80211_WEP_NKID]) {
#if __FreeBSD_version > 1200018
		*keyix = ieee80211_crypto_get_key_wepidx(vap, k);
#else
		*keyix = k - vap->iv_nw_keys;
#endif
		if (sc->sc_hwcrypto != RTWN_CRYPTO_FULL)
			k->wk_flags |= IEEE80211_KEY_SWCRYPT;
		else {
			RTWN_LOCK(sc);
			if (isset(sc->keys_bmap, *keyix)) {
				device_printf(sc->sc_dev,
				    "%s: group key slot %d is already used!\n",
				    __func__, *keyix);
				/* XXX recover? */
				RTWN_UNLOCK(sc);
				return (0);
			}

			setbit(sc->keys_bmap, *keyix);
			RTWN_UNLOCK(sc);
		}

		goto end;
	}

	start = sc->cam_entry_limit;
	switch (sc->sc_hwcrypto) {
	case RTWN_CRYPTO_SW:
		k->wk_flags |= IEEE80211_KEY_SWCRYPT;
		*keyix = 0;
		goto end;
	case RTWN_CRYPTO_PAIR:
		/* all slots for pairwise keys. */
		start = 0;
		RTWN_LOCK(sc);
		if (sc->sc_flags & RTWN_FLAG_CAM_FIXED)
			start = 4;
		RTWN_UNLOCK(sc);
		break;
	case RTWN_CRYPTO_FULL:
		/* first 4 - for group keys, others for pairwise. */
		start = 4;
		break;
	default:
		KASSERT(0, ("%s: case %d was not handled!\n",
		    __func__, sc->sc_hwcrypto));
		break;
	}

	RTWN_LOCK(sc);
	for (i = start; i < sc->cam_entry_limit; i++) {
		if (isclr(sc->keys_bmap, i)) {
			setbit(sc->keys_bmap, i);
			*keyix = i;
			break;
		}
	}
	RTWN_UNLOCK(sc);
	if (i == sc->cam_entry_limit) {
#if __FreeBSD_version > 1200008
		/* XXX check and remove keys with the same MAC address */
		k->wk_flags |= IEEE80211_KEY_SWCRYPT;
		*keyix = 0;
#else
		device_printf(sc->sc_dev,
		    "%s: no free space in the key table\n", __func__);
		return (0);
#endif
	}

end:
	*rxkeyix = *keyix;
	return (1);
}

static int
rtwn_key_set_cb0(struct rtwn_softc *sc, const struct ieee80211_key *k)
{
	uint8_t algo, keyid;
	int i, error;

	if (sc->sc_hwcrypto == RTWN_CRYPTO_FULL &&
	    k->wk_keyix < IEEE80211_WEP_NKID)
		keyid = k->wk_keyix;
	else
		keyid = 0;

	/* Map net80211 cipher to HW crypto algorithm. */
	switch (k->wk_cipher->ic_cipher) {
	case IEEE80211_CIPHER_WEP:
		if (k->wk_keylen < 8)
			algo = R92C_CAM_ALGO_WEP40;
		else
			algo = R92C_CAM_ALGO_WEP104;
		break;
	case IEEE80211_CIPHER_TKIP:
		algo = R92C_CAM_ALGO_TKIP;
		break;
	case IEEE80211_CIPHER_AES_CCM:
		algo = R92C_CAM_ALGO_AES;
		break;
	default:
		device_printf(sc->sc_dev, "%s: unknown cipher %u\n",
		    __func__, k->wk_cipher->ic_cipher);
		return (EINVAL);
	}

	RTWN_DPRINTF(sc, RTWN_DEBUG_KEY,
	    "%s: keyix %u, keyid %u, algo %u/%u, flags %04X, len %u, "
	    "macaddr %s\n", __func__, k->wk_keyix, keyid,
	    k->wk_cipher->ic_cipher, algo, k->wk_flags, k->wk_keylen,
	    ether_sprintf(k->wk_macaddr));

	/* Clear high bits. */
	rtwn_cam_write(sc, R92C_CAM_CTL6(k->wk_keyix), 0);
	rtwn_cam_write(sc, R92C_CAM_CTL7(k->wk_keyix), 0);

	/* Write key. */
	for (i = 0; i < 4; i++) {
		error = rtwn_cam_write(sc, R92C_CAM_KEY(k->wk_keyix, i),
		    le32dec(&k->wk_key[i * 4]));
		if (error != 0)
			goto fail;
	}

	/* Write CTL0 last since that will validate the CAM entry. */
	error = rtwn_cam_write(sc, R92C_CAM_CTL1(k->wk_keyix),
	    le32dec(&k->wk_macaddr[2]));
	if (error != 0)
		goto fail;
	error = rtwn_cam_write(sc, R92C_CAM_CTL0(k->wk_keyix),
	    SM(R92C_CAM_ALGO, algo) |
	    SM(R92C_CAM_KEYID, keyid) |
	    SM(R92C_CAM_MACLO, le16dec(&k->wk_macaddr[0])) |
	    R92C_CAM_VALID);
	if (error != 0)
		goto fail;

	return (0);

fail:
	device_printf(sc->sc_dev, "%s fails, error %d\n", __func__, error);
	return (error);
}

static void
rtwn_key_set_cb(struct rtwn_softc *sc, union sec_param *data)
{
	const struct ieee80211_key *k = &data->key;

	(void) rtwn_key_set_cb0(sc, k);
}

int
rtwn_init_static_keys(struct rtwn_softc *sc, struct rtwn_vap *rvp)
{
	int i, error;

	if (sc->sc_hwcrypto != RTWN_CRYPTO_FULL)
		return (0);		/* nothing to do */

	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		const struct ieee80211_key *k = rvp->keys[i];
		if (k != NULL) {
			error = rtwn_key_set_cb0(sc, k);
			if (error != 0)
				return (error);
		}
	}

	return (0);
}

static void
rtwn_key_del_cb(struct rtwn_softc *sc, union sec_param *data)
{
	struct ieee80211_key *k = &data->key;
	int i;

	RTWN_DPRINTF(sc, RTWN_DEBUG_KEY,
	    "%s: keyix %u, flags %04X, macaddr %s\n", __func__,
	    k->wk_keyix, k->wk_flags, ether_sprintf(k->wk_macaddr));

	rtwn_cam_write(sc, R92C_CAM_CTL0(k->wk_keyix), 0);
	rtwn_cam_write(sc, R92C_CAM_CTL1(k->wk_keyix), 0);

	/* Clear key. */
	for (i = 0; i < 4; i++)
		rtwn_cam_write(sc, R92C_CAM_KEY(k->wk_keyix, i), 0);
	clrbit(sc->keys_bmap, k->wk_keyix);
}

static int
rtwn_process_key(struct ieee80211vap *vap, const struct ieee80211_key *k,
    int set)
{
	struct rtwn_softc *sc = vap->iv_ic->ic_softc;

	if (k->wk_flags & IEEE80211_KEY_SWCRYPT) {
		/* Not for us. */
		return (1);
	}

	if (&vap->iv_nw_keys[0] <= k &&
	    k < &vap->iv_nw_keys[IEEE80211_WEP_NKID]) {
#if __FreeBSD_version <= 1200008
		struct ieee80211_key *k1 = &vap->iv_nw_keys[k->wk_keyix];

		if (sc->sc_hwcrypto != RTWN_CRYPTO_FULL) {
			k1->wk_flags |= IEEE80211_KEY_SWCRYPT;
			return (k->wk_cipher->ic_setkey(k1));
		} else {
#else
		if (sc->sc_hwcrypto == RTWN_CRYPTO_FULL) {
#endif
			struct rtwn_vap *rvp = RTWN_VAP(vap);

			RTWN_LOCK(sc);
			rvp->keys[k->wk_keyix] = (set ? k : NULL);
			if ((sc->sc_flags & RTWN_RUNNING) == 0) {
				if (!set)
					clrbit(sc->keys_bmap, k->wk_keyix);
				RTWN_UNLOCK(sc);
				return (1);
			}
			RTWN_UNLOCK(sc);
		}
	}

	return (!rtwn_cmd_sleepable(sc, k, sizeof(*k),
	    set ? rtwn_key_set_cb : rtwn_key_del_cb));
}

int
rtwn_key_set(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	return (rtwn_process_key(vap, k, 1));
}

int
rtwn_key_delete(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	return (rtwn_process_key(vap, k, 0));
}
