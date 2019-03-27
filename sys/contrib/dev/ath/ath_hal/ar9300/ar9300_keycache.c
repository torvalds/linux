/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"

#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"

/*
 * Note: The key cache hardware requires that each double-word
 * pair be written in even/odd order (since the destination is
 * a 64-bit register).  Don't reorder the writes in this code
 * w/o considering this!
 */
#define KEY_XOR         0xaa

#define IS_MIC_ENABLED(ah) \
    (AH9300(ah)->ah_sta_id1_defaults & AR_STA_ID1_CRPT_MIC_ENABLE)

/*
 * This isn't the keytable type; this is actually something separate
 * for the TX descriptor.
 */
static const int keyType[] = {
	1,	/* HAL_CIPHER_WEP */
	0,	/* HAL_CIPHER_AES_OCB */
	2,	/* HAL_CIPHER_AES_CCM */
	0,	/* HAL_CIPHER_CKIP */
	3,	/* HAL_CIPHER_TKIP */
	0	/* HAL_CIPHER_CLR */
};

/*
 * Return the size of the hardware key cache.
 */
u_int32_t
ar9300_get_key_cache_size(struct ath_hal *ah)
{
    return AH_PRIVATE(ah)->ah_caps.halKeyCacheSize;
}

/*
 * Return AH_TRUE if the specific key cache entry is valid.
 */
HAL_BOOL
ar9300_is_key_cache_entry_valid(struct ath_hal *ah, u_int16_t entry)
{
    if (entry < AH_PRIVATE(ah)->ah_caps.halKeyCacheSize) {
        u_int32_t val = OS_REG_READ(ah, AR_KEYTABLE_MAC1(entry));
        if (val & AR_KEYTABLE_VALID) {
            return AH_TRUE;
        }
    }
    return AH_FALSE;
}

/*
 * Clear the specified key cache entry and any associated MIC entry.
 */
HAL_BOOL
ar9300_reset_key_cache_entry(struct ath_hal *ah, u_int16_t entry)
{
    u_int32_t key_type;
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (entry >= AH_PRIVATE(ah)->ah_caps.halKeyCacheSize) {
        HALDEBUG(ah, HAL_DEBUG_KEYCACHE,
            "%s: entry %u out of range\n", __func__, entry);
        return AH_FALSE;
    }

    ahp->ah_keytype[entry] = keyType[HAL_CIPHER_CLR];

    key_type = OS_REG_READ(ah, AR_KEYTABLE_TYPE(entry));

    /* XXX why not clear key type/valid bit first? */
    OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), 0);
    OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), 0);
    OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), 0);
    OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), 0);
    OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), 0);
    OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), AR_KEYTABLE_TYPE_CLR);
    OS_REG_WRITE(ah, AR_KEYTABLE_MAC0(entry), 0);
    OS_REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), 0);
    if (key_type == AR_KEYTABLE_TYPE_TKIP && IS_MIC_ENABLED(ah)) {
        u_int16_t micentry = entry + 64;  /* MIC goes at slot+64 */

        HALASSERT(micentry < AH_PRIVATE(ah)->ah_caps.halKeyCacheSize);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), 0);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), 0);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), 0);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), 0);
        /* NB: key type and MAC are known to be ok */
    }

    if (AH_PRIVATE(ah)->ah_curchan == AH_NULL) {
        return AH_TRUE;
    }

    if (ar9300_get_capability(ah, HAL_CAP_BB_RIFS_HANG, 0, AH_NULL)
        == HAL_OK) {
        if (key_type == AR_KEYTABLE_TYPE_TKIP    ||
            key_type == AR_KEYTABLE_TYPE_40      ||
            key_type == AR_KEYTABLE_TYPE_104     ||
            key_type == AR_KEYTABLE_TYPE_128) {
            /* SW WAR for Bug 31602 */
            if (--ahp->ah_rifs_sec_cnt == 0) {
                HALDEBUG(ah, HAL_DEBUG_KEYCACHE,
                    "%s: Count = %d, enabling RIFS\n",
                    __func__, ahp->ah_rifs_sec_cnt);
                ar9300_set_rifs_delay(ah, AH_TRUE);
            }
        }
    }
    return AH_TRUE;
}

