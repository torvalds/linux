/* Copyright (C) 2007 Nanoradio AB */
/* $Id: roam.c 16988 2010-11-25 14:57:57Z copi $ */

#include "nrx_lib.h"
#include "nrx_priv.h"

/*!
 * @defgroup ROAM Roaming agent
 * @brief Configure parameters for roaming agent.
 *
 * The roaming procedure will be initiated on the following events:
 * - New scan indications are received when the STA is disconnected.
 * - The connection to the current AP is lost.
 * - The TX rate for the current AP falls below the configured threshold.
 * - The RSSI level for the current AP falls below the configured threshold.
 * - The SNR level for the current AP falls below the configured threshold.
 * - The delay spread value for the current AP falls below the configured
 *   threshold.
 *
 * When roaming, the best network is elected by a combination of RSSI and
 * SNR values. APs will also be filtered out according to WMM, blacklist and
 * SSID configuration.
 *
 * Prior to roaming, a directed scan can be issued on the following events:
 * - The TX rate for the current AP falls below the configured threshold
 *   for scanning.
 * - The RSSI level for the current AP falls below the configured threshold
 *   for scanning.
 * - The SNR level for the current AP falls below the configured threshold
 *   for scanning.
 * - The delay spread value for the current AP falls below the configured
 *   threshold for scanning.
 *
 * The purpose of the directed scan is to gain some more information of
 * roaming candidates when we're about to lose the connection to the
 * current AP.
 *
 */

/*!
 * @ingroup ROAM
 * @brief <b>Enable roaming</b>
 *
 * Romaing will be enabled and notification about current link quality will be
 * transmitted from firmware to host.
 *
 * Any attempts to set SSID manually will be overridden by the roaming agent
 * when roaming is enabled.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters.
 */
int
nrx_enable_roam(nrx_context ctx)
{
   struct nrx_ioc_uint32_t param;
   NRX_ASSERT(ctx);
   param.value = 1;
   return nrx_nrxioctl(ctx, NRXIOWROAMENABLE, &param.ioc);
}

/*!
 * @ingroup ROAM
 * @brief <b>Disabled roaming</b>
 *
 * Romaing will be disabled and notification about current link quality will
 * no longer be transmitted from firmware to host.
 *
 * SSID can now be set manually.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * 
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters.
 */
int
nrx_disable_roam(nrx_context ctx)
{
   struct nrx_ioc_uint32_t param;
   NRX_ASSERT(ctx);
   param.value = 0;
   return nrx_nrxioctl(ctx, NRXIOWROAMENABLE, &param.ioc);
}


/*!
 * @ingroup ROAM
 * @brief <b>Add SSID to ssid filter list</b>
 *
 * Adds an SSID to the SSID filter list. When electing nets, only APs that
 * have an SSID that matches one of the SSIDs in the SSID list will be elected.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param ssid The SSID to add.
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters or if the SSID list is full.
 */
int
nrx_add_roam_ssid_filter(nrx_context ctx, nrx_ssid_t ssid)
{
   struct nrx_ioc_roam_ssid param;
   NRX_ASSERT(ctx);
   if(ssid.ssid_len > sizeof(param.ssid))
      return EINVAL;
   memcpy(param.ssid, ssid.ssid, ssid.ssid_len);
   param.ssid_len = ssid.ssid_len;

   return nrx_nrxioctl(ctx, NRXIOWROAMADDSSIDFILTER, &param.ioc);
}


/*!
 * @ingroup ROAM
 * @brief <b>Remove SSID from ssid filter list</b>
 *
 * Remove an SSID from the SSID filter list. When electing nets, only APs that
 * have an SSID that matches one of the SSIDs in the SSID list will be elected.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param ssid The ssid to remove.
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters or if the SSID could not be found in the
 *    ssid list.
 */
int
nrx_del_roam_ssid_filter(nrx_context ctx, nrx_ssid_t ssid)
{
   struct nrx_ioc_roam_ssid param;
   NRX_ASSERT(ctx);
   if(ssid.ssid_len > sizeof(param.ssid))
      return EINVAL;
   memcpy(param.ssid, ssid.ssid, ssid.ssid_len);
   param.ssid_len = ssid.ssid_len;

   return nrx_nrxioctl(ctx, NRXIOWROAMDELSSIDFILTER, &param.ioc);
}

