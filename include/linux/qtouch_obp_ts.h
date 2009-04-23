/*
 * include/linux/qtouch_obp_ts.h - platform/protocol data for Quantum touch IC
 *
 * Copyright (C) 2009 Google, Inc.
  * Copyright (C) 2009 Motorola, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Derived from the Motorola OBP touch driver.
 *
 */

#ifndef _LINUX_QTOUCH_OBP_TS_H
#define _LINUX_QTOUCH_OBP_TS_H

#define QTOUCH_TS_NAME "qtouch-obp-ts"

#define QTM_OBP_ID_INFO_ADDR		0

enum {
	QTM_OBJ_RESERVED0		= 0,
	QTM_OBJ_RESERVED1		= 1,
	QTM_OBJ_DBG_DELTAS		= 2,
	QTM_OBJ_DBG_REFS		= 3,
	QTM_OBJ_DBG_SIGS		= 4,
	QTM_OBJ_GEN_MSG_PROC		= 5,
	QTM_OBJ_GEN_CMD_PROC		= 6,
	QTM_OBJ_GEN_PWR_CONF		= 7,
	QTM_OBJ_GEN_ACQUIRE_CONF	= 8,
	QTM_OBJ_TOUCH_MULTI		= 9,
	QTM_OBJ_TOUCH_SINGLE		= 10,
	QTM_OBJ_TOUCH_XSLIDER		= 11,
	QTM_OBJ_TOUCH_SLIDER		= 12,
	QTM_OBJ_TOUCH_XWHEEL		= 13,
	QTM_OBJ_TOUCH_YWHEEL		= 14,
	QTM_OBJ_TOUCH_KEYARRAY		= 15,
	QTM_OBJ_PROCG_SIG_FILTER	= 16,
	QTM_OBJ_PROCI_LINEAR_TBL	= 17,
	QTM_OBJ_PROCI_GESTURES_PROC	= 18,
	QTM_OBJ_PROCI_GRIPFACESUPPRESSION = 20,
	QTM_OBJ_NOISESUPPRESSION_1 = 36,

	/* Max number of objects currently defined */
	QTM_OBP_MAX_OBJECT_NUM = QTM_OBJ_NOISESUPPRESSION_1 + 1,
};

/* OBP structures as defined by the wire protocol. */

/* Note: Not all the structures below need an explicit packed attribute since
 * many of them just contain uint8_t's. However, the protocol is defined in
 * such a way that the structures may expand in the future, with
 * potential multi-byte fields. Thus, we will mark them all as packed to
 * minimize silly bugs in the future.
 */

/* part of the info block */
struct qtm_id_info {
	uint8_t			family_id;
	uint8_t			variant_id;
	uint8_t			version;
	uint8_t			build;
	uint8_t			matrix_x_size;
	uint8_t			matrix_y_size;
	uint8_t			num_objs;
} __attribute__ ((packed));

/* an entry in the ote table */
struct qtm_obj_entry {
	uint8_t			type;
	uint16_t		addr;
	uint8_t			size;
	uint8_t			num_inst;
	uint8_t			num_rids;
} __attribute__ ((packed));


/*******************************/
/*********** messages **********/
/*******************************/

/* generic message received from the message_processor object. size/buffer
 * defined at runtime after reading the info block */
struct qtm_obj_message {
	uint8_t			report_id;
	uint8_t			msg[0];
} __attribute__ ((packed));

/* status message sent by the command processor - T6 */
#define QTM_CMD_PROC_STATUS_RESET	(1 << 7)
#define QTM_CMD_PROC_STATUS_OFL		(1 << 6)
#define QTM_CMD_PROC_STATUS_SIGERR	(1 << 5)
#define QTM_CMD_PROC_STATUS_CAL		(1 << 4)
#define QTM_CMD_PROC_STATUS_CFGERR	(1 << 3)
struct qtm_cmd_proc_msg {
	uint8_t			report_id;
	uint8_t			status;
	uint16_t		checksum;
} __attribute__ ((packed));

