/* Copyright (C) 2007 Nanoradio AB */
/* $Id: counters.c 15531 2010-06-03 15:27:08Z joda $ */

#include "nrx_lib.h"
#include "nrx_priv.h"
#include "mac_mib_defs.h"

static int
get_counter_mib(nrx_context ctx, const char *mib, uint32_t *count)
{
   int ret = 0;
   uint32_t mib_val = 0;
   size_t mib_len = sizeof(mib_val);
   NRX_ASSERT(ctx);
   NRX_ASSERT(mib);
   NRX_ASSERT(count);
    
   ret = nrx_get_mib_val(ctx, mib, &mib_val, &mib_len);
   if (ret == 0)
      *count = mib_val;

   return ret;
}



/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current transmitted fragment count</b>
 * 
 * This counter is incremented for each acknowledged
 * transmitted fragment. A frame that is not fragmented will 
 * be considered as a single "fragment" and increases the 
 * counter with 1.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_tx_fragment_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11TransmittedFragmentCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current transmitted multicast frame count</b>
 * 
 * This counter is incremented for each successfully
 * transmitted multicast frame. Note that multicast in this case refers to
 * the 802.11 destination address, not to the ultimate destination
 * address.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_tx_mcast_frame_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11MulticastTransmittedFrameCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current failed transmitted frame count</b>
 * 
 * This counter is incremented when the transmit retry count
 * exceeds the retry limit.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_tx_failed_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11FailedCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current transmit retry count</b>
 * 
 * This counter is incremented for each frame successfully
 * transmitted after one or more retransmissions.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_tx_retry_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11RetryCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current multiple transmit retry count</b>
 * 
 * This counter is incremented for each frame successfully
 * transmitted after more than one retransmission.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_tx_multiple_retry_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11MultipleRetryCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current received duplicate frame count</b>
 * 
 * This counter is incremented for received frame duplicates.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_rx_frame_duplicates_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11FrameDuplicateCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current successful RTS/CTS count</b>
 * 
 * This counter is incremented when a CTS is received in
 * response to an RTS.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_rts_success_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11RTSSuccessCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current failed RTS/CTS count</b>
 * 
 * This counter is incremented when a CTS is not received in
 * response to an RTS.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_rts_failure_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11RTSFailureCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current ACK failure count</b>
 * 
 * This counter is incremented when an ACK is not received when
 * expected.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_ack_failure_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11AckFailureCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current received fragment count</b>
 * 
 * This counter is incremented for each successfully received
 * fragment. A frame that is not fragmented will be considered 
 * as a single "fragment" and increases the counter with 1.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_rx_fragment_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11ReceivedFragmentCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current received multicast frame count</b>
 * 
 * This counter is incremented for each successfully received
 * multicast frame (even though the frame is not intended for this STA).
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0). Note that multicast in this case refers to
 * the 802.11 destination address, not to the ultimate destination
 * address.
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_rx_mcast_frame_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11MulticastReceviedFrameCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current received FCS error count</b>
 * 
 * This counter is incremented when an FCS error is detected in
 * a received fragment. 
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_rx_fcs_error_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11FCSErrorCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current transmitted frame count</b>
 * 
 * This counter is incremented for each successfully
 * transmitted frame.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_tx_frame_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11TransmittedFrameCount, count);
}


/*!
 * @internal
 * @ingroup RADIO
 * @brief <b>Get the current received undecryptable frame count</b>
 * 
 * This counter is incremented when a received frame could not
 * be decrypted.
 *
 * The maximum value is 2^32-1, then it will wrap around
 * (i.e. restart from 0).
 *
 * @param [in]  ctx   NRX context that was created by the call to
 *                    nrx_init_context().
 *
 * @param [out] count Pointer to the output buffer that will hold the
 *                    requested counter value.
 *
 *
 * @retval 0      on success
 * @retval EINVAL on invalid arguments
 */
int
nrx_get_rx_undecryptable_count(nrx_context ctx, uint32_t *count)
{
   return get_counter_mib(ctx, MIB_dot11WEPUndecryptableCount, count);
}

