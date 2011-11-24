/* Copyright (C) 2007 Nanoradio AB */
/* $Id: pmc_proto.h,v 1.30 2007-12-19 14:09:24 kath Exp $ */
/* This is a generated file. */
#ifndef __pmc_proto_h__
#define __pmc_proto_h__

#include <stdarg.h>
#include "../libnrx/nrx_proto.h"

/** \defgroup IA Interference avoidance. */


/* BEGIN GENERATED PROTOS */

/*!
  * @ingroup IA
  * @brief Configure Contention window.
  *
 5A * Custom contention window settings can override the settings decided by the
  * AP. This function must be called while associated to have an effect. 
  * This function can be called several times to configure the contention
  * window for different access categories.
  *
  * The old window values that will be restored when the "override" parameter is set 
  * to FALSE are those that were in effect when nrx_conf_cwin() was last called with 
  * the "override" parameter set to TRUE. This means that the restored values may 
  * be incorrect for the current association if the association switched APs 
  * between the two calls.
  *
  * A time slot is 20us for the long slot time and 9us for the short slot time.
  * 
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param ac Bitmask of WMM access categories which the configuration should
  *           apply to. Note that this parameter also is used when "override"
  *           is FALSE. 0 is an invalid value. When the associated AP is not
  *           supporting WMM the contention window for AC_BE will be used.
  * @param min CWmin. Range 0-15. The window min will be 2^CWmin-1 time slots. 
  *            Default value is 4(BG), 4(BE), 3(VI), 2(VO).
  * @param max CWmax. Range 0-15. The window max will be 2^CWmax-1  time slots. 
  *            Must be larger than CWmin.
  *            Default value is 10(BG), 10(BE), 4V(I), 3(VO).
  * @param override Boolean value that decides whether the configured cwin
  *                 parameters should be used. If this value is false the
  *                 the previous settings will be restored. Only those access
  *                 categories previously changed will be restored.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_conf_cwin (nrx_context ctx,
               nrx_wmm_ac_t ac,
               uint8_t min,
               uint8_t max,
               nrx_bool override);

/*!
  * @ingroup IA
  * @brief Configure interference avoidance mode.
  * 
  * If interference avoidance is disabled entirely when this function
  * is called, it will we set in communication mode.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @param ed_ivc IVC trigger energy detect threshold (ED1) in
  * dBm. When the average energy level (AEL) over the ael_win last
  * transmitted packets exceed the ed_ivc threshold then the
  * interference value counter (IVC) is increased.
  *
  * @param ed_tx_ia The transmission energy detect threshold for
  * interference avoidance mode, in dBm. A packet will only be
  * transmitted if the energy level detected in the channel is lower
  * than the configured energy detect threshold. This threshold is only
  * used when the device is in interference avoidance mode. The energy
  * detect threshold for normal mode is not configurable. Minimum
  * value is -90 and maximum -30 dBm.
  * 
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_conf_iav (nrx_context ctx,
              int8_t ed_ivc,
              int8_t ed_tx_ia);

/*!
  * @ingroup IA
  * @brief Enter interference avoidance mode.
  *
  * The radio transmission energy detect threshold will be set to the
  * interference avoidance level (see nrx_conf_iav()).
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_enable_ia_mode (nrx_context ctx);

/*!
  * @ingroup IA
  * @brief Leave interference avoidance mode.
  *
  * The radio transmission energy detect level will be set to the
  * default, normal mode, level.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_disable_ia_mode (nrx_context ctx);

/*!
  * @ingroup IA
  * @brief Enable interference error rate threshold notifications.
  *
  * The notification callback is invoked when the interference error
  * rate (IER) goes above the configured threshold value and the packet
  * error rate goes above the configured threshold. Note that both
  * conditions must be fulfilled before the callback is
  * invoked. Several thresholds can be defined. They are identified by
  * the thr_id parameter. There is a dynamic
  * limit to the number of triggers that can exist in the system so
  * this call may fail if the limit would be passed. The limit depends
  * on the available memory on the host. The notification callback is
  * registered with a call to nrx_register_ier_threshold_callback().
  *
  * This function can be called independently of
  * nrx_enable_per_threshold(). However, some interdependency do exist
  * as IER and PER triggers are synchronized, see chk_period for
  * further information.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  *
  * @param thr_id The threshold id output buffer. This will identify the threshold
  *              so that several thresholds can be defined.
  *              The threshold ID value is filled in by this function call.
  * @param ier_thr Interference error rate threshold (in percent).
  * @param per_thr Packet error rate threshold (in percent).
  *
  * @param chk_period The interference error rate threshold check period in ms.
  *        The minimum value is 100 and maximum supported time is 35 minutes 
  *        (2100000 ms). The IER threshold will be compared with the
  *        interference error rate with this interval. Should several
  *        IER triggers be registered the shortest interval used will
  *        be used for all triggers. Furthermore, IER triggers are
  *        dependent of PER triggers and the interval will be the same
  *        for IER and PER triggers. It will be chosen as the shortest
  *        interval over all registered IER and PER triggers. 
  * @param dir Bitmask defining the trigger directions. The callback can be
  *            triggered when the value rises above or sinks below the
  *            IER threshold, or both.
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_enable_ier_threshold (nrx_context ctx,
                          int * thr_id,
                          uint32_t ier_thr,
                          uint32_t per_thr,
                          uint32_t chk_period,
                          nrx_thr_dir_t dir);

/*!
  * @ingroup IA
  * @brief Disable interference error rate threshold notifications.
  *
  *
  * When the interference error rate threshold feature is disabled the host will
  * never be woken up by the device.
  *
  * Calling this function will cancel corresponding callbacks which are
  * using the same thr_id.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id identifying the threshold trigger
  *        that should be disabled. 
  *
  * @return 
  * - 0 on success.
  * - EINVAL on invalid arguments.
  */