/* status message sent by the mutlitouch touch object - T9*/
#define QTM_TOUCH_MULTI_STATUS_TOUCH		(1 << 7)
#define QTM_TOUCH_MULTI_STATUS_PRESS		(1 << 6)
#define QTM_TOUCH_MULTI_STATUS_RELEASE		(1 << 5)
#define QTM_TOUCH_MULTI_STATUS_MOVE		(1 << 4)
#define QTM_TOUCH_MULTI_STATUS_VECTOR		(1 << 3)
#define QTM_TOUCH_MULTI_STATUS_AMPLITUDE	(1 << 2)
struct qtm_touch_multi_msg {
	uint8_t			report_id;
	uint8_t			status;
	uint8_t			xpos_msb;
	uint8_t			ypos_msb;
	uint8_t			xypos_lsb;
	uint8_t			touch_area;
	uint8_t			touch_amp;
	uint8_t			touch_vect;
} __attribute__ ((packed));

/* status message sent by the keyarray touch object - T15 */
#define QTM_TOUCH_KEYARRAY_STATUS_TOUCH		(1 << 7)
struct qtm_touch_keyarray_msg {
	uint8_t			report_id;
	uint8_t			status;
	uint32_t		keystate;
} __attribute__ ((packed));



/*******************************/
/**** configuration objects ****/
/*******************************/

/* GEN_COMMANDPROCESSOR_T6 */
struct qtm_gen_cmd_proc {
	uint8_t			reset;
	uint8_t			backupnv;
	uint8_t			calibrate;
	uint8_t			reportall;
	uint8_t			debugctrl;
} __attribute__ ((packed));

/* GEN_POWERCONFIG_T7 */
struct qtm_gen_power_cfg {
	uint8_t			idle_acq_int;      /* in ms */
	uint8_t			active_acq_int;    /* in ms */
	uint8_t			active_idle_to;    /* in 200ms */
} __attribute__ ((packed));

/* GEN_ACQUIRECONFIG_T8 */
struct qtm_gen_acquire_cfg {
	uint8_t			charge_time;       /* in 250ns */
	uint8_t			atouch_drift;      /* in 200ms */
	uint8_t			touch_drift;       /* in 200ms */
	uint8_t			drift_susp;        /* in 200ms */
	uint8_t			touch_autocal;     /* in 200ms */
	uint8_t			sync;
} __attribute__ ((packed));

/* TOUCH_MULTITOUCHSCREEN_T9 */
struct qtm_touch_multi_cfg {
	uint8_t			ctrl;
	uint8_t			x_origin;
	uint8_t			y_origin;
	uint8_t			x_size;
	uint8_t			y_size;
	uint8_t			aks_cfg;
	uint8_t			burst_len;
	uint8_t			tch_det_thr;
	uint8_t			tch_det_int;
	uint8_t			rsvd1;
	uint8_t			rsvd2;
	uint8_t			mov_hyst_init;
	uint8_t			mov_hyst_next;
	uint8_t			mov_filter;
	uint8_t			num_touch;
	uint8_t			merge_hyst;
	uint8_t			merge_thresh;
	uint8_t         amp_hyst;
	uint16_t		x_res;
	uint16_t		y_res;
	uint8_t			x_low_clip;
	uint8_t			x_high_clip;
	uint8_t			y_low_clip;
	uint8_t			y_high_clip;

} __attribute__ ((packed));

/* TOUCH_KEYARRAY_T15 */
struct qtm_touch_keyarray_cfg {
	uint8_t			ctrl;
	uint8_t			x_origin;
	uint8_t			y_origin;
	uint8_t			x_size;
	uint8_t			y_size;
	uint8_t			aks_cfg;
	uint8_t			burst_len;
	uint8_t			tch_det_thr;
	uint8_t			tch_det_int;
	uint8_t			rsvd1;
	uint8_t			rsvd2;
} __attribute__ ((packed));

