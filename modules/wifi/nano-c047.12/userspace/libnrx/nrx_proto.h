/* Copyright (C) 2007 Nanoradio AB */
/* $Id: nrx_proto.h 17821 2011-02-07 07:22:25Z peva $ */
/* This is a generated file. */
#ifndef __nrx_proto_h__
#define __nrx_proto_h__

#include <stdarg.h>

/* BEGIN GENERATED PROTOS */

/*!
  * @ingroup LIB
  * @brief <b>Returns the connection number used for event notification</b>
  *
  * @param ctx The context.
  *
  * @return The connection number, or a negative number on error.
  *
  * @note This functions does not follow the convention of 
  * returning zero or errno number.
  */
int
nrx_event_connection_number (nrx_context ctx);

/*!
  * @ingroup LIB
  * @brief <b>Waits for an event to arrive</b>
  *
  * @param ctx The context.
  * @param timeout The time to wait (in ms) for an event to occur,
  *                INFTIM (-1) means wait forever. If the value of
  *                timeout is 0, it shall return immediately. Should an
  *                event occur the function will return immediately.
  *                The timeout may be rounded upwards due to system
  *                limitations. Maximum value should be 2^31-1, but may
  *                be subjected to system limitations in the
  *                implementation of poll(2).
  *
  * @retval Zero         Indicates that there is an event to process.
  * @retval EWOULDBLOCK  Indicates that the wait timeout was exceeded.
  * @retval EPIPE        Poll indicates an abnormal condition. This happens
  *                      for instance if the context is destroyed.
  * @retval other        May return error numbers from socket(2), 
  *                      bind(2), or poll(2).
  */
int
nrx_wait_event (nrx_context ctx,
                int timeout);

/*!
  * @ingroup LIB
  * @brief <b>Retrieves and processes a pending event</b>
  *
  * @param ctx The context.
  *
  * @retval Zero         On success.
  * @retval other        May return error numbers from socket(2), 
  *                      bind(2) or recvfrom(2).
  */
int
nrx_next_event (nrx_context ctx);

/*!
  * @ingroup MISC
  * @brief <b>Set IBSS beacon period</b>
  *
  * The beacon period is the time between beacon frames and is used to inform
  * stations receiving the beacon when to expect the next beacon. In an IBSS
  * the station that starts the BSS will specify the beacon period and
  * establish the basic beaconing process for the IBSS. When selecting the
  * beacon-period in an IBSS you should consider power management and collision
  * avoidance. Setting the beacon period will only take effect when starting a
  * new IBSS.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param period The beacon period in TU (1 TU = 1024 microseconds). This
  *        cannot be 0; Maximum value is 2^16-1.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_set_ibss_beacon_period (nrx_context ctx,
                            uint16_t period);

/*!
  * @ingroup PS
  * @brief <b>Set IBSS ATIM period</b>
  *
  * The ATIM parameter defines the window size for sending ATIM frames.
  * This parameter is only used for IBSS power save. 
  * Setting the ATIM window will only take effect when starting a
  * new IBSS.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param window The ATIM window length in TU (1 TU = 1024 microseconds).
  * The maximum value is 2^16.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_set_ibss_atim_window (nrx_context ctx,
                          uint16_t window);

/*!
  * @ingroup LIB
  * @brief <b>Initializes an nrxlib context</b>
  *
  * The context should be freed after use.
  *
  * @param ctx A pointer to the context to initialise.
  * @param ifname The interface name for accesses. 
  *               If NULL an attempt to discover the correct interface
  *               will be made.
  *
  * @retval zero on success
  * @retval ENOMEM if memory could not be allocated
  * @retval ENODEV if ifname is NULL, but no interface could be found
  * @retval "other errno" in case of socket open failure
  *
  * <!-- NRX_API_EXCLUDE -->
  */
int
nrx_init_context (nrx_context *ctx,
                  const char *ifname);

/*!
  * @ingroup LIB
  * @brief <b>Free resources allocated to an nrxlib context</b>
  *
  * The context may not be used after this call.
  *
  * @param ctx The context to free.
  *
  * @return Returns nothing.
  *
  * <!-- NRX_API_EXCLUDE -->
  */
