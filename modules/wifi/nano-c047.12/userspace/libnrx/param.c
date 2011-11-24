/* Copyright (C) 2007 Nanoradio AB */
/* $Id: param.c 17582 2011-01-19 14:54:03Z niks $ */

#include "nrx_priv.h"
#include "mac_mib_defs.h"

/** \defgroup MISC Miscellaneous settings 
 * \brief The Miscellaneous settings functions are various functions that do not belong  
 *        to any of the other modules. They are mainly configuration functions used to set and enable/disable 
 *        certain parameters of the Nanoradio Linux driver registry file.
 *
 * There are also three functions to retrieve product version, product id and manufacturer id.
 * This information is taken from the MAC MIB of the NRX700 firmware.
 * 
 * Not all configuration parameters of the driver registry and the MAC MIB are available as NRX API-functions.
 * However, the list of configuration functions of the NRX API may be extended in future releases.
 * 
 * See <em>NRX700 MAC Management Reference Manual</em> (1543/16-NRX701) for details about the configuration 
 * parameters of the driver registry and the MAC MIB.
 * 
*/
 
/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Retrieve the current join timeout value</b>
 *
 * @param ctx A valid nrx_context.
 * @param value A pointer where the join timeout value given in beacon
 *        intervals will be stored.
 *
 * @return Zero on success or an error code.
 */
int
nrx_get_join_timeout(nrx_context ctx, uint32_t *value)
{
   int ret;
   struct nrx_ioc_uint32_t param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(value);

   ret = nrx_nrxioctl(ctx, NRXIORJOINTIMEOUT, &param.ioc);
   if(ret)
      return ret;

   *value = param.value;
   
   return 0;
}

/*!
 * 
 * @ingroup MISC
 * @brief <b>Set the join timeout value</b>
 *
 * When the scanning process results in one or more available BSSs, the STA
 * may choose to join a BSS. During this joining process all the STA
 * parameters are synchronized with the desired BSS. The joinTimeout parameter
 * is used to set a timeout for this BSS join process.
 *
 * @param ctx A valid nrx_context.
 * @param value The join timeout given in beacon intervals. This time
 *        should never exceed 35 minutes. Otherwise, functionality is
 *        undefined. If this parameter is set to 0, parameters will
 *        not be extracted from the next heard beacon. Instead, an
 *        immediate join is done, where parameters are based on last
 *        scan.
 * @return Zero on success or an error code.
 */
int
nrx_set_join_timeout(nrx_context ctx, uint32_t value)
{
   struct nrx_ioc_uint32_t param;
   NRX_ASSERT(ctx);

   param.value = value;

   return nrx_nrxioctl(ctx, NRXIOWJOINTIMEOUT, &param.ioc);
}

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Disable heartbeat</b>
 *
 * The heartbeat functionality is disabled an no more heartbeat signals will be
 * sent from the target.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_disable_heartbeat(nrx_context ctx)
{
   struct nrx_ioc_heartbeat param;
   NRX_ASSERT(ctx);

   param.enabled = 0;
   return nrx_nrxioctl(ctx, NRXIOWHEARTBEAT, &param.ioc);
}

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Enable heartbeat</b>
 *
 * The target will send heartbeat signals to the host. This allows
 * the driver to notice if the target stops working. If the heartbeat stops,
 * the driver will make a dump of the complete contents of the core RAM.
 * This function is useful during development, and allows Nanoradio R&D to get
 * a view of exactly what happened in the chip just before the system failure.
 * The core dump can be used for troubleshooting and analysis.
 *
 * Any core files will be put in /proc/drivers/\<interface name\>/core/ 
 * and will be named 0, 1 etc.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param hb_period Heartbeat period in ms. Current implementation
 * will round up to a multiple of 1000 (i.e. resolution in seconds). Minimum 
 * value is 1 ms (i.e. 1 second) and maximum is 35 minutes (2100000 ms). 0 will 
 * reset to default value (60 sec).
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_enable_heartbeat(nrx_context ctx, uint32_t hb_period)
{
   struct nrx_ioc_heartbeat param;
   NRX_ASSERT(ctx);
   NRX_CHECK(hb_period <= 2100000); /* 2^32-1 divided by 2e3 + margin */

   param.enabled = 1; /* 0=ONESHOT, 1=ALWAYS */
   param.interval = hb_period;
   return nrx_nrxioctl(ctx, NRXIOWHEARTBEAT, &param.ioc);
}

