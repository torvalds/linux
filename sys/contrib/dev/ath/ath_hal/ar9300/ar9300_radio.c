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
#include "ar9300/ar9300phy.h"

/* chansel table, used by Hornet and Poseidon */
static const u_int32_t ar9300_chansel_xtal_25M[] = {
    0x101479e, /* Freq 2412 - (128 << 17) + 83870  */
    0x101d027, /* Freq 2417 - (128 << 17) + 118823 */
    0x10258af, /* Freq 2422 - (129 << 17) + 22703  */
    0x102e138, /* Freq 2427 - (129 << 17) + 57656  */
    0x10369c0, /* Freq 2432 - (129 << 17) + 92608  */
    0x103f249, /* Freq 2437 - (129 << 17) + 127561 */
    0x1047ad1, /* Freq 2442 - (130 << 17) + 31441  */
    0x105035a, /* Freq 2447 - (130 << 17) + 66394  */
    0x1058be2, /* Freq 2452 - (130 << 17) + 101346 */
    0x106146b, /* Freq 2457 - (131 << 17) + 5227   */
    0x1069cf3, /* Freq 2462 - (131 << 17) + 40179  */
    0x107257c, /* Freq 2467 - (131 << 17) + 75132  */
    0x107ae04, /* Freq 2472 - (131 << 17) + 110084 */
    0x108f5b2, /* Freq 2484 - (132 << 17) + 62898  */
};

static const u_int32_t ar9300_chansel_xtal_40M[] = {
    0xa0ccbe, /* Freq 2412 - (80 << 17) + 52414  */
    0xa12213, /* Freq 2417 - (80 << 17) + 74259  */
    0xa17769, /* Freq 2422 - (80 << 17) + 96105  */
    0xa1ccbe, /* Freq 2427 - (80 << 17) + 117950 */
    0xa22213, /* Freq 2432 - (81 << 17) + 8723   */
    0xa27769, /* Freq 2437 - (81 << 17) + 30569  */
    0xa2ccbe, /* Freq 2442 - (81 << 17) + 52414  */
    0xa32213, /* Freq 2447 - (81 << 17) + 74259  */
    0xa37769, /* Freq 2452 - (81 << 17) + 96105  */
    0xa3ccbe, /* Freq 2457 - (81 << 17) + 117950 */
    0xa42213, /* Freq 2462 - (82 << 17) + 8723   */
    0xa47769, /* Freq 2467 - (82 << 17) + 30569  */
    0xa4ccbe, /* Freq 2472 - (82 << 17) + 52414  */
    0xa5998b, /* Freq 2484 - (82 << 17) + 104843 */
};


/*
 * Take the MHz channel value and set the Channel value
 *
 * ASSUMES: Writes enabled to analog bus
 *
 * Actual Expression,
 *
 * For 2GHz channel,
 * Channel Frequency = (3/4) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^17)
 * (freq_ref = 40MHz)
 *
 * For 5GHz channel,
 * Channel Frequency = (3/2) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^10)
 * (freq_ref = 40MHz/(24>>amode_ref_sel))
 *
 * For 5GHz channels which are 5MHz spaced,
 * Channel Frequency = (3/2) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^17)
 * (freq_ref = 40MHz)
 */