void
nrx_free_context (nrx_context ctx);

/*!
  * @ingroup LIB
  * @brief <b>Check version of Wireless Extensions</b>
  *
  * Checks which version of Wireless Extensions is used by the kernel.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param version The version to compare against.
  *
  * @retval Zero if the kernel version is exactly the same as the compared version.
  * @retval Negative if kernel version is less than the compared version.
  * @retval Positive if kernel version is greater than the compared version.
  */
int
nrx_check_wx_version (nrx_context ctx,
                      unsigned int version);

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
nrx_set_join_timeout (nrx_context ctx,
                      uint32_t value);

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
nrx_enable_multi_domain (nrx_context ctx);

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
nrx_conf_multi_domain (nrx_context ctx,
                       nrx_bool enforce);

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
nrx_disable_multi_domain (nrx_context ctx);

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
nrx_enable_bt_coex (nrx_context ctx);

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
nrx_disable_bt_coex (nrx_context ctx);

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
nrx_conf_bt_coex (nrx_context ctx,
                  uint8_t bt_vendor,
                  uint8_t pta_mode,
                  const nrx_gpio_list_t *pta_def,
                  uint8_t antenna_dual,
                  uint8_t antenna_sel0,
                  uint8_t antenna_sel1,
                  uint8_t antenna_level0,
                  uint8_t antenna_level1);

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
nrx_get_manufacturer_id (nrx_context ctx,
                         char *mfg_id,
                         size_t len);

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
nrx_get_product_id (nrx_context ctx,
                    uint32_t *prod_id);

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
nrx_get_product_version (nrx_context ctx,
                         char *prod_version,
                         size_t len);

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
nrx_register_dev_reset_notification (nrx_context ctx,
                                     nrx_callback_t cb,
                                     void *cb_ctx);

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
nrx_cancel_dev_reset_notification (nrx_context ctx,
                                   nrx_callback_handle handle);

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
nrx_ccx_send_addts (nrx_context ctx,
                    uint8_t identifier);

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
nrx_ccx_send_delts (nrx_context ctx,
                    uint8_t identifier);

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
nrx_shutdown (nrx_context ctx);

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
nrx_enable_ps (nrx_context ctx);

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
nrx_disable_ps (nrx_context ctx);

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
nrx_conf_ps (nrx_context ctx,
             nrx_bool rx_all_dtim,
             nrx_bool use_ps_poll,
             uint16_t listen_interval,
             uint32_t traffic_timeout);

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
nrx_enable_wmm_ps (nrx_context ctx,
                   nrx_sp_len_t sp_len);

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
nrx_conf_wmm_ps (nrx_context ctx,
                 uint32_t tx_period,
                 nrx_wmm_ac_t ac);

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
nrx_disable_wmm_ps (nrx_context ctx);

/*!
  * @ingroup PRIO
  * @brief Retrieve driver priority map.
  *
  * This retrieves the active priority map from the driver.
  *
  * @param [in]  ctx A valid nrx_context.
  * @param [out] priomap A pointer to a priomap to receive the map.
  *
  * @return Zero on success or an error code.
  *
  * <!-- NRX_API_EXCLUDE -->
  */
int
nrx_priomap_read (nrx_context ctx,
                  nrx_priomap *priomap);

/*!
  * @ingroup PRIO
  * @brief Update driver priority map.
  *
  * This updates the driver with a modified priority map.
  *
  * @param [in]  ctx A valid nrx_context.
  * @param [in]  priomap The priomap to set.
  *
  * @return Zero on success or an error code.
  *
  * <!-- NRX_API_EXCLUDE -->
  */
int
nrx_priomap_write (nrx_context ctx,
                   nrx_priomap priomap);

/*!
  * @ingroup PRIO
  * @brief Clears a priomap.
  *
  * This resets a priomap so all packets gets mapped to the BE class.
  *
  * @param [in]  ctx A valid nrx_context.
  * @param [out] priomap A pointer to the priomap to modify.
  *
  * @return Zero on success or an error code.
  *
  * <!-- NRX_API_EXCLUDE -->
  */