/*
 * Sets the mac part of the specified key cache entry (and any
 * associated MIC entry) and mark them valid.
 */
HAL_BOOL
ar9300_set_key_cache_entry_mac(
    struct ath_hal *ah,
    u_int16_t entry,
    const u_int8_t *mac)
{
    u_int32_t mac_hi, mac_lo;
    u_int32_t unicast_addr = AR_KEYTABLE_VALID;

    if (entry >= AH_PRIVATE(ah)->ah_caps.halKeyCacheSize) {
        HALDEBUG(ah, HAL_DEBUG_KEYCACHE,
            "%s: entry %u out of range\n", __func__, entry);
        return AH_FALSE;
    }
    /*
     * Set MAC address -- shifted right by 1.  mac_lo is
     * the 4 MSBs, and mac_hi is the 2 LSBs.
     */
    if (mac != AH_NULL) {
        /*
         *  If upper layers have requested mcast MACaddr lookup, then
         *  signify this to the hw by setting the (poorly named) valid_bit
         *  to 0.  Yes, really 0. The hardware specs, pcu_registers.txt, is
         *  has incorrectly named valid_bit. It should be called "Unicast".
         *  When the Key Cache entry is to decrypt Unicast frames, this bit
         *  should be '1'; for multicast and broadcast frames, this bit is '0'.
         */
        if (mac[0] & 0x01) {
            unicast_addr = 0;    /* Not an unicast address */
        }

        mac_hi = (mac[5] << 8)  |  mac[4];
        mac_lo = (mac[3] << 24) | (mac[2] << 16)
              | (mac[1] << 8)  |  mac[0];
        mac_lo >>= 1; /* Note that the bit 0 is shifted out. This bit is used to
                      * indicate that this is a multicast key cache. */
        mac_lo |= (mac_hi & 1) << 31; /* carry */
        mac_hi >>= 1;
    } else {
        mac_lo = mac_hi = 0;
    }
    OS_REG_WRITE(ah, AR_KEYTABLE_MAC0(entry), mac_lo);
    OS_REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), mac_hi | unicast_addr);
    return AH_TRUE;
}

/*
 * Sets the contents of the specified key cache entry
 * and any associated MIC entry.
 */
