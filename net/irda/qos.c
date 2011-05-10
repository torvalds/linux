/*********************************************************************
 *
 * Filename:      qos.c
 * Version:       1.0
 * Description:   IrLAP QoS parameter negotiation
 * Status:        Stable
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Sep  9 00:00:26 1997
 * Modified at:   Sun Jan 30 14:29:16 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-2000 Dag Brattli <dagb@cs.uit.no>,
 *     All Rights Reserved.
 *     Copyright (c) 2000-2001 Jean Tourrilhes <jt@hpl.hp.com>
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *     MA 02111-1307 USA
 *
 ********************************************************************/

#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/parameters.h>
#include <net/irda/qos.h>
#include <net/irda/irlap.h>
#include <net/irda/irlap_frame.h>

/*
 * Maximum values of the baud rate we negotiate with the other end.
 * Most often, you don't have to change that, because Linux-IrDA will
 * use the maximum offered by the link layer, which usually works fine.
 * In some very rare cases, you may want to limit it to lower speeds...
 */
int sysctl_max_baud_rate = 16000000;
/*
 * Maximum value of the lap disconnect timer we negotiate with the other end.
 * Most often, the value below represent the best compromise, but some user
 * may want to keep the LAP alive longer or shorter in case of link failure.
 * Remember that the threshold time (early warning) is fixed to 3s...
 */
int sysctl_max_noreply_time = 12;
/*
 * Minimum turn time to be applied before transmitting to the peer.
 * Nonzero values (usec) are used as lower limit to the per-connection
 * mtt value which was announced by the other end during negotiation.
 * Might be helpful if the peer device provides too short mtt.
 * Default is 10us which means using the unmodified value given by the
 * peer except if it's 0 (0 is likely a bug in the other stack).
 */
unsigned sysctl_min_tx_turn_time = 10;
/*
 * Maximum data size to be used in transmission in payload of LAP frame.
 * There is a bit of confusion in the IrDA spec :
 * The LAP spec defines the payload of a LAP frame (I field) to be
 * 2048 bytes max (IrLAP 1.1, chapt 6.6.5, p40).
 * On the other hand, the PHY mention frames of 2048 bytes max (IrPHY
 * 1.2, chapt 5.3.2.1, p41). But, this number includes the LAP header
 * (2 bytes), and CRC (32 bits at 4 Mb/s). So, for the I field (LAP
 * payload), that's only 2042 bytes. Oups !
 * My nsc-ircc hardware has troubles receiving 2048 bytes frames at 4 Mb/s,
 * so adjust to 2042... I don't know if this bug applies only for 2048
 * bytes frames or all negotiated frame sizes, but you can use the sysctl
 * to play with this value anyway.
 * Jean II */
unsigned sysctl_max_tx_data_size = 2042;
/*
 * Maximum transmit window, i.e. number of LAP frames between turn-around.
 * This allow to override what the peer told us. Some peers are buggy and
 * don't always support what they tell us.
 * Jean II */
unsigned sysctl_max_tx_window = 7;

static int irlap_param_baud_rate(void *instance, irda_param_t *param, int get);
static int irlap_param_link_disconnect(void *instance, irda_param_t *parm,
				       int get);
static int irlap_param_max_turn_time(void *instance, irda_param_t *param,
				     int get);
static int irlap_param_data_size(void *instance, irda_param_t *param, int get);
static int irlap_param_window_size(void *instance, irda_param_t *param,
				   int get);
static int irlap_param_additional_bofs(void *instance, irda_param_t *parm,
				       int get);
static int irlap_param_min_turn_time(void *instance, irda_param_t *param,
				     int get);

#ifndef CONFIG_IRDA_DYNAMIC_WINDOW
static __u32 irlap_requested_line_capacity(struct qos_info *qos);
#endif

static __u32 min_turn_times[]  = { 10000, 5000, 1000, 500, 100, 50, 10, 0 }; /* us */
static __u32 baud_rates[]      = { 2400, 9600, 19200, 38400, 57600, 115200, 576000,
				   1152000, 4000000, 16000000 };           /* bps */