int
nrx_priomap_clear (nrx_context ctx,
                   nrx_priomap *priomap);

/*!
  * @ingroup PRIO
  * @brief Gets an entry in a priomap.
  *
  * This returns one the priority for one particular TOS/DSCP value.
  *
  * @param [in]  ctx     A valid nrx_context.
  * @param [in]  priomap A pointer to the priomap to modify.
  * @param [in]  tos     The index to return the priority for.
  * @param [out] uprio   A pointer to the priomap to modify.
  *
  * @return Zero on success or an error code.
  *
  * <!-- NRX_API_EXCLUDE -->
  */
int
nrx_priomap_get (nrx_context ctx,
                 nrx_priomap priomap,
                 uint8_t tos,
                 uint8_t *uprio);

/*!
  * @ingroup PRIO
  * @brief Modifies a priomap.
  *
  * This modifies a priomap so that packets with a tos field set
  * to index will be mapped to 802.1d priority uprio.
  *
  * @param [in]     ctx A valid nrx_context.
  * @param [in,out] priomap A pointer to the priomap to modify.
  * @param [in]     tos Which TOS entry to modify.
  * @param [in]     uprio IEEE802.1d priority to set (0-7).
  *
  * @return Zero on success or an error code.
  *
  * <!-- NRX_API_EXCLUDE -->
  */
int
nrx_priomap_set (nrx_context ctx,
                 nrx_priomap *priomap,
                 uint8_t tos,
                 uint8_t uprio);

/*!
  * @ingroup PRIO
  * @brief Modify priomap.
  *
  * This modifies a priomap so that packets with a dscp code point of
  * dscp will be mapped to 802.1d priority uprio.
  *
  * @param [in]     ctx A valid nrx_context.
  * @param [in,out] priomap A pointer to the priomap to modify.
  * @param [in]     dscp Which code point to modify.
  * @param [in]     uprio IEEE802.1d priority to set (0-7).
  *
  * @return Zero on success or an error code.
  *
  * This differs from nrx_priomap_set in that you only specify a 6-bit
  * code point, instead of the complete 8-bit field. The two lower bits
  * in the DSCP field are unused.
  *
  * <!-- NRX_API_EXCLUDE -->
  */
int
nrx_priomap_set_dscp (nrx_context ctx,
                      nrx_priomap *priomap,
                      uint8_t dscp,
                      uint8_t uprio);

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
nrx_enable_ht_rates (nrx_context ctx);

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
nrx_disable_ht_rates (nrx_context ctx);

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
nrx_ht_rates_enabled (nrx_context ctx,
                      nrx_bool *enabled);

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
nrx_enable_wmm (nrx_context ctx);

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
nrx_disable_wmm (nrx_context ctx);

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
nrx_set_region_code (nrx_context ctx,
                     nrx_region_code_t code);

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
nrx_enable_link_supervision (nrx_context ctx);

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
nrx_conf_link_supervision_rx_beacon (nrx_context ctx,
                                     uint32_t rx_fail_limit,
                                     uint32_t rx_fail_timeout);

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
nrx_conf_link_supervision_tx (nrx_context ctx,
                              uint32_t tx_fail_limit);

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
nrx_get_snr (nrx_context ctx,
             int32_t *snr,
             nrx_detection_target_t type);

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
nrx_get_rssi (nrx_context ctx,
              int32_t *rssi,
              nrx_detection_target_t type);

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
nrx_get_tx_rate (nrx_context ctx,
                 nrx_rate_t *rate);

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
nrx_get_rx_rate (nrx_context ctx,
                 nrx_rate_t *rate);

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
nrx_enable_roam (nrx_context ctx);

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
nrx_disable_roam (nrx_context ctx);

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
nrx_add_roam_ssid_filter (nrx_context ctx,
                          nrx_ssid_t ssid);

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
nrx_del_roam_ssid_filter (nrx_context ctx,
                          nrx_ssid_t ssid);

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
nrx_conf_roam_filter (nrx_context ctx,
                      nrx_bool enable_blacklist,
                      nrx_bool enable_wmm,
                      nrx_bool enable_ssid);

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
nrx_conf_roam_rssi_thr (nrx_context ctx,
                        nrx_bool enable,
                        int32_t roam_thr,
                        int32_t scan_thr,
                        uint32_t margin);

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
nrx_conf_roam_snr_thr (nrx_context ctx,
                       nrx_bool enable,
                       int32_t roam_thr,
                       int32_t scan_thr,
                       uint32_t margin);

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
nrx_conf_roam_rate_thr (nrx_context ctx,
                        nrx_bool enable,
                        uint32_t roam_thr,
                        uint32_t scan_thr);

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
int
nrx_conf_roam_auth (nrx_context ctx,
                    uint8_t enable,
                    nrx_authentication_t auth_mode,
                    nrx_encryption_t enc_mode);