/*!
 * @internal
 * @brief Set max number of coredumps retained
 *
 * This limits the number of target coredumps retained by the driver.
 *
 * @param [in] ctx NRX context that was created by the call to nrx_init_context().
 * @param [in] count Number of coredumps to retain.
 *
 * @retval Zero on success.
 * @retval Errno on failure.
 *
 * <!-- NRX_API_FOR_TESTING -->
 */
int
nrx_set_maxcorecount(nrx_context ctx, uint32_t count)
{
   struct nrx_ioc_uint32_t param;
   NRX_ASSERT(ctx);

   param.value = count;
   return nrx_nrxioctl(ctx, NRXIOWCORECOUNT, &param.ioc);
}

/*!
 * @internal
 * @ingroup MISC
 * @brief Get max number of coredumps retained
 *
 * This gets the current coredumps limit.
 *
 * @param [in] ctx NRX context that was created by the call to nrx_init_context().
 * @param [out] count Number of coredumps to retain.
 *
 * @retval Zero on success.
 * @retval Errno on failure.
 *
 * <!-- NRX_API_FOR_TESTING -->
 */
int
nrx_get_maxcorecount(nrx_context ctx, uint32_t *count)
{
   struct nrx_ioc_uint32_t param;
   int ret;
   NRX_ASSERT(ctx);

   ret = nrx_nrxioctl(ctx, NRXIORCORECOUNT, &param.ioc);
   if(ret != 0)
      return ret;
   *count = param.value;
   
   return 0;
}


/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Retrieve the value of a string MIB</b>
 *
 * This is a low-level API function and usage directly from application should
 * be avoided if possible.
 *
 * @param ctx A valid nrx_context.
 * @param id The MIB id to retrieve.
 * @param res A buffer where the MIB value will be stored; Zero terminated
 * @param len The size of res.
 *
 * @return Zero on success or an error code.
 */
int nrx_get_mib_string(nrx_context ctx, const char *id, char *res, size_t len)
{
   int ret;
   NRX_ASSERT(ctx);
   NRX_ASSERT(id);
   NRX_ASSERT(res);
   
   ret = nrx_get_mib_val(ctx, id, res, &len);
   if(ret != 0)
      return ret;

   res[len] = '\0';

   return 0;
}

/*!
 * @ingroup MISC
 * @brief <b>Enable multi-domain support</b>
 *
 * Activates support for the IEEE 802.11d standard (the "Global Harmonization
 * standard"). This is required to allow a station to identify the regulatory
 * domain in which the station is located and to configure its PHY for
 * operation in that regulatory domain. Active scan will be avoided until
 * knowledge of the regulatory domain is obtained.
 *
 * Note that enabling multi-domain support will have a large impact on
 * the current consumption of the device if it is scanning frequently
 * while in disconnected mode since passive scanning is more expensive
 * than active scanning (the device has to stay awake longer).
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_enable_multi_domain(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);

   param.value = 1;
   return nrx_nrxioctl(ctx, NRXIOWMULTIDOMAIN, &param.ioc);
}

/*!
 * @ingroup MISC
 * @brief <b>Configure multi-domain support</b>
 *
 * This command will have effect only when multi-domain is enabled. It configures 
 * whether association is allowed with IEEE 802.11d compliant APs only or whether 
 * to use IEEE 802.11d information obtained but associate to both compliant and 
 * non-compliant APs. 
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param enforce When this parameter is non-zero association will only be done 
 *        with APs that are compliant with the IEEE 802.11d standard.  When 0, 
 *        association will also be done with APs that are not compliant. 
 */
int 
nrx_conf_multi_domain(nrx_context ctx, nrx_bool enforce)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);

   param.value = (enforce != 0);
   return nrx_nrxioctl(ctx, NRXIOWMULTIDOMAINENFORCE, &param.ioc);
}