/* PROCG_SIGNALFILTER_T16 */
struct qtm_procg_sig_filter_cfg {
	uint8_t			slew;
	uint8_t			median;
	uint8_t			iir;
} __attribute__ ((packed));

/* PROCI_LINEARIZATIONTABLE_T17 */
struct qtm_proci_linear_tbl_cfg {
	uint8_t			ctrl;
	uint16_t		x_offset;
	uint8_t			x_segment[16];
	uint16_t		y_offset;
	uint8_t			y_segment[16];
} __attribute__ ((packed));

/* PROCI_GRIPFACESUPPRESSION_T20 */
struct qtm_proci_grip_suppression_cfg {
	uint8_t			ctrl;
	uint8_t			xlogrip;
	uint8_t			xhigrip;
	uint8_t			ylogrip;
	uint8_t			yhigrip;
	uint8_t			maxtchs;
	uint8_t			reserve0;
	uint8_t			szthr1;
	uint8_t			szthr2;
	uint8_t			shpthr1;
	uint8_t			shpthr2;
} __attribute__ ((packed));

/* QTM_OBJ_NOISESUPPRESSION_1 */
struct qtm_proci_noise1_suppression_cfg {
	uint8_t			ctrl;
	uint8_t			reserved;
	uint8_t			atchthr;
	uint8_t			duty_cycle;
} __attribute__ ((packed));

/*******************************/
/******** platform data ********/
/*******************************/

struct vkey {
	int	code;
	int	min;
	int	max;
};

struct virt_keys {
	struct vkey		*keys;
	int			count;
	int			start;
};

struct qtouch_key {
	uint8_t				channel;
	int				code;
};

struct qtouch_key_array {
	struct qtm_touch_keyarray_cfg	*cfg;
	struct qtouch_key		*keys;
	int				num_keys;
};

#define QTOUCH_FLIP_X		(1 << 0)
#define QTOUCH_FLIP_Y		(1 << 1)
#define QTOUCH_SWAP_XY		(1 << 2)
#define QTOUCH_USE_MULTITOUCH	(1 << 3)
#define QTOUCH_USE_KEYARRAY	(1 << 4)
#define QTOUCH_CFG_BACKUPNV	(1 << 5)
#define QTOUCH_EEPROM_CHECKSUM  (1 << 6)

struct qtouch_ts_platform_data {
	uint32_t		flags;
	unsigned long		irqflags;

	uint32_t		abs_min_x;
	uint32_t		abs_max_x;
	uint32_t		abs_min_y;
	uint32_t		abs_max_y;
	uint32_t		abs_min_p;
	uint32_t		abs_max_p;
	uint32_t		abs_min_w;
	uint32_t		abs_max_w;

	uint32_t		x_delta;
	uint32_t		y_delta;

	uint16_t		nv_checksum;

	uint32_t		fuzz_x;
	uint32_t		fuzz_y;
	uint32_t		fuzz_p;
	uint32_t		fuzz_w;

	int			(*hw_reset)(void);

	struct virt_keys	vkeys;

	/* TODO: allow multiple key arrays */
	struct qtouch_key_array			key_array;

	/* object configuration information from board */
	struct qtm_gen_power_cfg		power_cfg;
	struct qtm_gen_acquire_cfg		acquire_cfg;
	struct qtm_touch_multi_cfg		multi_touch_cfg;
	struct qtm_touch_keyarray_cfg   key_array_cfg;
	struct qtm_procg_sig_filter_cfg		sig_filter_cfg;
	struct qtm_proci_linear_tbl_cfg		linear_tbl_cfg;
	struct qtm_proci_grip_suppression_cfg	grip_suppression_cfg;
	struct qtm_proci_noise1_suppression_cfg noise1_suppression_cfg;
};

#endif /* _LINUX_QTOUCH_OBP_TS_H */