/*!
  * @ingroup SCAN
  * @brief <b>Configure parameters for scan</b>
  *
  * Scans can be active (probe request) or passive (listening for
  * beacons) and can be directed (triggered by the application) or
  * periodic (triggered periodically by the device or the driver).  A
  * special case occurs when the device is associated. In this case
  * only active scans are allowed and since the device has to
  * leave the current channel when scanning it will adversely effect
  * traffic throughput. For this reason the scan period and timeouts
  * are specified separately for the associated case. Scan jobs will
  * remain in effect while associated and the scan job parameters
  * for scan while associated will come into effect. The normal
  * scan job parameters will be used again when the association is
  * terminated.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param preamble Long or short preamble for probe requests. Only valid
  *                 for active scan.
  * @param rate Rate in the "supported rates" encoding from the 802.11
  *             standard. This rate is used for probe
  *             requests. Only valid for active scan. A rate value of 0
  *             means that the firmware will pick the rate for probe
  *             requests as it sees fit.
  * @param probes_per_ch Number of probe requests sent on each channel.
  *                    Probe requests will be sent with an interval time
  *                    of min_ch_time. Minimum value is 1 and maximum is 255.
  *                    Only valid for active scan.
  *                    A higher value increases the likelyhood that the APs
  *                    on a channel will hear a probe request in case of
  *                    bad radio conditions.
  *                    The probe request will be directed to the ssid specified
  *                    in the scan job. If an ssid is not specified for the scan
  *                    job, then the probe request will be broadcasted.
  * @param sn_pol Bitmask that defines the notification policy. This defines
  *           when the host should be invoked. The host can be notified 
  * - as soon as the first network is found (FIRST_HIT)
  *   (notified at most once per scan job per scan period). 
  * - when a scan job is complete (JOB_COMPLETE)
  *   (notified once per scan job per scan period)
  * - when all scan jobs in a scan period is complete (ALL_DONE)
  *   (notified once per scan period)
  * - when a scan job is complete and a network is found (JOB_COMPLETE_WITH_HIT)
  *   (notified once per scan job per scan period)
  * The firmware will generate notifications to the driver based on the
  * notification policy set with nrx_conf_scan().  The driver will
  * notify the NRXAPI library which will in turn execute any registered
  * notification callbacks based on the notification policy set in
  * nrx_register_scan_notification_handler(). Because of this,
  * notification handlers registered with the library will only work if
  * the selected notification policy has also been configured in
  * firmware with nrx_conf_scan().
  *
  * @param scan_period Time in ms between scans. If this parameter is 0
  *                    the device will never scan by itself. Otherwise it
  *                    will periodically initiate a new scan when this
  *                    period has passed since the last scan was completed. 
  *                    This parameter is not used when the device is associated.
  *                    The maximum value is 2^32-1 ms.
  * @param probe_delay Time in microseconds spent on the channel before sending 
  *                    the first probe request. This parameter will only be used 
  *                    for active scan.
  *                    The minimum value is 1 and maximum is 2^16-1 microseconds.
  * @param pa_ch_time The time in TU (1 TU = 1024 microseconds) spent on
  *                    each channel during passiv scanning. The channel will be
  *                    changed when this amount of time has passed.
  *                    This parameter is not used when the device is
  *                    associated.  The minimum value is 0. The maximum value
  *                    is 2^16-1 TU.
  * @param ac_min_ch_time The minimum time in TU (1 TU = 1024 microseconds) spent on
  *                    each channel
  *                    during active scanning. If the medium is idle during
  *                    this time, scanning proceeds to the next channel.
  *                    Otherwise the device keeps listening for probe responses
  *                    in the channel for the ac_max_ch_time. This
  *                    parameter is not used when the device is
  *                    associated.  The minimum value is 0. The maximum
  *                    value is 2^16-1 TU.
  * @param ac_max_ch_time The maximum time in TU (1 TU = 1024 microseconds) spent on
  *                    each channel
  *                    during active scanning. This
  *                    parameter is not used when the device is
  *                    associated.  The minimum value is 0. The maximum
  *                    value is 2^16-1 TU.  The value must be larger than
  *                    min_ch_time.
  * @param as_scan_period Time in ms between scans when the device is
  *                    associated. 
  *                    If this parameter is 0 the device will never scan when
  *                    associated. Otherwise it will periodically initiate a
  *                    new scan when this period has passed since the last scan
  *                    was completed.
  *                    This parameter affects traffic throughput and latency,
  *                    shorter times degrade performance more.
  *                    This parameter is only used when the device is associated.
  *                    The maximum value is 2^32-1 ms. 
   * @param as_min_ch_time The minimum time in TU (1 TU = 1024 microseconds) spent on
  *                    each channel when the device is associated. If the medium is 
  *                    idle during this time, scanning proceeds to the next channel.
  *                    Otherwise the device keeps listening for probe responses
  *                    in the channel for as_max_ch_time.
  *                    This parameter is only used when the device is associated.
  *                    The minimum value is 0. The maximum value is 2^16-1 TU.
  * @param as_max_ch_time The maximum time in TU (1 TU = 1024 microseconds) spent on
  *                    each channel when the device
  *                    is associated. This parameter is only used when the device is
  *                    associated.
  *                    The minimum value is 0. The maximum value is 2^16-1 TU.
  *                    The value must be larger than as_min_ch_time.
  * @param max_scan_period Maximum time in ms between scan periods when disconnected. 
  *                    This parameter is used as a limitation of the scan period when 
  *                    the nominal scan period doubles after every period_repetition.
  *                    The maximum value is 2^32-1 ms. 
  *                    Scenario: scan_period = 5000, max_scan_period = 50000
  *                       gives the following periods 5000, 10000, 20000, 40000, 50000
  *                       period doubled until 40000 reached, then it will stay with 50000 
  * @param max_as_scan_period Maximum time in ms between scan periods when connected. 
  *                    This parameter is used as a limitation of the scan period when 
  *                    the nominal scan period doubles after every period_repetition.
  *                    The maximum value is 2^32-1 ms. 
  *                    Scenario: as_scan_period = 5000, max_as_scan_period = 50000
  *                       gives the following periods 5000, 10000, 20000, 40000, 50000
  *                       period doubled until 40000 reached, then it will stay with 50000 
  *
  * @param period_repetition Number of repetitions of a scan period before it is doubled.
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  *  
  */