/*!
 * @ingroup MISC
 * @brief <b>Disable multi-domain support</b>
 *
 * Deactivates support for the IEEE 802.11d standard 
 * (the "Global Harmonization standard").
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_disable_multi_domain(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);

   param.value = 0;
   return nrx_nrxioctl(ctx, NRXIOWMULTIDOMAIN, &param.ioc);
}

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Configure ARP filtering in FW</b>
 *
 * To avoid that ARP requests wake up host during power save, it is
 * possible to let fw handle these requests to different degrees.
 *
 * @param mode What type of filter to use.
 * @param ip My ip address.
 * @return
 * - 0 on success
 * - error code on failure
 */
int
nrx_conf_arp_filter(nrx_context ctx, nrx_arp_policy_t mode, in_addr_t ip)
{
   struct nrx_ioc_arp_conf param;
   NRX_ASSERT(ctx);
   NRX_CHECK(mode < 4);

   param.mode = mode;
   param.ip = ip;
   return nrx_nrxioctl(ctx, NRXIOWARPCONF, &param.ioc);
}

/*!
 * @ingroup MISC
 * @brief <b>Enable bluetooth coexistence</b>
 *
 * The bluetooth coexistence mode depends on knowledge of the type of bluetooth
 * traffic.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_enable_bt_coex(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);

   param.value = 1;
   return nrx_nrxioctl(ctx, NRXIOWBTCOEXENABLE, &param.ioc);
}


/*!
 * @ingroup MISC
 * @brief <b>Disable bluetooth coexistence</b>
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_disable_bt_coex(nrx_context ctx)
{
   struct nrx_ioc_bool param;
   NRX_ASSERT(ctx);

   param.value = 0;
   return nrx_nrxioctl(ctx, NRXIOWBTCOEXENABLE, &param.ioc);
}


/*!
 * @ingroup MISC
 * @brief <b>Configure BT coexistence</b>
 *
 * Configures the bluetooth coexistence, as several types of coexistence schemes exist. 
 * A new bluetooth coexistence configuration will only be activated when bluetooth
 * coexistence is enabled. When the configuration is changed with nrx_conf_bt_coex()
 * then nrx_disable_bt_coex() should be called followed by nrx_enable_bt_coex()
 * to effect the change.
 * 
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param bt_vendor Vendor of Bluetooth hardware
 *                   - 0x00 = CSR, 
 *                   - 0x01 = Broadcom, 
 *                   - 0x02 = STMicroelectronics, 
 *                   - 0x03-0xFF = RESERVED
 * @param pta_mode  Vendor specific definition of PTA interface. 
 *                  Settings CSR:
 *                   - 0x00 = 2-wire scheme
 *                   - 0x01 = 3-wire scheme
 *                   - 0x02 = Enhanced 3-wire
 *                  Settings Broadcom:
 *                   - 0x00 = 3-wire scheme
 *                  Settings STMicroelectronics:
 *                   - 0x00 = 3-wire scheme
 *                   - 0x01 = 4-wire scheme
 * @param pta_def   Length equals number of wires. For each PTA interface wire from Bluetooth, 
 *                  specify which gpio_coex_pin id (0 to 4) the wire is connected to and its 
 *                  active logic level (0="Low", 1="High"). For WLAN_Active, WLAN_Activity and
 *                  RF_Confirm this referes to the logic level when BT is blocked.
 *                  For CSR 2-wire scheme specify pins in the following order:
 *                   - BT_Priority
 *                   - WLAN_Active
 *                  For CSR 3-wire scheme specify pins in the following order:
 *                   - BT_Active
 *                   - WLAN_Active
 *                   - BT_Priority
 *                  For CSR Enhanced 3-wire scheme specify pins in the following order:
 *                   - RF_Active
 *                   - WLAN_Active
 *                   - BT_State
 *                  For Broadcom 3-wire scheme specify pins in the following order:
 *                   - BT_Activity
 *                   - WLAN_Activity
 *                   - BT_Priority and Status
 *                  For ST 3-wire scheme specify pins in the following order:
 *                   - BT_Activity
 *                   - WLAN_Activity
 *                   - BT_Priority and Status
 *                  For STMicroelectronics 3-wire scheme specify pins in the following order:
 *                   - RF_Request
 *                   - RF_Confirm
 *                   - Status
 *                  For STMicroelectronics 4-wire scheme specify pins in the following order:
 *                   - RF_Request
 *                   - RF_Confirm
 *                   - Status
 *                   - Freq
 * @param antenna_dual  0=Single, 1=Dual. 
 * @param antenna_sel0  0=Don't use AntSel0, 1=Use AntSel0.
 * @param antenna_sel1  0=Don't use AntSel1, 1=Use AntSel1.
 * @param antenna_level0 Logical level for AntSel0 in position BT, 0="Low", 1="High". 
 * @param antenna_level1 Logical level for AntSel1 in position BT, 0="Low", 1="High". 
 *
 * @return 
 * - 0 on success
 * - EINVAL on invalid arguments or failure.
 */