int
nrx_disable_ier_threshold (nrx_context ctx,
                           int thr_id);

/*!
  * @ingroup IA
  * @brief Register interference error rate callback.
  * 
  * This will register a callback for IER triggers, see
  * nrx_enable_ier_threshold for further details.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param thr_id The threshold id. This identifies the threshold
  *              so that several thresholds can be defined. 
  *              The id value is obtained from nrx_enable_ier_threshold
  * @param cb Callback that is invoked to notify the caller that
  *           the threshold trigger has been triggered.
  *           The callback is invoked with operation NRX_CB_TRIGGER on a 
  *           successful notification whereupon event_data will be a pointer 
  *           to a nrx_event_mibtrigger structure which contains further 
  *           information. When the threshold is cancelled cb is called 
  *           with operation NRX_CB_CANCEL and event_data set to NULL.
  *
  * @param cb_ctx Pointer to a user-defined callback context that will
  *               be passed to the callback on invocation. This is for
  *               caller use only, it will not be parsed or modified in
  *               any way by this library. This parameter can be NULL.
  * 
  * @return A handle to a callback (an unsigned integer type). The only
  * use for this is to pass it to nrx_cancel_ier_threshold_callback
  * to cancel the callback.
  * @retval Zero on memory allocation failure
  * @retval Non-zero a valid callback handle.
  */
nrx_callback_handle
nrx_register_ier_threshold_callback (nrx_context ctx,
                                     int32_t thr_id,
                                     nrx_callback_t cb,
                                     void * cb_ctx);

/*!
  * @ingroup IA
  * @brief Cancel IER threshold callback
  * 
  * This will cancel a callback for IER trigger.
  *
  * @param ctx NRX context that was created by the call to nrx_init_context().
  * @param handle Callback handle obtained from nrx_register_ier_threshold_callback.
  * 
  * @return Always return zero.
  */
int
nrx_cancel_ier_threshold_callback (nrx_context ctx,
                                   nrx_callback_handle handle);

/* END GENERATED PROTOS */

#endif /* __pmc_proto_h__ */