/*!
 * @ingroup ROAM
 * @brief <b>Toggle net election filtering rules</b>
 *
 * Enables and disables some rules that are used during net election.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param enable_blacklist Set to TRUE to enable, FALSE to disable.
 *        When blacklist is enabled, any APs that we failed to connect to
 *        will be filtered out during the net election step.
 * @param enable_wmm Set to TRUE to enable, FALSE to disable.
 *        When WMM filtering is enabled, any APs that does not support WMM
 *        will be filter out if WMM is enabled on the STA.
 * @param enable_ssid Set to TRUE to enable, FALSE to disable.
 *        When SSID filtering is enabled, any APs that does not match any of
 *        the SSIDs in the ssid list fill be filtered out.
 *        SSIDs can be added and deleted through nrx_add_roam_ssid_filter()
 *        and nrx_del_roam_ssid_filter().
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters.
 */
int
nrx_conf_roam_filter(nrx_context ctx, nrx_bool enable_blacklist,
                     nrx_bool enable_wmm, nrx_bool enable_ssid)
{
   struct nrx_ioc_roam_filter param;
   NRX_ASSERT(ctx);
   param.enable_blacklist = enable_blacklist;
   param.enable_wmm = enable_wmm;
   param.enable_ssid = enable_ssid;
   return nrx_nrxioctl(ctx, NRXIOWROAMCONFFILTER, &param.ioc);
}


/*!
 * @ingroup ROAM
 * @brief <b>Configure RSSI thresholds</b>
 *
 * Configure thresholds for initiating roaming and scanning based on RSSI level
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param enable When enabled, roaming and scanning will be performed when
 *        RSSI falls below the configured thresholds.
 *
 * @param roam_thr When the current RSSI level falls below this threshold,
 *        then romaing will be initiated. Range is [-100, 0].
 *
 * @param scan_thr When the current RSSI level falls below this threshold,
 *        then scanning will be initiated. Range is [-100, 0].
 *
 * @param margin The elected AP must have RSSI and RSSI levels that are better
 *        than the thresholds plus some margin. The margin is used to avoid
 *        roaming "oscillations". The elected AP must have a RSSI level that is
 *        better than the RSSI roaming threshold plus this margin. Range is
 *        [0, 100].
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters.
 */
int
nrx_conf_roam_rssi_thr(nrx_context ctx,
                       nrx_bool enable,
                       int32_t roam_thr,
                       int32_t scan_thr,
                       uint32_t margin)
{
   struct nrx_ioc_roam_rssi_thr param;
   NRX_ASSERT(ctx);
   param.enable = enable;
   param.roam_thr = roam_thr;
   param.scan_thr = scan_thr;
   param.margin = margin;
   return nrx_nrxioctl(ctx, NRXIOWROAMCONFRSSITHR, &param.ioc);
}


/*!
 * @ingroup ROAM
 * @brief <b>Configure SNR thresholds</b>
 *
 * Configure thresholds for initiating roaming and scanning based on SNR level
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param enable When enabled, roaming and scanning will be performed when
 *        SNR falls below the configured thresholds.
 *
 * @param roam_thr When the current SNR level falls below this threshold,
 *        then romaing will be initiated. Range is [0, 40].
 *
 * @param scan_thr When the current SNR level falls below this threshold,
 *        then scanning will be initiated. Range is [0, 40].
 *
 * @param margin The elected AP must have SNR and SNR levels that are better
 *        than the thresholds plus some margin. The margin is used to avoid
 *        roaming "oscillations". The elected AP must have a SNR level that is
 *        better than the SNR roaming threshold plus this margin.
 *        Range is [0, 40].
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters.
 */
int
nrx_conf_roam_snr_thr(nrx_context ctx,
                      nrx_bool enable,
                      int32_t roam_thr,
                      int32_t scan_thr,
                      uint32_t margin)
{
   struct nrx_ioc_roam_snr_thr param;
   NRX_ASSERT(ctx);
   param.enable = enable;
   param.roam_thr = roam_thr;
   param.scan_thr = scan_thr;
   param.margin = margin;
   return nrx_nrxioctl(ctx, NRXIOWROAMCONFSNRTHR, &param.ioc);
}