int
nrx_conf_bt_coex(nrx_context ctx, 
                 uint8_t bt_vendor,  
                 uint8_t pta_mode, 
                 const nrx_gpio_list_t *pta_def, 
                 uint8_t antenna_dual,
                 uint8_t antenna_sel0,
                 uint8_t antenna_sel1,
                 uint8_t antenna_level0,
                 uint8_t antenna_level1)
{
   int i;
   struct nrx_ioc_bt_conf param;

   /* Sanity check */
   NRX_CHECK(bt_vendor <= 2);
   NRX_CHECK(pta_def != NULL);
   for (i = 0; i < pta_def->len; i++)
      NRX_CHECK(pta_def->pin[i].gpio_pin <= 4);
   switch (bt_vendor)
   {
      case 0:                   /* CSR */
         NRX_CHECK(pta_mode <= 2);
         switch (pta_mode) {
            case 0x00: NRX_CHECK(pta_def->len == 2);break;
            case 0x01: NRX_CHECK(pta_def->len == 3);break;
            case 0x02: NRX_CHECK(pta_def->len == 3);break;      
         };break;
      case 1:                   /* Broadcom */
         NRX_CHECK(pta_mode == 0);
         switch (pta_mode) {
            case 0x00: NRX_CHECK(pta_def->len == 3);break;
         };break;
      case 2:                   /* STMicroelectronics */
         NRX_CHECK(pta_mode <= 1);
         switch (pta_mode) {
            case 0x00: NRX_CHECK(pta_def->len == 3);break;
            case 0x01: NRX_CHECK(pta_def->len == 4);break;
         };break;
   }
   NRX_CHECK(antenna_dual==0 || antenna_dual==1);
   NRX_CHECK(antenna_sel0==0 || antenna_sel0==1);
   NRX_CHECK(antenna_sel1==0 || antenna_sel1==1);
   NRX_CHECK(antenna_level0==0 || antenna_level0==1);
   NRX_CHECK(antenna_level1==0 || antenna_level1==1);


   /* Copy to struct */
   param.bt_vendor = bt_vendor;
   param.pta_mode = pta_mode;
   memset(param.pta_def, 0x0, sizeof(param.pta_def));
   for (i = 0; i < pta_def->len; i++) {
      param.pta_def[i]  = pta_def->pin[i].gpio_pin;
      if (pta_def->pin[i].active_high)
         param.pta_def[i] |=  0x10;
   }
   param.len = pta_def->len;
   param.antenna_dual = antenna_dual;
   param.antenna_sel0 = antenna_sel0;
   param.antenna_sel1 = antenna_sel1;
   param.antenna_level0 = antenna_level0;
   param.antenna_level1 = antenna_level1;

   return nrx_nrxioctl(ctx, NRXIOWBTCOEXCONF, &param.ioc);
}

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Antenna diversity</b>
 *
 * Settings to enable or disable antenna diversity.
 *
 * PRELIMINARY : Antenna diversity is not supported in the current
 *               implementation. This function currently does nothing.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param antenna_mode Specifies the antennas to use. Possible modes are:
 *                  -  0: use default setting (currently antenna #1).
 *                  -  1: use antenna #1.
 *                  -  2: use antenna #2.
 *                  -  3: use antenna diversity, i.e. both antenna #1 and #2.
 * @param rssi_thr RSSI threshold used by the antenna selection 
 *                algorithm. This is only used in mode 3. Minimum value is -120 dBm
 *                and maximum is 0 dBm.
 *
 * @retval Zero on success.
 * @retval EINVAL on invalid arguments.
 */
int
nrx_antenna_diversity(nrx_context ctx, 
                      uint32_t antenna_mode,
                      int32_t rssi_thr)
{
   struct nrx_ioc_ant_div param;
   NRX_ASSERT(ctx);
   NRX_CHECK(antenna_mode >= 0 && antenna_mode <= 3);
   if (antenna_mode == 3)
      NRX_CHECK(rssi_thr >= -120 && rssi_thr <= 0);

   param.antenna_mode = antenna_mode;
   param.rssi_threshold = rssi_thr;
   return nrx_nrxioctl(ctx, NRXIOWANTENNADIV, &param.ioc);
}

/*!
 * @ingroup MISC
 * @brief <b>Retrieve the manufacturer id string</b>
 *
 * @param ctx A valid nrx_context.
 * @param mfg_id A buffer where the manufacturer id will be stored as a zero-terminated string. 
 *               The maximum length of the manufacturer id string is 128 bytes.
 * @param len The size of buffer mfg_id. No more than len characters will be written
 *            to mfg_id. Minimum value is 1 and maximum value is 2^32-1.
 *
 * @return Zero on success or an error code.
 */
int
nrx_get_manufacturer_id(nrx_context ctx, char *mfg_id, size_t len)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(mfg_id);
   NRX_CHECK(len != 0);

   return nrx_get_mib_string(ctx, MIB_dot11ManufacturerID, mfg_id, len);
}