static HAL_BOOL
ar9300_set_channel(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    u_int16_t b_mode, frac_mode = 0, a_mode_ref_sel = 0;
    u_int32_t freq, channel_sel, reg32;
    u_int8_t clk_25mhz = AH9300(ah)->clk_25mhz;
    CHAN_CENTERS centers;
    int load_synth_channel;
#ifdef	AH_DEBUG_ALQ
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
#endif

    /*
     * Put this behind AH_DEBUG_ALQ for now until the Hornet
     * channel_sel code below is made to work.
     */
#ifdef	AH_DEBUG_ALQ
    OS_MARK(ah, AH_MARK_SETCHANNEL, ichan->channel);
#endif

    ar9300_get_channel_centers(ah, chan, &centers);
    freq = centers.synth_center;

    if (freq < 4800) {     /* 2 GHz, fractional mode */
        b_mode = 1; /* 2 GHz */

        if (AR_SREV_HORNET(ah)) {
#if 0
            u_int32_t ichan =
              ieee80211_mhz2ieee(ah, chan->ic_freq, chan->ic_flags);
            HALASSERT(ichan > 0 && ichan <= 14);
            if (clk_25mhz) {
                channel_sel = ar9300_chansel_xtal_25M[ichan - 1];
            } else {
                channel_sel = ar9300_chansel_xtal_40M[ichan - 1];
            }
#endif
            uint32_t i;

            /*
             * Pay close attention to this bit!
             *
             * We need to map the actual desired synth frequency to
             * one of the channel select array entries.
             *
             * For HT20, it'll align with the channel we select.
             *
             * For HT40 though it won't - the centre frequency
             * will not be the frequency of chan->ic_freq or ichan->freq;
             * it needs to be whatever frequency maps to 'freq'.
             */
            i = ath_hal_mhz2ieee_2ghz(ah, freq);
            HALASSERT(i > 0 && i <= 14);
            if (clk_25mhz) {
                channel_sel = ar9300_chansel_xtal_25M[i - 1];
            } else {
                channel_sel = ar9300_chansel_xtal_40M[i - 1];
            }
        } else if (AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah)) {
            u_int32_t channel_frac;
            /* 
             * freq_ref = (40 / (refdiva >> a_mode_ref_sel));
             *     (where refdiva = 1 and amoderefsel = 0)
             * ndiv = ((chan_mhz * 4) / 3) / freq_ref;
             * chansel = int(ndiv),  chanfrac = (ndiv - chansel) * 0x20000
             */
            channel_sel = (freq * 4) / 120;
            channel_frac = (((freq * 4) % 120) * 0x20000) / 120;
            channel_sel = (channel_sel << 17) | (channel_frac);
        } else if (AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah) || AR_SREV_HONEYBEE(ah)) {
            u_int32_t channel_frac;
            if (clk_25mhz) {
                /* 
                 * freq_ref = (50 / (refdiva >> a_mode_ref_sel));
                 *     (where refdiva = 1 and amoderefsel = 0)
                 * ndiv = ((chan_mhz * 4) / 3) / freq_ref;
                 * chansel = int(ndiv),  chanfrac = (ndiv - chansel) * 0x20000
                 */
                if (AR_SREV_SCORPION(ah) || AR_SREV_HONEYBEE(ah)) {
                    /* Doubler is off for Scorpion */
                    channel_sel = (freq * 4) / 75;
                    channel_frac = (((freq * 4) % 75) * 0x20000) / 75;
                } else {
                    channel_sel = (freq * 2) / 75;
                    channel_frac = (((freq * 2) % 75) * 0x20000) / 75;
                }
            } else {
                /* 
                 * freq_ref = (50 / (refdiva >> a_mode_ref_sel));
                 *     (where refdiva = 1 and amoderefsel = 0)
                 * ndiv = ((chan_mhz * 4) / 3) / freq_ref;
                 * chansel = int(ndiv),  chanfrac = (ndiv - chansel) * 0x20000
                 */
                if (AR_SREV_SCORPION(ah)) {
                    /* Doubler is off for Scorpion */
                    channel_sel = (freq * 4) / 120;
                    channel_frac = (((freq * 4) % 120) * 0x20000) / 120;
                } else {
                    channel_sel = (freq * 2) / 120;
                    channel_frac = (((freq * 2) % 120) * 0x20000) / 120;
                }
            }
            channel_sel = (channel_sel << 17) | (channel_frac);
        } else {
            channel_sel = CHANSEL_2G(freq);
        }
    } else {
        b_mode = 0; /* 5 GHz */
        if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && clk_25mhz){
            u_int32_t channel_frac;
            /* 
             * freq_ref = (50 / (refdiva >> amoderefsel));
             *     (refdiva = 1, amoderefsel = 0)
             * ndiv = ((chan_mhz * 2) / 3) / freq_ref;
             * chansel = int(ndiv),  chanfrac = (ndiv - chansel) * 0x20000
             */
            channel_sel = freq / 75 ;
            channel_frac = ((freq % 75) * 0x20000) / 75;
            channel_sel = (channel_sel << 17) | (channel_frac);
        } else {
            channel_sel = CHANSEL_5G(freq);
            /* Doubler is ON, so, divide channel_sel by 2. */
            channel_sel >>= 1;
        }
    }


	/* Enable fractional mode for all channels */
    frac_mode = 1;
    a_mode_ref_sel = 0;
    load_synth_channel = 0;
    
    reg32 = (b_mode << 29);
    OS_REG_WRITE(ah, AR_PHY_SYNTH_CONTROL, reg32);

	/* Enable Long shift Select for Synthesizer */
    OS_REG_RMW_FIELD(ah,
        AR_PHY_65NM_CH0_SYNTH4, AR_PHY_SYNTH4_LONG_SHIFT_SELECT, 1);

    /* program synth. setting */
    reg32 =
        (channel_sel       <<  2) |
        (a_mode_ref_sel      << 28) |
        (frac_mode         << 30) |
        (load_synth_channel << 31);
    if (IEEE80211_IS_CHAN_QUARTER(chan)) {
        reg32 += CHANSEL_5G_DOT5MHZ;
    }
    OS_REG_WRITE(ah, AR_PHY_65NM_CH0_SYNTH7, reg32);
    /* Toggle Load Synth channel bit */
    load_synth_channel = 1;
    reg32 |= load_synth_channel << 31;
    OS_REG_WRITE(ah, AR_PHY_65NM_CH0_SYNTH7, reg32);


    AH_PRIVATE(ah)->ah_curchan = chan;

    return AH_TRUE;
}


#if 0
static HAL_BOOL
ar9300_get_chip_power_limits(struct ath_hal *ah, HAL_CHANNEL *chans,
                         u_int32_t nchans)
{
    int i;

    for (i = 0; i < nchans; i++) {
        chans[i].max_tx_power = AR9300_MAX_RATE_POWER;
        chans[i].min_tx_power = AR9300_MAX_RATE_POWER;
    }
    return AH_TRUE;
}
#endif

/* XXX FreeBSD */
static HAL_BOOL
ar9300_get_chip_power_limits(struct ath_hal *ah,
    struct ieee80211_channel *chan)
{
        /* XXX ? */
        chan->ic_minpower = 0;
        chan->ic_maxpower = AR9300_MAX_RATE_POWER;

        return AH_TRUE;
}

HAL_BOOL
ar9300_rf_attach(struct ath_hal *ah, HAL_STATUS *status)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    ahp->ah_rf_hal.set_channel    = ar9300_set_channel;
    ahp->ah_rf_hal.get_chip_power_lim   = ar9300_get_chip_power_limits;

    *status = HAL_OK;

    return AH_TRUE;
}