HAL_BOOL
ar9300_set_key_cache_entry(struct ath_hal *ah, u_int16_t entry,
                       const HAL_KEYVAL *k, const u_int8_t *mac,
                       int xor_key)
{
    const HAL_CAPABILITIES *p_cap = &AH_PRIVATE(ah)->ah_caps;
    u_int32_t key0, key1, key2, key3, key4;
    u_int32_t key_type;
    u_int32_t xor_mask = xor_key ?
        (KEY_XOR << 24 | KEY_XOR << 16 | KEY_XOR << 8 | KEY_XOR) : 0;
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t pwrmgt, pwrmgt_mic, uapsd_cfg, psta = 0;
    int is_proxysta_key = k->kv_type & HAL_KEY_PROXY_STA_MASK;


    if (entry >= p_cap->halKeyCacheSize) {
        HALDEBUG(ah, HAL_DEBUG_KEYCACHE,
            "%s: entry %u out of range\n", __func__, entry);
        return AH_FALSE;
    }
    HALDEBUG(ah, HAL_DEBUG_KEYCACHE, "%s[%d] mac %s proxy %d\n",
        __func__, __LINE__, mac ? ath_hal_ether_sprintf(mac) : "null",
        is_proxysta_key);
	
    switch (k->kv_type & AH_KEYTYPE_MASK) {
    case HAL_CIPHER_AES_OCB:
        key_type = AR_KEYTABLE_TYPE_AES;
        break;
    case HAL_CIPHER_AES_CCM:
        if (!p_cap->halCipherAesCcmSupport) {
            HALDEBUG(ah, HAL_DEBUG_KEYCACHE, "%s: AES-CCM not supported by "
                "mac rev 0x%x\n",
                __func__, AH_PRIVATE(ah)->ah_macRev);
            return AH_FALSE;
        }
        key_type = AR_KEYTABLE_TYPE_CCM;
        break;
    case HAL_CIPHER_TKIP:
        key_type = AR_KEYTABLE_TYPE_TKIP;
        if (IS_MIC_ENABLED(ah) && entry + 64 >= p_cap->halKeyCacheSize) {
            HALDEBUG(ah, HAL_DEBUG_KEYCACHE,
                "%s: entry %u inappropriate for TKIP\n",
                __func__, entry);
            return AH_FALSE;
        }
        break;
    case HAL_CIPHER_WEP:
        if (k->kv_len < 40 / NBBY) {
            HALDEBUG(ah, HAL_DEBUG_KEYCACHE, "%s: WEP key length %u too small\n",
                __func__, k->kv_len);
            return AH_FALSE;
        }
        if (k->kv_len <= 40 / NBBY) {
            key_type = AR_KEYTABLE_TYPE_40;
        } else if (k->kv_len <= 104 / NBBY) {
            key_type = AR_KEYTABLE_TYPE_104;
        } else {
            key_type = AR_KEYTABLE_TYPE_128;
        }
        break;
    case HAL_CIPHER_CLR:
        key_type = AR_KEYTABLE_TYPE_CLR;
        break;
    default:
        HALDEBUG(ah, HAL_DEBUG_KEYCACHE, "%s: cipher %u not supported\n",
            __func__, k->kv_type);
        return AH_FALSE;
    }

    key0 =  LE_READ_4(k->kv_val +  0) ^ xor_mask;
    key1 = (LE_READ_2(k->kv_val +  4) ^ xor_mask) & 0xffff;
    key2 =  LE_READ_4(k->kv_val +  6) ^ xor_mask;
    key3 = (LE_READ_2(k->kv_val + 10) ^ xor_mask) & 0xffff;
    key4 =  LE_READ_4(k->kv_val + 12) ^ xor_mask;
    if (k->kv_len <= 104 / NBBY) {
        key4 &= 0xff;
    }

    /* Extract the UAPSD AC bits and shift it appropriately */
    uapsd_cfg = k->kv_apsd;
    uapsd_cfg = (u_int32_t) SM(uapsd_cfg, AR_KEYTABLE_UAPSD);

    /* Need to preserve the power management bit used by MAC */
    pwrmgt = OS_REG_READ(ah, AR_KEYTABLE_TYPE(entry)) & AR_KEYTABLE_PWRMGT;
    
    if (is_proxysta_key) {
        u_int8_t bcast_mac[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        if (!mac || OS_MEMCMP(mac, bcast_mac, 6)) {
            psta = AR_KEYTABLE_DIR_ACK_BIT;
        }
    }
    /*
     * Note: key cache hardware requires that each double-word
     * pair be written in even/odd order (since the destination is
     * a 64-bit register).  Don't reorder these writes w/o
     * considering this!
     */
    if (key_type == AR_KEYTABLE_TYPE_TKIP && IS_MIC_ENABLED(ah)) {
        u_int16_t micentry = entry + 64;  /* MIC goes at slot+64 */

        /* Need to preserve the power management bit used by MAC */
        pwrmgt_mic =
            OS_REG_READ(ah, AR_KEYTABLE_TYPE(micentry)) & AR_KEYTABLE_PWRMGT;

        /*
         * Invalidate the encrypt/decrypt key until the MIC
         * key is installed so pending rx frames will fail
         * with decrypt errors rather than a MIC error.
         */
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), ~key0);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), ~key1);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
        OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(entry),
            key_type | pwrmgt | uapsd_cfg | psta);
        ar9300_set_key_cache_entry_mac(ah, entry, mac);

        /*
         * since the AR_MISC_MODE register was written with the contents of
         * ah_misc_mode (if any) in ar9300_attach, just check ah_misc_mode and
         * save a pci read per key set.
         */
        if (ahp->ah_misc_mode & AR_PCU_MIC_NEW_LOC_ENA) {
            u_int32_t mic0, mic1, mic2, mic3, mic4;
            /*
             * both RX and TX mic values can be combined into
             * one cache slot entry.
             * 8*N + 800         31:0    RX Michael key 0
             * 8*N + 804         15:0    TX Michael key 0 [31:16]
             * 8*N + 808         31:0    RX Michael key 1
             * 8*N + 80C         15:0    TX Michael key 0 [15:0]
             * 8*N + 810         31:0    TX Michael key 1
             * 8*N + 814         15:0    reserved
             * 8*N + 818         31:0    reserved
             * 8*N + 81C         14:0    reserved
             *                   15      key valid == 0
             */
            /* RX mic */
            mic0 = LE_READ_4(k->kv_mic + 0);
            mic2 = LE_READ_4(k->kv_mic + 4);
            /* TX mic */
            mic1 = LE_READ_2(k->kv_txmic + 2) & 0xffff;
            mic3 = LE_READ_2(k->kv_txmic + 0) & 0xffff;
            mic4 = LE_READ_4(k->kv_txmic + 4);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), mic0);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), mic1);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), mic2);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), mic3);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(micentry), mic4);
            OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(micentry),
                         AR_KEYTABLE_TYPE_CLR | pwrmgt_mic | uapsd_cfg);

        } else {
            u_int32_t mic0, mic2;

            mic0 = LE_READ_4(k->kv_mic + 0);
            mic2 = LE_READ_4(k->kv_mic + 4);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), mic0);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), 0);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), mic2);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), 0);
            OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(micentry), 0);
            OS_REG_WRITE(ah,
                AR_KEYTABLE_TYPE(micentry | pwrmgt_mic | uapsd_cfg),
                AR_KEYTABLE_TYPE_CLR);
        }
        /* NB: MIC key is not marked valid and has no MAC address */
        OS_REG_WRITE(ah, AR_KEYTABLE_MAC0(micentry), 0);
        OS_REG_WRITE(ah, AR_KEYTABLE_MAC1(micentry), 0);

        /* correct intentionally corrupted key */
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);
    } else {
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);
        OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
        OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(entry),
            key_type | pwrmgt | uapsd_cfg | psta);

        /*
        ath_hal_printf(ah, "%s[%d] mac %s proxy %d\n",
            __func__, __LINE__, mac ? ath_hal_ether_sprintf(mac) : "null",
            is_proxysta_key);
         */

        ar9300_set_key_cache_entry_mac(ah, entry, mac);
    }

    ahp->ah_keytype[entry] = keyType[k->kv_type];
    HALDEBUG(ah, HAL_DEBUG_KEYCACHE, "%s: entry=%d, k->kv_type=%d,"
      "keyType=%d\n", __func__, entry, k->kv_type, keyType[k->kv_type]);


    if (AH_PRIVATE(ah)->ah_curchan == AH_NULL) {
        return AH_TRUE;
    }

    if (ar9300_get_capability(ah, HAL_CAP_BB_RIFS_HANG, 0, AH_NULL)
        == HAL_OK) {
        if (key_type == AR_KEYTABLE_TYPE_TKIP    ||
            key_type == AR_KEYTABLE_TYPE_40      ||
            key_type == AR_KEYTABLE_TYPE_104     ||
            key_type == AR_KEYTABLE_TYPE_128) {
            /* SW WAR for Bug 31602 */
            ahp->ah_rifs_sec_cnt++;
            HALDEBUG(ah, HAL_DEBUG_KEYCACHE,
                "%s: Count = %d, disabling RIFS\n",
                __func__, ahp->ah_rifs_sec_cnt);
            ar9300_set_rifs_delay(ah, AH_FALSE);
        }
    }
    HALDEBUG(ah, HAL_DEBUG_KEYCACHE, "%s[%d] mac %s proxy %d\n",
        __func__, __LINE__, mac ? ath_hal_ether_sprintf(mac) : "null",
        is_proxysta_key);

    return AH_TRUE;
}