int
nrx_conf_scan (nrx_context ctx,
               nrx_preamble_type_t preamble,
               uint8_t rate,
               uint8_t probes_per_ch,
               nrx_sn_pol_t sn_pol,
               uint32_t scan_period,
               uint16_t probe_delay,
               uint16_t pa_ch_time,
               uint16_t ac_min_ch_time,
               uint16_t ac_max_ch_time,
               uint32_t as_scan_period,
               uint16_t as_min_ch_time,
               uint16_t as_max_ch_time,
               uint32_t max_scan_period,
               uint32_t max_as_scan_period,
               uint8_t period_repetition);

/*!
  * @ingroup SCAN
  * @brief <b>Start or stop a scan job</b>
  *
  * Scan jobs added with nrx_add_scan_job() will be stopped by default. The scan
  * job state must be set to RUNNING in order to start the scan job. When the
  * scan job is started it will executed according to the configured time
  * parameters. The scan job state can be set to STOPPED to stop the scan job.
  * Each scan job can be started/stopped independently.
  *
  * During ongoing scanning, any scan jobs that are set to state RUNNING will
  * begin execution during the next scan period. But, if the scan job that is
  * set to RUNNING state has lower priority than the currently running scan job,
  * it will be executed during the current scan period.
  *
  * The first scan job which is set to state running scan job will start the
  * scan period.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param sj_id A scan job id. This identifies the scan
  *              job to be started/stopped.
  *
  * @param state The state that the scan job should be set to.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  *
  */
