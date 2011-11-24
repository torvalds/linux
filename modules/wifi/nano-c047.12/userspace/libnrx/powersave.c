/* Copyright (C) 2007 Nanoradio AB */
/* $Id: powersave.c 15614 2010-06-10 15:52:52Z toos $ */

#include "nrx_priv.h"
#include "mac_mib_defs.h"

/** \defgroup PS Power Save 
 * \brief The Power Save settings are used to configure and enable/disable 
 *        both the legacy power save mode and WMM power save mode.
 * 
 * See the <em>NRX700 Power Save Function Description</em> (15516/14-NRX700) for further
 * details about the NRX700 Power Save implementation. 
 */

/*!
 * @ingroup PS
 * @brief <b>Enable legacy power save mode</b>
 *
 * Enable legacy power save mode in both associated and disconnected
 *        mode.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_enable_ps(nrx_context ctx)
{
   struct nrx_ioc ioc;
   NRX_ASSERT(ctx);
   return nrx_nrxioctl(ctx, NRXIOCPSENABLE, &ioc);
}

/*!
 * @internal
 * @ingroup PS
 * @brief <b>Get power save mode</b>
 *
 * Detects whether or not power save is enabled.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param ps_enabled Where the result is stored. This is 1 when power
 *        save is enabled and 0 when it's disabled.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_get_ps_mode(nrx_context ctx, nrx_bool *ps_enabled)
{
   int ret;
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(ps_enabled);

   ret = nrx_nrxioctl(ctx, NRXIORPSENABLE, &param.ioc);
   if (ret != 0)
      return ret;
   *ps_enabled = param.value;
   return 0;
}

/*!
 * @ingroup PS
 * @brief <b>Disable legacy power save mode</b>
 *
 * Disable legacy power save mode in both associated and disconnected
 *        mode.
 * This call is a no-op when power save is already disabled.
 *
 * Note that disabling Legacy Power Save mode also disables WMM Power Save
 * mode if that is active. WMM Power Save requires Legacy Power Save.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_disable_ps(nrx_context ctx)
{
   struct nrx_ioc ioc;
   NRX_ASSERT(ctx);
   return nrx_nrxioctl(ctx, NRXIOCPSDISABLE, &ioc);
}

/*!
 * @ingroup PS
 * @brief <b>Configure power save mode</b>
 *
 * These parameters are shared by WMM power save and legacy power save modes.
 * This function can be called any time, but certain parameters may
 * be saved in the driver and will be used first upon
 * association/reassociation.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param rx_all_dtim Boolean that defines whether the device should
 *                    wake up to receive all DTIM frames (1) or
 *                    if it can skip some of them (0), see 802.11 for 
 *                    details. This parameter can be changed dynamically 
 *                    at any time.
 *
 * @param use_ps_poll Boolean that defines whether the device should
 *                    send PS-Poll frames (1) or not (0) when in
 *                    power save mode. This parameter can be changed
 *                    dynamically at any time.
 *
 * @param listen_interval This value indicates to the AP how often (in
 *                        beacon intervals) the station will wakeup
 *                        and listen to beacons. This parameter is
 *                        only used when associating. Changing this
 *                        parameter does not affect WiFi traffic in
 *                        any way, it's only used to indicate to the
 *                        AP how much buffer space it should allocate
 *                        to the station.
 *
 * @param traffic_timeout Traffic timeout in milliseconds after last traffic,
 *        which the device goes back into power save sleep mode. In case
 *        traffic occurs at the same time as the traffic timeout has elapsed
 *        the pending traffic will be handled, then the device will go into
 *        sleep mode immediately. This parameter can be changed dynamically at
 *        any time and cannot be 0. 
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_conf_ps(nrx_context ctx,
            nrx_bool rx_all_dtim,
            nrx_bool use_ps_poll,
            uint16_t listen_interval,
            uint32_t traffic_timeout)
{
   struct nrx_ioc_ps_conf param;
   NRX_ASSERT(ctx);
   NRX_CHECK(rx_all_dtim || listen_interval);
   NRX_CHECK(traffic_timeout != 0);

   param.rx_all_dtim = rx_all_dtim;
   param.ps_poll = use_ps_poll;
   param.listen_interval = listen_interval;
   param.traffic_timeout = traffic_timeout;

   return nrx_nrxioctl(ctx, NRXIOWPSCONF, &param.ioc);
}

/*!
 * @internal
 * @ingroup PS
 * @brief <b>Set the device listen interval</b>
 *
 * The device listen interval can be changed dynamically when connected.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param listen_every_beacon Choose 1 to listen on every single beacon 
 *        sent by the AP or 0 to use the beacon interval agreed with the AP (default). 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_set_dev_listen_interval(nrx_context ctx,
                            nrx_bool listen_every_beacon)
{
   int32_t val = listen_every_beacon; /* 4 bytes */
   NRX_ASSERT(ctx);
   NRX_CHECK(listen_every_beacon==0 || listen_every_beacon==1);
   return nrx_set_mib_val(ctx, MIB_dot11listenEveryBeacon, &val, sizeof(val));
}

