/* Copyright (C) 2007 Nanoradio AB */
/* $Id: radio.c 16988 2010-11-25 14:57:57Z copi $ */

#include "nrx_lib.h"
#include "nrx_priv.h"
#include "mac_mib_defs.h"

/** \defgroup RADIO Transmission/reception settings 
 * \brief The Transmission/reception settings includes quite a large number of functions related to the
 *        Tx/Rx functionality of the device. The functions can be divided into a number of subgroups:
 *
 * \par Get counters
 * The counters are used to evaluate the quality of the link between the STA and AP. 
 * For example, there are counters available for the number of transmitted and received frames, 
 * fragments and multicast frames. And by comparing these counters with the corresponding failure counters
 * it is possible to calculate values for the link quality.
 * 
 * \par Get/set triggers and tresholds
 * The functions to get and set triggers and tresholds are part of the Nanoradio MIB Trigger Supervision 
 * functionality that can be used for supervising real time changes of variables. 
 * When a certain pre-defined condition is fulfilled this can be used as a trigger to take further action.
 * The following triggers can be set:
 *  - Signal to Noise Ratio (SNR)
 *  - Received Signal Strength Indication (RSSI)
 *  - Packet Error Rate (PER)
 * 
 * See the <em>NRX700 MIB Trigger Supervision Function Description</em> (15516/14-NRX700) for further  
 * information about MIB Triggers.
 * 
 * \par Tx rate settings
 * Included here are functions to set the TX retry limits, adaptive Tx rate mode, lock/unlock Tx rates.
 * 
 * \par Channels and rate settings
 * There are functions available to set the allowed channels, region codes and rates. 
 * 
 * \par Other
 *  - Link monitoring, monitors received beacon frames to be able to terminate the connection 
 * if a predefined number of consecutive beacons have been missed.
 *  - Traffic filtering, filter out broadcast or multicast traffic in the device
 *  - Connection failure notification, notify when a connection or a connection attempt has failed
 *  - Maximum Tx power levels, set for 802.11g 802.11b rates by specifying attenuation levels
 */

/************************************
 *
 *           Tx/Rx stuff
 *
 ************************************/

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set the maximum allowed transmission power levels</b>
 *
 * The maximum transmission power levels are set for OFDM (802.11g)
 * and/or BPSK/QPSK/CCK (802.11b) rates.
 * The levels are specified by setting attenuation levels
 * from the default maximum. The maximum transmission power overrides power
 * limits announced in 802.11d, MIN(max_tx_power, 802.11d) will be used for
 * transmission. Attenuation levels that exceed the hardware maximum
 * power limit results in an effective maximum transmission power of 0 dB.
 *
 * The power levels for OFDM and QPSK are independent. The output power
 * may therefore change as transmission rates are changed.
 *
 * The maximum transmission power levels can be changed dynamically when
 * associated.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param qpsk_attenuation_db Tx attenuation in dB from the maximum allowed
 *        value. Valid values are 0-19 dB. This will limit QPSK transmission
 *        power to the hardware maximum (21dBm) minus this attenuation value.
 * @param ofdm_attenuation_db Tx attenuation in dB from the maximum allowed
 *        value. Valid values are 0-19 dB. This will limit OFDM transmission
 *        power to the hardware maximum (18dBm) minus this attenuation value.
 * @return 
 * - 0 on success.
 * - EINVAL on invalid attenuation values.
 */
int
nrx_set_max_tx_power(nrx_context ctx,
                     uint8_t qpsk_attenuation_db,
                     uint8_t ofdm_attenuation_db)
{
   uint8_t val[2];
   NRX_ASSERT(ctx);
   NRX_CHECK(qpsk_attenuation_db <= 19);
   NRX_CHECK(ofdm_attenuation_db <= 19);

   val[0] = qpsk_attenuation_db;
   val[1] = ofdm_attenuation_db;
   
   return nrx_set_mib_val(ctx, MIB_dot11powerIndex, val, sizeof(val));
}




/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the currently used channel</b>
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param channel Where the currently used channel is stored. 
 *
 * @return 
 * - 0 on success.
 * - EINVAL when not associated.
 */
int
nrx_get_curr_channel(nrx_context ctx,
                     nrx_channel_t *channel)
{
   int ret;
   uint32_t freq;
   NRX_ASSERT(ctx);
   NRX_ASSERT(channel);

   ret = nrx_get_curr_freq(ctx, &freq);
   if(ret)
      return ret;
   return nrx_convert_frequency_to_channel(ctx, freq, channel);
}



/* Internal function to compare two uint8_t */
static int compare(const void *A, const void *B)
{
   uint8_t a = *(const uint8_t *)A;
   uint8_t b = *(const uint8_t *)B;

   return a < b ? -1 : a == b ? 0 : 1; /* -1 , 0, +1 when a<b, a==b, a>b respectively */
}

/* Our supported rates. Don't change! */
#define NO_SUPP_RATES 14
static const nrx_rate_t supp_rates[NO_SUPP_RATES] = {2, 4, 11, 12, 18, 22, 24, 36, 44, 48, 66, 72, 96, 108}; // Remove 22 (i.e. 11 MB)???

/*!
 * @internal
 * @brief <b>Check if a rate is supported by the system</b>
 * @param rate The rate to be investigated
 * @retval 1 on success
 * @retval 0 if rate is not supported
 */