int
nrx_set_scan_job_state (nrx_context ctx,
                        int32_t sj_id,
                        nrx_scan_job_state_t state);

/*!
  * @ingroup SCAN
  * @brief <b>Add a scan filter</b>
  *
  * The scan filters reside in the device. When host wakeup is enabled
  * the device will only wake the host up on results that pass the scan
  * filter coupled to the scan job. Filtered scan results are silently
  * discarded by the device and will not be visible to the
  * host. Several scan filters can be defined. They are identified by
  * the sf_id parameter. There is a
  * limit to the number of scan filters that can exist in the device so
  * this call may fail if the current limit is passed. 
  * The scan filters will be associated to scan jobs (see nrx_add_scan_job) and
  * will therefore be used whenever the job is executued (periodic or
  * triggered).
  *
  * The application have to keep track of all the currently configured scan
  * filters.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param sf_id The scan filter id output buffer. This identifies the scan
  *              filter so that several scan filters can be defined.
  *              The id value is filled in by this function call.
  * @param bss_type Bitmask defining which BSS type to scan for (infrastructure and/or ad-hoc).
  * @param rssi_thr RSSI threshold. Pass results with a higher RSSI than this
  *                 value [dBm] (signed). Minimum value is -120 dBm and maximum is 100 dBm.
  *                 Values > 0 valid for RELATIVE thresholds only
  * @param snr_thr SNR threshold. Pass results with a higher SNR than this
  *                 value. Minimum value is 0 and maximum 40.
  * @param threshold_type Type of threshold, ABSOLUTE(0) or RELATIVE(1) to the connected STA
  *                       Relative type only active when connected.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * 
  */
int
nrx_add_scan_filter (nrx_context ctx,
                     int32_t *sf_id,
                     nrx_bss_type_t bss_type,
                     int32_t rssi_thr,
                     uint32_t snr_thr,
                     uint16_t threshold_type);

/*! 
  * @ingroup SCAN
  * @brief <b>Delete a scan filter</b>
  *
  * The scan job tied to the scan filter must be deleted before the filter
  * can be deleted for proper operation. If a scan filter that is in use
  * by a scan job is deleted then the scan job will continue to execute
  * without a scan filter. Such a scan job will use any new filters that have
  * the same id as a previously deleted filter. 
  * 
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param sf_id A user supplied scan filter id. This identifies the scan
  *              filter to be deleted. The scan filter id -1 is reserved and cannot be used.
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * 
  */
int
nrx_delete_scan_filter (nrx_context ctx,
                        int sf_id);

/*!
  * @ingroup SCAN
  * @brief <b>Add a scan job</b>
  *
  * The device maintains a list of scan jobs that may be executed
  * sequentially or be aggregated in any way useful according to the parameters
  * setup in
  * nrx_conf_scan(). When a scan job is executed probe requests for the
  * requested SSID will be sent on the channels specified in the job if
  * active scan is enabled. Otherwise the device will passively listen
  * for beacons on the specified channels. A scan job is identified by
  * the sj_id parameter. The scan jobs will
  * be executed in priority order (higher prioiry values run first). If two
  * scan jobs have the same priority their execution order is undefined.
  * There is a limit to the number of scan jobs that can exist in the device so
  * this call may fail if the current limit is passed.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param sj_id The scan job id output buffer. This identifies the scan
  *              job so that several scan jobs can be defined. 
  *              The ID value is filled in by this function call.
  * @param ssid Defines the SSID that the scan job should look for. 
  *             A zero-length SSID counts as a "broadcast" SSID
  *             that will cause all SSIDs to be scanned for.
  * @param bssid A BSSID to scan for. If this is set to the broadcast BSSID
  *              (ff:ff:ff:ff:ff:ff) then all BSSIDs will be scanned (subject
  *              to the SSID parameter). BSSID-specific scan can be performed
  *              be using zero-length SSID. Otherwise the results have to match
  *              both ssid and bssid.
  * @param scan_type Active or passive scan. The device will always perform
  *             active scans when it is associated. To prevent execution of a
  *             job when associated the flags parameter should be used.
  * @param ch_list Defines the channels to scan. The channel list is subject
  *                to further filtering due to 802.11d restrictions.
  * @param flags Bitmask that defines whether the scan job should be used when
  *              associated and/or when not associated.
  * @param prio  Priority value for the scan job. Jobs with higher priority
  *              values are run before jobs with lower priority values.
  *              Valid priorities are in the range 1-128, e.g 128 is prioritized.
  * @param ap_exclude May be either 0 or 1. When set to 1 the AP which the
  *                   device is associated with should be exluded from the scan
  *                   result. This means that scan result information and 
  *                   scan notifications triggered by this AP will not be 
  *                   forwarded to the host. However, this AP will still be 
  *                   included in the scan results list presented by e.g
  *                   nrx_get_scan_list().
  *
  *
  * @param sf_id Defines the scan filter to use for the scan job. The value
  *              should be set to -1 if no filter should be used.
  * @param run_every_nth_period Defines how often the scan job should be executed
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - TODO if no more scan jobs can be created.
  * 
  */