/*!
 * @ingroup PS
 * @brief <b>Enable WMM power save mode</b>
 *
 * The WMM power save mode shares configuration parameters with the
 * legacy power save mode. Use nrx_conf_ps() to set these.  This
 * configuration should happen before association, or be followed by a
 * reassociation, as the parameters are used in the association
 * negotiation with the AP.
 *
 * This function will enable WMM and configure the service period length.
 * nrx_enable_ps() must still be issued in order to enable power save.
 * WMM power save will only be enabled in case U-APSD is supported by the AP.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param sp_len Service period length in packets. This defines how many
 *               packets the AP is allowed to deliver during a service period
 *               (see 802.11e for details). When sp_len is 0 the AP will
 *               deliver all buffered data during a service period.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_enable_wmm_ps(nrx_context ctx,
                  nrx_sp_len_t sp_len)
{
   struct nrx_ioc_uint32_t param;
   NRX_ASSERT(ctx);
   NRX_CHECK(sp_len==NRX_SP_LEN_ALL || sp_len==NRX_SP_LEN_2 || sp_len==NRX_SP_LEN_4 || sp_len==NRX_SP_LEN_6);
   param.value = (uint32_t)sp_len;
   return nrx_nrxioctl(ctx, NRXIOWWMMPSENABLE, &param.ioc);
}

/*!
 * @ingroup PS
 * @brief <b>Configure WMM power save mode</b>
 *
 * Set dynamically configurable WMM power save parameters.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param tx_period Period in milliseconds, after which Null-Data frames are sent
 *                  to request data delivery from the access point.
 *                  (see 802.11e for details). When tx_period is 0
 *                  no Null-Data frames are sent, receive traffic will
 *                  only be delivered from the AP when host (application) sends
 *                  data. Maximum value is 4294967 ms.
 *                  This parameter can be changed dynamically at any time.
 * @param ac        Bitmask that specifies for which access categories wmm
 *                  power save should be enabled.
 *                  The flags are relevant only in infrastructure (BSS) mode
 *                  and if WMM is enabled on both the AP and STA.
 *                  Include the AC in the bitmask if you want to use WMM Power
 *                  Save for the specific type of data.
 *                  This parameter will only have an impact on next
 *                  (re)association. All ac:s can be set separately or
 *                  together.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_conf_wmm_ps(nrx_context ctx,
		uint32_t tx_period,
                nrx_wmm_ac_t ac)
{
   struct nrx_ioc_wmm_power_save_conf wmm_ps;
   NRX_ASSERT(ctx);
   NRX_CHECK(tx_period<=4294967); /* 2^32-1 divided by 1000 */
   wmm_ps.tx_period = tx_period * (uint32_t)1000;
   wmm_ps.be = (ac & NRX_WMM_AC_BE) != 0;
   wmm_ps.bk = (ac & NRX_WMM_AC_BK) != 0;
   wmm_ps.vi = (ac & NRX_WMM_AC_VI) != 0;
   wmm_ps.vo = (ac & NRX_WMM_AC_VO) != 0;

   return nrx_nrxioctl(ctx, NRXIOWWMMPSCONF, &wmm_ps.ioc);
}

/*!
 * @ingroup PS
 * @brief <b>Disable WMM power save mode</b>
 *
 * Disables both WMM and WMM power save.
 * The device will go back to legacy power save mode.
 *
 * To turn off both WMM and Legacy Power Save, call nrx_disable_ps()
 * instead.
 *
 * This call is a no-op when WMM power save is already disabled.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_disable_wmm_ps(nrx_context ctx)
{
   struct nrx_ioc ioc;
   NRX_ASSERT(ctx);
   return nrx_nrxioctl(ctx, NRXIOCWMMPSDISABLE, &ioc);
}