int _nrx_is_supp_rate(nrx_rate_t rate)
{
   int i;
   for (i = 0; i < NRX_ARRAY_SIZE(supp_rates); i++)
      if (supp_rates[i] == rate)
         return 1;
   return 0;         /* Not found among used rates */
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set the adaptive TX rate mode</b>
 * 
 * The adaptive TX rate mode defines if and how the transmission rate
 * will change to adapt to local radio conditions. The initial rate
 * is the preferred rate. Tx power will be restricted by the value set by
 * nrx_set_max_tx_power().
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param modes Bitmask that defines the adaptive TX rate modes to be used.
 *              The mode defines the transmission rate adaptation strategy. 
 *              Several modes can be enabled by OR-ing them together.
 * @param initial_rate The initial rate for the rate adaptation strategy.
 *              A change to the initial_rate parameter will only be effected
 *              upon reassociation.
 * @param penalty_rates The rates that should be used by the tx rate adaption
 *                      algorithm. The initial rate must be included in this list.
 *                      If this parameter is null then all the rates may 
 *                      be used. Only the penalty rates that matches
 *                      the supported rates for the current association will
 *                      be used. Should no match exist, this list will be 
 *                      ignored. For older hardware, the highest supported 
 *                      rate for the current association will be used although not  
 *                      included in this list (affects baseband NRX701A, NRX701B, 
 *                      and radio NRX702, NRX510A).
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_set_adaptive_tx_rate_mode(nrx_context ctx,
                              nrx_adaptive_tx_rate_mode_t modes,
                              nrx_rate_t initial_rate,
                              const nrx_rate_list_t *penalty_rates)
{
   int i, ret;
   uint8_t rates[NO_SUPP_RATES];
   size_t len = sizeof(rates);
   NRX_ASSERT(ctx);
   NRX_CHECK((modes & 0xf0) == 0);
   NRX_CHECK(_nrx_is_supp_rate(initial_rate));

   memcpy(rates, supp_rates, sizeof(rates));

   if (penalty_rates != NULL) {
      if (penalty_rates->len < 1 || penalty_rates->len > len)
         return EINVAL;
      len = penalty_rates->len;
      memcpy(rates, penalty_rates->rates, len);
      qsort(rates, len, sizeof(*rates), compare);       /* Rates need to be sorted. */
   }

   /* Sanity check */
   for (i = 0; i < len ; i++) 
      if (rates[i] == initial_rate)
         break;
   if (i == len)  /* Initial rate not found */
      return EINVAL;

   ret =  nrx_set_mib_val(ctx, MIB_dot11adaptiveTxRateMask, rates, len);
   ret += nrx_set_mib_val(ctx, MIB_dot11adaptiveTxRateLvl1, &modes, sizeof(modes));
   ret += nrx_set_mib_val(ctx, "5.2.14", &initial_rate, sizeof(initial_rate));

   return ret ? EINVAL : 0;
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set the TX retry limits per rate</b>
 *
 * Specify the number of transmission retries before discarding a frame.
 * The retry limit is specified per rate. First the number of retries of
 * the current original rate will be performed. Then the rate is stepped
 * down and the corresponding entry in the subsequent list will be used.
 * The total number of retries will be limited by the retry limit set by
 * set_tx_retry_limits().
 *
 * Example:
 * original_rate_retries is set to 3 for 54M, 12M and 5M.
 * subsequent_rate_retries is set to 1 for 54M, 12M and 5M.
 * The rate sequence before discarding a frame will be 54M, 54M, 54M, 54M, 12M,
 * 5.5M.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param original_rate_retries The number of retries performed at a certain 
 *                              original rate. Each position in this list is 
 *                              matched with the corresponding position in the 
 *                              "rates" parameter. Hence, both lists must have the 
 *                              same length. Maximum value for each rate is
 *                              255. NULL will avoid setting this value.
 * @param subsequent_rate_retries After original rate retries, the rate is stepped 
 *                                down and the list of subsequent retries is used. 
 *                                Each position in this list is matched with the 
 *                                corresponding position in the "rates" parameter. 
 *                                Hence, both lists must have the same length.
 *                                Maximum value for each rate is 255. NULL 
 *                                will avoid setting this value.
 * @param rates The rates for which rate limits will be set. The retry limit
 *              will only be updated for the rates included in the list. 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_set_tx_retry_limit_by_rate(nrx_context ctx,
                               const nrx_retry_list_t *original_rate_retries,
                               const nrx_retry_list_t *subsequent_rate_retries,
                               const nrx_rate_list_t *rates)
{
   int ret, i, j;
   uint8_t retransmOrig[NO_SUPP_RATES];
   uint8_t retransmNonOrig[NO_SUPP_RATES];
   size_t len = sizeof(supp_rates), len2, len3;
   NRX_ASSERT(ctx);
   NRX_ASSERT(original_rate_retries || subsequent_rate_retries);
   NRX_ASSERT(rates);
   for (i = 0; i < rates->len; i++) 
      NRX_CHECK(_nrx_is_supp_rate(rates->rates[i]));

   len2 = sizeof(retransmOrig);
   if ((ret = nrx_get_mib_val(ctx, "5.2.12", retransmOrig, &len2)))
      return ret;
   len3 = sizeof(retransmNonOrig);
   if ((ret = nrx_get_mib_val(ctx, "5.2.13", retransmNonOrig, &len3)))
      return ret;

   /* Sanity check, vectors have same length */
   if (len != len2 || len != len3)
      return EINVAL;

   /* Match correct place in retransmOrig vector */
   if (original_rate_retries != NULL) {
      for (i = 0; i < rates->len; i++) 
         for (j = 0; j < len; j++)
            if (supp_rates[j] == rates->rates[i]) {
               retransmOrig[j] = original_rate_retries->retries[i];
               break;
            }
      if ((ret =  nrx_set_mib_val(ctx, "5.2.12", retransmOrig, len))) /* retransmissionsOnOrgRat */
         return ret;
   }

   /* Match correct place in retransmNonOrig vector */
   if (subsequent_rate_retries != NULL) {
      for (i = 0; i < rates->len; i++) 
         for (j = 0; j < len; j++)
            if (supp_rates[j] == rates->rates[i]) {
               retransmNonOrig[j] = subsequent_rate_retries->retries[i];
               break;
            }
      
      if ((ret += nrx_set_mib_val(ctx, "5.2.13", retransmNonOrig, len))) /* retransmissionsOnNonOrgRate */
         return ret;
   }

   return 0;
}



/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Lock the TX rate that the device may use</b>
 *
 * Locking a rate forces the device to only use this particular rate
 * when transmitting. The transmission rate adaptation feature will
 * also be forced to use this rate only. Note that a succesful locking
 * of a rate requires a valid rate value supported by the AP. Locking of
 * unsupported rates results in undefined behaviour.
 * 
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param rate The rate that will be used by the device.
 * @return 
 * - 0 on success.
 * - EOPNOTSUPP if the rate is not present in the AP supported rates list.
 * - EINVAL on invalid arguments.
 */
int
nrx_lock_rate(nrx_context ctx, nrx_rate_t rate) 
{
   NRX_ASSERT(ctx);
   NRX_CHECK(_nrx_is_supp_rate(rate));

   return nrx_set_mib_val(ctx, MIB_dot11fixedRate, &rate, sizeof(rate));  
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Unlock the TX rate that the device may use</b>
 *
 * Unlocks a locked TX rate. The device transmits using the rates that both
 * the device and the AP supports as it sees fit. The transmission will
 * start at the initial rate.
 *
 * If rate adaption was enabled when nrx_lock_rate() was called it will still
 * be enabled when nrx_unlock_rate() is called.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_unlock_rate(nrx_context ctx)
{
   nrx_rate_t rate;
   NRX_ASSERT(ctx);
   rate = 0;
   return nrx_set_mib_val(ctx, MIB_dot11fixedRate, &rate, sizeof(rate));
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set the operational rates that the device may use</b>
 *
 * Specify the set of basic and extended rates that the device should
 * support. The default is to support all 802.11b and 802.11g rates.
 * This overrides the registry setting.
 *
 * The rates set will affect association in two ways. 
 *
 * The rates specified in this call will be added to the basic rates
 * advertised by the AP and used as operational rates in the
 * association request with the AP.
 * 
 * Rates specified as basic (high bit set), will guard against
 * association with an AP (BSS) or STA (IBSS) which does not support
 * that rate.
 *
 * Changing the rates during an active association does not change
 * the rates used for that association, the change takes effect when
 * the next association is started.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param rates The list of rates that will be used by the device.
 *
 * @retval 0 on success.
 * @retval EINVAL on invalid arguments.
 */
int
nrx_set_op_rates(nrx_context ctx,
                 const nrx_rate_list_t *rates)
{
   int i;
   struct nrx_ioc_rates param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(rates);
   NRX_CHECK(rates->len >= 1 && rates->len <= NRX_MAX_NUM_RATES);
   for (i = 0; i < rates->len; i++)
      NRX_CHECK(_nrx_is_supp_rate(rates->rates[i] & 0x7f));

   param.num_rates = rates->len;
   memcpy(param.rates, rates->rates, param.num_rates);
   return nrx_nrxioctl(ctx, NRXIOWOPRATES, &param.ioc);
}

/*!
 * @ingroup RADIO
 * @brief Enables HT rates in driver.
 *
 * HT rate support is dependent on driver, firmware, and hardware
 * support. This function does not indicate an error if HT rate
 * support is missing.
 *
 * @retval 0 on success
 */
int
nrx_enable_ht_rates(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);
   param.value = 1;
   return nrx_nrxioctl(ctx, NRXIOWENABLEHTRATES, &param.ioc);
}
                    
/*!
 * @ingroup RADIO
 * @brief Disables HT rates in driver.
 *
 * HT rate support is dependent on driver, firmware, and hardware
 * support. This function does not indicate an error if HT rate
 * support is missing.
 *
 * @retval 0 on success
 */
int
nrx_disable_ht_rates(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);
   param.value = 0;
   return nrx_nrxioctl(ctx, NRXIOWENABLEHTRATES, &param.ioc);
}
                    
/*!
 * @ingroup RADIO
 * @brief Return whether HT rates are enabled.
 *
 * This function will report TRUE if HT rates are currently enabled in
 * the driver. If it reports FALSE, HT rates are either disabled (with
 * nrx_disable_ht_rates()), or HT rate support is missing from the
 * driver, firmware or hardware.
 *
 * @retval 0 on success
 */
int
nrx_ht_rates_enabled(nrx_context ctx, nrx_bool *enabled)
{
   struct nrx_ioc_bool param;
   int ret;
   NRX_ASSERT(ctx);
   ret = nrx_nrxioctl(ctx, NRXIORENABLEHTRATES, &param.ioc);
   if(ret != 0)
      return ret;
   *enabled = param.value;
   return 0;
}

/*!
 * @ingroup MISC
 * @brief <b>Enable WMM</b>
 *
 * Note that this function will only enable WMM, i.e not WMM PS.
 * To enable WMM PS, nrx_enable_wmm_ps() should be called after this function
 * has been called.
 *
 * This configuration should happen before association, or be followed by a
 * reassociation, as the parameters are used in the association
 * negotiation with the AP.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_enable_wmm(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);
   param.value = 1;
   return nrx_nrxioctl(ctx, NRXIOWWMM, &param.ioc);
}

/*!
 * @ingroup MISC
 * @brief <b>Disable WMM</b>
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_disable_wmm(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);
   param.value = 0;
   return nrx_nrxioctl(ctx, NRXIOWWMM, &param.ioc);
}

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Reassociate with the current AP</b>
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_reassociate(nrx_context ctx)
{
   struct nrx_ioc ioc;
   NRX_ASSERT(ctx);
   return nrx_nrxioctl(ctx, NRXIOCREASSOCIATE, &ioc);
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set allowed channels</b>
 *
 * The channel_list parameter specifies the channels that should be allowed to
 * use. The channels used vary in different regions of the world.
 * In most of Europe thirteen channels are allowed. In Japan also channel 14
 * may be used.
 * Note that nrx_set_region_code() can also be used to change the channels
 * used.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param ch The list of allowed channels.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_set_channel_list(nrx_context ctx,
                     const nrx_ch_list_t *ch)
{
   int i;
   struct nrx_ioc_channels param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(ch);
   NRX_CHECK(ch->len >= 1 && ch->len <=NRX_MAX_CHAN_LIST_LEN);

   for(i = 0; i < ch->len; i++) {
      if((ch->channel[i] & 0xff00) != 0) 
         /* upper byte set means some other channel domain than the
          * default, for example US Public Safety seem to use channel
          * 5, 10, .. 95 in the 5GHz band. We don't support any of
          * that for now. */
         return EINVAL;
      param.channel[i] = ch->channel[i] & 0xff;
   }
   param.num_channels = i;
   return nrx_nrxioctl(ctx, NRXIOWREGCHANNELS, &param.ioc);
}


/*!
 * @ingroup RADIO
 * @brief <b>Set the region code</b>
 *
 * The region code defines which channels and transmission power levels that are
 * allowed in the current physical location. Setting the region code
 * limits the device to those channels and tx power levels that are allowed
 * in that region. Note that the region code is different from the country
 * code, which is standardized. The region code is a proprietary code that
 * defines wider geographical regions (Japan, Americas and EMEA).
 * Note that nrx_set_channel_list() will change the same channel settings.
 *
 * 802.11d has priority over this setting in the current
 * implementation. The region code setting will only
 * apply when no 802.11d information is available from
 * the AP. 
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param code Valid region codes are: NRX_REGION_JAPAN, NRX_REGION_AMERICA
 *             and NRX_REGION_EMEA (Europe, Middle East, Africa)
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_set_region_code(nrx_context ctx,
                    nrx_region_code_t code)
{
   uint32_t vect[3];
   NRX_ASSERT(ctx);

   switch (code) {
      case NRX_REGION_JAPAN:
         vect[0] = 1;  /* Start channel */
         vect[1] = 14; /* No of channels */
         vect[2] = 17; /* Power in dBm */
         break;
      case NRX_REGION_AMERICA:
         vect[0] = 1;  /* Start channel */
         vect[1] = 11; /* No of channels */
         vect[2] = 17; /* Power in dBm */
         break;
      case NRX_REGION_EMEA:
         vect[0] = 1;  /* Start channel */
         vect[1] = 13; /* No of channels */
         vect[2] = 17; /* Power in dBm */
         break;
      default:
         return EINVAL;
   }
#define MIB_dot11MultidomainCapabilityTable(x) ("1.7." #x ".2") /* Setting MIB "1.7.1.2" resets remaining 1.7.*.2 MIBs */
   return nrx_set_mib_val(ctx, MIB_dot11MultidomainCapabilityTable(1), /* Several calls needed if list has holes. */
                          vect, sizeof(vect));