static __u32 data_sizes[]      = { 64, 128, 256, 512, 1024, 2048 };        /* bytes */
static __u32 add_bofs[]        = { 48, 24, 12, 5, 3, 2, 1, 0 };            /* bytes */
static __u32 max_turn_times[]  = { 500, 250, 100, 50 };                    /* ms */
static __u32 link_disc_times[] = { 3, 8, 12, 16, 20, 25, 30, 40 };         /* secs */

static __u32 max_line_capacities[10][4] = {
       /* 500 ms     250 ms  100 ms  50 ms (max turn time) */
	{    100,      0,      0,     0 }, /*     2400 bps */
	{    400,      0,      0,     0 }, /*     9600 bps */
	{    800,      0,      0,     0 }, /*    19200 bps */
	{   1600,      0,      0,     0 }, /*    38400 bps */
	{   2360,      0,      0,     0 }, /*    57600 bps */
	{   4800,   2400,    960,   480 }, /*   115200 bps */
	{  28800,  11520,   5760,  2880 }, /*   576000 bps */
	{  57600,  28800,  11520,  5760 }, /*  1152000 bps */
	{ 200000, 100000,  40000, 20000 }, /*  4000000 bps */
	{ 800000, 400000, 160000, 80000 }, /* 16000000 bps */
};

static pi_minor_info_t pi_minor_call_table_type_0[] = {
	{ NULL, 0 },
/* 01 */{ irlap_param_baud_rate,       PV_INTEGER | PV_LITTLE_ENDIAN },
	{ NULL, 0 },
	{ NULL, 0 },
	{ NULL, 0 },
	{ NULL, 0 },
	{ NULL, 0 },
	{ NULL, 0 },
/* 08 */{ irlap_param_link_disconnect, PV_INT_8_BITS }
};

static pi_minor_info_t pi_minor_call_table_type_1[] = {
	{ NULL, 0 },
	{ NULL, 0 },
/* 82 */{ irlap_param_max_turn_time,   PV_INT_8_BITS },
/* 83 */{ irlap_param_data_size,       PV_INT_8_BITS },
/* 84 */{ irlap_param_window_size,     PV_INT_8_BITS },
/* 85 */{ irlap_param_additional_bofs, PV_INT_8_BITS },
/* 86 */{ irlap_param_min_turn_time,   PV_INT_8_BITS },
};

static pi_major_info_t pi_major_call_table[] = {
	{ pi_minor_call_table_type_0, 9 },
	{ pi_minor_call_table_type_1, 7 },
};

static pi_param_info_t irlap_param_info = { pi_major_call_table, 2, 0x7f, 7 };

/* ---------------------- LOCAL SUBROUTINES ---------------------- */
/* Note : we start with a bunch of local subroutines.
 * As the compiler is "one pass", this is the only way to get them to
 * inline properly...
 * Jean II
 */
/*
 * Function value_index (value, array, size)
 *
 *    Returns the index to the value in the specified array
 */
static inline int value_index(__u32 value, __u32 *array, int size)
{
	int i;

	for (i=0; i < size; i++)
		if (array[i] == value)
			break;
	return i;
}

/*
 * Function index_value (index, array)
 *
 *    Returns value to index in array, easy!
 *
 */
static inline __u32 index_value(int index, __u32 *array)
{
	return array[index];
}

/*
 * Function msb_index (word)
 *
 *    Returns index to most significant bit (MSB) in word
 *
 */
static int msb_index (__u16 word)
{
	__u16 msb = 0x8000;
	int index = 15;   /* Current MSB */

	/* Check for buggy peers.
	 * Note : there is a small probability that it could be us, but I
	 * would expect driver authors to catch that pretty early and be
	 * able to check precisely what's going on. If a end user sees this,
	 * it's very likely the peer. - Jean II */
	if (word == 0) {
		IRDA_WARNING("%s(), Detected buggy peer, adjust null PV to 0x1!\n",
			 __func__);
		/* The only safe choice (we don't know the array size) */
		word = 0x1;
	}

	while (msb) {
		if (word & msb)
			break;   /* Found it! */
		msb >>=1;
		index--;
	}
	return index;
}