/*
 * Enable the Keysearch for every subframe of an aggregate
 */
void
ar9300_enable_keysearch_always(struct ath_hal *ah, int enable)
{
    u_int32_t val;

    if (!ah) {
        return;
    }
    val = OS_REG_READ(ah, AR_PCU_MISC);
    if (enable) {
        val |= AR_PCU_ALWAYS_PERFORM_KEYSEARCH;
    } else {
        val &= ~AR_PCU_ALWAYS_PERFORM_KEYSEARCH;
    }
    OS_REG_WRITE(ah, AR_PCU_MISC, val);
}
void ar9300_dump_keycache(struct ath_hal *ah, int n, u_int32_t *entry)
{
#define AH_KEY_REG_SIZE	8
    int i;

    for (i = 0; i < AH_KEY_REG_SIZE; i++) {
        entry[i] = OS_REG_READ(ah, AR_KEYTABLE_KEY0(n) + i * 4);
    }
#undef AH_KEY_REG_SIZE
}

#if ATH_SUPPORT_KEYPLUMB_WAR
/*
 * Check the contents of the specified key cache entry
 * and any associated MIC entry.
 */
    HAL_BOOL
ar9300_check_key_cache_entry(struct ath_hal *ah, u_int16_t entry,
        const HAL_KEYVAL *k, int xorKey)
{
    const HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;
    u_int32_t key0, key1, key2, key3, key4;
    u_int32_t keyType;
    u_int32_t xorMask = xorKey ?
        (KEY_XOR << 24 | KEY_XOR << 16 | KEY_XOR << 8 | KEY_XOR) : 0;
    struct ath_hal_9300 *ahp = AH9300(ah);


    if (entry >= pCap->hal_key_cache_size) {
        HALDEBUG(ah, HAL_DEBUG_KEYCACHE,
                "%s: entry %u out of range\n", __func__, entry);
        return AH_FALSE;
    }
    switch (k->kv_type) {
        case HAL_CIPHER_AES_OCB:
            keyType = AR_KEYTABLE_TYPE_AES;
            break;
        case HAL_CIPHER_AES_CCM:
            if (!pCap->hal_cipher_aes_ccm_support) {
                HALDEBUG(ah, HAL_DEBUG_KEYCACHE, "%s: AES-CCM not supported by "
                        "mac rev 0x%x\n",
                        __func__, AH_PRIVATE(ah)->ah_macRev);
                return AH_FALSE;
            }
            keyType = AR_KEYTABLE_TYPE_CCM;
            break;
        case HAL_CIPHER_TKIP:
            keyType = AR_KEYTABLE_TYPE_TKIP;
            if (IS_MIC_ENABLED(ah) && entry + 64 >= pCap->hal_key_cache_size) {
                HALDEBUG(ah, HAL_DEBUG_KEYCACHE,
                        "%s: entry %u inappropriate for TKIP\n",
                        __func__, entry);
                return AH_FALSE;
            }
            break;
        case HAL_CIPHER_WEP:
            if (k->kv_len < 40 / NBBY) {
                HALDEBUG(ah, HAL_DEBUG_KEYCACHE, "%s: WEP key length %u too small\n",
                        __func__, k->kv_len);
                return AH_FALSE;
            }
            if (k->kv_len <= 40 / NBBY) {
                keyType = AR_KEYTABLE_TYPE_40;
            } else if (k->kv_len <= 104 / NBBY) {
                keyType = AR_KEYTABLE_TYPE_104;
            } else {
                keyType = AR_KEYTABLE_TYPE_128;
            }
            break;
        case HAL_CIPHER_CLR:
            keyType = AR_KEYTABLE_TYPE_CLR;
            return AH_TRUE;
        default:
            HALDEBUG(ah, HAL_DEBUG_KEYCACHE, "%s: cipher %u not supported\n",
                    __func__, k->kv_type);
            return AH_TRUE;
    }

    key0 =  LE_READ_4(k->kv_val +  0) ^ xorMask;
    key1 = (LE_READ_2(k->kv_val +  4) ^ xorMask) & 0xffff;
    key2 =  LE_READ_4(k->kv_val +  6) ^ xorMask;
    key3 = (LE_READ_2(k->kv_val + 10) ^ xorMask) & 0xffff;
    key4 =  LE_READ_4(k->kv_val + 12) ^ xorMask;
    if (k->kv_len <= 104 / NBBY) {
        key4 &= 0xff;
    }

    /*
     * Note: key cache hardware requires that each double-word
     * pair be written in even/odd order (since the destination is
     * a 64-bit register).  Don't reorder these writes w/o
     * considering this!
     */
    if (keyType == AR_KEYTABLE_TYPE_TKIP && IS_MIC_ENABLED(ah)) {
        u_int16_t micentry = entry + 64;  /* MIC goes at slot+64 */


        /*
         * Invalidate the encrypt/decrypt key until the MIC
         * key is installed so pending rx frames will fail
         * with decrypt errors rather than a MIC error.
         */
        if ((OS_REG_READ(ah, AR_KEYTABLE_KEY0(entry)) == key0) && 
                (OS_REG_READ(ah, AR_KEYTABLE_KEY1(entry)) == key1) &&
                (OS_REG_READ(ah, AR_KEYTABLE_KEY2(entry)) == key2) &&
                (OS_REG_READ(ah, AR_KEYTABLE_KEY3(entry)) == key3) &&
                (OS_REG_READ(ah, AR_KEYTABLE_KEY4(entry)) == key4) &&
                ((OS_REG_READ(ah, AR_KEYTABLE_TYPE(entry)) & AR_KEY_TYPE) == (keyType & AR_KEY_TYPE))) 
        {

            /*
             * since the AR_MISC_MODE register was written with the contents of
             * ah_miscMode (if any) in ar9300Attach, just check ah_miscMode and
             * save a pci read per key set.
             */
            if (ahp->ah_misc_mode & AR_PCU_MIC_NEW_LOC_ENA) {
                u_int32_t mic0,mic1,mic2,mic3,mic4;
                /*
                 * both RX and TX mic values can be combined into
                 * one cache slot entry.
                 * 8*N + 800         31:0    RX Michael key 0
                 * 8*N + 804         15:0    TX Michael key 0 [31:16]
                 * 8*N + 808         31:0    RX Michael key 1
                 * 8*N + 80C         15:0    TX Michael key 0 [15:0]
                 * 8*N + 810         31:0    TX Michael key 1
                 * 8*N + 814         15:0    reserved
                 * 8*N + 818         31:0    reserved
                 * 8*N + 81C         14:0    reserved
                 *                   15      key valid == 0
                 */
                /* RX mic */
                mic0 = LE_READ_4(k->kv_mic + 0);
                mic2 = LE_READ_4(k->kv_mic + 4);
                /* TX mic */
                mic1 = LE_READ_2(k->kv_txmic + 2) & 0xffff;
                mic3 = LE_READ_2(k->kv_txmic + 0) & 0xffff;
                mic4 = LE_READ_4(k->kv_txmic + 4);
                if ((OS_REG_READ(ah, AR_KEYTABLE_KEY0(micentry)) == mic0) &&
                        (OS_REG_READ(ah, AR_KEYTABLE_KEY1(micentry)) == mic1) &&
                        (OS_REG_READ(ah, AR_KEYTABLE_KEY2(micentry)) == mic2) &&
                        (OS_REG_READ(ah, AR_KEYTABLE_KEY3(micentry)) == mic3) &&
                        (OS_REG_READ(ah, AR_KEYTABLE_KEY4(micentry)) == mic4) &&
                        ((OS_REG_READ(ah, AR_KEYTABLE_TYPE(micentry)) & AR_KEY_TYPE) == (AR_KEYTABLE_TYPE_CLR & AR_KEY_TYPE))) {
                    return AH_TRUE;
                }

            } else {
                return AH_TRUE;
            }
        }
    } else {
        if ((OS_REG_READ(ah, AR_KEYTABLE_KEY0(entry)) == key0) &&
                (OS_REG_READ(ah, AR_KEYTABLE_KEY1(entry)) == key1) &&
                (OS_REG_READ(ah, AR_KEYTABLE_KEY2(entry)) == key2) &&
                (OS_REG_READ(ah, AR_KEYTABLE_KEY3(entry)) == key3) &&
                (OS_REG_READ(ah, AR_KEYTABLE_KEY4(entry)) == key4) &&
                ((OS_REG_READ(ah, AR_KEYTABLE_TYPE(entry)) & AR_KEY_TYPE) == (keyType & AR_KEY_TYPE))) {
            return AH_TRUE;
        }
    }
    return AH_FALSE;
}
#endif