/*!
 * @internal
 * @ingroup ROAM
 * @brief <b>Configure delay spread thresholds</b>
 *
 * Configure thresholds for initiating roaming and scanning based on
 * delay spread level.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param enable When enabled, roaming and scanning will be performed when
 *        delay spread rises above the configured thresholds.
 *
 * @param roam_thr When the current delay spread level rises above this
 *        threshold, then romaing will be initiated. Range is [0, 100].
 *
 * @param scan_thr When the current delay spread level rises above this
 *        threshold, then scanning will be initiated. Range is [0, 100].
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters.
 */
int
nrx_conf_roam_ds_thr(nrx_context ctx,
                     nrx_bool enable,
                     uint32_t roam_thr,
                     uint32_t scan_thr)
{
   struct nrx_ioc_roam_ds_thr param;
   NRX_ASSERT(ctx);
   param.enable = enable;
   param.roam_thr = roam_thr;
   param.scan_thr = scan_thr;
   return nrx_nrxioctl(ctx, NRXIOWROAMCONFDSTHR, &param.ioc);
}



/*!
 * @ingroup ROAM
 * @brief <b>Configure TX rate thresholds</b>
 *
 * Configure thresholds for initiating roaming and scanning based on
 * TX rate level.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param enable When enabled, roaming and scanning will be performed when
 *        TX rate falls below the configured thresholds.
 *
 * @param roam_thr When the current TX rate level falls below this
 *        threshold, then romaing will be initiated.
 *
 * @param scan_thr When the current TX rate level falls below this
 *        threshold, then scanning will be initiated.
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters.
 */
int
nrx_conf_roam_rate_thr(nrx_context ctx,
                       nrx_bool enable,
                       uint32_t roam_thr,
                       uint32_t scan_thr)
{
   struct nrx_ioc_roam_rate_thr param;
   NRX_ASSERT(ctx);
   param.enable = enable;
   param.roam_thr = roam_thr;
   param.scan_thr = scan_thr;
   return nrx_nrxioctl(ctx, NRXIOWROAMCONFRATETHR, &param.ioc);
}


/*!
 * @internal
 * @ingroup ROAM
 * @brief <b>Configure constants for calculating net link quality when electing
 *        AP</b>
 *
 * The constants will affect how much RSSI is prioritized compared to SNR when
 * electing the best net. The final link quality value will be normalize, so
 * k1 and k2 can have any value in the range of the data type.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param k1 RSSI weight.
 * @param k2 SNR weight.
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters.
 */
int
nrx_conf_roam_net_election(nrx_context ctx, uint32_t k1, uint32_t k2)
{
   struct nrx_ioc_roam_net_election param;
   NRX_ASSERT(ctx);
   param.k1 = k1;
   param.k2 = k2;
 
   return nrx_nrxioctl(ctx, NRXIOWROAMCONFNETELECTION, &param.ioc);
}


/*!
 *
 * @ingroup ROAM
 * @brief <b>Force encryption and authentication mode while roaming.</b>
 *
 * This function can be used when roaming in WPA networks. In that case the
 * WPA supplicant should not scan and roam on its own, so we need another way of
 * configuring the encryption and authentication before association.
 *
 * Will also make sure that the encryption and authentication mode is set
 * again before associating to a new AP due to roaming, in case they are
 * cleared by the WPA supplicant for some reason.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param enable When enabled, the forced encryption and authentication mode
 *        will be used.
 *
 * @param auth_mode Forced authentication mode.
 *
 * @param enc_mode Forced encryption mode.
 * 
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE on error.
 */
int nrx_conf_roam_auth(nrx_context ctx,
                       uint8_t enable,
                       nrx_authentication_t auth_mode,
                       nrx_encryption_t enc_mode)
{
   struct nrx_ioc_roam_conf_auth param;
   NRX_ASSERT(ctx);
   param.enable = enable;
   param.auth_mode = (int) auth_mode;
   param.enc_mode = (int) enc_mode;
   
   return nrx_nrxioctl(ctx, NRXIOWROAMCONFAUTH, &param.ioc);
}


/*!
 * @internal
 * @ingroup ROAM
 * @brief <b>Configure delay spread calculation</b>
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return
 *  - 0 on success
 *  - EINVAL on invalid parameters.
 */
int
nrx_conf_delay_spread(nrx_context ctx, uint32_t thr, uint16_t winsize)
{
   struct nrx_ioc_ds_conf param;
   NRX_ASSERT(ctx);
   param.thr = thr;
   param.winsize = winsize;
 
   return nrx_nrxioctl(ctx, NRXIOWCONFDELAYSPREAD, &param.ioc);
}