/*
 * Function value_lower_bits (value, array)
 *
 *    Returns a bit field marking all possibility lower than value.
 */
static inline int value_lower_bits(__u32 value, __u32 *array, int size, __u16 *field)
{
	int	i;
	__u16	mask = 0x1;
	__u16	result = 0x0;

	for (i=0; i < size; i++) {
		/* Add the current value to the bit field, shift mask */
		result |= mask;
		mask <<= 1;
		/* Finished ? */
		if (array[i] >= value)
			break;
	}
	/* Send back a valid index */
	if(i >= size)
	  i = size - 1;	/* Last item */
	*field = result;
	return i;
}

/*
 * Function value_highest_bit (value, array)
 *
 *    Returns a bit field marking the highest possibility lower than value.
 */
static inline int value_highest_bit(__u32 value, __u32 *array, int size, __u16 *field)
{
	int	i;
	__u16	mask = 0x1;
	__u16	result = 0x0;

	for (i=0; i < size; i++) {
		/* Finished ? */
		if (array[i] <= value)
			break;
		/* Shift mask */
		mask <<= 1;
	}
	/* Set the current value to the bit field */
	result |= mask;
	/* Send back a valid index */
	if(i >= size)
	  i = size - 1;	/* Last item */
	*field = result;
	return i;
}

/* -------------------------- MAIN CALLS -------------------------- */

/*
 * Function irda_qos_compute_intersection (qos, new)
 *
 *    Compute the intersection of the old QoS capabilities with new ones
 *
 */
void irda_qos_compute_intersection(struct qos_info *qos, struct qos_info *new)
{
	IRDA_ASSERT(qos != NULL, return;);
	IRDA_ASSERT(new != NULL, return;);

	/* Apply */
	qos->baud_rate.bits       &= new->baud_rate.bits;
	qos->window_size.bits     &= new->window_size.bits;
	qos->min_turn_time.bits   &= new->min_turn_time.bits;
	qos->max_turn_time.bits   &= new->max_turn_time.bits;
	qos->data_size.bits       &= new->data_size.bits;
	qos->link_disc_time.bits  &= new->link_disc_time.bits;
	qos->additional_bofs.bits &= new->additional_bofs.bits;

	irda_qos_bits_to_value(qos);
}

/*
 * Function irda_init_max_qos_capabilies (qos)
 *
 *    The purpose of this function is for layers and drivers to be able to
 *    set the maximum QoS possible and then "and in" their own limitations
 *
 */
void irda_init_max_qos_capabilies(struct qos_info *qos)
{
	int i;
	/*
	 *  These are the maximum supported values as specified on pages
	 *  39-43 in IrLAP
	 */

	/* Use sysctl to set some configurable values... */
	/* Set configured max speed */
	i = value_lower_bits(sysctl_max_baud_rate, baud_rates, 10,
			     &qos->baud_rate.bits);
	sysctl_max_baud_rate = index_value(i, baud_rates);

	/* Set configured max disc time */
	i = value_lower_bits(sysctl_max_noreply_time, link_disc_times, 8,
			     &qos->link_disc_time.bits);
	sysctl_max_noreply_time = index_value(i, link_disc_times);

	/* LSB is first byte, MSB is second byte */
	qos->baud_rate.bits    &= 0x03ff;

	qos->window_size.bits     = 0x7f;
	qos->min_turn_time.bits   = 0xff;
	qos->max_turn_time.bits   = 0x0f;
	qos->data_size.bits       = 0x3f;
	qos->link_disc_time.bits &= 0xff;
	qos->additional_bofs.bits = 0xff;
}
EXPORT_SYMBOL(irda_init_max_qos_capabilies);

/*
 * Function irlap_adjust_qos_settings (qos)
 *
 *     Adjust QoS settings in case some values are not possible to use because
 *     of other settings
 */