int
nrx_add_scan_job (nrx_context ctx,
                  int32_t *sj_id,
                  nrx_ssid_t ssid,
                  nrx_mac_addr_t bssid,
                  nrx_scan_type_t scan_type,
                  nrx_ch_list_t ch_list,
                  nrx_job_flags_t flags,
                  uint8_t prio,
                  uint8_t ap_exclude,
                  int sf_id,
                  uint8_t run_every_nth_period);

/*!
  * @ingroup SCAN
  * @brief <b>Delete a scan job</b>
  *
  * If the scan job is deleted during ongoing scan an error code will be
  * returned. In this case the application can retry again later until the
  * the deletion is successful.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param sj_id A scan job id. This identifies the scan
  *              job to be deleted. 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - EBUSY if the scan job is currently executing.
  * 
  */
int
nrx_delete_scan_job (nrx_context ctx,
                     int32_t sj_id);

/*!
  * @ingroup SCAN
  * @brief <b>Get notification of completed scans</b>
  *
  * The caller will be notified when a scan sequence is complete
  * (periodic or manually triggered). The current scan results can then be
  * retrieved by a call to nrx_get_scan_list(). The callback is invoked every
  * time (until disabled) when the condition specified by sn_pol is met.
  *
  * This function can be called several times, e.g. with different notification
  * policies for different callbacks. 
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param sn_pol Bitmask that defines the notification policy. This
  * defines when the callback should be invoked. Observe that the
  * callback will not be invoked unless the bitmask is a subset of the
  * notification policy used in the scan configuration command. The
  * callback can be notified
  * - as soon as the first network is found (FIRST_HIT) (notified at
  *   most once per scan job per scan period). This policy only
  *   works when a scan filter has been configured.
  * - when a scan job is complete (JOB_COMPLETE) (notified once per
  *   scan job per scan period)
  * - when all scan jobs in a scan period is complete (ALL_DONE)
  *   (notified once per scan period)
  * - when a scan job is complete and a network is found
  *    (JOB_COMPLETE_WITH_HIT) (notified once per scan job per scan
  *    period)
  *
  * @param cb Callback that is invoked to notify the caller that
  *        a directed scan sequence has completed.  The callback is
  *        invoked with operation NRX_CB_TRIGGER on a successful 
  *        notification.
  *        It will be passed a pointer to nrx_scan_notif_t, containing
  *        the policy that triggered the callback and the job id for
  *        the notifying job, for event_data. The job id element is not valid
  *        in case the policy is ALL_DONE.
  *
  * @param cb_ctx Pointer to a user-defined callback context that will
  *               be passed to the callback on invocation. This is for
  *               caller use only, it will not be parsed or modified in
  *               any way by this library. This parameter can be NULL.
  * @return 
  * - Pointer to callback handle if success.
  * - 0 if error.
  *
  */