/*!
 * @ingroup MISC
 * @brief <b>Retrieve the product id</b>
 *
 * @param ctx A valid nrx_context.
 * @param prod_id A pointer to where the product id will be stored.
 *
 * @return Zero on success or an error code.
 */
int
nrx_get_product_id(nrx_context ctx, uint32_t *prod_id)
{
   int ret;
   size_t len = sizeof(*prod_id);
   NRX_ASSERT(ctx);
   NRX_ASSERT(prod_id);
   
   ret = nrx_get_mib_val(ctx, MIB_dot11ProductID, prod_id, &len);
   if(ret != 0)
      return ret;
   if(len != sizeof(*prod_id))
      ;
   
   return 0;
}

/*!
 * @ingroup MISC
 * @brief <b>Retrieve the product version string</b>
 *
 * @param ctx A valid nrx_context.
 * @param prod_version A buffer where the product version will be stored as a zero-terminated string.
 *                     The maximum length of the manufacturer id string is
 *                     32 bytes.
 * @param len The size of prod_version. No more than len characters will be
 *            written to prod_version. Minimum value is 1 and maximum is 2^32-1.
 *
 * @return Zero on success or an error code.
 */
int
nrx_get_product_version(nrx_context ctx, char *prod_version, size_t len)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(prod_version);
   NRX_CHECK(len != 0);

   return nrx_get_mib_string(ctx, MIB_dot11manufacturerProductVersion, 
                             prod_version, len);
}

/*!
 * @ingroup MISC
 * @brief <b>Enable device reset notification</b>
 *
 * Notify the caller when the device has stopped responding.  
 * The device will be reset and restarted by the driver but volatile
 * configurations such as scan jobs, scan filters, notifications
 * (except this one) and thresholds are lost. The
 * application should register this notification callback so that it can
 * reinitialize all those settings in case of device crash.  The
 * callback is invoked every time (until disabled) the condition is
 * met. 
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * @param cb Callback that is invoked to notify the caller that
 *           the device has been reset. The callback is invoked with operation 
 *           NRX_CB_TRIGGER on a successful notification. It will
 *           be passed a NULL-pointer for event_data.
 * @param cb_ctx Pointer to a user-defined callback context that will
 *               be passed to the callback on invocation. This is for
 *               caller use only, it will not be parsed or modified in
 *               any way by this library. This parameter can be NULL.
 *
 * @return An integer representing the event handler.
 */
nrx_callback_handle
nrx_register_dev_reset_notification(nrx_context ctx,
                                    nrx_callback_t cb,
                                    void *cb_ctx)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);
   return nrx_register_custom_event(ctx, "RESET", 0, cb, cb_ctx);
}
                        