static void irlap_adjust_qos_settings(struct qos_info *qos)
{
	__u32 line_capacity;
	int index;

	IRDA_DEBUG(2, "%s()\n", __func__);

	/*
	 * Make sure the mintt is sensible.
	 * Main culprit : Ericsson T39. - Jean II
	 */
	if (sysctl_min_tx_turn_time > qos->min_turn_time.value) {
		int i;

		IRDA_WARNING("%s(), Detected buggy peer, adjust mtt to %dus!\n",
			 __func__, sysctl_min_tx_turn_time);

		/* We don't really need bits, but easier this way */
		i = value_highest_bit(sysctl_min_tx_turn_time, min_turn_times,
				      8, &qos->min_turn_time.bits);
		sysctl_min_tx_turn_time = index_value(i, min_turn_times);
		qos->min_turn_time.value = sysctl_min_tx_turn_time;
	}

	/*
	 * Not allowed to use a max turn time less than 500 ms if the baudrate
	 * is less than 115200
	 */
	if ((qos->baud_rate.value < 115200) &&
	    (qos->max_turn_time.value < 500))
	{
		IRDA_DEBUG(0,
			   "%s(), adjusting max turn time from %d to 500 ms\n",
			   __func__, qos->max_turn_time.value);
		qos->max_turn_time.value = 500;
	}

	/*
	 * The data size must be adjusted according to the baud rate and max
	 * turn time
	 */
	index = value_index(qos->data_size.value, data_sizes, 6);
	line_capacity = irlap_max_line_capacity(qos->baud_rate.value,
						qos->max_turn_time.value);

#ifdef CONFIG_IRDA_DYNAMIC_WINDOW
	while ((qos->data_size.value > line_capacity) && (index > 0)) {
		qos->data_size.value = data_sizes[index--];
		IRDA_DEBUG(2, "%s(), reducing data size to %d\n",
			   __func__, qos->data_size.value);
	}
#else /* Use method described in section 6.6.11 of IrLAP */
	while (irlap_requested_line_capacity(qos) > line_capacity) {
		IRDA_ASSERT(index != 0, return;);

		/* Must be able to send at least one frame */
		if (qos->window_size.value > 1) {
			qos->window_size.value--;
			IRDA_DEBUG(2, "%s(), reducing window size to %d\n",
				   __func__, qos->window_size.value);
		} else if (index > 1) {
			qos->data_size.value = data_sizes[index--];
			IRDA_DEBUG(2, "%s(), reducing data size to %d\n",
				   __func__, qos->data_size.value);
		} else {
			IRDA_WARNING("%s(), nothing more we can do!\n",
				     __func__);
		}
	}
#endif /* CONFIG_IRDA_DYNAMIC_WINDOW */
	/*
	 * Fix tx data size according to user limits - Jean II
	 */
	if (qos->data_size.value > sysctl_max_tx_data_size)
		/* Allow non discrete adjustement to avoid losing capacity */
		qos->data_size.value = sysctl_max_tx_data_size;
	/*
	 * Override Tx window if user request it. - Jean II
	 */
	if (qos->window_size.value > sysctl_max_tx_window)
		qos->window_size.value = sysctl_max_tx_window;
}

/*
 * Function irlap_negotiate (qos_device, qos_session, skb)
 *
 *    Negotiate QoS values, not really that much negotiation :-)
 *    We just set the QoS capabilities for the peer station
 *
 */
int irlap_qos_negotiate(struct irlap_cb *self, struct sk_buff *skb)
{
	int ret;

	ret = irda_param_extract_all(self, skb->data, skb->len,
				     &irlap_param_info);

	/* Convert the negotiated bits to values */
	irda_qos_bits_to_value(&self->qos_tx);
	irda_qos_bits_to_value(&self->qos_rx);

	irlap_adjust_qos_settings(&self->qos_tx);

	IRDA_DEBUG(2, "Setting BAUD_RATE to %d bps.\n",
		   self->qos_tx.baud_rate.value);
	IRDA_DEBUG(2, "Setting DATA_SIZE to %d bytes\n",
		   self->qos_tx.data_size.value);
	IRDA_DEBUG(2, "Setting WINDOW_SIZE to %d\n",
		   self->qos_tx.window_size.value);
	IRDA_DEBUG(2, "Setting XBOFS to %d\n",
		   self->qos_tx.additional_bofs.value);
	IRDA_DEBUG(2, "Setting MAX_TURN_TIME to %d ms.\n",
		   self->qos_tx.max_turn_time.value);
	IRDA_DEBUG(2, "Setting MIN_TURN_TIME to %d usecs.\n",
		   self->qos_tx.min_turn_time.value);
	IRDA_DEBUG(2, "Setting LINK_DISC to %d secs.\n",
		   self->qos_tx.link_disc_time.value);
	return ret;
}