nrx_callback_handle
nrx_register_scan_notification_handler (nrx_context ctx,
                                        nrx_sn_pol_t sn_pol,
                                        nrx_callback_t cb,
                                        void *cb_ctx);

/*!
  * @ingroup SCAN
  * @brief <b>Disable scan notifications</b>
  *
  * The scan notification feature is disabled and no further
  * scan notifications will be made.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle A handle previously obtained from
  * nrx_register_scan_notification_handler. The handle will no longer be
  * valid after this call.
  * 
  * @return
  * - 0 on success.
  * - EINVAL on invalid parameters, e.g. the callback is not registered.
  */
int
nrx_cancel_scan_notification_handler (nrx_context ctx,
                                      nrx_callback_handle handle);

/*!
  * @ingroup SCAN
  * @brief <b>Start a directed scan and specify channel interval</b>
  *
  * Any scan complete notification callbacks registered will be triggered
  * when the scan is complete. A directed scan can be performed in
  * parallel with a periodic scan.
  * It is always safe to call this function, when directed scans are
  * prohibited the call will do nothing.
  * A directed scan can trigger scan notifications in the same way as periodic
  * scans since the only difference between directed and period scan is the time
  * when the scan job is executed.
  * Note that scan jobs that are in state SUSPENDED cannot be triggered.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param sj_id A scan job id to trigger. 
  * @param channel_interval interval in ms between channels
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - EBUSY if a directed scan is already in progress (RELEASE 5).
  * 
  */
int
nrx_trigger_scan_ex (nrx_context ctx,
                     int32_t sj_id,
                     uint16_t channel_interval);

/*!
  * @ingroup SCAN
  * @brief <b>Start a directed scan</b>
  *
  * Any scan complete notification callbacks registered will be triggered
  * when the scan is complete. A directed scan can be performed in
  * parallel with a periodic scan.
  * It is always safe to call this function, when directed scans are
  * prohibited the call will do nothing.
  * A directed scan can trigger scan notifications in the same way as periodic
  * scans since the only difference between directed and period scan is the time
  * when the scan job is executed.
  * Note that scan jobs that are in state SUSPENDED cannot be triggered.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param sj_id A scan job id to trigger. 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  * - EBUSY if a directed scan is already in progress (RELEASE 5).
  * 
  */
int
nrx_trigger_scan (nrx_context ctx,
                  int32_t sj_id);

/*!
  * @ingroup SCAN
  * @brief <b>Flush scan list</b>
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * Flushes the scan list
  *
  * @return 
  * - 0 on success.
  * 
  */
int
nrx_flush_scanlist (nrx_context ctx);

/*!
  * @ingroup SCAN
  * @brief <b>Get the latest scan results</b>
  *
  * The number of results that can be stored in the scan list is
  * limited by the memory on the host. A network is identified by the
  * combination of the BSSID and SSID. This means that there may be
  * several entries with the same SSID but different BSSIDs (such as
  * several APs in the same ESS) and several entries with the same
  * BSSID but different SSIDs (such a stealth AP being shown once 
  * as found by a "broadcast" scan job (without a SSID) and once
  * as found by a scan job that probes for the particular SSID of the
  * stealth AP (with the SSID)).
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param scan_nets Output buffer, allocated by the caller.
  *                  The output format is the same as used by the
  *                  SIOCGIWSCAN Wireless Extension ioctl. This consists of
  *                  a list of elements specifying different aspects of a
  *                  net, for instance BSSID, SSID, beacon RSSI etc. Code
  *                  that parses this structure can be found in Wireless
  *                  Tools iwlib.c function iw_process_scanning_token().
  *                  TODO : Further document result format, and implement
  *                  functions to help parse the data.
  * @param len Pointer to the size of the input buffer (parameter scan_nets) on
  *            input, pointer to the number of bytes copied on a successful call
  *            or to the size needed if the return value was EMSGSIZE.
  *
  *
  * @return
  * - 0 on success.
  * - EMSGSIZE if the input buffer was too small.
  */
int
nrx_get_scan_list (nrx_context ctx,
                   void *scan_nets,
                   size_t *len);

/* END GENERATED PROTOS */

#endif /* __nrx_proto_h__ */