#undef MIB_dot11MultidomainCapabilityTable

}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Reset traffic filter discard counters</b>
 *
 * Reset the statistic counters for filtered frames. 
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_reset_traffic_filter_counters(nrx_context ctx)
{
   const int count = 0;
   NRX_ASSERT(ctx);
   return nrx_set_mib_val(ctx, MIB_dot11trafficFilterCounter, &count, sizeof(count));
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get a traffic filter discard counter</b>
 *
 * The device keeps a counter for the number of frames discarded
 * by the traffic filter.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param cntr Pointer to the output buffer that will hold the requested
 *             counter value (in host byteorder).
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_get_traffic_filter_counters(nrx_context ctx,
                                uint32_t *cntr)
{
   size_t len = sizeof(*cntr);
   NRX_ASSERT(ctx);
   NRX_ASSERT(cntr);

   return nrx_get_mib_val(ctx, MIB_dot11trafficFilterCounter, cntr, &len);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Enable link monitoring</b>
 * 
 * The device monitors received beacon frames when link monitoring is
 * enabled.  When the specified percentage of beacon frames within a
 * monitoring interval has been missed (the link has failed), a
 * trigger is generated. It is possible to register a callback on this
 * trigger.
 *
 * As an example, setting interval to 5 (beacons) and
 * miss_thres to 80 (%), will notify the caller when 4 out of 5
 * beacon frames have been missed. When the device is in power
 * save mode the interval will be scaled by the DTIM period
 * (nrx_conf_ps() parameter listen_interval) - the interval
 * defines the number of beacons that was expected to be received.
 *
 * Link monitoring is only enabled when the device is associated
 * and it only monitors beacons for the associated access point.
 * 
 * At handover to another AP, an automatic adjustment of internal timers
 * to the new AP will be done such that the number of missed beacons 
 * always will be constant.
 *
 * There exists a default link monitoring in the firmware that may terminate a poor link, 
 * which will conflict with this command, see parameters for more details. The internal 
 * termination of the link can be enabled/disabled. When disabled the user himself has 
 * the responsibility of terminating a poor link as it will no longer be done automatically by 
 * drivers/firmware. To disable the internal default link monitoring, write
 * \code
 * echo 0 > /proc/driver/ifname/config/link-monitoring
 * \endcode
 * where ifname is the interface name (e.g. eth1). To enable, write
 * \code
 * echo 1 > /proc/driver/ifname/config/link-monitoring
 * \endcode
 * 
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id output buffer. This identifies the threshold
 *               so that several thresholds can be defined. 
 *               The id value is filled in by the function call.
 * @param interval The link monitoring interval in number of beacons.  Should several
 *                 link monitoring triggers with different intervals be registered the
 *                 shortest interval will be choosen (necessary due to firmware 
 *                 limitations).
 *                 Minimum value is 1 beacon. Maximum value is 2^32-1, but will most
 *                 likely result in undefined behavior. Only values up to 10 will work
 *                 under all circumstances.  Consider the following firmware limits
 *                 that affect this parameter
 *                 - When enabled, the internal firmware link monitoring has a fix limit of 20 consecutive 
 *                   missed beacons thereafter the link will be considered terminated. 
 *                   As link monitoring is done only when associated, it will be halted at this 
 *                   point. To be guaranteed that link monitoring reaches 100% before it is halted, 
 *                   this parameter must be set to half the firmware limit or less, i.e. max 10. 
 *                   Alternatively, the internal termination of the link can be disabled with the
 *                   implications mentioned above. 
 *                 - Firmware has a max-timeout limitation of roughly 35 minutes. Although unlikely,
 *                   the 802.11 protocol allows an AP to have an interval between beacons of
 *                   maximally 65 seconds (2^16-1 ms). Hence, maximum 32 beacons can be 
 *                   guaranteed not to overflow the internal timer. Should an overflow occur, the
 *                   behavior is undefined.
 *                   
 * @param miss_thres The miss threshold in percent. Range 1-99.
 * 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - EBUSY when previous link monitoring have not been disabled first. 
 */
int
nrx_enable_link_monitoring(nrx_context ctx,
                           int32_t *thr_id,
                           uint32_t interval,
                           uint8_t miss_thres)
{
   int ret;
/*    struct nrx_ioc_bool param; */
   NRX_ASSERT(ctx);
   NRX_ASSERT(thr_id);
   NRX_CHECK(interval >= 1);
   NRX_CHECK(miss_thres >= 1 && miss_thres <= 99);

   /* Set trigger */
   ret = nrx_register_mib_trigger(ctx,
                                  thr_id,
                                  MIB_dot11beaconLossRate,
                                  0,
                                  interval,                /* unit is in beacons */
                                  miss_thres,
                                  NRX_THR_RISING,          /* trig dir */
                                  1,                       /* Number of times below level before trigger */
                                  1);                      /* Continuous trigging */
   return ret;
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable link monitoring</b>
 *
 * The link monitoring feature is disabled and no further notifications
 * of link failure will be made.
 *
 * Calling this function will cancel corresponding callbacks which are
 * using the same thr_id.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id identifying the threshold trigger that should
 *               be disabled. 
 * 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments or non-existing thr_id.
 */
int
nrx_disable_link_monitoring(nrx_context ctx, 
                            int32_t thr_id)
{
   int ret;
   nrx_bool does_exist; 

   NRX_ASSERT(ctx);

   ret = nrx_check_trigger_existence(ctx, thr_id, MIB_dot11beaconLossRate, &does_exist);
   if (ret != 0)
      return ret;
   NRX_CHECK(thr_id != 0 && does_exist);

   return nrx_del_mib_trigger(ctx, thr_id);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Register link monitoring callback</b>
 * 
 * This will register a callback for the link monitoring triggers, see
 * nrx_enable_link_monitoring for further details.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id. This identifies the threshold
 *              so that several thresholds can be defined. 
 *              The id value is obtained from nrx_enable_link_monitoring.
 * @param cb The callback function that is to be invoked by threshold notifications.
 *           The callback is invoked with operation NRX_CB_TRIGGER on a 
 *           successful notification whereupon event_data will be a pointer 
 *           to a nrx_event_mibtrigger structure which contains further 
 *           information. When the threshold is cancelled cb is called 
 *           with operation NRX_CB_CANCEL and event_data set to NULL.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *               be passed to the callback on invocation. This is for
 *               caller use only, it will not be parsed or modified in
 *               any way by this library. This parameter can be NULL.
 * 
 * @return A handle to a callback (an unsigned integer type). The only
 * use for this is to pass it to nrx_cancel_link_monitoring_callback
 * to cancel the callback.
 * @retval Zero on memory allocation failure
 * @retval Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_link_monitoring_callback(nrx_context ctx,
                                      int32_t thr_id,
                                      nrx_callback_t cb,
                                      void *cb_ctx)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);
   return nrx_register_mib_trigger_event_handler(ctx, thr_id, cb, cb_ctx);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Cancel link monitoring callback</b>
 * 
 * This will cancel a callback for the link monitoring triggers.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle Callback handle obtained from nrx_register_link_monitoring_callback.
 * The handle will no longer be valid after this call.
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters, e.g the handle is not registered.
 */
int
nrx_cancel_link_monitoring_callback(nrx_context ctx,
                                    nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_mib_trigger_event_handler(ctx, handle);
}


/*!
 * 
 * @brief <b>Enable link supervision features in the NIC</b>
 *
 * A deauthentication will be generated when NIC internally determines
 * the link to be faulty. Three types of link supervision can done by
 * the NIC; rx fail of beacons, tx fail and roundtrip fail. These may
 * be individually enabled/disabled and configured by the functions
 * nrx_conf_link_supervision_rx_beacon(),
 * nrx_conf_link_supervision_tx() and
 * nrx_conf_link_supervision_roundtrip().
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return
 *  - 0 on success,
 *  - an error code on failure.
 */
int
nrx_enable_link_supervision(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);
   param.value = 1;
   return nrx_nrxioctl(ctx, NRXIOWLINKSUPERV, &param.ioc);
}


/*!
 * @internal
 * @brief <b>Disable all link supervision features in the NIC</b>
 *
 * This will stop firmware from doing supervison of the link. 
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return
 *  - 0 on success,
 *  - an error code on failure.
 */
int
nrx_disable_link_supervision(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);
   param.value = 0;
   return nrx_nrxioctl(ctx, NRXIOWLINKSUPERV, &param.ioc);
}


/*!
 * 
 * @brief <b>Configuration of RX beacons criteria for link supervision</b>
 *
 * Sets the minimum number of consecutive beacons that should be
 * missed and the minimum time since last heard beacon that should
 * have elapsed before link is voluntary terminated.
 *
 * Link supervision for RX beacon must be enabled for this function to
 * have an effect. Both the criteria of beacon count and timeout must
 * be fulfilled before deauthentication.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param rx_fail_limit Minimum number of missed beacons before firmware
 *        will assume link is terminated. 0 will disable this 
 *        criteria.
 *
 * @param rx_fail_timeout Minimum time in milliseconds since last received
 *        beacons before deauthentication. This value is rounded
 *        upwards to the nearest beacon. 0 will disable the timeout
 *        criteria. Maximum supported time is 35 minutes (2100000 ms).
 *
 * @return
 *  - 0 on success,
 *  - an error code on failure.
 */
int
nrx_conf_link_supervision_rx_beacon(nrx_context ctx,
                                    uint32_t rx_fail_limit, 
                                    uint32_t rx_fail_timeout)
{
   int ret;
   struct nrx_ioc_uint32_t param;
   NRX_ASSERT(ctx);
   NRX_CHECK(rx_fail_timeout<=2100000);

   param.value = rx_fail_limit;
   ret = nrx_nrxioctl(ctx, NRXIOWLINKSUPERVRXBEACONCOUNT, &param.ioc);
   if (ret != 0)
      return ret;

   param.value = rx_fail_timeout * 1000; /* ms to us */
   return nrx_nrxioctl(ctx, NRXIOWLINKSUPERVRXBEACONTIMEOUT, &param.ioc);
}


/*!
 * 
 * @brief <b>Configuration of TX fail criteria for link supervision</b>
 *
 * NIC will generate a deauthentication indication when it has failed to
 * transmit a number of frames.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param tx_fail_limit Number of consecutive attempts to transmit a
 *        frame that have failed. No ACK has been received in any of the
 *        attempts to send. Minimum number is 1 attempt. 0 will disable this
 *        feature.
 *
 * @return
 *  - 0 on success,
 *  - an error code on failure.
 */
int
nrx_conf_link_supervision_tx(nrx_context ctx, uint32_t tx_fail_limit)
{
   struct nrx_ioc_uint32_t param;
   NRX_ASSERT(ctx);
   param.value = tx_fail_limit;
   return nrx_nrxioctl(ctx, NRXIOWLINKSUPERVTXFAILCOUNT, &param.ioc);
}


/*!
 * @internal
 * @brief <b>Configuration of roundtrip criteria for link supervision</b>
 *
 * This will test the link by attempting to send a roundtrip
 * message. The feature will only be used for a few AP configurations
 * where the link status can not be detected unless a roundtrip
 * message is sent.
 *
 * The feature will monitor traffic and when enough packets are
 * transmitted without any reply, the link will be determined
 * faulty. When an AP is connected, the monitoring is initially
 * passive and it is assumed that other traffic (e.g. DHCP) will
 * generate enough statistics to determine the link status. After the
 * passive period, the feature will inject its own packets to improve
 * the statistical confidence.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param roundtrip_fail_limit Should no reply have been received
 *        after this number of transmitted messages, the link is 
 *        determined faulty. 0 will disable the roundtrip feature.
 * 
 * @param silent_intervals Number of intervals to wait before this
 *        feature inject its own packets to the recently connected
 *        AP. Each interval is at least 100 ms and could be
 *        considerably longer in power save mode. Should this be set
 *        to 0, there will be no silent period upon which packets are
 *        transmitted immediately after an AP is connected. Should
 *        this be 0xffffffff, the feature will work in passive mode
 *        only and not inject any own packets.
 *
 * @return
 *  - 0 on success,
 *  - an error code on failure.
 */
int 
nrx_conf_link_supervision_roundtrip(nrx_context ctx,
                                    uint32_t roundtrip_fail_limit,
                                    uint32_t silent_intervals)
{
   struct nrx_ioc_uint32_t param;
   int ret;
   NRX_ASSERT(ctx);
   param.value = roundtrip_fail_limit;
   ret = nrx_nrxioctl(ctx, NRXIOWLINKSUPERVRTRIPCOUNT, &param.ioc);
   if (ret != 0)
      return ret;
   param.value = silent_intervals;
   return nrx_nrxioctl(ctx, NRXIOWLINKSUPERVRTRIPSILENT, &param.ioc);
}



/*!
 * @internal
 * Internal structure for filtering connection lost types.
 */
struct nrx_conn_lost_notification_t {
      nrx_conn_lost_type_t type;
      nrx_callback_t handler;
      void *user_data;
};

/*!
 * @internal
 *
 * This function will filter so that only messages subscribed for 
 * are passed along to the user's callback.  
 *
 */
static int
nrx_handle_conn_lost_notification(nrx_context ctx,
                                  int operation,
                                  void *event_data,
                                  size_t event_data_size,
                                  void *user_data)
{
   struct nrx_conn_lost_notification_t *private_p = (struct nrx_conn_lost_notification_t *)user_data;
   struct nrx_we_custom_data *cdata = event_data;
   struct nrx_conn_lost_data cl;
   uint32_t type = ~0;
   int i;
   
   if(operation == NRX_CB_CANCEL) {
      (*private_p->handler)(ctx, NRX_CB_CANCEL, NULL, 0, private_p->user_data);
      free(private_p);
      return 0;
   }
   for(i = 0; i < cdata->nvar; i++) {
      if(strcmp(cdata->var[i], "type") == 0) {
         type = strtoul(cdata->val[i], NULL, 0);
      }
      if(strcmp(cdata->var[i], "bssid") == 0) {
         nrx_string_to_binary(cdata->val[i], 
                              cl.bssid.octet, 
                              sizeof(cl.bssid.octet));
      }
      if(strcmp(cdata->var[i], "reason") == 0) {
         cl.reason_code = strtoul(cdata->val[i], NULL, 0);
      }
   }
   switch(type) {
      case 0:/* WE_CONN_LOST_AUTH_FAIL */
         cl.type = NRX_BLR_AUTH_FAIL;
         break;
      case 1:/* WE_CONN_LOST_ASSOC_FAIL */
         cl.type = NRX_BLR_ASSOC_FAIL;
         break;
      case 2:/* WE_CONN_LOST_DEAUTH */
         cl.type = NRX_BLR_DEAUTH;
         break;
      case 3:  /* WE_CONN_LOST_DISASS */
         cl.type = NRX_BLR_DEAUTH;
         break;
      default:
         cl.type = 0;
         break;
   }
   if (private_p->type == 0 ||
       (cl.type & private_p->type) != 0) {
      (*private_p->handler)(ctx, operation, &cl, 
                          sizeof(cl), 
                          private_p->user_data);
   }
   return 0;
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Enable connection failure notification</b>
 *
 * Notify the caller when a connection or a connection attempt has
 * failed. The caller can elect to be notified on distinct association
 * failures : authentication failures, association failures and
 * disassociations. The callback will be invoked every time (until disabled)
 * the condition is met.
 *
 * This function can be called several times, e.g. with different notification policies for 
 * different callbacks.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param type Bitmask that defines the notification reasons to use.
 * @param cb Callback that is invoked to notify the caller that
 *           the a connection or connection attempt has failed.
 *           The callback is invoked with operation NRX_CB_TRIGGER on
 *           a successful notification. It will be passed a pointer to
 *           struct nrx_conn_lost_data, which identifies the reason
 *           and type of the disconnect event, for event_data. 
 *           nrx_conn_lost_data also contains the BSSID of the 
 *           disconnecting entity. If the disconnect decision was made
 *           by the driver or firmware then the BSSID field will contain
 *           the MAC address of the device.
 *           Memory for event_data will be freed immediately
 *           after the callback has returned.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *               be passed to the callback on invocation. This is for
 *               caller use only, it will not be parsed or modified in
 *               any way by this library. This parameter can be NULL.
 * @return 
 * - Zero on memory allocation failure
 * - Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_conn_lost_notification(nrx_context ctx,
                                    nrx_conn_lost_type_t type,
                                    nrx_callback_t cb,
                                    void *cb_ctx)
{
   struct nrx_conn_lost_notification_t *cln;
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);
   
   cln = malloc(sizeof(*cln));
   if (cln == NULL)
      return 0;
   cln->type = type;
   cln->handler = cb;
   cln->user_data = cb_ctx;
   return nrx_register_custom_event(ctx, 
                                    "CONNLOST", 
                                    0,
                                    nrx_handle_conn_lost_notification,
                                    cln);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable connection failure notification</b>
 *
 * The connection failure notification feature is disabled. No further
 * connection failure notifications will be made.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle A handle previously obtained from
 *        nrx_register_conn_lost_notification.
 *        The handle will no longer be valid after this call.
 * 
 * @return
 * - 0 on success.
 * - EINVAL on invalid parameters, e.g the handle is not registered.
 */
int
nrx_cancel_conn_lost_notification(nrx_context ctx, nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_custom_event(ctx, handle);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Enable connection incompatible notification</b>
 *
 * Notify the caller when a connection is detected to be
 * incompatible. The connection will still remain valid, no
 * disconnection will be done, but reconfiguration is neccessary for
 * traffic to pass on the connection. The callback will be invoked
 * every time (until disabled) the condition is met.
 *
 * This function can be called several times. Calling this function
 * will not unregister any other callbacks.
 *
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param cb Callback that is invoked to notify the caller that
 *           the connection is invalid. The callback is invoked with
 *           operation NRX_CB_TRIGGER on a successful notification. It
 *           will be passed a NULL pointer to struct
 *           nrx_conn_lost_data.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *               be passed to the callback on invocation. This is for
 *               caller use only, it will not be parsed or modified in
 *               any way by this library. This parameter can be NULL.
 * @return 
 * - Zero on memory allocation failure
 * - Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_conn_incompatible_notification(nrx_context ctx,
                                            nrx_callback_t cb,
                                            void *cb_ctx)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);

   return nrx_register_custom_event(ctx, 
                                    "CONNINCOMPATIBLE", 
                                    0,
                                    cb,
                                    cb_ctx);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable connection incompatible notification</b>
 *
 * The connection incompatible notification feature is disabled. No
 * further connection incompatible notifications will be made.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle A handle previously obtained from
 *        nrx_register_conn_incompatible_notification.  The handle
 *        will no longer be valid after this call.
 * @return
 * - 0 on success.
 * - EINVAL on invalid parameters, e.g the handle is not registered.
 */
int
nrx_cancel_conn_incompatible_notification(nrx_context ctx, nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_custom_event(ctx, handle);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set an SNR threshold trigger</b>
 *
 * The callback is invoked every time (until disable) the Signal to noise
 * ratio passes the defined threshold in the direction specified.
 * Several thresholds can be defined. They are identified by the 
 * thr_id parameter. There is a dynamic limit to the number of
 * triggers that can exist in the system so this call may fail if the
 * limit would be passed. The limit depends on the available memory on the host.
 * 
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id output buffer. This identifies the threshold
 *              so that several thresholds can be defined. 
 *              The id value is filled in by the function call.
 *              
 * @param snr_thr The SNR threshold in dB. Minimum value is 0 and maximum is 
 *        40, which should be sufficient on current hardware.
 * @param chk_period The SNR threshold check period in milliseconds. The minimum value is 
 *        100 and maximum supported time is 35 minutes (2100000 ms). The SNR threshold 
 *        will be compared with the current SNR using this period.
 * @param dir Bitmask defining the trigger directions. The callback can be
 *            triggered when the value rises above or falls below the
 *            threshold, or both.
 * @param type Bitmask that defines if the threshold trigger should apply to the SNR
 *             of beacon or data frames.
 *                    
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - TODO if no more triggers can be created.
*/
int
nrx_enable_snr_threshold(nrx_context ctx, 
                         int32_t *thr_id,
                         uint32_t snr_thr,
                         uint32_t chk_period,
                         nrx_thr_dir_t dir,
                         nrx_detection_target_t type)
{
   char *mib;
   int ret;
   NRX_ASSERT(ctx);
   NRX_ASSERT(thr_id);
   NRX_CHECK(chk_period>=100);
   NRX_CHECK(chk_period<=2100000);
   NRX_CHECK(snr_thr >= 0 && snr_thr <= 40);
   NRX_CHECK(dir!=0);
   NRX_CHECK((dir & ~NRX_THR_RISING & ~NRX_THR_FALLING) == 0);

   switch (type) {
      case NRX_DT_BEACON:
         mib = MIB_dot11snrBeacon;
         break;
      case NRX_DT_DATA:
         mib = MIB_dot11snrData;
         break;
      default:
         return EINVAL;
   }

   ret = nrx_register_mib_trigger(ctx, 
                                  thr_id,
                                  mib,
                                  0,
                                  chk_period*1000, /* conv to microseconds */
                                  snr_thr,
                                  dir,         /* trig dir */
                                  1,           /* Number of times below level before trigger */
                                  1);          /* Continuous trigging */
   return ret;
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable an SNR threshold</b>
 *
 * The specified SNR threshold trigger is disabled. The specified
 * trigger in the device is cleared and no further notifications from
 * this trigger will be made.
 *
 * Calling this function will cancel corresponding callbacks which are
 * using the same thr_id.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id identifying the threshold trigger that should
 *               be disabled.
 * 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments or non-existing thr_id.
 */
int
nrx_disable_snr_threshold(nrx_context ctx,
                          int32_t thr_id)
{
   int ret;
   nrx_bool does_exist;

   NRX_ASSERT(ctx);

   ret = nrx_check_trigger_existence(ctx, thr_id, MIB_dot11snrBeacon, &does_exist);
   if (ret != 0)
      return ret;
   if ( ! does_exist) {
      ret = nrx_check_trigger_existence(ctx, thr_id, MIB_dot11snrData, &does_exist);
      if (ret != 0)
         return ret;
      NRX_CHECK(thr_id != 0 && does_exist);
   }

   return nrx_del_mib_trigger(ctx, thr_id);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Register an SNR threshold callback</b>
 *
  * This will register a callback for a SNR threshold, see
  * nrx_enable_snr_threshold for further details. Several callbacks
 * can be registered to the same threshold.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id. This identifies the threshold
 *              so that several thresholds can be defined. 
 *              The thr_id value is obtained from nrx_enable_snr_threshold.
 * @param cb The callback function that is to be invoked by threshold notifications.
 *           The callback is invoked with operation NRX_CB_TRIGGER on a 
 *           successful notification whereupon event_data will be a pointer 
 *           to a nrx_event_mibtrigger structure which contains further 
 *           information. When the threshold is cancelled cb is called 
 *           with operation NRX_CB_CANCEL and event_data set to NULL.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *               be passed to the callback on invocation. This is for
 *               caller use only, it will not be parsed or modified in
 *               any way by this library. This parameter can be NULL.
 * 
 * @return A handle to a callback (an unsigned integer type). The only
 * use for this is to pass it to nrx_cancel_snr_threshold_callback
 * to cancel the callback.
 * @retval Zero on memory allocation failure.
 * @retval Non-zero a valid callback handle.
*/
nrx_callback_handle
nrx_register_snr_threshold_callback(nrx_context ctx,
                                    uint32_t thr_id,
                                    nrx_callback_t cb,
                                    void *cb_ctx)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);
   return nrx_register_mib_trigger_event_handler(ctx, thr_id, cb, cb_ctx);
}
                                     
/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Cancel an SNR threshold callback</b>
 *
 * This will cancel the callback for a SNR threshold.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle Callback handle obtained from nrx_register_snr_threshold_callback.
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid arguments, e.g. the callback is not registered.
 */
int
nrx_cancel_snr_threshold_callback(nrx_context ctx,
                                  nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_mib_trigger_event_handler(ctx, handle);
}
                                     
/*!
 * @ingroup RADIO
 * @brief <b>Get the current SNR</b>
 *
 * The most recently measured signal to noise ratio for beacon or data
 * frames is reported. 
 * The signal to noise ratio is measured as an average over the last 16 beacons
 * or data frames (using a sliding window). The size of the sliding window
 * can be configured with nrx_conf_snr().
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param snr Pointer to the output buffer that will hold the requested
 *            SNR value in dB (in host byteorder).
 * @param type Defines if the requested SNR value should come from
 *             beacon  (NRX_DT_BEACON) or data (NRX_DT_DATA) frames.
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_get_snr(nrx_context ctx,
            int32_t *snr,
            nrx_detection_target_t type)
{
   size_t len = sizeof(*snr);
   NRX_ASSERT(ctx);
   NRX_ASSERT(snr);
   
   switch(type) {
      case NRX_DT_BEACON:
         return nrx_get_mib_val(ctx, MIB_dot11snrBeacon, snr, &len);
      case NRX_DT_DATA:
         return nrx_get_mib_val(ctx, MIB_dot11snrData, snr, &len);
   }

   return EINVAL;
}



/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set an RSSI threshold trigger</b>
 *
 * The callback is invoked every time (until disabled) the RSSI ratio passes
 * the defined threshold in the direction specified.  Several thresholds
 * can be defined. They are identified by the thr_id
 * parameter. There is a dynamic limit to the number of
 * triggers that can exist in the system so this call may fail if the
 * limit would be passed. The limit depends on the available memory on the host.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id output buffer. This identifies the threshold
 *              so that several thresholds can be defined.  
 *              The id value is filled in by the function call.
 *              
 * @param rssi_thr The RSSI threshold in dBm (signed). Useful RSSI thresholds lie
 *                 between 0 and -120.
 * @param chk_period The RSSI check period in milliseconds. The minimum value is 100 ms and 
 *        maximum supported time is 35 minutes (2100000 ms). The RSSI threshold will be compared with 
 *        the current RSSI value using this period.
 *
 * @param dir Bitmask defining the trigger directions. The callback can be triggered
 *            when the value rises above or falls below the threshold, or both.
 * @param type Defines if the threshold trigger should apply to the RSSI
 *             of beacon or data frames.
 *                    
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - ENOMEM if no more triggers can be created.
*/
int
nrx_enable_rssi_threshold(nrx_context ctx,
                          int32_t *thr_id,
                          int32_t rssi_thr,
                          uint32_t chk_period,
                          nrx_thr_dir_t dir,
                          nrx_detection_target_t type)
{
   char *mib;
   int ret;
   NRX_ASSERT(ctx);
   NRX_ASSERT(thr_id);
   NRX_CHECK(chk_period>=100);
   NRX_CHECK(chk_period<=2100000);
   NRX_CHECK(rssi_thr >= -120 && rssi_thr <= 0);
   NRX_CHECK(dir!=0);
   NRX_CHECK((dir & ~NRX_THR_RISING & ~NRX_THR_FALLING) == 0);

   switch (type) {
      case NRX_DT_BEACON:
         mib = MIB_dot11rssi;
         break;
      case NRX_DT_DATA:
         mib = MIB_dot11rssiDataFrame;
         break;
      default:
         return EINVAL;
   }

   ret = nrx_register_mib_trigger(ctx, 
                                  thr_id,
                                  mib,
                                  0,
                                  chk_period*1000, /* conv to microseconds */
                                  rssi_thr,
                                  dir,         /* trig dir */
                                  1,           /* Number of times below level before trigger */
                                  1);          /* Continuous trigging */
   return ret;
}



/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable an RSSI threshold</b>
 *
 * The specified RSSI threshold trigger is disabled. The specified
 * trigger in the device is cleared and no further notifications from
 * this trigger will be made.
 *
 * Calling this function will cancel corresponding callbacks which are
 * using the same thr_id.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id identifying the threshold trigger that should
 *               be disabled.
 * 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments or non-existing thr_id.
 */
int
nrx_disable_rssi_threshold(nrx_context ctx,
                           int thr_id)
{
   int ret;
   nrx_bool does_exist;

   NRX_ASSERT(ctx);

   ret = nrx_check_trigger_existence(ctx, thr_id, MIB_dot11rssi, &does_exist);
   if (ret != 0)
      return ret;
   if ( ! does_exist) {
      ret = nrx_check_trigger_existence(ctx, thr_id, MIB_dot11rssiDataFrame, &does_exist);
      if (ret != 0)
         return ret;
      NRX_CHECK(thr_id != 0 && does_exist);
   }

   return nrx_del_mib_trigger(ctx, thr_id);
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Register an RSSI threshold callback</b>
 *
 * This will register a callback for a RSSI threshold, see
 * nrx_enable_rssi_threshold for further details. Several callbacks
 * can be registered to the same threshold.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id. This identifies the threshold
 *               so that several thresholds can be defined. 
 *               The thr_id value is obtained from nrx_enable_rssi_threshold.
 * @param cb The callback function that is to be invoked by threshold notifications.
 *           The callback is invoked with operation NRX_CB_TRIGGER on a 
 *           successful notification whereupon event_data will be a pointer 
 *           to a nrx_event_mibtrigger structure which contains further 
 *           information. When the threshold is cancelled cb is called 
 *           with operation NRX_CB_CANCEL and event_data set to NULL.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *               be passed to the callback on invocation. This is for
 *               caller use only, it will not be parsed or modified in
 *               any way by this library. This parameter can be NULL.
 * 
 * @return A handle to a callback (an unsigned integer type). The only
 * use for this is to pass it to nrx_cancel_rssi_threshold_callback
 * to cancel the callback.
 * @retval Zero on memory allocation failure.
 * @retval Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_rssi_threshold_callback(nrx_context ctx,
                                     uint32_t thr_id,
                                     nrx_callback_t cb,
                                     void *cb_ctx)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);
   return nrx_register_mib_trigger_event_handler(ctx, thr_id, cb, cb_ctx);
}
                                     
/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Cancel an RSSI threshold callback</b>
 *
 * This will cancel the callback for a RSSI threshold.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle Callback handle obtained from nrx_register_rssi_threshold_callback.
 *
 * @return
 * - 0 on success.
 * - EINVAL on invalid parameters, e.g the handle is not registered.
 */
int
nrx_cancel_rssi_threshold_callback(nrx_context ctx,
                                   nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_mib_trigger_event_handler(ctx, handle);
}
                                     



/*!
 * @ingroup RADIO
 * @brief <b>Get the current RSSI</b>
 *
 * The most recently measured signal strength for beacon or data
 * frames is reported.
 * The signal strength is measured as an average over the last 16 beacons or
 * data frames (using a sliding window). The size of the sliding window can
 * be configured with nrx_conf_rssi().
 *
 * @param ctx  NRX context that was created by the call to nrx_init_context().
 * @param rssi Pointer to the output buffer that will hold the requested
 *             RSSI value in dBm (in host byteorder).
 * @param type Defines if the requested RSSI value should
 *             come from beacon (NRX_DT_BEACON) or data (NRX_DT_DATA) frames.
 *
 * @note
 * The meaning of the type parameter changed from and/or to or
 * as it's unclear what beacon or data would mean in this context.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_rssi(nrx_context ctx,
             int32_t *rssi,
             nrx_detection_target_t type)
{
   size_t len = sizeof(*rssi);
   NRX_ASSERT(ctx);
   NRX_ASSERT(rssi);
   
   switch(type) {
      case NRX_DT_BEACON:
         return nrx_get_mib_val(ctx, MIB_dot11rssi, rssi, &len);
      case NRX_DT_DATA:
         return nrx_get_mib_val(ctx, MIB_dot11rssiDataFrame, rssi, &len);
   }

   return EINVAL;
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set the RSSI calculation window size</b>
 *
 * Configure the number of beacons and data frames that are
 * used for the RSSI averageing calculation. The windows
 * are specified as powers of two for performance reasons.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param win_size_beacon_order The RSSI calculation window for beacons.
 *        The window size is a power of two, the actual window used
 *        will be 2^win_size_beacon_order. Allowed values are  0, 1, 2, 3 and 4.
 * @param win_size_data_order The RSSI calculation window for data frames.
 *        The window size is a power of two, the actual window used
 *        will be 2^win_size_data_order. Allowed values are  0, 1, 2, 3 and 4.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
*/
int
nrx_conf_rssi(nrx_context ctx,
              uint8_t win_size_beacon_order,
              uint8_t win_size_data_order)
{
   int err;

   NRX_CHECK(win_size_beacon_order <= 4);
   NRX_CHECK(win_size_data_order <= 4);
/*    NRX_CHECK(win_size_beacon_order >= 0); */
/*    NRX_CHECK(win_size_data_order >= 0); */

   win_size_beacon_order = 1 << win_size_beacon_order;
   win_size_data_order   = 1 << win_size_data_order;

   /* 5.2.16.1 Number of RSSI data frames used for average calculation */
   err = nrx_set_mib_val(ctx, "5.2.16.1", &win_size_data_order, sizeof(win_size_data_order));
   if (err)
      return err;

   /* 5.2.16.2 Number of RSSI beacon frames used for average calculation */
   return nrx_set_mib_val(ctx, "5.2.16.2", &win_size_beacon_order, sizeof(win_size_beacon_order));
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set the SNR calculation window size</b>
 *
 * Configure the number of beacons and data frames that are
 * used for the SNR averageing calculation. The windows
 * are specified as powers of two for performance reasons.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param win_size_beacon_order The SNR calculation window for beacons.
 *        The window size is a power of two, the actual window used
 *        will be 2^win_size_beacon_order. Allowed values are 0, 1, 2, 3 and 4.
 *
 * @param win_size_data_order The SNR calculation window for data frames.
 *        The window size is a power of two, the actual window used
 *        will be 2^win_size_data_order. Allowed values are 0, 1, 2, 3 and 4.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - TODO if no more triggers can be created.
*/
int
nrx_conf_snr(nrx_context ctx,
             uint8_t win_size_beacon_order,
             uint8_t win_size_data_order)
{
   int err;
   NRX_CHECK(win_size_beacon_order <= 4);
   NRX_CHECK(win_size_data_order <= 4);
/*    NRX_CHECK(win_size_beacon_order >= 0); */
/*    NRX_CHECK(win_size_data_order >= 0); */

   /* 5.2.17.1 Number of SNR data frames used for average calculation */
   err = nrx_set_mib_val(ctx, "5.2.17.1", &win_size_data_order, sizeof(win_size_data_order));
   if (err)
      return err;

   /* 5.2.17.2 Number of SNR beacon frames used for average calculation */
   return nrx_set_mib_val(ctx, "5.2.17.2", &win_size_beacon_order, sizeof(win_size_beacon_order));
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set a PER cutoff threshold</b>
 *
 * A packet error rate (PER) will only be considered valid when the
 * number of packets transmitted during the detection interval exceeds
 * PER cutoff threshold. If fewer than per_cutoff_thr packets have
 * been sent in the interval the PER will be 0.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param per_cutoff_thr The packet error rate cutoff expressed as
 *                       a number of packets. This parameter must be
 *                       a positive integer value. 0 is invalid.
 *                       Maximum value is 1000.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - TODO if no more triggers can be created.
*/
int
nrx_set_per_cutoff_thr(nrx_context ctx,
                       uint32_t per_cutoff_thr)
{
   uint16_t u16;
   NRX_ASSERT(ctx);
   NRX_CHECK(per_cutoff_thr > 0);
   NRX_CHECK(per_cutoff_thr <= 1000); /* Arbitrary limit */

   u16 = per_cutoff_thr;
   return nrx_set_mib_val(ctx, "5.2.15.4", &u16, sizeof(u16));
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Set a PER threshold trigger</b>
 *
 * The callback is invoked every time (until disabled) the ratio PER
 * (Packet Error Rate) passes the defined threshold in the direction
 * specified.  Several thresholds can be defined. They are identified
 * by the thr_id parameter. There is a dynamic limit to the number of
 * triggers that can exist in the system so this call may fail if the
 * current limit is passed. The limit depends on the available memory
 * on the host.  The PER calculation is controlled by the configured
 * PER cutoff threshold set through nrx_set_per_cutoff_thr().
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id     The threshold id output buffer. This identifies
 *                   the threshold so that several thresholds can be
 *                   defined. The id value is filled in by the
 *                   function call.
 * @param chk_period The PER check period in milliseconds. The PER threshold
 *                   will periodically be compared with the current
 *                   PER with this interval. Minimum value is 100 ms
 *                   and maximum supported time is 35 minutes (2100000 ms).
 * @param per_thr    The PER threshold in percent (range 0 to 100).
 * @param dir        The trigger directions. The callback can either
 *                   be triggered when the value rises above or
 *                   falls below the threshold, but not both.
 *                    
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - TODO if no more triggers can be created.
*/
int
nrx_enable_per_threshold(nrx_context ctx,
                         int32_t *thr_id,
                         uint32_t chk_period,
                         uint32_t per_thr,
                         nrx_thr_dir_t dir)
{
   int ret;
   NRX_ASSERT(ctx);
   NRX_ASSERT(thr_id);
   NRX_CHECK(per_thr >= 0 && per_thr <= 100);
   NRX_CHECK(chk_period>=100);
   NRX_CHECK(chk_period<=2100000);
   NRX_CHECK(dir!=0);
   NRX_CHECK((dir & ~NRX_THR_RISING & ~NRX_THR_FALLING) == 0);

   ret = nrx_register_mib_trigger(ctx, 
                                  thr_id,
                                  MIB_dot11packetErrorRate, 
                                  0,
                                  chk_period*1000, /* conv to microseconds */
                                  per_thr,
                                  dir,         /* trig dir */
                                  1,           /* Number of times below level before trigger */
                                  1);          /* Continuous trigging */
   return ret;
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable a PER threshold</b>
 *
 * The specified PER threshold trigger is disabled. The specified
 * trigger in the device is cleared and no further notifications from
 * this trigger will be made.
 *
 * Calling this function will cancel corresponding callbacks which are
 * using the same thr_id.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id identifying the threshold trigger that should
 *               be disabled.
 * 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments or non-existing thr_id.
 */
int
nrx_disable_per_threshold(nrx_context ctx,
                          int thr_id)
{
   int ret;
   nrx_bool is_ier_thr, does_exist;

   NRX_ASSERT(ctx);

   ret = nrx_check_trigger_existence(ctx, thr_id, MIB_dot11interfererenceErrorRate, &is_ier_thr);
   if (ret != 0)
      return ret;
   NRX_CHECK( ! is_ier_thr);

   ret = nrx_check_trigger_existence(ctx, thr_id, MIB_dot11packetErrorRate, &does_exist);
   if (ret != 0)
      return ret;
   NRX_CHECK(thr_id != 0 && does_exist);

   return nrx_del_mib_trigger(ctx, thr_id);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Register an PER threshold callback</b>
 *
 * This will register a callback for a PER threshold, see
 * nrx_enable_per_threshold for further details. Several callbacks
 * can be registered to the same threshold.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id. This identifies the threshold
 *              so that several thresholds can be defined. 
 *              The thr_id value is obtained from nrx_enable_per_threshold.
 * @param cb The callback function that is to be invoked by threshold notifications.
 *           The callback is invoked with operation NRX_CB_TRIGGER on a 
 *           successful notification whereupon event_data will be a pointer 
 *           to a nrx_event_mibtrigger structure which contains further 
 *           information. When the threshold is cancelled cb is called 
 *           with operation NRX_CB_CANCEL and event_data set to NULL.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *               be passed to the callback on invocation. This is for
 *               caller use only, it will not be parsed or modified in
 *               any way by this library. This parameter can be NULL.
 * 
 * @return A handle to a callback (an unsigned integer type). The only
 * use for this is to pass it to nrx_cancel_per_threshold_callback
 * to cancel the callback.
 * @retval Zero on memory allocation failure.
 * @retval Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_per_threshold_callback(nrx_context ctx,
                                     uint32_t thr_id,
                                     nrx_callback_t cb,
                                     void *cb_ctx)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);
   return nrx_register_mib_trigger_event_handler(ctx, thr_id, cb, cb_ctx);
}
                                     
/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Cancel an PER threshold callback</b>
 *
 *
 * This will cancel the callback for a PER threshold.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle Callback handle obtained from nrx_register_per_threshold_callback.
 *
 *
 * @return
 * - 0 on success.
 * - EINVAL on invalid parameters, e.g the handle is not registered.
 *
 */
int
nrx_cancel_per_threshold_callback(nrx_context ctx,
                                   nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_mib_trigger_event_handler(ctx, handle);
}
                                     

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current PER</b>
 *
 * The most recently measured packet error rate is reported. The error
 * rate is defined as the ratio of failed transmissions and attempted
 * transmissions over some interval (default one second).
 *
 * This function requires that a  per trigger is registered
 * (nrx_enable_per_threshold()).
 *
 * @param [in]  ctx         context created by nrx_init_context().
 * @param [out] error_rate  Will hold current PER (in percent).
 *
 * @note Before this function is used, PER measurement must be configured 
 * with nrx_set_per_cutoff_thr(). When the total number of packets during 
 * an interval is less than configured, this function will report zero.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_per(nrx_context ctx, uint32_t *error_rate)
{
   size_t len = sizeof(*error_rate);
   NRX_ASSERT(ctx);
   NRX_ASSERT(error_rate);
   
   return nrx_get_mib_val(ctx, MIB_dot11packetErrorRate, error_rate, &len);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get current fragmentation threshold</b>
 *
 * Gets the current threshold for fragmentation of frames.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param fragment_thr Where the fragmentation threshold is stored.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_fragment_thr(nrx_context ctx, int32_t *fragment_thr)
{
   int ret;
   struct nrx_ioc_int32_t param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(fragment_thr);

   ret = nrx_nrxioctl(ctx, NRXIORFRAGMENTTHR, &param.ioc);
   if (ret != 0)
      return ret;

   *fragment_thr = param.value;
   return 0;
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get current fragmentation threshold</b>
 *
 * Sets threshold for fragmentation of frames.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param fragment_thr New fragmentation threshold.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_set_fragment_thr(nrx_context ctx, int32_t fragment_thr)
{
   struct nrx_ioc_int32_t param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(fragment_thr);

   param.value = fragment_thr;
   return nrx_nrxioctl(ctx, NRXIOWFRAGMENTTHR, &param.ioc);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get current RTS threshold</b>
 *
 * Retreives the threshold where RTS/CTS protocol will be used.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param rts_thr Where the current RTS threshold is stored.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_rts_thr(nrx_context ctx, int32_t *rts_thr)
{
   int ret;
   struct nrx_ioc_int32_t param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(rts_thr);

   ret = nrx_nrxioctl(ctx, NRXIORRTSTHR, &param.ioc);
   if (ret != 0)
      return ret;

   *rts_thr = param.value;
   return 0;
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get current RTS threshold</b>
 *
 * Below this threshold RTS/CTS protocol will be used.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param rts_thr New RTS threshold.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_set_rts_thr(nrx_context ctx, int32_t rts_thr)
{
   struct nrx_ioc_int32_t param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(rts_thr);

   param.value = rts_thr;
   return nrx_nrxioctl(ctx, NRXIOWRTSTHR, &param.ioc);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get registry</b>
 *
 * Retreives current registry settings from driver as a text file.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param registry Buffer for text to be stored.
 * @param len Size of buffer.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_registry(nrx_context ctx, char *registry, size_t *len)
{
   int ret;
   struct nrx_ioc_len_buf param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(registry);

   param.len = *len;
   param.buf = (void *)registry;
   ret = nrx_nrxioctl(ctx, NRXIOWRREGISTRY, &param.ioc);
   if (ret != 0)
      return ret;
   if (param.len < *len) /* Nice for test program if we can zero-terminate */
      registry[param.len] = '\0';
   *len = param.len;
   return 0;
}


/*! 
 * @internal
 * @brief <b>Check existence of trigger</b>
 * 
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param trig_id Virtual trigger id.
 * @param mib_id Zero-terminated string specifying mib trigger is related to. If this is NULL, 
 *        no check on mib_id will be performed, i.e. only trig_id is 
 *        verified.
 *
 * @return
 *  - 0 on success
 *  - an error code on failure 
 */
int 
nrx_check_trigger_existence(nrx_context ctx, 
                            int32_t thr_id, 
                            const char *mib_id, 
                            nrx_bool *does_exist)
{
   int ret;
   struct nrx_ioc_verify_mib_trigger param;

   NRX_ASSERT(ctx);

   param.trig_id = thr_id;
   if (mib_id != NULL) {
      NRX_CHECK(strlen(mib_id) < sizeof param.mib_id);
      strcpy(param.mib_id, mib_id);
   }
   else {
      param.mib_id[0] = '\0';
   }
   param.mib_id_len = strlen(param.mib_id);

   ret = nrx_nrxioctl(ctx, NRXIOWRDOESTRIGEXIST, &param.ioc);
   if (ret != 0)
      return ret;

   *does_exist = param.does_exist;

   return 0;
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Enable TX rate threshold</b>
 *
 * A trigger will happen when TX rate falls below the threshold
 * level. The level is compared to the median over a specified number
 * of transmitted messages.
 *
 * Note: Only one threshold can be registered so far and its direction
 * must be falling.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id output buffer. This identifies the threshold.
 *        The id value is filled in by the function call.
 * @param sample_len Number of transmitted messages over which the
 *        median value is calculated.
 * @param thr_limit Threshold level specified as a native 802.11 rate, 
 *        i.e. in steps of 500kbps.
 * @param dir Must be NRX_THR_FALLING.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int 
nrx_enable_tx_rate_threshold(nrx_context ctx, 
                             int32_t *thr_id,
                             uint32_t sample_len,
                             nrx_rate_t thr_limit,
                             nrx_thr_dir_t dir)
{
   int ret;
   struct nrx_ioc_ratemon param;
   NRX_ASSERT(ctx);
   NRX_CHECK(dir == NRX_THR_FALLING);
   NRX_CHECK(sample_len>=1);

   param.sample_len = sample_len;
   param.thr_level = thr_limit;
   
   ret = nrx_nrxioctl(ctx, NRXIOWTXRATEMONENABLE, &param.ioc);

   if (ret == 0)
      *thr_id = param.thr_id;

   return ret;
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable TX rate threshold</b>
 *
 * No TX rate triggers will be issued after this API is used.
 *
 * Calling this function will cancel corresponding callbacks which are
 * using the same thr_id.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id Threshold id received in previous call to nrx_enable_tx_rate_threshold().
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int 
nrx_disable_tx_rate_threshold(nrx_context ctx, int32_t thr_id)
{
   struct nrx_ioc_int32_t param;
   NRX_ASSERT(ctx);
   param.value = thr_id;
   return nrx_nrxioctl(ctx, NRXIOCTXRATEMONDISABLE, &param.ioc);
}


struct nrx_notification_t {
      nrx_callback_t handler;
      void *user_data;
};

/*!
 * @internal
 *
 * This function will convert to the correct data type and pass along
 * messages to the user's callback.
 *
 */
static int
nrx_handle_rate_notification(nrx_context ctx,
                             int operation,
                             void *event_data,
                             size_t event_data_size,
                             void *user_data)
{
   struct nrx_notification_t *priv = (struct nrx_notification_t *)user_data;
   struct nrx_we_custom_data *cdata = (struct nrx_we_custom_data *)event_data;
   nrx_rate_t rate = 0;
   int i;
   
   if(operation == NRX_CB_CANCEL) {
      (*priv->handler)(ctx, NRX_CB_CANCEL, NULL, 0, priv->user_data);
      free(priv);
      return 0;
   }

   NRX_ASSERT(event_data_size == sizeof(*cdata));

   for(i = 0; i < cdata->nvar; i++) {
      if(strcmp(cdata->var[i], "value") == 0) {
         rate = strtoul(cdata->val[i], NULL, 0);
      }
   }
   if (rate) {
      (*priv->handler)(ctx, operation, &rate, 
                          sizeof(rate), 
                          priv->user_data);
   } else {
      /* Message format incorrect. This should never happen */
   }
   return 0;
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Register TX rate threshold callback</b>
 *
 * Notify the caller when TX rate has fallen below current threshold. The
 * callback will be invoked every time (until disabled) the condition
 * is met.
 *
 * This function can be called several times and it will not
 * unregister any other callbacks.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id Threshold ID to trigger on, can be zero to match all
 *        thresholds, or an id returned by
 *        nrx_enable_tx_rate_threshold().
 * @param cb Callback that is invoked to notify the caller that the
 *        rate is below threshold. The callback is invoked with
 *        operation NRX_CB_TRIGGER on a successful notification. It
 *        will be passed a pointer to nrx_rate_t, which contains
 *        the rate that triggered the indication.  Memory for
 *        event_data will be freed immediately after the callback
 *        has returned. When the threshold is canceled cb is
 *        called with operation NRX_CB_CANCEL and event_data set to
 *        NULL.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *        be passed to the callback on invocation. This is for
 *        caller use only, it will not be parsed or modified in
 *        any way by this library. This parameter can be NULL.
 * @return 
 * - Zero on memory allocation failure
 * - Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_tx_rate_threshold_callback(nrx_context ctx,
                                        int32_t thr_id,
                                        nrx_callback_t cb,
                                        void *cb_ctx)
{
   struct nrx_notification_t *notify;
   struct nrx_conn_lost_notification_t *cln;
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);
   
   notify = malloc(sizeof(*cln));
   if (notify == NULL)
      return 0;
   notify->handler = cb;
   notify->user_data = cb_ctx;
   return nrx_register_custom_event(ctx, 
                                    "TXRATE", 
                                    thr_id,
                                    nrx_handle_rate_notification,
                                    notify);
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Cancel TX rate threshold callback</b>
 *
 * The TX rate notification feature is disabled. No further
 * TX rate notifications will be made.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle A handle previously obtained from
 *        nrx_register_tx_rate_threshold_callback(). The handle will no longer be
 *        valid after this call.
 * 
 * @return
 * - 0 on success.
 * - EINVAL on invalid parameters, e.g the handle is not registered.
 */
int
nrx_cancel_tx_rate_threshold_callback(nrx_context ctx, nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_custom_event(ctx, handle);
}

/*!
 * @ingroup RADIO
 * @brief <b>Get TX rate</b>
 *
 * Will get rate for the last transmission.
 * 
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param rate Where current TX rate will be stored.
 * @retval 0 on success
 * @retval EINVAL on invalid arguments or when no transmissions have been done yet.
 */
int 
nrx_get_tx_rate(nrx_context ctx, nrx_rate_t *rate)
{
   int ret;
   struct nrx_ioc_uint8_t param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(rate);

   ret = nrx_nrxioctl(ctx, NRXIORTXRATE, &param.ioc);

   if (ret == 0)
      *rate = param.value;

   return ret;
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Enable RX rate threshold</b>
 *
 * A trigger will happen when RX rate falls below the threshold
 * level. The level is compared to the median over a specified number
 * of received messages.
 *
 * Note: Only one threshold can be registered so far and its direction
 * must be falling.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id The threshold id output buffer. This identifies the threshold.
 *        The id value is filled in by the function call.
 * @param sample_len Number of received messages over which the
 *        median value is calculated.
 * @param thr_rate Threshold level specified as a native 802.11 rate,
 *        i.e. in steps of 500kbps.
 * @param dir Must be NRX_THR_FALLING.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int 
nrx_enable_rx_rate_threshold(nrx_context ctx, 
                             int32_t *thr_id,
                             uint32_t sample_len,
                             nrx_rate_t thr_rate,
                             nrx_thr_dir_t dir)
{
   int ret;
   struct nrx_ioc_ratemon param;
   NRX_ASSERT(ctx);
   NRX_CHECK(dir == NRX_THR_FALLING);
   NRX_CHECK(sample_len>=1);

   param.sample_len = sample_len;
   param.thr_level = thr_rate;
   
   ret = nrx_nrxioctl(ctx, NRXIOWRXRATEMONENABLE, &param.ioc);

   if (ret == 0)
      *thr_id = param.thr_id;

   return ret;
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable RX rate threshold</b>
 *
 * No RX rate indications will be sent after this API is used.
 *
 * Calling this function will cancel corresponding callbacks which are
 * using the same thr_id.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id Threshold id received in previous call to nrx_enable_rx_rate_threshold().
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int 
nrx_disable_rx_rate_threshold(nrx_context ctx, int32_t thr_id)
{
   struct nrx_ioc_int32_t param;
   NRX_ASSERT(ctx);
   param.value = thr_id;
   return nrx_nrxioctl(ctx, NRXIOCRXRATEMONDISABLE, &param.ioc);
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Register RX rate threshold callback</b>
 *
 * Notify the caller when RX rate has fallen below current threshold. The
 * callback will be invoked every time (until disabled) the condition
 * is met.
 *
 * This function can be called several times and it will not
 * unregister any other callbacks.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param thr_id Threshold ID to trigger on, can be zero to match all
 *        thresholds, or an id returned by
 *        nrx_enable_rx_rate_threshold().
 * @param cb Callback that is invoked to notify the caller that the
 *        rate is below threshold. The callback is invoked with
 *        operation NRX_CB_TRIGGER on a successful notification. It
 *        will be passed a pointer to nrx_rate_t, which contains
 *        the rate that triggered the indication.  Memory for
 *        event_data will be freed immediately after the callback
 *        has returned. When the threshold is canceled cb is
 *        called with operation NRX_CB_CANCEL and event_data set to
 *        NULL.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *        be passed to the callback on invocation. This is for
 *        caller use only, it will not be parsed or modified in
 *        any way by this library. This parameter can be NULL.
 * @return 
 * - Zero on memory allocation failure
 * - Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_rx_rate_threshold_callback(nrx_context ctx,
                                        int32_t thr_id,
                                        nrx_callback_t cb,
                                        void *cb_ctx)
{
   struct nrx_notification_t *notify;
   struct nrx_conn_lost_notification_t *cln;
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);
   
   notify = malloc(sizeof(*cln));
   if (notify == NULL)
      return 0;
   notify->handler = cb;
   notify->user_data = cb_ctx;
   return nrx_register_custom_event(ctx, 
                                    "RXRATE", 
                                    thr_id,
                                    nrx_handle_rate_notification,
                                    notify);
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Cancel RX rate threshold callback</b>
 *
 * The RX rate notification feature is disabled. No further
 * RX rate notifications will be made.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle A handle previously obtained from
 *        nrx_register_rx_rate_threshold_callback(). The handle will no
 *        longer be valid after this call.
 * 
 * @return
 * - 0 on success.
 * - EINVAL on invalid parameters, e.g the handle is not registered.
 */
int
nrx_cancel_rx_rate_threshold_callback(nrx_context ctx, nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_custom_event(ctx, handle);
}

/*!
 * @ingroup RADIO
 * @brief <b>Get RX rate</b>
 *
 * Will get rate for the last reception.
 * 
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param rate Where current RX rate will be stored.
 * @retval 0 on success
 * @retval EINVAL on invalid arguments or when no receptions have been done yet.
 */
int 
nrx_get_rx_rate(nrx_context ctx, nrx_rate_t *rate)
{
   int ret;
   struct nrx_ioc_uint8_t param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(rate);

   ret = nrx_nrxioctl(ctx, NRXIORRXRATE, &param.ioc);

   if (ret == 0)
      *rate = param.value;

   return ret;
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Enable TX fail notification</b>
 *
 * A notification will happen when the number of consecutive
 * transmissions that have failed equals the threshold level.
 *
 * Note: Only one threshold can be registered.
 *
 * @param ctx NRX context that was created by the call to
 *        nrx_init_context().
 * @param thr_limit Limit of consecutive failed transmissions
 *        thereafter notification will happen.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int 
nrx_enable_tx_fail_notification(nrx_context ctx, 
                                uint32_t thr_limit)
{
   NRX_ASSERT(ctx);
   return nrx_set_mib_val(ctx, "5.2.19.8", &thr_limit, sizeof(thr_limit));
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable TX fail notification</b>
 *
 * Notifications will no longer be done due to TX failure.
 *
 * Calling this function will not unregister any callbacks.
 *
 * @param ctx NRX context that was created by the call to
 *        nrx_init_context().
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int 
nrx_disable_tx_fail_notification(nrx_context ctx)
{
   uint32_t val;
   NRX_ASSERT(ctx);
   val = 0;
   return nrx_set_mib_val(ctx, MIB_dot11LinkMonitoringTxFailureCountWarning, &val, sizeof(val));
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Register TX fail notification callback</b>
 *
 * Notify the caller when limit for transmission failure is fulfilled. The
 * callback will be invoked every time (until disabled) the condition
 * is met.
 *
 * This function can be called several times and it will not
 * unregister any other callbacks.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param cb Callback that is invoked to notify the caller that the
 *        number of failed transmissions equals the threshold. The
 *        callback is invoked with operation NRX_CB_TRIGGER on a
 *        successful notification. It will be passed a NULL pointer
 *        as event_data. When the threshold is canceled cb is
 *        called with operation NRX_CB_CANCEL and event_data set to
 *        NULL.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *        be passed to the callback on invocation. This is for
 *        caller use only, it will not be parsed or modified in
 *        any way by this library. This parameter can be NULL.
 * @return 
 * - Zero on memory allocation failure
 * - Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_tx_fail_notification(nrx_context ctx,
                                  nrx_callback_t cb,
                                  void *cb_ctx)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);

   return nrx_register_custom_event(ctx, 
                                    "TXFAIL", 
                                    0,
                                    cb,
                                    cb_ctx);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Cancel TX fail notification callback</b>
 *
 * The tx fail notification feature is disabled. No further
 * tx fail notifications will issued.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle A handle previously obtained from
 *        nrx_register_tx_fail_notification(). The handle will no
 *        longer be valid after this call.
 * 
 * @return
 * - 0 on success.
 * - EINVAL on invalid parameters, e.g the handle is not registered.
 */
int
nrx_cancel_tx_fail_notification(nrx_context ctx, nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_custom_event(ctx, handle);
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Enable missed beacon indication</b>
 *
 * A notification will happen when the number of consecutive
 * beacons that have not been received equals the threshold level.
 *
 * Note: Only one threshold can be registered.
 *
 * @param ctx NRX context that was created by the call to
 *        nrx_init_context().
 * @param thr_limit Limit of consecutive failed receptions of beacon
 *        thereafter notification will happen.
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int 
nrx_enable_missed_beacon_notification(nrx_context ctx, 
                                      uint32_t thr_limit)
{
   NRX_ASSERT(ctx);
   return nrx_set_mib_val(ctx, MIB_dot11LinkMonitoringBeaconCountWarning, &thr_limit, sizeof(thr_limit));
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Disable missed beacon notification</b>
 *
 * Notifications will no longer be done due to missed beacons.
 *
 * Calling this function will not unregister any callbacks.
 *
 * @param ctx NRX context that was created by the call to
 *        nrx_init_context().
 *
 * @retval 0 on success
 * @retval EINVAL on invalid arguments
 */
int 
nrx_disable_missed_beacon_notification(nrx_context ctx)
{
   uint32_t val;
   NRX_ASSERT(ctx);
   val = 0;
   return nrx_set_mib_val(ctx, "5.2.19.6", &val, sizeof(val));
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Register missed beacon notification callback</b>
 *
 * Notify the caller when limit for missed beacons is fulfilled. The
 * callback will be invoked every time (until disabled) the condition
 * is met.
 *
 * This function can be called several times and it will not
 * unregister any other callbacks.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param cb Callback that is invoked to notify the caller that the
 *        number of missed beacons equals the threshold. The
 *        callback is invoked with operation NRX_CB_TRIGGER on a
 *        successful notification. It will be passed a NULL pointer
 *        as event_data. When the threshold is canceled cb is
 *        called with operation NRX_CB_CANCEL and event_data set to
 *        NULL.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *        be passed to the callback on invocation. This is for
 *        caller use only, it will not be parsed or modified in
 *        any way by this library. This parameter can be NULL.
 * @return 
 * - Zero on memory allocation failure
 * - Non-zero a valid callback handle.
 */
nrx_callback_handle
nrx_register_missed_beacon_notification(nrx_context ctx,
                                        nrx_callback_t cb,
                                        void *cb_ctx)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);

   return nrx_register_custom_event(ctx, 
                                    "NOBEACON", 
                                    0,
                                    cb,
                                    cb_ctx);
}

/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Cancel missed beacon notification callback</b>
 *
 * The missed beacon notification feature is disabled. No further
 * missed beacon notifications will issued.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle A handle previously obtained from
 *        nrx_register_missed_beacon_notification(). The handle will no
 *        longer be valid after this call.
 * 
 * @return
 * - 0 on success.
 * - EINVAL on invalid parameters, e.g the handle is not registered.
 */
int
nrx_cancel_missed_beacon_notification(nrx_context ctx, nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_custom_event(ctx, handle);
}