/*
 * Function irlap_insert_negotiation_params (qos, fp)
 *
 *    Insert QoS negotiaion pararameters into frame
 *
 */
int irlap_insert_qos_negotiation_params(struct irlap_cb *self,
					struct sk_buff *skb)
{
	int ret;

	/* Insert data rate */
	ret = irda_param_insert(self, PI_BAUD_RATE, skb_tail_pointer(skb),
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert max turnaround time */
	ret = irda_param_insert(self, PI_MAX_TURN_TIME, skb_tail_pointer(skb),
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert data size */
	ret = irda_param_insert(self, PI_DATA_SIZE, skb_tail_pointer(skb),
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert window size */
	ret = irda_param_insert(self, PI_WINDOW_SIZE, skb_tail_pointer(skb),
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert additional BOFs */
	ret = irda_param_insert(self, PI_ADD_BOFS, skb_tail_pointer(skb),
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert minimum turnaround time */
	ret = irda_param_insert(self, PI_MIN_TURN_TIME, skb_tail_pointer(skb),
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert link disconnect/threshold time */
	ret = irda_param_insert(self, PI_LINK_DISC, skb_tail_pointer(skb),
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	return 0;
}

/*
 * Function irlap_param_baud_rate (instance, param, get)
 *
 *    Negotiate data-rate
 *
 */
static int irlap_param_baud_rate(void *instance, irda_param_t *param, int get)
{
	__u16 final;

	struct irlap_cb *self = (struct irlap_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	if (get) {
		param->pv.i = self->qos_rx.baud_rate.bits;
		IRDA_DEBUG(2, "%s(), baud rate = 0x%02x\n",
			   __func__, param->pv.i);
	} else {
		/*
		 *  Stations must agree on baud rate, so calculate
		 *  intersection
		 */
		IRDA_DEBUG(2, "Requested BAUD_RATE: 0x%04x\n", (__u16) param->pv.i);
		final = (__u16) param->pv.i & self->qos_rx.baud_rate.bits;

		IRDA_DEBUG(2, "Final BAUD_RATE: 0x%04x\n", final);
		self->qos_tx.baud_rate.bits = final;
		self->qos_rx.baud_rate.bits = final;
	}

	return 0;
}

/*
 * Function irlap_param_link_disconnect (instance, param, get)
 *
 *    Negotiate link disconnect/threshold time.
 *
 */
static int irlap_param_link_disconnect(void *instance, irda_param_t *param,
				       int get)
{
	__u16 final;

	struct irlap_cb *self = (struct irlap_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	if (get)
		param->pv.i = self->qos_rx.link_disc_time.bits;
	else {
		/*
		 *  Stations must agree on link disconnect/threshold
		 *  time.
		 */
		IRDA_DEBUG(2, "LINK_DISC: %02x\n", (__u8) param->pv.i);
		final = (__u8) param->pv.i & self->qos_rx.link_disc_time.bits;

		IRDA_DEBUG(2, "Final LINK_DISC: %02x\n", final);
		self->qos_tx.link_disc_time.bits = final;
		self->qos_rx.link_disc_time.bits = final;
	}
	return 0;
}

/*
 * Function irlap_param_max_turn_time (instance, param, get)
 *
 *    Negotiate the maximum turnaround time. This is a type 1 parameter and
 *    will be negotiated independently for each station
 *
 */
static int irlap_param_max_turn_time(void *instance, irda_param_t *param,
				     int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	if (get)
		param->pv.i = self->qos_rx.max_turn_time.bits;
	else
		self->qos_tx.max_turn_time.bits = (__u8) param->pv.i;

	return 0;
}

/*
 * Function irlap_param_data_size (instance, param, get)
 *
 *    Negotiate the data size. This is a type 1 parameter and
 *    will be negotiated independently for each station
 *
 */
static int irlap_param_data_size(void *instance, irda_param_t *param, int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	if (get)
		param->pv.i = self->qos_rx.data_size.bits;
	else
		self->qos_tx.data_size.bits = (__u8) param->pv.i;

	return 0;
}

/*
 * Function irlap_param_window_size (instance, param, get)
 *
 *    Negotiate the window size. This is a type 1 parameter and
 *    will be negotiated independently for each station
 *
 */
static int irlap_param_window_size(void *instance, irda_param_t *param,
				   int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	if (get)
		param->pv.i = self->qos_rx.window_size.bits;
	else
		self->qos_tx.window_size.bits = (__u8) param->pv.i;

	return 0;
}

/*
 * Function irlap_param_additional_bofs (instance, param, get)
 *
 *    Negotiate additional BOF characters. This is a type 1 parameter and
 *    will be negotiated independently for each station.
 */
static int irlap_param_additional_bofs(void *instance, irda_param_t *param, int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	if (get)
		param->pv.i = self->qos_rx.additional_bofs.bits;
	else
		self->qos_tx.additional_bofs.bits = (__u8) param->pv.i;

	return 0;
}

/*
 * Function irlap_param_min_turn_time (instance, param, get)
 *
 *    Negotiate the minimum turn around time. This is a type 1 parameter and
 *    will be negotiated independently for each station
 */
static int irlap_param_min_turn_time(void *instance, irda_param_t *param,
				     int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == LAP_MAGIC, return -1;);

	if (get)
		param->pv.i = self->qos_rx.min_turn_time.bits;
	else
		self->qos_tx.min_turn_time.bits = (__u8) param->pv.i;

	return 0;
}

/*
 * Function irlap_max_line_capacity (speed, max_turn_time, min_turn_time)
 *
 *    Calculate the maximum line capacity
 *
 */
__u32 irlap_max_line_capacity(__u32 speed, __u32 max_turn_time)
{
	__u32 line_capacity;
	int i,j;

	IRDA_DEBUG(2, "%s(), speed=%d, max_turn_time=%d\n",
		   __func__, speed, max_turn_time);

	i = value_index(speed, baud_rates, 10);
	j = value_index(max_turn_time, max_turn_times, 4);

	IRDA_ASSERT(((i >=0) && (i <10)), return 0;);
	IRDA_ASSERT(((j >=0) && (j <4)), return 0;);

	line_capacity = max_line_capacities[i][j];

	IRDA_DEBUG(2, "%s(), line capacity=%d bytes\n",
		   __func__, line_capacity);

	return line_capacity;
}

#ifndef CONFIG_IRDA_DYNAMIC_WINDOW
static __u32 irlap_requested_line_capacity(struct qos_info *qos)
{
	__u32 line_capacity;

	line_capacity = qos->window_size.value *
		(qos->data_size.value + 6 + qos->additional_bofs.value) +
		irlap_min_turn_time_in_bytes(qos->baud_rate.value,
					     qos->min_turn_time.value);

	IRDA_DEBUG(2, "%s(), requested line capacity=%d\n",
		   __func__, line_capacity);

	return line_capacity;
}
#endif

void irda_qos_bits_to_value(struct qos_info *qos)
{
	int index;

	IRDA_ASSERT(qos != NULL, return;);

	index = msb_index(qos->baud_rate.bits);
	qos->baud_rate.value = baud_rates[index];

	index = msb_index(qos->data_size.bits);
	qos->data_size.value = data_sizes[index];

	index = msb_index(qos->window_size.bits);
	qos->window_size.value = index+1;

	index = msb_index(qos->min_turn_time.bits);
	qos->min_turn_time.value = min_turn_times[index];

	index = msb_index(qos->max_turn_time.bits);
	qos->max_turn_time.value = max_turn_times[index];

	index = msb_index(qos->link_disc_time.bits);
	qos->link_disc_time.value = link_disc_times[index];

	index = msb_index(qos->additional_bofs.bits);
	qos->additional_bofs.value = add_bofs[index];
}
EXPORT_SYMBOL(irda_qos_bits_to_value);