/*!
 * @ingroup MISC
 * @brief <b>Disable device reset notification</b>
 *
 * The device hang notification feature is disabled. No further
 * device reset notifications will be made.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle A handle previously obtained from
 * nrx_register_dev_reset_notification.
 * 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments, e.g the handle is not registered.
 */
int
nrx_cancel_dev_reset_notification(nrx_context ctx, nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_custom_event(ctx, handle);
}

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Set the driver activity timeout value</b>
 *
 * This configures the time of inactivity to elapse before the driver 
 * will consider itself inactive. 
 * The inact_check_interval parameter defines how exact the timeout
 * will be. If the inact_check_interval is very short then the timeout
 * will be very accurate but the host CPU load will be high since the
 * timer will trigger frequently. Longer values for
 * inact_check_interval will decrease CPU load but reduce timeout
 * accuracy. Driver inactivity will never be signaled before the
 * timeout has passed, but it may be signaled after. The worst case
 * lag is timeout + inact_check_interval.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param timeout Activity timeout value in milliseconds. The driver is considered
 *        inactive if no rx/tx activity has occurred within the timeout period.
 *        A 0 value disabled the inactivity indications. Maximum value is 2^30-1.
 * @param inact_check_interval The activity timeout check interval. Range 1 to 2^30-1.
 * 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int
nrx_set_activity_timeout(nrx_context ctx, 
                         uint32_t timeout,
                         uint32_t inact_check_interval)
{
   struct nrx_ioc_activitytimeout param;
   NRX_ASSERT(ctx);
   if (timeout) {
      NRX_CHECK(timeout < 1073741824);
      NRX_CHECK(inact_check_interval > 0);
      NRX_CHECK(inact_check_interval < 1073741824);
   }
   param.timeout = timeout;
   param.inact_check_interval = inact_check_interval;
   return nrx_nrxioctl(ctx, NRXIOCACTIVITYTIMEOUT, &param.ioc);
}

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Crash firmware</b>
 *
 * Issuing this command will cause the firmware to crash. This is used
 * for debugging purposes.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int 
nrx_fw_suicide(nrx_context ctx)
{
   struct nrx_ioc ioc;
   NRX_ASSERT(ctx);
   return nrx_nrxioctl(ctx, NRXIOCFWSUICIDE, &ioc);
}

/*!
 * @ingroup MISC
 * @brief Instructs the driver to send an ADDTS request with the default values
 *
 * @param [in] ctx NRX context that was created by the call to nrx_init_context().
 *
 * @retval Zero on success.
 * @retval Errno on failure.
 *
 * <!-- NRX_API_FOR_TESTING -->
 */
int
nrx_ccx_send_addts(nrx_context ctx, uint8_t identifier)
{
#if 0
   struct nrx_ioc_addts param;
   NRX_ASSERT(ctx);

   param.identifier = identifier;

   return nrx_nrxioctl(ctx, NRXIOSNDADDTS, &param.ioc);
#endif
   struct nrx_ioc_uint8_t param;
   NRX_ASSERT(ctx);
   param.value = identifier;
   return nrx_nrxioctl(ctx, NRXIOSNDADDTS, &param.ioc);
}

/*!
 * @ingroup MISC
 * @brief Instructs the driver to send a DELTS request with the default values
 *
 * @param [in] ctx NRX context that was created by the call to nrx_init_context().
 * @param [in] identifier The identifier of the stream.
 *
 * @retval Zero on success.
 * @retval Errno on failure.
 *
 * <!-- NRX_API_FOR_TESTING -->
 */
int
nrx_ccx_send_delts(nrx_context ctx, uint8_t identifier)
{
   struct nrx_ioc_uint8_t param;
   NRX_ASSERT(ctx);
   param.value = identifier;
   return nrx_nrxioctl(ctx, NRXIOSNDDELTS, &param.ioc);
}

/*!
 * @ingroup MISC
 * @brief <b>Enter shutdown state</b>
 *
 * Issuing this command will cause the device to enter the shutdown state.
 * 
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 */
int 
nrx_shutdown(nrx_context ctx)
{
   struct nrx_ioc ioc;
   NRX_ASSERT(ctx);

   return nrx_nrxioctl(ctx, NRXIOCSHUTDOWN, &ioc);
}
