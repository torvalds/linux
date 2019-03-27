/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD OR GPL-2.0)
 *
 * Copyright (c) 2000 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * the GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef	_SCSI_SES_H_
#define	_SCSI_SES_H_

#include <cam/scsi/scsi_all.h>

/*========================== Field Extraction Macros =========================*/
#define MK_ENUM(S, F, SUFFIX) S ## _ ## F ## SUFFIX

#define GEN_GETTER(LS, US, LF, UF)					    \
static inline int							    \
LS ## _get_ ## LF(struct LS *elem) {					    \
	return ((elem->bytes[MK_ENUM(US,UF,_BYTE)] & MK_ENUM(US,UF,_MASK))  \
	     >> MK_ENUM(US,UF,_SHIFT));					    \
}

#define GEN_SETTER(LS, US, LF, UF)					    \
static inline void							    \
LS ## _set_ ## LF(struct LS *elem, int val) {				    \
	elem->bytes[MK_ENUM(US,UF,_BYTE)] &= ~MK_ENUM(US,UF,_MASK);	    \
	elem->bytes[MK_ENUM(US,UF,_BYTE)] |=				    \
	    (val << MK_ENUM(US,UF,_SHIFT)) & MK_ENUM(US,UF,_MASK);	    \
}

#define GEN_HDR_GETTER(LS, US, LF, UF)					    \
static inline int							    \
LS ## _get_ ## LF(struct LS *page) {					    \
	return ((page->hdr.page_specific_flags & MK_ENUM(US,UF,_MASK))	    \
	     >> MK_ENUM(US,UF,_SHIFT));					    \
}

#define GEN_HDR_SETTER(LS, US, LF, UF)					    \
static inline void							    \
LS ## _set_ ## LF(struct LS *page, int val) {				    \
	page->hdr.page_specific_flags &= ~MK_ENUM(US,UF,_MASK);		    \
	page->hdr.page_specific_flags |=				    \
	    (val << MK_ENUM(US,UF,_SHIFT)) & MK_ENUM(US,UF,_MASK);	    \
}

#define GEN_ACCESSORS(LS, US, LF, UF)					    \
GEN_GETTER(LS, US, LF, UF)						    \
GEN_SETTER(LS, US, LF, UF)

#define GEN_HDR_ACCESSORS(LS, US, LF, UF)				    \
GEN_HDR_GETTER(LS, US, LF, UF)						    \
GEN_HDR_SETTER(LS, US, LF, UF)

/*===============  Common SCSI ENC Diagnostic Page Structures ===============*/
struct ses_page_hdr {
	uint8_t page_code;
	uint8_t page_specific_flags;
	uint8_t length[2];
	uint8_t gen_code[4];
};

static inline size_t
ses_page_length(const struct ses_page_hdr *hdr)
{
	/*
	 * The page length as received only accounts for bytes that
	 * follow the length field, namely starting with the generation
	 * code field.
	 */
	return (scsi_2btoul(hdr->length)
	      + offsetof(struct ses_page_hdr, gen_code));
}

/*============= SCSI ENC Configuration Diagnostic Page Structures ============*/
struct ses_enc_desc {
	uint8_t byte0;
	/*
	 * reserved0	: 1,
	 * rel_id	: 3,	relative enclosure process id
	 * reserved1	: 1,
	 * num_procs	: 3;	number of enclosure procesenc
	 */
	uint8_t	subenc_id;	/* Sub-enclosure Identifier */
	uint8_t	num_types;	/* # of supported types */
	uint8_t	length;		/* Enclosure Descriptor Length */
	uint8_t	logical_id[8];	/* formerly wwn */
	uint8_t	vendor_id[8];
	uint8_t	product_id[16];
	uint8_t	product_rev[4];
	uint8_t vendor_bytes[];
};

static inline uint8_t *
ses_enc_desc_last_byte(struct ses_enc_desc *encdesc)
{
	return (&encdesc->length + encdesc->length);
}

static inline struct ses_enc_desc *
ses_enc_desc_next(struct ses_enc_desc *encdesc)
{
	return ((struct ses_enc_desc *)(ses_enc_desc_last_byte(encdesc) + 1));
}

static inline int
ses_enc_desc_is_complete(struct ses_enc_desc *encdesc, uint8_t *last_buf_byte)
{
	return (&encdesc->length <= last_buf_byte
	     && ses_enc_desc_last_byte(encdesc) <= last_buf_byte);
}

struct ses_elm_type_desc {
	uint8_t	etype_elm_type;	/* type of element */
	uint8_t	etype_maxelt;	/* maximum supported */
	uint8_t	etype_subenc;	/* in sub-enclosure #n */
	uint8_t	etype_txt_len;	/* Type Descriptor Text Length */
};

struct ses_cfg_page {
	struct ses_page_hdr hdr;
	struct ses_enc_desc subencs[];
	/* type descriptors */
	/* type text */
};

static inline int
ses_cfg_page_get_num_subenc(struct ses_cfg_page *page)
{
	return (page->hdr.page_specific_flags + 1);
}


/*================ SCSI SES Control Diagnostic Page Structures ==============*/
struct ses_ctrl_common {
	uint8_t bytes[1];
};

enum ses_ctrl_common_field_data {
	SES_CTRL_COMMON_SELECT_BYTE		= 0,
	SES_CTRL_COMMON_SELECT_MASK		= 0x80,
	SES_CTRL_COMMON_SELECT_SHIFT		= 7,

	SES_CTRL_COMMON_PRDFAIL_BYTE		= 0,
	SES_CTRL_COMMON_PRDFAIL_MASK		= 0x40,
	SES_CTRL_COMMON_PRDFAIL_SHIFT		= 6,

	SES_CTRL_COMMON_DISABLE_BYTE		= 0,
	SES_CTRL_COMMON_DISABLE_MASK		= 0x20,
	SES_CTRL_COMMON_DISABLE_SHIFT		= 5,

	SES_CTRL_COMMON_RST_SWAP_BYTE		= 0,
	SES_CTRL_COMMON_RST_SWAP_MASK		= 0x10,
	SES_CTRL_COMMON_RST_SWAP_SHIFT		= 4
};

#define GEN_SES_CTRL_COMMON_ACCESSORS(LCASE, UCASE) \
    GEN_ACCESSORS(ses_ctrl_common, SES_CTRL_COMMON, LCASE, UCASE)
GEN_SES_CTRL_COMMON_ACCESSORS(select,   SELECT)
GEN_SES_CTRL_COMMON_ACCESSORS(prdfail,  PRDFAIL)
GEN_SES_CTRL_COMMON_ACCESSORS(disable,  DISABLE)
GEN_SES_CTRL_COMMON_ACCESSORS(rst_swap, RST_SWAP)
#undef GEN_SES_CTRL_COMMON_ACCESSORS

/*------------------------ Device Slot Control Element ----------------------*/
struct ses_ctrl_dev_slot {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_dev_slot_field_data {
	SES_CTRL_DEV_SLOT_RQST_ACTIVE_BYTE	= 1,
	SES_CTRL_DEV_SLOT_RQST_ACTIVE_MASK	= 0x80,
	SES_CTRL_DEV_SLOT_RQST_ACTIVE_SHIFT	= 7,

	SES_CTRL_DEV_SLOT_DO_NOT_REMOVE_BYTE	= 1,
	SES_CTRL_DEV_SLOT_DO_NOT_REMOVE_MASK	= 0x40,
	SES_CTRL_DEV_SLOT_DO_NOT_REMOVE_SHIFT	= 6,

	SES_CTRL_DEV_SLOT_RQST_MISSING_BYTE	= 1,
	SES_CTRL_DEV_SLOT_RQST_MISSING_MASK	= 0x10,
	SES_CTRL_DEV_SLOT_RQST_MISSING_SHIFT	= 4,

	SES_CTRL_DEV_SLOT_RQST_INSERT_BYTE	= 1,
	SES_CTRL_DEV_SLOT_RQST_INSERT_MASK	= 0x08,
	SES_CTRL_DEV_SLOT_RQST_INSERT_SHIFT	= 3,

	SES_CTRL_DEV_SLOT_RQST_REMOVE_BYTE	= 1,
	SES_CTRL_DEV_SLOT_RQST_REMOVE_MASK	= 0x04,
	SES_CTRL_DEV_SLOT_RQST_REMOVE_SHIFT	= 2,

	SES_CTRL_DEV_SLOT_RQST_IDENT_BYTE	= 1,
	SES_CTRL_DEV_SLOT_RQST_IDENT_MASK	= 0x02,
	SES_CTRL_DEV_SLOT_RQST_IDENT_SHIFT	= 1,

	SES_CTRL_DEV_SLOT_RQST_FAULT_BYTE	= 2,
	SES_CTRL_DEV_SLOT_RQST_FAULT_MASK	= 0x20,
	SES_CTRL_DEV_SLOT_RQST_FAULT_SHIFT	= 5,

	SES_CTRL_DEV_SLOT_DEVICE_OFF_BYTE	= 2,
	SES_CTRL_DEV_SLOT_DEVICE_OFF_MASK	= 0x10,
	SES_CTRL_DEV_SLOT_DEVICE_OFF_SHIFT	= 4,

	SES_CTRL_DEV_SLOT_ENABLE_BYP_A_BYTE	= 2,
	SES_CTRL_DEV_SLOT_ENABLE_BYP_A_MASK	= 0x08,
	SES_CTRL_DEV_SLOT_ENABLE_BYP_A_SHIFT	= 3,

	SES_CTRL_DEV_SLOT_ENABLE_BYP_B_BYTE	= 2,
	SES_CTRL_DEV_SLOT_ENABLE_BYP_B_MASK	= 0x04,
	SES_CTRL_DEV_SLOT_ENABLE_BYP_B_SHIFT	= 2
};
#define GEN_SES_CTRL_DEV_SLOT_ACCESSORS(LCASE, UCASE) \
    GEN_ACCESSORS(ses_ctrl_dev_slot, SES_CTRL_DEV_SLOT, LCASE, UCASE)

GEN_SES_CTRL_DEV_SLOT_ACCESSORS(rqst_active,   RQST_ACTIVE)
GEN_SES_CTRL_DEV_SLOT_ACCESSORS(do_not_remove, DO_NOT_REMOVE)
GEN_SES_CTRL_DEV_SLOT_ACCESSORS(rqst_missing,  RQST_MISSING)
GEN_SES_CTRL_DEV_SLOT_ACCESSORS(rqst_insert,   RQST_INSERT)
GEN_SES_CTRL_DEV_SLOT_ACCESSORS(rqst_remove,   RQST_REMOVE)
GEN_SES_CTRL_DEV_SLOT_ACCESSORS(rqst_ident,    RQST_IDENT)
GEN_SES_CTRL_DEV_SLOT_ACCESSORS(rqst_fault,    RQST_FAULT)
GEN_SES_CTRL_DEV_SLOT_ACCESSORS(device_off,    DEVICE_OFF)
GEN_SES_CTRL_DEV_SLOT_ACCESSORS(enable_byp_a,  ENABLE_BYP_A)
GEN_SES_CTRL_DEV_SLOT_ACCESSORS(enable_byp_b,  ENABLE_BYP_B)
#undef GEN_SES_CTRL_DEV_SLOT_ACCESSORS

/*--------------------- Array Device Slot Control Element --------------------*/
struct ses_ctrl_array_dev_slot {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_array_dev_slot_field_data {
	SES_CTRL_ARRAY_DEV_SLOT_RQST_OK_BYTE			= 0,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_OK_MASK			= 0x80,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_OK_SHIFT			= 7,

	SES_CTRL_ARRAY_DEV_SLOT_RQST_RSVD_DEVICE_BYTE		= 0,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_RSVD_DEVICE_MASK		= 0x40,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_RSVD_DEVICE_SHIFT		= 6,

	SES_CTRL_ARRAY_DEV_SLOT_RQST_HOT_SPARE_BYTE		= 0,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_HOT_SPARE_MASK		= 0x20,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_HOT_SPARE_SHIFT		= 5,

	SES_CTRL_ARRAY_DEV_SLOT_RQST_CONS_CHECK_BYTE		= 0,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_CONS_CHECK_MASK		= 0x10,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_CONS_CHECK_SHIFT		= 4,

	SES_CTRL_ARRAY_DEV_SLOT_RQST_IN_CRIT_ARRAY_BYTE		= 0,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_IN_CRIT_ARRAY_MASK		= 0x08,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_IN_CRIT_ARRAY_SHIFT	= 3,

	SES_CTRL_ARRAY_DEV_SLOT_RQST_IN_FAILED_ARRAY_BYTE	= 0,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_IN_FAILED_ARRAY_MASK	= 0x04,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_IN_FAILED_ARRAY_SHIFT	= 2,

	SES_CTRL_ARRAY_DEV_SLOT_RQST_REBUILD_REMAP_BYTE		= 0,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_REBUILD_REMAP_MASK		= 0x02,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_REBUILD_REMAP_SHIFT	= 1,

	SES_CTRL_ARRAY_DEV_SLOT_RQST_REBUILD_REMAP_ABORT_BYTE	= 0,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_REBUILD_REMAP_ABORT_MASK	= 0x01,
	SES_CTRL_ARRAY_DEV_SLOT_RQST_REBUILD_REMAP_ABORT_SHIFT	= 0

	/*
	 * The remaining fields are identical to the device
	 * slot element type.  Access them through the device slot
	 * element type and its accessors.
	 */
};
#define GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_array_dev_slot, SES_CTRL_ARRAY_DEV_SLOT,	\
		  LCASE, UCASE)
GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS(rqst_ok,             RQST_OK)
GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS(rqst_rsvd_device,    RQST_RSVD_DEVICE)
GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS(rqst_hot_spare,      RQST_HOT_SPARE)
GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS(rqst_cons_check,     RQST_CONS_CHECK)
GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS(rqst_in_crit_array,  RQST_IN_CRIT_ARRAY)
GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS(rqst_in_failed_array,
				      RQST_IN_FAILED_ARRAY)
GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS(rqst_rebuild_remap,  RQST_REBUILD_REMAP)
GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS(rqst_rebuild_remap_abort,
				      RQST_REBUILD_REMAP_ABORT)
#undef GEN_SES_CTRL_ARRAY_DEV_SLOT_ACCESSORS

/*----------------------- Power Supply Control Element -----------------------*/
struct ses_ctrl_power_supply {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_power_supply_field_data {
	SES_CTRL_POWER_SUPPLY_RQST_IDENT_BYTE	= 0,
	SES_CTRL_POWER_SUPPLY_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_POWER_SUPPLY_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_POWER_SUPPLY_RQST_FAIL_BYTE	= 2,
	SES_CTRL_POWER_SUPPLY_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_POWER_SUPPLY_RQST_FAIL_SHIFT	= 6,

	SES_CTRL_POWER_SUPPLY_RQST_ON_BYTE	= 2,
	SES_CTRL_POWER_SUPPLY_RQST_ON_MASK	= 0x20,
	SES_CTRL_POWER_SUPPLY_RQST_ON_SHIFT	= 5
};

#define GEN_SES_CTRL_POWER_SUPPLY_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_power_supply, SES_CTRL_POWER_SUPPLY, LCASE, UCASE)
GEN_SES_CTRL_POWER_SUPPLY_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_POWER_SUPPLY_ACCESSORS(rqst_fail,  RQST_FAIL)
GEN_SES_CTRL_POWER_SUPPLY_ACCESSORS(rqst_on,    RQST_ON)
#undef GEN_SES_CTRL_POWER_SUPPLY_ACCESSORS

/*-------------------------- Cooling Control Element -------------------------*/
struct ses_ctrl_cooling {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_cooling_field_data {
	SES_CTRL_COOLING_RQST_IDENT_BYTE		= 0,
	SES_CTRL_COOLING_RQST_IDENT_MASK		= 0x80,
	SES_CTRL_COOLING_RQST_IDENT_SHIFT		= 7,

	SES_CTRL_COOLING_RQST_FAIL_BYTE			= 2,
	SES_CTRL_COOLING_RQST_FAIL_MASK			= 0x40,
	SES_CTRL_COOLING_RQST_FAIL_SHIFT		= 6,

	SES_CTRL_COOLING_RQST_ON_BYTE			= 2,
	SES_CTRL_COOLING_RQST_ON_MASK			= 0x20,
	SES_CTRL_COOLING_RQST_ON_SHIFT			= 5,

	SES_CTRL_COOLING_RQSTED_SPEED_CODE_BYTE		= 2,
	SES_CTRL_COOLING_RQSTED_SPEED_CODE_MASK		= 0x07,
	SES_CTRL_COOLING_RQSTED_SPEED_CODE_SHIFT	= 2,
	SES_CTRL_COOLING_RQSTED_SPEED_CODE_UNCHANGED	= 0x00,
	SES_CTRL_COOLING_RQSTED_SPEED_CODE_LOWEST	= 0x01,
	SES_CTRL_COOLING_RQSTED_SPEED_CODE_HIGHEST	= 0x07
};

#define GEN_SES_CTRL_COOLING_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_cooling, SES_CTRL_COOLING, LCASE, UCASE)
GEN_SES_CTRL_COOLING_ACCESSORS(rqst_ident,        RQST_IDENT)
GEN_SES_CTRL_COOLING_ACCESSORS(rqst_fail,         RQST_FAIL)
GEN_SES_CTRL_COOLING_ACCESSORS(rqst_on,           RQST_ON)
GEN_SES_CTRL_COOLING_ACCESSORS(rqsted_speed_code, RQSTED_SPEED_CODE)
#undef GEN_SES_CTRL_COOLING_ACCESSORS

/*-------------------- Temperature Sensor Control Element --------------------*/
struct ses_ctrl_temp_sensor {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_temp_sensor_field_data {
	SES_CTRL_TEMP_SENSOR_RQST_IDENT_BYTE	= 0,
	SES_CTRL_TEMP_SENSOR_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_TEMP_SENSOR_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_TEMP_SENSOR_RQST_FAIL_BYTE	= 0,
	SES_CTRL_TEMP_SENSOR_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_TEMP_SENSOR_RQST_FAIL_SHIFT	= 6
};

#define GEN_SES_CTRL_TEMP_SENSOR_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_temp_sensor, SES_CTRL_TEMP_SENSOR, LCASE, UCASE)
GEN_SES_CTRL_TEMP_SENSOR_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_TEMP_SENSOR_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_TEMP_SENSOR_ACCESSORS

/*------------------------- Door Lock Control Element ------------------------*/
struct ses_ctrl_door_lock {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_door_lock_field_data {
	SES_CTRL_DOOR_LOCK_RQST_IDENT_BYTE	= 0,
	SES_CTRL_DOOR_LOCK_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_DOOR_LOCK_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_DOOR_LOCK_RQST_FAIL_BYTE	= 0,
	SES_CTRL_DOOR_LOCK_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_DOOR_LOCK_RQST_FAIL_SHIFT	= 6,

	SES_CTRL_DOOR_LOCK_UNLOCK_BYTE		= 2,
	SES_CTRL_DOOR_LOCK_UNLOCK_MASK		= 0x01,
	SES_CTRL_DOOR_LOCK_UNLOCK_SHIFT		= 0
};

#define GEN_SES_CTRL_DOOR_LOCK_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_door_lock, SES_CTRL_DOOR_LOCK, LCASE, UCASE)
GEN_SES_CTRL_DOOR_LOCK_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_DOOR_LOCK_ACCESSORS(rqst_fail,  RQST_FAIL)
GEN_SES_CTRL_DOOR_LOCK_ACCESSORS(unlock,     UNLOCK)
#undef GEN_SES_CTRL_DOOR_LOCK_ACCESSORS

/*----------------------- Audible Alarm Control Element ----------------------*/
struct ses_ctrl_audible_alarm {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_audible_alarm_field_data {
	SES_CTRL_AUDIBLE_ALARM_RQST_IDENT_BYTE		= 0,
	SES_CTRL_AUDIBLE_ALARM_RQST_IDENT_MASK		= 0x80,
	SES_CTRL_AUDIBLE_ALARM_RQST_IDENT_SHIFT		= 7,

	SES_CTRL_AUDIBLE_ALARM_RQST_FAIL_BYTE		= 0,
	SES_CTRL_AUDIBLE_ALARM_RQST_FAIL_MASK		= 0x40,
	SES_CTRL_AUDIBLE_ALARM_RQST_FAIL_SHIFT		= 6,

	SES_CTRL_AUDIBLE_ALARM_SET_MUTE_BYTE		= 2,
	SES_CTRL_AUDIBLE_ALARM_SET_MUTE_MASK		= 0x40,
	SES_CTRL_AUDIBLE_ALARM_SET_MUTE_SHIFT		= 6,

	SES_CTRL_AUDIBLE_ALARM_SET_REMIND_BYTE		= 2,
	SES_CTRL_AUDIBLE_ALARM_SET_REMIND_MASK		= 0x10,
	SES_CTRL_AUDIBLE_ALARM_SET_REMIND_SHIFT		= 4,

	SES_CTRL_AUDIBLE_ALARM_TONE_CONTROL_BYTE	= 2,
	SES_CTRL_AUDIBLE_ALARM_TONE_CONTROL_MASK	= 0x0F,
	SES_CTRL_AUDIBLE_ALARM_TONE_CONTROL_SHIFT	= 0,
	SES_CTRL_AUDIBLE_ALARM_TONE_CONTROL_INFO	= 0x08,
	SES_CTRL_AUDIBLE_ALARM_TONE_CONTROL_NON_CRIT	= 0x04,
	SES_CTRL_AUDIBLE_ALARM_TONE_CONTROL_CRIT	= 0x02,
	SES_CTRL_AUDIBLE_ALARM_TONE_CONTROL_UNRECOV	= 0x01
};

#define GEN_SES_CTRL_AUDIBLE_ALARM_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_audible_alarm, SES_CTRL_AUDIBLE_ALARM, LCASE, UCASE)
GEN_SES_CTRL_AUDIBLE_ALARM_ACCESSORS(rqst_ident,   RQST_IDENT)
GEN_SES_CTRL_AUDIBLE_ALARM_ACCESSORS(rqst_fail,    RQST_FAIL)
GEN_SES_CTRL_AUDIBLE_ALARM_ACCESSORS(set_mute,     SET_MUTE)
GEN_SES_CTRL_AUDIBLE_ALARM_ACCESSORS(set_remind,   SET_REMIND)
GEN_SES_CTRL_AUDIBLE_ALARM_ACCESSORS(tone_control, TONE_CONTROL)
#undef GEN_SES_CTRL_AUDIBLE_ALARM_ACCESSORS

/*--------- Enclosure Services Controller Electronics Control Element --------*/
struct ses_ctrl_ecc_electronics {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_ecc_electronics_field_data {
	SES_CTRL_ECC_ELECTRONICS_RQST_IDENT_BYTE	= 0,
	SES_CTRL_ECC_ELECTRONICS_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_ECC_ELECTRONICS_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_ECC_ELECTRONICS_RQST_FAIL_BYTE		= 0,
	SES_CTRL_ECC_ELECTRONICS_RQST_FAIL_MASK		= 0x40,
	SES_CTRL_ECC_ELECTRONICS_RQST_FAIL_SHIFT	= 6,

	SES_CTRL_ECC_ELECTRONICS_SELECT_ELEMENT_BYTE	= 1,
	SES_CTRL_ECC_ELECTRONICS_SELECT_ELEMENT_MASK	= 0x01,
	SES_CTRL_ECC_ELECTRONICS_SELECT_ELEMENT_SHIFT	= 0
};

#define GEN_SES_CTRL_ECC_ELECTRONICS_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_ecc_electronics, SES_CTRL_ECC_ELECTRONICS,	\
		  LCASE, UCASE)
GEN_SES_CTRL_ECC_ELECTRONICS_ACCESSORS(rqst_ident,     RQST_IDENT)
GEN_SES_CTRL_ECC_ELECTRONICS_ACCESSORS(rqst_fail,      RQST_FAIL)
GEN_SES_CTRL_ECC_ELECTRONICS_ACCESSORS(select_element, SELECT_ELEMENT)
#undef GEN_SES_CTRL_ECC_ELECTRONICS_ACCESSORS

/*----------- SCSI Services Controller Electronics Control Element -----------*/
struct ses_ctrl_scc_electronics {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_scc_electronics_field_data {
	SES_CTRL_SCC_ELECTRONICS_RQST_IDENT_BYTE	= 0,
	SES_CTRL_SCC_ELECTRONICS_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_SCC_ELECTRONICS_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_SCC_ELECTRONICS_RQST_FAIL_BYTE		= 0,
	SES_CTRL_SCC_ELECTRONICS_RQST_FAIL_MASK		= 0x40,
	SES_CTRL_SCC_ELECTRONICS_RQST_FAIL_SHIFT	= 6
};

#define GEN_SES_CTRL_SCC_ELECTRONICS_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_scc_electronics, SES_CTRL_SCC_ELECTRONICS,	\
		  LCASE, UCASE)
GEN_SES_CTRL_SCC_ELECTRONICS_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_SCC_ELECTRONICS_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_SCC_ELECTRONICS_ACCESSORS

/*--------------------- Nonvolatile Cache Control Element --------------------*/
struct ses_ctrl_nv_cache {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_nv_cache_field_data {
	SES_CTRL_NV_CACHE_RQST_IDENT_BYTE	= 0,
	SES_CTRL_NV_CACHE_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_NV_CACHE_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_NV_CACHE_RQST_FAIL_BYTE	= 0,
	SES_CTRL_NV_CACHE_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_NV_CACHE_RQST_FAIL_SHIFT	= 6
};

#define GEN_SES_CTRL_NV_CACHE_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_nv_cache, SES_CTRL_NV_CACHE,	LCASE, UCASE)
GEN_SES_CTRL_NV_CACHE_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_NV_CACHE_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_NV_CACHE_ACCESSORS

/*----------------- Invalid Operation Reason Control Element -----------------*/
struct ses_ctrl_invalid_op_reason {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

/* There are no element specific fields currently defined in the spec. */

/*--------------- Uninterruptible Power Supply Control Element ---------------*/
struct ses_ctrl_ups {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_ups_field_data {
	SES_CTRL_UPS_RQST_IDENT_BYTE	= 2,
	SES_CTRL_UPS_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_UPS_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_UPS_RQST_FAIL_BYTE	= 2,
	SES_CTRL_UPS_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_UPS_RQST_FAIL_SHIFT	= 6
};

#define GEN_SES_CTRL_UPS_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_ups, SES_CTRL_UPS, LCASE, UCASE)
GEN_SES_CTRL_UPS_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_UPS_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_UPS_ACCESSORS

/*-------------------------- Display Control Element -------------------------*/
struct ses_ctrl_display {
	struct ses_ctrl_common common;
	uint8_t bytes[1];
	uint8_t display_character[2];
};

enum ses_ctrl_display_field_data {
	SES_CTRL_DISPLAY_RQST_IDENT_BYTE	= 0,
	SES_CTRL_DISPLAY_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_DISPLAY_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_DISPLAY_RQST_FAIL_BYTE		= 0,
	SES_CTRL_DISPLAY_RQST_FAIL_MASK		= 0x40,
	SES_CTRL_DISPLAY_RQST_FAIL_SHIFT	= 6,

	SES_CTRL_DISPLAY_DISPLAY_MODE_BYTE	= 0,
	SES_CTRL_DISPLAY_DISPLAY_MODE_MASK	= 0x03,
	SES_CTRL_DISPLAY_DISPLAY_MODE_SHIFT	= 6,
	SES_CTRL_DISPLAY_DISPLAY_MODE_UNCHANGED = 0x0,
	SES_CTRL_DISPLAY_DISPLAY_MODE_ESP	= 0x1,
	SES_CTRL_DISPLAY_DISPLAY_MODE_DC_FIELD	= 0x2
};

#define GEN_SES_CTRL_DISPLAY_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_display, SES_CTRL_DISPLAY, LCASE, UCASE)
GEN_SES_CTRL_DISPLAY_ACCESSORS(rqst_ident,   RQST_IDENT)
GEN_SES_CTRL_DISPLAY_ACCESSORS(rqst_fail,    RQST_FAIL)
GEN_SES_CTRL_DISPLAY_ACCESSORS(display_mode, DISPLAY_MODE)
#undef GEN_SES_CTRL_DISPLAY_ACCESSORS

/*----------------------- Key Pad Entry Control Element ----------------------*/
struct ses_ctrl_key_pad_entry {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_key_pad_entry_field_data {
	SES_CTRL_KEY_PAD_ENTRY_RQST_IDENT_BYTE	= 0,
	SES_CTRL_KEY_PAD_ENTRY_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_KEY_PAD_ENTRY_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_KEY_PAD_ENTRY_RQST_FAIL_BYTE	= 0,
	SES_CTRL_KEY_PAD_ENTRY_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_KEY_PAD_ENTRY_RQST_FAIL_SHIFT	= 6
};

#define GEN_SES_CTRL_KEY_PAD_ENTRY_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_key_pad_entry, SES_CTRL_KEY_PAD_ENTRY, LCASE, UCASE)
GEN_SES_CTRL_KEY_PAD_ENTRY_ACCESSORS(rqst_ident,   RQST_IDENT)
GEN_SES_CTRL_KEY_PAD_ENTRY_ACCESSORS(rqst_fail,    RQST_FAIL)
#undef GEN_SES_CTRL_KEY_PAD_ENTRY_ACCESSORS

/*------------------------- Enclosure Control Element ------------------------*/
struct ses_ctrl_enclosure {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_enclosure_field_data {
	SES_CTRL_ENCLOSURE_RQST_IDENT_BYTE		= 0,
	SES_CTRL_ENCLOSURE_RQST_IDENT_MASK		= 0x80,
	SES_CTRL_ENCLOSURE_RQST_IDENT_SHIFT		= 7,

	SES_CTRL_ENCLOSURE_POWER_CYCLE_RQST_BYTE	= 1,
	SES_CTRL_ENCLOSURE_POWER_CYCLE_RQST_MASK	= 0xC0,
	SES_CTRL_ENCLOSURE_POWER_CYCLE_RQST_SHIFT	= 6,
	SES_CTRL_ENCLOSURE_POWER_CYCLE_RQST_NONE	= 0x0,
	SES_CTRL_ENCLOSURE_POWER_CYCLE_RQST_AFTER_DELAY	= 0x1,
	SES_CTRL_ENCLOSURE_POWER_CYCLE_RQST_CANCEL	= 0x2,

	SES_CTRL_ENCLOSURE_POWER_CYCLE_DELAY_BYTE	= 1,
	SES_CTRL_ENCLOSURE_POWER_CYCLE_DELAY_MASK	= 0x3F,
	SES_CTRL_ENCLOSURE_POWER_CYCLE_DELAY_SHIFT	= 0,
	SES_CTRL_ENCLOSURE_POWER_CYCLE_DELAY_MAX	= 60,/*minutes*/

	SES_CTRL_ENCLOSURE_POWER_OFF_DURATION_BYTE	= 2,
	SES_CTRL_ENCLOSURE_POWER_OFF_DURATION_MASK	= 0xFC,
	SES_CTRL_ENCLOSURE_POWER_OFF_DURATION_SHIFT	= 2,
	SES_CTRL_ENCLOSURE_POWER_OFF_DURATION_MAX_AUTO	= 60,
	SES_CTRL_ENCLOSURE_POWER_OFF_DURATION_MANUAL	= 63,

	SES_CTRL_ENCLOSURE_RQST_FAIL_BYTE		= 2,
	SES_CTRL_ENCLOSURE_RQST_FAIL_MASK		= 0x02,
	SES_CTRL_ENCLOSURE_RQST_FAIL_SHIFT		= 1,

	SES_CTRL_ENCLOSURE_RQST_WARN_BYTE		= 2,
	SES_CTRL_ENCLOSURE_RQST_WARN_MASK		= 0x01,
	SES_CTRL_ENCLOSURE_RQST_WARN_SHIFT		= 0
};

#define GEN_SES_CTRL_ENCLOSURE_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_enclosure, SES_CTRL_ENCLOSURE, LCASE, UCASE)
GEN_SES_CTRL_ENCLOSURE_ACCESSORS(rqst_ident,         RQST_IDENT)
GEN_SES_CTRL_ENCLOSURE_ACCESSORS(power_cycle_rqst,   POWER_CYCLE_RQST)
GEN_SES_CTRL_ENCLOSURE_ACCESSORS(power_cycle_delay,  POWER_CYCLE_DELAY)
GEN_SES_CTRL_ENCLOSURE_ACCESSORS(power_off_duration, POWER_OFF_DURATION)
GEN_SES_CTRL_ENCLOSURE_ACCESSORS(rqst_fail,          RQST_FAIL)
GEN_SES_CTRL_ENCLOSURE_ACCESSORS(rqst_warn,          RQST_WARN)
#undef GEN_SES_CTRL_ENCLOSURE_ACCESSORS

/*------------------- SCSI Port/Transceiver Control Element ------------------*/
struct ses_ctrl_scsi_port_or_xcvr {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_scsi_port_or_xcvr_field_data {
	SES_CTRL_SCSI_PORT_OR_XCVR_RQST_IDENT_BYTE	= 0,
	SES_CTRL_SCSI_PORT_OR_XCVR_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_SCSI_PORT_OR_XCVR_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_SCSI_PORT_OR_XCVR_RQST_FAIL_BYTE	= 0,
	SES_CTRL_SCSI_PORT_OR_XCVR_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_SCSI_PORT_OR_XCVR_RQST_FAIL_SHIFT	= 6,

	SES_CTRL_SCSI_PORT_OR_XCVR_DISABLE_BYTE		= 2,
	SES_CTRL_SCSI_PORT_OR_XCVR_DISABLE_MASK		= 0x10,
	SES_CTRL_SCSI_PORT_OR_XCVR_DISABLE_SHIFT	= 4
};

#define GEN_SES_CTRL_SCSI_PORT_OR_XCVR_ACCESSORS(LCASE, UCASE)		 \
    GEN_ACCESSORS(ses_ctrl_scsi_port_or_xcvr, SES_CTRL_SCSI_PORT_OR_XCVR,\
		  LCASE, UCASE)
GEN_SES_CTRL_SCSI_PORT_OR_XCVR_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_SCSI_PORT_OR_XCVR_ACCESSORS(disable,    DISABLE)
GEN_SES_CTRL_SCSI_PORT_OR_XCVR_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_SCSI_PORT_OR_XCVR_ACCESSORS

/*------------------------- Language Control Element -------------------------*/
struct ses_ctrl_language {
	struct ses_ctrl_common common;
	uint8_t bytes[1];
	uint8_t language_code[2];
};

enum ses_ctrl_language_field_data {
	SES_CTRL_LANGUAGE_RQST_IDENT_BYTE	= 0,
	SES_CTRL_LANGUAGE_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_LANGUAGE_RQST_IDENT_SHIFT	= 7
};

#define GEN_SES_CTRL_LANGUAGE_ACCESSORS(LCASE, UCASE)		 \
    GEN_ACCESSORS(ses_ctrl_language, SES_CTRL_LANGUAGE, LCASE, UCASE)
GEN_SES_CTRL_LANGUAGE_ACCESSORS(rqst_ident, RQST_IDENT)
#undef GEN_SES_CTRL_LANGUAGE_ACCESSORS

/*-------------------- Communication Port Control Element --------------------*/
struct ses_ctrl_comm_port {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_comm_port_field_data {
	SES_CTRL_COMM_PORT_RQST_IDENT_BYTE	= 0,
	SES_CTRL_COMM_PORT_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_COMM_PORT_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_COMM_PORT_RQST_FAIL_BYTE	= 0,
	SES_CTRL_COMM_PORT_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_COMM_PORT_RQST_FAIL_SHIFT	= 6,

	SES_CTRL_COMM_PORT_DISABLE_BYTE		= 2,
	SES_CTRL_COMM_PORT_DISABLE_MASK		= 0x01,
	SES_CTRL_COMM_PORT_DISABLE_SHIFT	= 0
};

#define GEN_SES_CTRL_COMM_PORT_ACCESSORS(LCASE, UCASE)		 \
    GEN_ACCESSORS(ses_ctrl_comm_port, SES_CTRL_COMM_PORT, LCASE, UCASE)
GEN_SES_CTRL_COMM_PORT_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_COMM_PORT_ACCESSORS(rqst_fail,  RQST_FAIL)
GEN_SES_CTRL_COMM_PORT_ACCESSORS(disable,    DISABLE)
#undef GEN_SES_CTRL_COMM_PORT_ACCESSORS

/*---------------------- Voltage Sensor Control Element ----------------------*/
struct ses_ctrl_voltage_sensor {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_voltage_sensor_field_data {
	SES_CTRL_VOLTAGE_SENSOR_RQST_IDENT_BYTE		= 0,
	SES_CTRL_VOLTAGE_SENSOR_RQST_IDENT_MASK		= 0x80,
	SES_CTRL_VOLTAGE_SENSOR_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_VOLTAGE_SENSOR_RQST_FAIL_BYTE		= 0,
	SES_CTRL_VOLTAGE_SENSOR_RQST_FAIL_MASK		= 0x40,
	SES_CTRL_VOLTAGE_SENSOR_RQST_FAIL_SHIFT		= 6
};

#define GEN_SES_CTRL_VOLTAGE_SENSOR_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_voltage_sensor, SES_CTRL_VOLTAGE_SENSOR,	\
		  LCASE, UCASE)
GEN_SES_CTRL_VOLTAGE_SENSOR_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_VOLTAGE_SENSOR_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_VOLTAGE_SENSOR_ACCESSORS

/*---------------------- Current Sensor Control Element ----------------------*/
struct ses_ctrl_current_sensor {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_current_sensor_field_data {
	SES_CTRL_CURRENT_SENSOR_RQST_IDENT_BYTE		= 0,
	SES_CTRL_CURRENT_SENSOR_RQST_IDENT_MASK		= 0x80,
	SES_CTRL_CURRENT_SENSOR_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_CURRENT_SENSOR_RQST_FAIL_BYTE		= 0,
	SES_CTRL_CURRENT_SENSOR_RQST_FAIL_MASK		= 0x40,
	SES_CTRL_CURRENT_SENSOR_RQST_FAIL_SHIFT		= 6
};

#define GEN_SES_CTRL_CURRENT_SENSOR_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_current_sensor, SES_CTRL_CURRENT_SENSOR,	\
		  LCASE, UCASE)
GEN_SES_CTRL_CURRENT_SENSOR_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_CURRENT_SENSOR_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_CURRENT_SENSOR_ACCESSORS

/*--------------------- SCSI Target Port Control Element ---------------------*/
struct ses_ctrl_target_port {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_scsi_target_port_field_data {
	SES_CTRL_TARGET_PORT_RQST_IDENT_BYTE	= 0,
	SES_CTRL_TARGET_PORT_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_TARGET_PORT_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_TARGET_PORT_RQST_FAIL_BYTE	= 0,
	SES_CTRL_TARGET_PORT_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_TARGET_PORT_RQST_FAIL_SHIFT	= 6,

	SES_CTRL_TARGET_PORT_ENABLE_BYTE	= 2,
	SES_CTRL_TARGET_PORT_ENABLE_MASK	= 0x01,
	SES_CTRL_TARGET_PORT_ENABLE_SHIFT	= 0
};

#define GEN_SES_CTRL_TARGET_PORT_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_target_port, SES_CTRL_TARGET_PORT, LCASE, UCASE)
GEN_SES_CTRL_TARGET_PORT_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_TARGET_PORT_ACCESSORS(rqst_fail,  RQST_FAIL)
GEN_SES_CTRL_TARGET_PORT_ACCESSORS(enable,     ENABLE)
#undef GEN_SES_CTRL_TARGET_PORT_ACCESSORS

/*-------------------- SCSI Initiator Port Control Element -------------------*/
struct ses_ctrl_initiator_port {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_initiator_port_field_data {
	SES_CTRL_INITIATOR_PORT_RQST_IDENT_BYTE		= 0,
	SES_CTRL_INITIATOR_PORT_RQST_IDENT_MASK		= 0x80,
	SES_CTRL_INITIATOR_PORT_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_INITIATOR_PORT_RQST_FAIL_BYTE		= 0,
	SES_CTRL_INITIATOR_PORT_RQST_FAIL_MASK		= 0x40,
	SES_CTRL_INITIATOR_PORT_RQST_FAIL_SHIFT		= 6,

	SES_CTRL_INITIATOR_PORT_ENABLE_BYTE		= 2,
	SES_CTRL_INITIATOR_PORT_ENABLE_MASK		= 0x01,
	SES_CTRL_INITIATOR_PORT_ENABLE_SHIFT		= 0
};

#define GEN_SES_CTRL_INITIATOR_PORT_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_initiator_port, SES_CTRL_INITIATOR_PORT,	\
		  LCASE, UCASE)
GEN_SES_CTRL_INITIATOR_PORT_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_INITIATOR_PORT_ACCESSORS(rqst_fail,  RQST_FAIL)
GEN_SES_CTRL_INITIATOR_PORT_ACCESSORS(enable,     ENABLE)
#undef GEN_SES_CTRL_INITIATOR_PORT_ACCESSORS

/*-------------------- Simple Subenclosure Control Element -------------------*/
struct ses_ctrl_simple_subenc {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_simple_subenc_field_data {
	SES_CTRL_SIMPlE_SUBSES_RQST_IDENT_BYTE	= 0,
	SES_CTRL_SIMPlE_SUBSES_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_SIMPlE_SUBSES_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_SIMPlE_SUBSES_RQST_FAIL_BYTE	= 0,
	SES_CTRL_SIMPlE_SUBSES_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_SIMPlE_SUBSES_RQST_FAIL_SHIFT	= 6
};

#define GEN_SES_CTRL_SIMPlE_SUBSES_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_simple_subenc, SES_CTRL_SIMPlE_SUBSES,	\
		  LCASE, UCASE)
GEN_SES_CTRL_SIMPlE_SUBSES_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_SIMPlE_SUBSES_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_SIMPlE_SUBSES_ACCESSORS

/*----------------------- SAS Expander Control Element -----------------------*/
struct ses_ctrl_sas_expander {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_sas_expander_field_data {
	SES_CTRL_SAS_EXPANDER_RQST_IDENT_BYTE	= 0,
	SES_CTRL_SAS_EXPANDER_RQST_IDENT_MASK	= 0x80,
	SES_CTRL_SAS_EXPANDER_RQST_IDENT_SHIFT	= 7,

	SES_CTRL_SAS_EXPANDER_RQST_FAIL_BYTE	= 0,
	SES_CTRL_SAS_EXPANDER_RQST_FAIL_MASK	= 0x40,
	SES_CTRL_SAS_EXPANDER_RQST_FAIL_SHIFT	= 6
};

#define GEN_SES_CTRL_SAS_EXPANDER_ACCESSORS(LCASE, UCASE)	\
    GEN_ACCESSORS(ses_ctrl_sas_expander, SES_CTRL_SAS_EXPANDER,	LCASE, UCASE)
GEN_SES_CTRL_SAS_EXPANDER_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_SAS_EXPANDER_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_SAS_EXPANDER_ACCESSORS

/*----------------------- SAS Connector Control Element ----------------------*/
struct ses_ctrl_sas_connector {
	struct ses_ctrl_common common;
	uint8_t bytes[3];
};

enum ses_ctrl_sas_connector_field_data {
	SES_CTRL_SAS_CONNECTOR_RQST_IDENT_BYTE		= 0,
	SES_CTRL_SAS_CONNECTOR_RQST_IDENT_MASK		= 0x80,
	SES_CTRL_SAS_CONNECTOR_RQST_IDENT_SHIFT		= 7,

	SES_CTRL_SAS_CONNECTOR_RQST_FAIL_BYTE		= 2,
	SES_CTRL_SAS_CONNECTOR_RQST_FAIL_MASK		= 0x40,
	SES_CTRL_SAS_CONNECTOR_RQST_FAIL_SHIFT		= 6
};

#define GEN_SES_CTRL_SAS_CONNECTOR_ACCESSORS(LCASE, UCASE)		\
    GEN_ACCESSORS(ses_ctrl_sas_connector, SES_CTRL_SAS_CONNECTOR,	\
		  LCASE, UCASE)
GEN_SES_CTRL_SAS_CONNECTOR_ACCESSORS(rqst_ident, RQST_IDENT)
GEN_SES_CTRL_SAS_CONNECTOR_ACCESSORS(rqst_fail,  RQST_FAIL)
#undef GEN_SES_CTRL_SAS_CONNECTOR_ACCESSORS

/*------------------------- Universal Control Element ------------------------*/
union ses_ctrl_element {
	struct ses_ctrl_common            common;
	struct ses_ctrl_dev_slot          dev_slot;
	struct ses_ctrl_array_dev_slot    array_dev_slot;
	struct ses_ctrl_power_supply      power_supply;
	struct ses_ctrl_cooling           cooling;
	struct ses_ctrl_temp_sensor       temp_sensor;
	struct ses_ctrl_door_lock         door_lock;
	struct ses_ctrl_audible_alarm     audible_alarm;
	struct ses_ctrl_ecc_electronics   ecc_electronics;
	struct ses_ctrl_scc_electronics   scc_electronics;
	struct ses_ctrl_nv_cache          nv_cache;
	struct ses_ctrl_invalid_op_reason invalid_op_reason;
	struct ses_ctrl_ups               ups;
	struct ses_ctrl_display           display;
	struct ses_ctrl_key_pad_entry     key_pad_entry;
	struct ses_ctrl_scsi_port_or_xcvr scsi_port_or_xcvr;
	struct ses_ctrl_language          language;
	struct ses_ctrl_comm_port         comm_port;
	struct ses_ctrl_voltage_sensor    voltage_sensor;
	struct ses_ctrl_current_sensor    current_sensor;
	struct ses_ctrl_target_port	  target_port;
	struct ses_ctrl_initiator_port    initiator_port;
	struct ses_ctrl_simple_subenc	  simple_subenc;
	struct ses_ctrl_sas_expander      sas_expander;
	struct ses_ctrl_sas_connector     sas_connector;
};

/*--------------------- SCSI SES Control Diagnostic Page ---------------------*/
struct ses_ctrl_page {
	struct ses_page_hdr hdr;
	union ses_ctrl_element   elements[];
};

enum ses_ctrl_page_field_data {
	SES_CTRL_PAGE_INFO_MASK		= 0x08,
	SES_CTRL_PAGE_INFO_SHIFT	= 3,

	SES_CTRL_PAGE_NON_CRIT_MASK	= 0x04,
	SES_CTRL_PAGE_NON_CRIT_SHIFT	= 2,

	SES_CTRL_PAGE_CRIT_MASK		= 0x02,
	SES_CTRL_PAGE_CRIT_SHIFT	= 1,

	SES_CTRL_PAGE_UNRECOV_MASK	= 0x01,
	SES_CTRL_PAGE_UNRECOV_SHIFT	= 0
};

#define GEN_SES_CTRL_PAGE_ACCESSORS(LCASE, UCASE) \
    GEN_HDR_ACCESSORS(ses_ctrl_page, SES_CTRL_PAGE, LCASE, UCASE)

GEN_SES_CTRL_PAGE_ACCESSORS(info,     INFO)
GEN_SES_CTRL_PAGE_ACCESSORS(non_crit, NON_CRIT)
GEN_SES_CTRL_PAGE_ACCESSORS(crit,     CRIT)
GEN_SES_CTRL_PAGE_ACCESSORS(unrecov,  UNRECOV)
#undef GEN_SES_CTRL_PAGE_ACCESSORS

/*================= SCSI SES Status Diagnostic Page Structures ===============*/
struct ses_status_common {
	uint8_t bytes[1];
};

enum ses_status_common_field_data {
	SES_STATUS_COMMON_PRDFAIL_BYTE			= 0,
	SES_STATUS_COMMON_PRDFAIL_MASK			= 0x40,
	SES_STATUS_COMMON_PRDFAIL_SHIFT			= 6,

	SES_STATUS_COMMON_DISABLED_BYTE			= 0,
	SES_STATUS_COMMON_DISABLED_MASK			= 0x20,
	SES_STATUS_COMMON_DISABLED_SHIFT		= 5,

	SES_STATUS_COMMON_SWAP_BYTE			= 0,
	SES_STATUS_COMMON_SWAP_MASK			= 0x10,
	SES_STATUS_COMMON_SWAP_SHIFT			= 4,

	SES_STATUS_COMMON_ELEMENT_STATUS_CODE_BYTE	= 0,
	SES_STATUS_COMMON_ELEMENT_STATUS_CODE_MASK	= 0x0F,
	SES_STATUS_COMMON_ELEMENT_STATUS_CODE_SHIFT	= 0
};

#define GEN_SES_STATUS_COMMON_ACCESSORS(LCASE, UCASE) \
    GEN_GETTER(ses_status_common, SES_STATUS_COMMON, LCASE, UCASE)

GEN_SES_STATUS_COMMON_ACCESSORS(prdfail,             PRDFAIL)
GEN_SES_STATUS_COMMON_ACCESSORS(disabled,            DISABLED)
GEN_SES_STATUS_COMMON_ACCESSORS(swap,                SWAP)
GEN_SES_STATUS_COMMON_ACCESSORS(element_status_code, ELEMENT_STATUS_CODE)
#undef GEN_SES_STATUS_COMMON_ACCESSORS

/*------------------------- Device Slot Status Element -----------------------*/
struct ses_status_dev_slot {
	struct ses_status_common common;
	uint8_t slot_address;
	uint8_t bytes[2];
};

enum ses_status_dev_slot_field_data {
	SES_STATUS_DEV_SLOT_APP_CLIENT_BYPED_A_BYTE	= 0,
	SES_STATUS_DEV_SLOT_APP_CLIENT_BYPED_A_MASK	= 0x80,
	SES_STATUS_DEV_SLOT_APP_CLIENT_BYPED_A_SHIFT	= 7,

	SES_STATUS_DEV_SLOT_DO_NOT_REMOVE_BYTE		= 0,
	SES_STATUS_DEV_SLOT_DO_NOT_REMOVE_MASK		= 0x40,
	SES_STATUS_DEV_SLOT_DO_NOT_REMOVE_SHIFT		= 6,

	SES_STATUS_DEV_SLOT_ENCLOSURE_BYPED_A_BYTE	= 0,
	SES_STATUS_DEV_SLOT_ENCLOSURE_BYPED_A_MASK	= 0x20,
	SES_STATUS_DEV_SLOT_ENCLOSURE_BYPED_A_SHIFT	= 5,

	SES_STATUS_DEV_SLOT_ENCLOSURE_BYPED_B_BYTE	= 0,
	SES_STATUS_DEV_SLOT_ENCLOSURE_BYPED_B_MASK	= 0x10,
	SES_STATUS_DEV_SLOT_ENCLOSURE_BYPED_B_SHIFT	= 4,

	SES_STATUS_DEV_SLOT_INSERT_READY_BYTE		= 0,
	SES_STATUS_DEV_SLOT_INSERT_READY_MASK		= 0x08,
	SES_STATUS_DEV_SLOT_INSERT_READY_SHIFT		= 3,

	SES_STATUS_DEV_SLOT_REMOVE_BYTE			= 0,
	SES_STATUS_DEV_SLOT_REMOVE_MASK			= 0x04,
	SES_STATUS_DEV_SLOT_REMOVE_SHIFT		= 2,

	SES_STATUS_DEV_SLOT_IDENT_BYTE			= 0,
	SES_STATUS_DEV_SLOT_IDENT_MASK			= 0x02,
	SES_STATUS_DEV_SLOT_IDENT_SHIFT			= 1,

	SES_STATUS_DEV_SLOT_REPORT_BYTE			= 0,
	SES_STATUS_DEV_SLOT_REPORT_MASK			= 0x01,
	SES_STATUS_DEV_SLOT_REPORT_SHIFT		= 0,

	SES_STATUS_DEV_SLOT_APP_CLIENT_BYPED_B_BYTE	= 1,
	SES_STATUS_DEV_SLOT_APP_CLIENT_BYPED_B_MASK	= 0x80,
	SES_STATUS_DEV_SLOT_APP_CLIENT_BYPED_B_SHIFT	= 7,

	SES_STATUS_DEV_SLOT_FAULT_SENSED_BYTE		= 1,
	SES_STATUS_DEV_SLOT_FAULT_SENSED_MASK		= 0x40,
	SES_STATUS_DEV_SLOT_FAULT_SENSED_SHIFT		= 6,

	SES_STATUS_DEV_SLOT_FAULT_REQUESTED_BYTE	= 1,
	SES_STATUS_DEV_SLOT_FAULT_REQUESTED_MASK	= 0x20,
	SES_STATUS_DEV_SLOT_FAULT_REQUESTED_SHIFT	= 5,

	SES_STATUS_DEV_SLOT_DEVICE_OFF_BYTE		= 1,
	SES_STATUS_DEV_SLOT_DEVICE_OFF_MASK		= 0x10,
	SES_STATUS_DEV_SLOT_DEVICE_OFF_SHIFT		= 4,

	SES_STATUS_DEV_SLOT_BYPED_A_BYTE		= 1,
	SES_STATUS_DEV_SLOT_BYPED_A_MASK		= 0x08,
	SES_STATUS_DEV_SLOT_BYPED_A_SHIFT		= 3,

	SES_STATUS_DEV_SLOT_BYPED_B_BYTE		= 1,
	SES_STATUS_DEV_SLOT_BYPED_B_MASK		= 0x04,
	SES_STATUS_DEV_SLOT_BYPED_B_SHIFT		= 2,

	SES_STATUS_DEV_SLOT_DEVICE_BYPED_A_BYTE		= 1,
	SES_STATUS_DEV_SLOT_DEVICE_BYPED_A_MASK		= 0x02,
	SES_STATUS_DEV_SLOT_DEVICE_BYPED_A_SHIFT	= 1,

	SES_STATUS_DEV_SLOT_DEVICE_BYPED_B_BYTE		= 1,
	SES_STATUS_DEV_SLOT_DEVICE_BYPED_B_MASK		= 0x01,
	SES_STATUS_DEV_SLOT_DEVICE_BYPED_B_SHIFT	= 0
};
#define GEN_SES_STATUS_DEV_SLOT_ACCESSORS(LCASE, UCASE) \
    GEN_GETTER(ses_status_dev_slot, SES_STATUS_DEV_SLOT, LCASE, UCASE)

GEN_SES_STATUS_DEV_SLOT_ACCESSORS(app_client_byped_a, APP_CLIENT_BYPED_A)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(do_not_remove,      DO_NOT_REMOVE)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(enclosure_byped_a,  ENCLOSURE_BYPED_A)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(enclosure_byped_b,  ENCLOSURE_BYPED_B)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(insert_ready,       INSERT_READY)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(remove,             REMOVE)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(ident,              IDENT)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(report,             REPORT)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(app_client_byped_b, APP_CLIENT_BYPED_B)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(fault_sensed,       FAULT_SENSED)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(fault_requested,    FAULT_REQUESTED)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(device_off,         DEVICE_OFF)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(byped_a,            BYPED_A)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(byped_b,            BYPED_B)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(device_byped_a,     DEVICE_BYPED_A)
GEN_SES_STATUS_DEV_SLOT_ACCESSORS(device_byped_b,     DEVICE_BYPED_B)
#undef GEN_SES_STATUS_DEV_SLOT_ACCESSORS

/*---------------------- Array Device Slot Status Element --------------------*/
struct ses_status_array_dev_slot {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_array_dev_slot_field_data {
	SES_STATUS_ARRAY_DEV_SLOT_OK_BYTE			= 0,
	SES_STATUS_ARRAY_DEV_SLOT_OK_MASK			= 0x80,
	SES_STATUS_ARRAY_DEV_SLOT_OK_SHIFT			= 7,

	SES_STATUS_ARRAY_DEV_SLOT_RSVD_DEVICE_BYTE		= 0,
	SES_STATUS_ARRAY_DEV_SLOT_RSVD_DEVICE_MASK		= 0x40,
	SES_STATUS_ARRAY_DEV_SLOT_RSVD_DEVICE_SHIFT		= 6,

	SES_STATUS_ARRAY_DEV_SLOT_HOT_SPARE_BYTE		= 0,
	SES_STATUS_ARRAY_DEV_SLOT_HOT_SPARE_MASK		= 0x20,
	SES_STATUS_ARRAY_DEV_SLOT_HOT_SPARE_SHIFT		= 5,

	SES_STATUS_ARRAY_DEV_SLOT_CONS_CHECK_BYTE		= 0,
	SES_STATUS_ARRAY_DEV_SLOT_CONS_CHECK_MASK		= 0x10,
	SES_STATUS_ARRAY_DEV_SLOT_CONS_CHECK_SHIFT		= 4,

	SES_STATUS_ARRAY_DEV_SLOT_IN_CRIT_ARRAY_BYTE		= 0,
	SES_STATUS_ARRAY_DEV_SLOT_IN_CRIT_ARRAY_MASK		= 0x08,
	SES_STATUS_ARRAY_DEV_SLOT_IN_CRIT_ARRAY_SHIFT		= 3,

	SES_STATUS_ARRAY_DEV_SLOT_IN_FAILED_ARRAY_BYTE		= 0,
	SES_STATUS_ARRAY_DEV_SLOT_IN_FAILED_ARRAY_MASK		= 0x04,
	SES_STATUS_ARRAY_DEV_SLOT_IN_FAILED_ARRAY_SHIFT		= 2,

	SES_STATUS_ARRAY_DEV_SLOT_REBUILD_REMAP_BYTE		= 0,
	SES_STATUS_ARRAY_DEV_SLOT_REBUILD_REMAP_MASK		= 0x02,
	SES_STATUS_ARRAY_DEV_SLOT_REBUILD_REMAP_SHIFT		= 1,

	SES_STATUS_ARRAY_DEV_SLOT_REBUILD_REMAP_ABORT_BYTE	= 0,
	SES_STATUS_ARRAY_DEV_SLOT_REBUILD_REMAP_ABORT_MASK	= 0x01,
	SES_STATUS_ARRAY_DEV_SLOT_REBUILD_REMAP_ABORT_SHIFT	= 0

	/*
	 * The remaining fields are identical to the device
	 * slot element type.  Access them through the device slot
	 * element type and its accessors.
	 */
};
#define GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_array_dev_slot, SES_STATUS_ARRAY_DEV_SLOT,	\
	       LCASE, UCASE)
GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS(ok,              OK)
GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS(rsvd_device,     RSVD_DEVICE)
GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS(hot_spare,       HOT_SPARE)
GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS(cons_check,      CONS_CHECK)
GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS(in_crit_array,   IN_CRIT_ARRAY)
GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS(in_failed_array, IN_FAILED_ARRAY)
GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS(rebuild_remap,   REBUILD_REMAP)
GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS(rebuild_remap_abort,
					REBUILD_REMAP_ABORT)
#undef GEN_SES_STATUS_ARRAY_DEV_SLOT_ACCESSORS

/*----------------------- Power Supply Status Element ------------------------*/
struct ses_status_power_supply {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_power_supply_field_data {
	SES_STATUS_POWER_SUPPLY_IDENT_BYTE		= 0,
	SES_STATUS_POWER_SUPPLY_IDENT_MASK		= 0x80,
	SES_STATUS_POWER_SUPPLY_IDENT_SHIFT		= 7,

	SES_STATUS_POWER_SUPPLY_DC_OVER_VOLTAGE_BYTE	= 1,
	SES_STATUS_POWER_SUPPLY_DC_OVER_VOLTAGE_MASK	= 0x08,
	SES_STATUS_POWER_SUPPLY_DC_OVER_VOLTAGE_SHIFT	= 3,

	SES_STATUS_POWER_SUPPLY_DC_UNDER_VOLTAGE_BYTE	= 1,
	SES_STATUS_POWER_SUPPLY_DC_UNDER_VOLTAGE_MASK	= 0x04,
	SES_STATUS_POWER_SUPPLY_DC_UNDER_VOLTAGE_SHIFT	= 2,

	SES_STATUS_POWER_SUPPLY_DC_OVER_CURRENT_BYTE	= 1,
	SES_STATUS_POWER_SUPPLY_DC_OVER_CURRENT_MASK	= 0x02,
	SES_STATUS_POWER_SUPPLY_DC_OVER_CURRENT_SHIFT	= 1,

	SES_STATUS_POWER_SUPPLY_HOT_SWAP_BYTE		= 2,
	SES_STATUS_POWER_SUPPLY_HOT_SWAP_MASK		= 0x80,
	SES_STATUS_POWER_SUPPLY_HOT_SWAP_SHIFT		= 7,

	SES_STATUS_POWER_SUPPLY_FAIL_BYTE		= 2,
	SES_STATUS_POWER_SUPPLY_FAIL_MASK		= 0x40,
	SES_STATUS_POWER_SUPPLY_FAIL_SHIFT		= 6,

	SES_STATUS_POWER_SUPPLY_REQUESTED_ON_BYTE	= 2,
	SES_STATUS_POWER_SUPPLY_REQUESTED_ON_MASK	= 0x20,
	SES_STATUS_POWER_SUPPLY_REQUESTED_ON_SHIFT	= 5,

	SES_STATUS_POWER_SUPPLY_OFF_BYTE		= 2,
	SES_STATUS_POWER_SUPPLY_OFF_MASK		= 0x10,
	SES_STATUS_POWER_SUPPLY_OFF_SHIFT		= 4,

	SES_STATUS_POWER_SUPPLY_OVERTMP_FAIL_BYTE	= 2,
	SES_STATUS_POWER_SUPPLY_OVERTMP_FAIL_MASK	= 0x08,
	SES_STATUS_POWER_SUPPLY_OVERTMP_FAIL_SHIFT	= 3,

	SES_STATUS_POWER_SUPPLY_TEMP_WARN_BYTE		= 2,
	SES_STATUS_POWER_SUPPLY_TEMP_WARN_MASK		= 0x04,
	SES_STATUS_POWER_SUPPLY_TEMP_WARN_SHIFT		= 2,

	SES_STATUS_POWER_SUPPLY_AC_FAIL_BYTE		= 2,
	SES_STATUS_POWER_SUPPLY_AC_FAIL_MASK		= 0x02,
	SES_STATUS_POWER_SUPPLY_AC_FAIL_SHIFT		= 1,

	SES_STATUS_POWER_SUPPLY_DC_FAIL_BYTE		= 2,
	SES_STATUS_POWER_SUPPLY_DC_FAIL_MASK		= 0x01,
	SES_STATUS_POWER_SUPPLY_DC_FAIL_SHIFT		= 0
};

#define GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_power_supply, SES_STATUS_POWER_SUPPLY, LCASE, UCASE)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(ident,            IDENT)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(dc_over_voltage,  DC_OVER_VOLTAGE)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(dc_under_voltage, DC_UNDER_VOLTAGE)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(dc_over_current,  DC_OVER_CURRENT)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(hot_swap,         HOT_SWAP)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(fail,             FAIL)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(requested_on,     REQUESTED_ON)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(off,              OFF)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(overtmp_fail,     OVERTMP_FAIL)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(temp_warn,        TEMP_WARN)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(ac_fail,          AC_FAIL)
GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS(dc_fail,          DC_FAIL)
#undef GEN_SES_STATUS_POWER_SUPPLY_ACCESSORS

/*-------------------------- Cooling Status Element --------------------------*/
struct ses_status_cooling {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_cooling_field_data {
	SES_STATUS_COOLING_IDENT_BYTE			= 0,
	SES_STATUS_COOLING_IDENT_MASK			= 0x80,
	SES_STATUS_COOLING_IDENT_SHIFT			= 7,

	SES_STATUS_COOLING_ACTUAL_FAN_SPEED_MSB_BYTE	= 0,
	SES_STATUS_COOLING_ACTUAL_FAN_SPEED_MSB_MASK	= 0x07,
	SES_STATUS_COOLING_ACTUAL_FAN_SPEED_MSB_SHIFT	= 0,

	SES_STATUS_COOLING_ACTUAL_FAN_SPEED_LSB_BYTE	= 1,
	SES_STATUS_COOLING_ACTUAL_FAN_SPEED_LSB_MASK	= 0xFF,
	SES_STATUS_COOLING_ACTUAL_FAN_SPEED_LSB_SHIFT	= 0,

	SES_STATUS_COOLING_HOT_SWAP_BYTE		= 2,
	SES_STATUS_COOLING_HOT_SWAP_MASK		= 0x40,
	SES_STATUS_COOLING_HOT_SWAP_SHIFT		= 6,

	SES_STATUS_COOLING_FAIL_BYTE			= 2,
	SES_STATUS_COOLING_FAIL_MASK			= 0x40,
	SES_STATUS_COOLING_FAIL_SHIFT			= 6,

	SES_STATUS_COOLING_REQUESTED_ON_BYTE		= 2,
	SES_STATUS_COOLING_REQUESTED_ON_MASK		= 0x20,
	SES_STATUS_COOLING_REQUESTED_ON_SHIFT		= 5,

	SES_STATUS_COOLING_OFF_BYTE			= 2,
	SES_STATUS_COOLING_OFF_MASK			= 0x20,
	SES_STATUS_COOLING_OFF_SHIFT			= 5,

	SES_STATUS_COOLING_ACTUAL_SPEED_CODE_BYTE	= 2,
	SES_STATUS_COOLING_ACTUAL_SPEED_CODE_MASK	= 0x07,
	SES_STATUS_COOLING_ACTUAL_SPEED_CODE_SHIFT	= 2,
	SES_STATUS_COOLING_ACTUAL_SPEED_CODE_STOPPED	= 0x00,
	SES_STATUS_COOLING_ACTUAL_SPEED_CODE_LOWEST	= 0x01,
	SES_STATUS_COOLING_ACTUAL_SPEED_CODE_HIGHEST	= 0x07
};

#define GEN_SES_STATUS_COOLING_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_cooling, SES_STATUS_COOLING, LCASE, UCASE)
GEN_SES_STATUS_COOLING_ACCESSORS(ident,                IDENT)
GEN_SES_STATUS_COOLING_ACCESSORS(actual_fan_speed_msb, ACTUAL_FAN_SPEED_MSB)
GEN_SES_STATUS_COOLING_ACCESSORS(actual_fan_speed_lsb, ACTUAL_FAN_SPEED_LSB)
GEN_SES_STATUS_COOLING_ACCESSORS(hot_swap,             HOT_SWAP)
GEN_SES_STATUS_COOLING_ACCESSORS(fail,                 FAIL)
GEN_SES_STATUS_COOLING_ACCESSORS(requested_on,         REQUESTED_ON)
GEN_SES_STATUS_COOLING_ACCESSORS(off,                  OFF)
GEN_SES_STATUS_COOLING_ACCESSORS(actual_speed_code,    ACTUAL_SPEED_CODE)
#undef GEN_SES_STATUS_COOLING_ACCESSORS

static inline int
ses_status_cooling_get_actual_fan_speed(struct ses_status_cooling *elem)
{
	return (ses_status_cooling_get_actual_fan_speed_msb(elem) << 8
	      | ses_status_cooling_get_actual_fan_speed_lsb(elem));
}

/*-------------------- Temperature Sensor Status Element ---------------------*/
struct ses_status_temp_sensor {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_temp_sensor_field_data {
	SES_STATUS_TEMP_SENSOR_IDENT_BYTE		= 0,
	SES_STATUS_TEMP_SENSOR_IDENT_MASK		= 0x80,
	SES_STATUS_TEMP_SENSOR_IDENT_SHIFT		= 7,

	SES_STATUS_TEMP_SENSOR_FAIL_BYTE		= 0,
	SES_STATUS_TEMP_SENSOR_FAIL_MASK		= 0x40,
	SES_STATUS_TEMP_SENSOR_FAIL_SHIFT		= 6,

	SES_STATUS_TEMP_SENSOR_TEMPERATURE_BYTE		= 1,
	SES_STATUS_TEMP_SENSOR_TEMPERATURE_MASK		= 0xFF,
	SES_STATUS_TEMP_SENSOR_TEMPERATURE_SHIFT	= 0,

	SES_STATUS_TEMP_SENSOR_OT_FAILURE_BYTE		= 2,
	SES_STATUS_TEMP_SENSOR_OT_FAILURE_MASK		= 0x08,
	SES_STATUS_TEMP_SENSOR_OT_FAILURE_SHIFT		= 3,

	SES_STATUS_TEMP_SENSOR_OT_WARNING_BYTE		= 2,
	SES_STATUS_TEMP_SENSOR_OT_WARNING_MASK		= 0x04,
	SES_STATUS_TEMP_SENSOR_OT_WARNING_SHIFT		= 2,

	SES_STATUS_TEMP_SENSOR_UT_FAILURE_BYTE		= 2,
	SES_STATUS_TEMP_SENSOR_UT_FAILURE_MASK		= 0x02,
	SES_STATUS_TEMP_SENSOR_UT_FAILURE_SHIFT		= 1,

	SES_STATUS_TEMP_SENSOR_UT_WARNING_BYTE		= 2,
	SES_STATUS_TEMP_SENSOR_UT_WARNING_MASK		= 0x01,
	SES_STATUS_TEMP_SENSOR_UT_WARNING_SHIFT		= 0
};

#define GEN_SES_STATUS_TEMP_SENSOR_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_temp_sensor, SES_STATUS_TEMP_SENSOR, LCASE, UCASE)
GEN_SES_STATUS_TEMP_SENSOR_ACCESSORS(ident,       IDENT)
GEN_SES_STATUS_TEMP_SENSOR_ACCESSORS(fail,        FAIL)
GEN_SES_STATUS_TEMP_SENSOR_ACCESSORS(temperature, TEMPERATURE)
GEN_SES_STATUS_TEMP_SENSOR_ACCESSORS(ot_failure,  OT_FAILURE)
GEN_SES_STATUS_TEMP_SENSOR_ACCESSORS(ot_warning,  OT_WARNING)
GEN_SES_STATUS_TEMP_SENSOR_ACCESSORS(ut_failure,  UT_FAILURE)
GEN_SES_STATUS_TEMP_SENSOR_ACCESSORS(ut_warning,  UT_WARNING)
#undef GEN_SES_STATUS_TEMP_SENSOR_ACCESSORS

/*------------------------- Door Lock Status Element -------------------------*/
struct ses_status_door_lock {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_door_lock_field_data {
	SES_STATUS_DOOR_LOCK_IDENT_BYTE		= 0,
	SES_STATUS_DOOR_LOCK_IDENT_MASK		= 0x80,
	SES_STATUS_DOOR_LOCK_IDENT_SHIFT	= 7,

	SES_STATUS_DOOR_LOCK_FAIL_BYTE		= 0,
	SES_STATUS_DOOR_LOCK_FAIL_MASK		= 0x40,
	SES_STATUS_DOOR_LOCK_FAIL_SHIFT		= 6,

	SES_STATUS_DOOR_LOCK_UNLOCKED_BYTE	= 2,
	SES_STATUS_DOOR_LOCK_UNLOCKED_MASK	= 0x01,
	SES_STATUS_DOOR_LOCK_UNLOCKED_SHIFT	= 0
};

#define GEN_SES_STATUS_DOOR_LOCK_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_door_lock, SES_STATUS_DOOR_LOCK, LCASE, UCASE)
GEN_SES_STATUS_DOOR_LOCK_ACCESSORS(ident,    IDENT)
GEN_SES_STATUS_DOOR_LOCK_ACCESSORS(fail,     FAIL)
GEN_SES_STATUS_DOOR_LOCK_ACCESSORS(unlocked, UNLOCKED)
#undef GEN_SES_STATUS_DOOR_LOCK_ACCESSORS

/*----------------------- Audible Alarm Status Element -----------------------*/
struct ses_status_audible_alarm {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_audible_alarm_field_data {
	SES_STATUS_AUDIBLE_ALARM_IDENT_BYTE			= 0,
	SES_STATUS_AUDIBLE_ALARM_IDENT_MASK			= 0x80,
	SES_STATUS_AUDIBLE_ALARM_IDENT_SHIFT			= 7,

	SES_STATUS_AUDIBLE_ALARM_FAIL_BYTE			= 0,
	SES_STATUS_AUDIBLE_ALARM_FAIL_MASK			= 0x40,
	SES_STATUS_AUDIBLE_ALARM_FAIL_SHIFT			= 6,

	SES_STATUS_AUDIBLE_ALARM_RQST_MUTE_BYTE			= 2,
	SES_STATUS_AUDIBLE_ALARM_RQST_MUTE_MASK			= 0x80,
	SES_STATUS_AUDIBLE_ALARM_RQST_MUTE_SHIFT		= 7,

	SES_STATUS_AUDIBLE_ALARM_MUTED_BYTE			= 2,
	SES_STATUS_AUDIBLE_ALARM_MUTED_MASK			= 0x40,
	SES_STATUS_AUDIBLE_ALARM_MUTED_SHIFT			= 6,

	SES_STATUS_AUDIBLE_ALARM_REMIND_BYTE			= 2,
	SES_STATUS_AUDIBLE_ALARM_REMIND_MASK			= 0x10,
	SES_STATUS_AUDIBLE_ALARM_REMIND_SHIFT			= 4,

	SES_STATUS_AUDIBLE_ALARM_TONE_INDICATOR_BYTE		= 2,
	SES_STATUS_AUDIBLE_ALARM_TONE_INDICATOR_MASK		= 0x0F,
	SES_STATUS_AUDIBLE_ALARM_TONE_INDICATOR_SHIFT		= 0,
	SES_STATUS_AUDIBLE_ALARM_TONE_INDICATOR_INFO		= 0x08,
	SES_STATUS_AUDIBLE_ALARM_TONE_INDICATOR_NON_CRIT	= 0x04,
	SES_STATUS_AUDIBLE_ALARM_TONE_INDICATOR_CRIT		= 0x02,
	SES_STATUS_AUDIBLE_ALARM_TONE_INDICATOR_UNRECOV		= 0x01
};

#define GEN_SES_STATUS_AUDIBLE_ALARM_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_audible_alarm, SES_STATUS_AUDIBLE_ALARM, LCASE, UCASE)
GEN_SES_STATUS_AUDIBLE_ALARM_ACCESSORS(ident,          IDENT)
GEN_SES_STATUS_AUDIBLE_ALARM_ACCESSORS(fail,           FAIL)
GEN_SES_STATUS_AUDIBLE_ALARM_ACCESSORS(rqst_mute,      RQST_MUTE)
GEN_SES_STATUS_AUDIBLE_ALARM_ACCESSORS(muted,          MUTED)
GEN_SES_STATUS_AUDIBLE_ALARM_ACCESSORS(remind,         REMIND)
GEN_SES_STATUS_AUDIBLE_ALARM_ACCESSORS(tone_indicator, TONE_INDICATOR)
#undef GEN_SES_STATUS_AUDIBLE_ALARM_ACCESSORS

/*---------- Enclosure Services Statusler Electronics Status Element ---------*/
struct ses_status_ecc_electronics {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_ecc_electronics_field_data {
	SES_STATUS_ECC_ELECTRONICS_IDENT_BYTE		= 0,
	SES_STATUS_ECC_ELECTRONICS_IDENT_MASK		= 0x80,
	SES_STATUS_ECC_ELECTRONICS_IDENT_SHIFT		= 7,

	SES_STATUS_ECC_ELECTRONICS_FAIL_BYTE		= 0,
	SES_STATUS_ECC_ELECTRONICS_FAIL_MASK		= 0x40,
	SES_STATUS_ECC_ELECTRONICS_FAIL_SHIFT		= 6,

	SES_STATUS_ECC_ELECTRONICS_REPORT_BYTE		= 1,
	SES_STATUS_ECC_ELECTRONICS_REPORT_MASK		= 0x01,
	SES_STATUS_ECC_ELECTRONICS_REPORT_SHIFT		= 0,

	SES_STATUS_ECC_ELECTRONICS_HOT_SWAP_BYTE	= 2,
	SES_STATUS_ECC_ELECTRONICS_HOT_SWAP_MASK	= 0x80,
	SES_STATUS_ECC_ELECTRONICS_HOT_SWAP_SHIFT	= 7
};

#define GEN_SES_STATUS_ECC_ELECTRONICS_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_ecc_electronics, SES_STATUS_ECC_ELECTRONICS,	\
		  LCASE, UCASE)
GEN_SES_STATUS_ECC_ELECTRONICS_ACCESSORS(ident,     IDENT)
GEN_SES_STATUS_ECC_ELECTRONICS_ACCESSORS(fail,      FAIL)
GEN_SES_STATUS_ECC_ELECTRONICS_ACCESSORS(report,    REPORT)
GEN_SES_STATUS_ECC_ELECTRONICS_ACCESSORS(hot_swap,  HOT_SWAP)
#undef GEN_SES_STATUS_ECC_ELECTRONICS_ACCESSORS

/*------------ SCSI Services Statusler Electronics Status Element ------------*/
struct ses_status_scc_electronics {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_scc_electronics_field_data {
	SES_STATUS_SCC_ELECTRONICS_IDENT_BYTE	= 0,
	SES_STATUS_SCC_ELECTRONICS_IDENT_MASK	= 0x80,
	SES_STATUS_SCC_ELECTRONICS_IDENT_SHIFT	= 7,

	SES_STATUS_SCC_ELECTRONICS_FAIL_BYTE	= 0,
	SES_STATUS_SCC_ELECTRONICS_FAIL_MASK	= 0x40,
	SES_STATUS_SCC_ELECTRONICS_FAIL_SHIFT	= 6,

	SES_STATUS_SCC_ELECTRONICS_REPORT_BYTE	= 1,
	SES_STATUS_SCC_ELECTRONICS_REPORT_MASK	= 0x01,
	SES_STATUS_SCC_ELECTRONICS_REPORT_SHIFT	= 0
};

#define GEN_SES_STATUS_SCC_ELECTRONICS_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_scc_electronics, SES_STATUS_SCC_ELECTRONICS,	\
		  LCASE, UCASE)
GEN_SES_STATUS_SCC_ELECTRONICS_ACCESSORS(ident,     IDENT)
GEN_SES_STATUS_SCC_ELECTRONICS_ACCESSORS(fail,      FAIL)
GEN_SES_STATUS_SCC_ELECTRONICS_ACCESSORS(report,    REPORT)
#undef GEN_SES_STATUS_SCC_ELECTRONICS_ACCESSORS

/*--------------------- Nonvolatile Cache Status Element ---------------------*/
struct ses_status_nv_cache {
	struct ses_status_common common;
	uint8_t bytes[1];
	uint8_t cache_size[2];
};

enum ses_status_nv_cache_field_data {
	SES_STATUS_NV_CACHE_IDENT_BYTE			= 0,
	SES_STATUS_NV_CACHE_IDENT_MASK			= 0x80,
	SES_STATUS_NV_CACHE_IDENT_SHIFT			= 7,

	SES_STATUS_NV_CACHE_FAIL_BYTE			= 0,
	SES_STATUS_NV_CACHE_FAIL_MASK			= 0x40,
	SES_STATUS_NV_CACHE_FAIL_SHIFT			= 6,

	SES_STATUS_NV_CACHE_SIZE_MULTIPLIER_BYTE	= 0,
	SES_STATUS_NV_CACHE_SIZE_MULTIPLIER_MASK	= 0x03,
	SES_STATUS_NV_CACHE_SIZE_MULTIPLIER_SHIFT	= 0,
	SES_STATUS_NV_CACHE_SIZE_MULTIPLIER_BYTES	= 0x0,
	SES_STATUS_NV_CACHE_SIZE_MULTIPLIER_KBYTES	= 0x1,
	SES_STATUS_NV_CACHE_SIZE_MULTIPLIER_MBYTES	= 0x2,
	SES_STATUS_NV_CACHE_SIZE_MULTIPLIER_GBYTES	= 0x3
};

#define GEN_SES_STATUS_NV_CACHE_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_nv_cache, SES_STATUS_NV_CACHE, LCASE, UCASE)
GEN_SES_STATUS_NV_CACHE_ACCESSORS(ident,           IDENT)
GEN_SES_STATUS_NV_CACHE_ACCESSORS(fail,            FAIL)
GEN_SES_STATUS_NV_CACHE_ACCESSORS(size_multiplier, SIZE_MULTIPLIER)
#undef GEN_SES_STATUS_NV_CACHE_ACCESSORS

static inline uintmax_t
ses_status_nv_cache_get_cache_size(struct ses_status_nv_cache *elem)
{
	uintmax_t cache_size;
	int multiplier;

	/* Multiplier is in units of 2^10 */
	cache_size = scsi_2btoul(elem->cache_size);
	multiplier = 10 * ses_status_nv_cache_get_size_multiplier(elem);
	return (cache_size << multiplier);
}

/*----------------- Invalid Operation Reason Status Element ------------------*/
struct ses_status_invalid_op_reason {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_invalid_op_field_data {
	SES_STATUS_INVALID_OP_REASON_TYPE_BYTE				= 0,
	SES_STATUS_INVALID_OP_REASON_TYPE_MASK				= 0xC0,
	SES_STATUS_INVALID_OP_REASON_TYPE_SHIFT				= 6,
	SES_STATUS_INVALID_OP_REASON_TYPE_PC_ERROR			= 0x00,
	SES_STATUS_INVALID_OP_REASON_TYPE_PF_ERROR			= 0x01,
	SES_STATUS_INVALID_OP_REASON_TYPE_VS_ERROR			= 0x03,

	SES_STATUS_INVALID_OP_REASON_PC_ERROR_PC_NOT_SUPPORTED_BYTE	= 0,
	SES_STATUS_INVALID_OP_REASON_PC_ERROR_PC_NOT_SUPPORTED_MASK	= 0x01,
	SES_STATUS_INVALID_OP_REASON_PC_ERROR_PC_NOT_SUPPORTED_SHIFT	= 0,

	SES_STATUS_INVALID_OP_REASON_PF_ERROR_BIT_NUMBER_BYTE		= 0,
	SES_STATUS_INVALID_OP_REASON_PF_ERROR_BIT_NUMBER_MASK		= 0x03,
	SES_STATUS_INVALID_OP_REASON_PF_ERROR_BIT_NUMBER_SHIFT		= 0
};

#define GEN_SES_STATUS_INVALID_OP_REASON_ACCESSORS(LCASE, UCASE)	   \
    GEN_GETTER(ses_status_invalid_op_reason, SES_STATUS_INVALID_OP_REASON, \
	       LCASE, UCASE)
GEN_SES_STATUS_INVALID_OP_REASON_ACCESSORS(type, TYPE)
GEN_SES_STATUS_INVALID_OP_REASON_ACCESSORS(pc_error_pc_not_supported,
					   PC_ERROR_PC_NOT_SUPPORTED)
GEN_SES_STATUS_INVALID_OP_REASON_ACCESSORS(pf_error_bit_number,
					   PF_ERROR_BIT_NUMBER)
#undef GEN_SES_STATUS_INVALID_OP_ACCESSORS

/*--------------- Uninterruptible Power Supply Status Element ----------------*/
struct ses_status_ups {
	struct ses_status_common common;
	/* Minutes of remaining capacity. */
	uint8_t battery_status;
	uint8_t bytes[2];
};

enum ses_status_ups_field_data {
	SES_STATUS_UPS_AC_LO_BYTE	= 0,
	SES_STATUS_UPS_AC_LO_MASK	= 0x80,
	SES_STATUS_UPS_AC_LO_SHIFT	= 7,

	SES_STATUS_UPS_AC_HI_BYTE	= 0,
	SES_STATUS_UPS_AC_HI_MASK	= 0x40,
	SES_STATUS_UPS_AC_HI_SHIFT	= 6,

	SES_STATUS_UPS_AC_QUAL_BYTE	= 0,
	SES_STATUS_UPS_AC_QUAL_MASK	= 0x20,
	SES_STATUS_UPS_AC_QUAL_SHIFT	= 5,

	SES_STATUS_UPS_AC_FAIL_BYTE	= 0,
	SES_STATUS_UPS_AC_FAIL_MASK	= 0x10,
	SES_STATUS_UPS_AC_FAIL_SHIFT	= 4,

	SES_STATUS_UPS_DC_FAIL_BYTE	= 0,
	SES_STATUS_UPS_DC_FAIL_MASK	= 0x08,
	SES_STATUS_UPS_DC_FAIL_SHIFT	= 3,

	SES_STATUS_UPS_UPS_FAIL_BYTE	= 0,
	SES_STATUS_UPS_UPS_FAIL_MASK	= 0x04,
	SES_STATUS_UPS_UPS_FAIL_SHIFT	= 2,

	SES_STATUS_UPS_WARN_BYTE	= 0,
	SES_STATUS_UPS_WARN_MASK	= 0x02,
	SES_STATUS_UPS_WARN_SHIFT	= 1,

	SES_STATUS_UPS_INTF_FAIL_BYTE	= 0,
	SES_STATUS_UPS_INTF_FAIL_MASK	= 0x01,
	SES_STATUS_UPS_INTF_FAIL_SHIFT	= 0,

	SES_STATUS_UPS_IDENT_BYTE	= 0,
	SES_STATUS_UPS_IDENT_MASK	= 0x80,
	SES_STATUS_UPS_IDENT_SHIFT	= 7,

	SES_STATUS_UPS_FAIL_BYTE	= 1,
	SES_STATUS_UPS_FAIL_MASK	= 0x40,
	SES_STATUS_UPS_FAIL_SHIFT	= 6,

	SES_STATUS_UPS_BATT_FAIL_BYTE	= 1,
	SES_STATUS_UPS_BATT_FAIL_MASK	= 0x02,
	SES_STATUS_UPS_BATT_FAIL_SHIFT	= 1,

	SES_STATUS_UPS_BPF_BYTE		= 1,
	SES_STATUS_UPS_BPF_MASK		= 0x01,
	SES_STATUS_UPS_BPF_SHIFT	= 0
};

#define GEN_SES_STATUS_UPS_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_ups, SES_STATUS_UPS, LCASE, UCASE)
GEN_SES_STATUS_UPS_ACCESSORS(ac_lo,           AC_LO)
GEN_SES_STATUS_UPS_ACCESSORS(ac_hi,            AC_HI)
GEN_SES_STATUS_UPS_ACCESSORS(ac_qual,          AC_QUAL)
GEN_SES_STATUS_UPS_ACCESSORS(ac_fail,          AC_FAIL)
GEN_SES_STATUS_UPS_ACCESSORS(dc_fail,          DC_FAIL)
GEN_SES_STATUS_UPS_ACCESSORS(ups_fail,         UPS_FAIL)
GEN_SES_STATUS_UPS_ACCESSORS(warn,             WARN)
GEN_SES_STATUS_UPS_ACCESSORS(intf_fail,        INTF_FAIL)
GEN_SES_STATUS_UPS_ACCESSORS(ident,            IDENT)
GEN_SES_STATUS_UPS_ACCESSORS(fail,             FAIL)
GEN_SES_STATUS_UPS_ACCESSORS(batt_fail,        BATT_FAIL)
GEN_SES_STATUS_UPS_ACCESSORS(bpf,              BPF)
#undef GEN_SES_STATUS_UPS_ACCESSORS

/*-------------------------- Display Status Element --------------------------*/
struct ses_status_display {
	struct ses_status_common common;
	uint8_t bytes[1];
	uint8_t display_character[2];
};

enum ses_status_display_field_data {
	SES_STATUS_DISPLAY_IDENT_BYTE			= 0,
	SES_STATUS_DISPLAY_IDENT_MASK			= 0x80,
	SES_STATUS_DISPLAY_IDENT_SHIFT			= 7,

	SES_STATUS_DISPLAY_FAIL_BYTE			= 0,
	SES_STATUS_DISPLAY_FAIL_MASK			= 0x40,
	SES_STATUS_DISPLAY_FAIL_SHIFT			= 6,

	SES_STATUS_DISPLAY_DISPLAY_MODE_BYTE		= 0,
	SES_STATUS_DISPLAY_DISPLAY_MODE_MASK		= 0x03,
	SES_STATUS_DISPLAY_DISPLAY_MODE_SHIFT		= 6,
	SES_STATUS_DISPLAY_DISPLAY_MODE_DC_FIELD_UNSUPP	= 0x0,
	SES_STATUS_DISPLAY_DISPLAY_MODE_DC_FIELD_SUPP	= 0x1,
	SES_STATUS_DISPLAY_DISPLAY_MODE_DC_FIELD	= 0x2
};

#define GEN_SES_STATUS_DISPLAY_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_display, SES_STATUS_DISPLAY, LCASE, UCASE)
GEN_SES_STATUS_DISPLAY_ACCESSORS(ident,        IDENT)
GEN_SES_STATUS_DISPLAY_ACCESSORS(fail,         FAIL)
GEN_SES_STATUS_DISPLAY_ACCESSORS(display_mode, DISPLAY_MODE)
#undef GEN_SES_STATUS_DISPLAY_ACCESSORS

/*----------------------- Key Pad Entry Status Element -----------------------*/
struct ses_status_key_pad_entry {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_key_pad_entry_field_data {
	SES_STATUS_KEY_PAD_ENTRY_IDENT_BYTE	= 0,
	SES_STATUS_KEY_PAD_ENTRY_IDENT_MASK	= 0x80,
	SES_STATUS_KEY_PAD_ENTRY_IDENT_SHIFT	= 7,

	SES_STATUS_KEY_PAD_ENTRY_FAIL_BYTE	= 0,
	SES_STATUS_KEY_PAD_ENTRY_FAIL_MASK	= 0x40,
	SES_STATUS_KEY_PAD_ENTRY_FAIL_SHIFT	= 6
};

#define GEN_SES_STATUS_KEY_PAD_ENTRY_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_key_pad_entry, SES_STATUS_KEY_PAD_ENTRY, LCASE, UCASE)
GEN_SES_STATUS_KEY_PAD_ENTRY_ACCESSORS(ident, IDENT)
GEN_SES_STATUS_KEY_PAD_ENTRY_ACCESSORS(fail,  FAIL)
#undef GEN_SES_STATUS_KEY_PAD_ENTRY_ACCESSORS

/*------------------------- Enclosure Status Element -------------------------*/
struct ses_status_enclosure {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_enclosure_field_data {
	SES_STATUS_ENCLOSURE_IDENT_BYTE					= 0,
	SES_STATUS_ENCLOSURE_IDENT_MASK					= 0x80,
	SES_STATUS_ENCLOSURE_IDENT_SHIFT				= 7,

	SES_STATUS_ENCLOSURE_TIME_UNTIL_POWER_CYCLE_BYTE		= 1,
	SES_STATUS_ENCLOSURE_TIME_UNTIL_POWER_CYCLE_MASK		= 0xFC,
	SES_STATUS_ENCLOSURE_TIME_UNTIL_POWER_CYCLE_SHIFT		= 2,

	SES_STATUS_ENCLOSURE_FAIL_BYTE					= 1,
	SES_STATUS_ENCLOSURE_FAIL_MASK					= 0x02,
	SES_STATUS_ENCLOSURE_FAIL_SHIFT					= 1,

	SES_STATUS_ENCLOSURE_WARN_BYTE					= 1,
	SES_STATUS_ENCLOSURE_WARN_MASK					= 0x01,
	SES_STATUS_ENCLOSURE_WARN_SHIFT					= 0,

	SES_STATUS_ENCLOSURE_REQUESTED_POWER_OFF_DURATION_BYTE		= 2,
	SES_STATUS_ENCLOSURE_REQUESTED_POWER_OFF_DURATION_MASK		= 0xFC,
	SES_STATUS_ENCLOSURE_REQUESTED_POWER_OFF_DURATION_SHIFT		= 2,
	SES_STATUS_ENCLOSURE_REQUESTED_POWER_OFF_DURATION_MAX_AUTO	= 60,
	SES_STATUS_ENCLOSURE_REQUESTED_POWER_OFF_DURATION_MANUAL	= 63,

	SES_STATUS_ENCLOSURE_REQUESTED_FAIL_BYTE			= 2,
	SES_STATUS_ENCLOSURE_REQUESTED_FAIL_MASK			= 0x02,
	SES_STATUS_ENCLOSURE_REQUESTED_FAIL_SHIFT			= 1,

	SES_STATUS_ENCLOSURE_REQUESTED_WARN_BYTE			= 2,
	SES_STATUS_ENCLOSURE_REQUESTED_WARN_MASK			= 0x01,
	SES_STATUS_ENCLOSURE_REQUESTED_WARN_SHIFT			= 0
};

#define GEN_SES_STATUS_ENCLOSURE_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_enclosure, SES_STATUS_ENCLOSURE, LCASE, UCASE)
GEN_SES_STATUS_ENCLOSURE_ACCESSORS(ident,          IDENT)
GEN_SES_STATUS_ENCLOSURE_ACCESSORS(time_until_power_cycle,
				   TIME_UNTIL_POWER_CYCLE)
GEN_SES_STATUS_ENCLOSURE_ACCESSORS(fail,           FAIL)
GEN_SES_STATUS_ENCLOSURE_ACCESSORS(warn,           WARN)
GEN_SES_STATUS_ENCLOSURE_ACCESSORS(requested_power_off_duration,
				   REQUESTED_POWER_OFF_DURATION)
GEN_SES_STATUS_ENCLOSURE_ACCESSORS(requested_fail, REQUESTED_FAIL)
GEN_SES_STATUS_ENCLOSURE_ACCESSORS(requested_warn, REQUESTED_WARN)
#undef GEN_SES_STATUS_ENCLOSURE_ACCESSORS

/*------------------- SCSI Port/Transceiver Status Element -------------------*/
struct ses_status_scsi_port_or_xcvr {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_scsi_port_or_xcvr_field_data {
	SES_STATUS_SCSI_PORT_OR_XCVR_IDENT_BYTE		= 0,
	SES_STATUS_SCSI_PORT_OR_XCVR_IDENT_MASK		= 0x80,
	SES_STATUS_SCSI_PORT_OR_XCVR_IDENT_SHIFT	= 7,

	SES_STATUS_SCSI_PORT_OR_XCVR_FAIL_BYTE		= 0,
	SES_STATUS_SCSI_PORT_OR_XCVR_FAIL_MASK		= 0x40,
	SES_STATUS_SCSI_PORT_OR_XCVR_FAIL_SHIFT		= 6,

	SES_STATUS_SCSI_PORT_OR_XCVR_REPORT_BYTE	= 1,
	SES_STATUS_SCSI_PORT_OR_XCVR_REPORT_MASK	= 0x01,
	SES_STATUS_SCSI_PORT_OR_XCVR_REPORT_SHIFT	= 0,

	SES_STATUS_SCSI_PORT_OR_XCVR_DISABLED_BYTE	= 2,
	SES_STATUS_SCSI_PORT_OR_XCVR_DISABLED_MASK	= 0x10,
	SES_STATUS_SCSI_PORT_OR_XCVR_DISABLED_SHIFT	= 4,

	SES_STATUS_SCSI_PORT_OR_XCVR_LOL_BYTE		= 2,
	SES_STATUS_SCSI_PORT_OR_XCVR_LOL_MASK		= 0x02,
	SES_STATUS_SCSI_PORT_OR_XCVR_LOL_SHIFT		= 1,

	SES_STATUS_SCSI_PORT_OR_XCVR_XMIT_FAIL_BYTE	= 2,
	SES_STATUS_SCSI_PORT_OR_XCVR_XMIT_FAIL_MASK	= 0x01,
	SES_STATUS_SCSI_PORT_OR_XCVR_XMIT_FAIL_SHIFT	= 0
};

#define GEN_SES_STATUS_SCSI_PORT_OR_XCVR_ACCESSORS(LCASE, UCASE)	 \
    GEN_GETTER(ses_status_scsi_port_or_xcvr, SES_STATUS_SCSI_PORT_OR_XCVR,\
	       LCASE, UCASE)
GEN_SES_STATUS_SCSI_PORT_OR_XCVR_ACCESSORS(ident,     IDENT)
GEN_SES_STATUS_SCSI_PORT_OR_XCVR_ACCESSORS(fail,      FAIL)
GEN_SES_STATUS_SCSI_PORT_OR_XCVR_ACCESSORS(report,    REPORT)
GEN_SES_STATUS_SCSI_PORT_OR_XCVR_ACCESSORS(disable,   DISABLED)
GEN_SES_STATUS_SCSI_PORT_OR_XCVR_ACCESSORS(lol,       LOL)
GEN_SES_STATUS_SCSI_PORT_OR_XCVR_ACCESSORS(xmit_fail, XMIT_FAIL)
#undef GEN_SES_STATUS_SCSI_PORT_OR_XCVR_ACCESSORS

/*------------------------- Language Status Element --------------------------*/
struct ses_status_language {
	struct ses_status_common common;
	uint8_t bytes[1];
	uint8_t language_code[2];
};

enum ses_status_language_field_data {
	SES_STATUS_LANGUAGE_IDENT_BYTE	= 0,
	SES_STATUS_LANGUAGE_IDENT_MASK	= 0x80,
	SES_STATUS_LANGUAGE_IDENT_SHIFT	= 7
};

#define GEN_SES_STATUS_LANGUAGE_ACCESSORS(LCASE, UCASE)		 \
    GEN_GETTER(ses_status_language, SES_STATUS_LANGUAGE, LCASE, UCASE)
GEN_SES_STATUS_LANGUAGE_ACCESSORS(ident, IDENT)
#undef GEN_SES_STATUS_LANGUAGE_ACCESSORS

/*-------------------- Communication Port Status Element ---------------------*/
struct ses_status_comm_port {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_comm_port_field_data {
	SES_STATUS_COMM_PORT_IDENT_BYTE		= 0,
	SES_STATUS_COMM_PORT_IDENT_MASK		= 0x80,
	SES_STATUS_COMM_PORT_IDENT_SHIFT	= 7,

	SES_STATUS_COMM_PORT_FAIL_BYTE		= 0,
	SES_STATUS_COMM_PORT_FAIL_MASK		= 0x40,
	SES_STATUS_COMM_PORT_FAIL_SHIFT		= 6,

	SES_STATUS_COMM_PORT_DISABLED_BYTE	= 2,
	SES_STATUS_COMM_PORT_DISABLED_MASK	= 0x01,
	SES_STATUS_COMM_PORT_DISABLED_SHIFT	= 0
};

#define GEN_SES_STATUS_COMM_PORT_ACCESSORS(LCASE, UCASE)		 \
    GEN_GETTER(ses_status_comm_port, SES_STATUS_COMM_PORT, LCASE, UCASE)
GEN_SES_STATUS_COMM_PORT_ACCESSORS(ident,    IDENT)
GEN_SES_STATUS_COMM_PORT_ACCESSORS(fail,     FAIL)
GEN_SES_STATUS_COMM_PORT_ACCESSORS(disabled, DISABLED)
#undef GEN_SES_STATUS_COMM_PORT_ACCESSORS

/*---------------------- Voltage Sensor Status Element -----------------------*/
struct ses_status_voltage_sensor {
	struct ses_status_common common;
	uint8_t bytes[1];
	uint8_t voltage[2];
};

enum ses_status_voltage_sensor_field_data {
	SES_STATUS_VOLTAGE_SENSOR_IDENT_BYTE		= 0,
	SES_STATUS_VOLTAGE_SENSOR_IDENT_MASK		= 0x80,
	SES_STATUS_VOLTAGE_SENSOR_IDENT_SHIFT		= 7,

	SES_STATUS_VOLTAGE_SENSOR_FAIL_BYTE		= 0,
	SES_STATUS_VOLTAGE_SENSOR_FAIL_MASK		= 0x40,
	SES_STATUS_VOLTAGE_SENSOR_FAIL_SHIFT		= 6,

	SES_STATUS_VOLTAGE_SENSOR_WARN_OVER_BYTE	= 0,
	SES_STATUS_VOLTAGE_SENSOR_WARN_OVER_MASK	= 0x08,
	SES_STATUS_VOLTAGE_SENSOR_WARN_OVER_SHIFT	= 3,

	SES_STATUS_VOLTAGE_SENSOR_WARN_UNDER_BYTE	= 0,
	SES_STATUS_VOLTAGE_SENSOR_WARN_UNDER_MASK	= 0x04,
	SES_STATUS_VOLTAGE_SENSOR_WARN_UNDER_SHIFT	= 2,

	SES_STATUS_VOLTAGE_SENSOR_CRIT_OVER_BYTE	= 0,
	SES_STATUS_VOLTAGE_SENSOR_CRIT_OVER_MASK	= 0x02,
	SES_STATUS_VOLTAGE_SENSOR_CRIT_OVER_SHIFT	= 1,

	SES_STATUS_VOLTAGE_SENSOR_CRIT_UNDER_BYTE	= 0,
	SES_STATUS_VOLTAGE_SENSOR_CRIT_UNDER_MASK	= 0x01,
	SES_STATUS_VOLTAGE_SENSOR_CRIT_UNDER_SHIFT	= 0
};

#define GEN_SES_STATUS_VOLTAGE_SENSOR_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_voltage_sensor, SES_STATUS_VOLTAGE_SENSOR,	\
		  LCASE, UCASE)
GEN_SES_STATUS_VOLTAGE_SENSOR_ACCESSORS(ident,      IDENT)
GEN_SES_STATUS_VOLTAGE_SENSOR_ACCESSORS(fail,       FAIL)
GEN_SES_STATUS_VOLTAGE_SENSOR_ACCESSORS(warn_over,  WARN_OVER)
GEN_SES_STATUS_VOLTAGE_SENSOR_ACCESSORS(warn_under, WARN_UNDER)
GEN_SES_STATUS_VOLTAGE_SENSOR_ACCESSORS(crit_over,  CRIT_OVER)
GEN_SES_STATUS_VOLTAGE_SENSOR_ACCESSORS(crit_under, CRIT_UNDER)
#undef GEN_SES_STATUS_VOLTAGE_SENSOR_ACCESSORS

/*---------------------- Current Sensor Status Element -----------------------*/
struct ses_status_current_sensor {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_current_sensor_field_data {
	SES_STATUS_CURRENT_SENSOR_IDENT_BYTE		= 0,
	SES_STATUS_CURRENT_SENSOR_IDENT_MASK		= 0x80,
	SES_STATUS_CURRENT_SENSOR_IDENT_SHIFT		= 7,

	SES_STATUS_CURRENT_SENSOR_FAIL_BYTE		= 0,
	SES_STATUS_CURRENT_SENSOR_FAIL_MASK		= 0x40,
	SES_STATUS_CURRENT_SENSOR_FAIL_SHIFT		= 6,

	SES_STATUS_CURRENT_SENSOR_WARN_OVER_BYTE	= 0,
	SES_STATUS_CURRENT_SENSOR_WARN_OVER_MASK	= 0x08,
	SES_STATUS_CURRENT_SENSOR_WARN_OVER_SHIFT	= 3,

	SES_STATUS_CURRENT_SENSOR_CRIT_OVER_BYTE	= 0,
	SES_STATUS_CURRENT_SENSOR_CRIT_OVER_MASK	= 0x02,
	SES_STATUS_CURRENT_SENSOR_CRIT_OVER_SHIFT	= 1
};

#define GEN_SES_STATUS_CURRENT_SENSOR_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_current_sensor, SES_STATUS_CURRENT_SENSOR,	\
		  LCASE, UCASE)
GEN_SES_STATUS_CURRENT_SENSOR_ACCESSORS(ident,      IDENT)
GEN_SES_STATUS_CURRENT_SENSOR_ACCESSORS(fail,       FAIL)
GEN_SES_STATUS_CURRENT_SENSOR_ACCESSORS(warn_over,  WARN_OVER)
GEN_SES_STATUS_CURRENT_SENSOR_ACCESSORS(crit_over,  CRIT_OVER)
#undef GEN_SES_STATUS_CURRENT_SENSOR_ACCESSORS

/*--------------------- SCSI Target Port Status Element ----------------------*/
struct ses_status_target_port {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_scsi_target_port_field_data {
	SES_STATUS_TARGET_PORT_IDENT_BYTE	= 0,
	SES_STATUS_TARGET_PORT_IDENT_MASK	= 0x80,
	SES_STATUS_TARGET_PORT_IDENT_SHIFT	= 7,

	SES_STATUS_TARGET_PORT_FAIL_BYTE	= 0,
	SES_STATUS_TARGET_PORT_FAIL_MASK	= 0x40,
	SES_STATUS_TARGET_PORT_FAIL_SHIFT	= 6,

	SES_STATUS_TARGET_PORT_REPORT_BYTE	= 1,
	SES_STATUS_TARGET_PORT_REPORT_MASK	= 0x01,
	SES_STATUS_TARGET_PORT_REPORT_SHIFT	= 0,

	SES_STATUS_TARGET_PORT_ENABLED_BYTE	= 2,
	SES_STATUS_TARGET_PORT_ENABLED_MASK	= 0x01,
	SES_STATUS_TARGET_PORT_ENABLED_SHIFT	= 0
};

#define GEN_SES_STATUS_TARGET_PORT_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_target_port, SES_STATUS_TARGET_PORT, LCASE, UCASE)
GEN_SES_STATUS_TARGET_PORT_ACCESSORS(ident,   IDENT)
GEN_SES_STATUS_TARGET_PORT_ACCESSORS(fail,    FAIL)
GEN_SES_STATUS_TARGET_PORT_ACCESSORS(report,  REPORT)
GEN_SES_STATUS_TARGET_PORT_ACCESSORS(enabled, ENABLED)
#undef GEN_SES_STATUS_TARGET_PORT_ACCESSORS

/*-------------------- SCSI Initiator Port Status Element --------------------*/
struct ses_status_initiator_port {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_scsi_initiator_port_field_data {
	SES_STATUS_INITIATOR_PORT_IDENT_BYTE	= 0,
	SES_STATUS_INITIATOR_PORT_IDENT_MASK	= 0x80,
	SES_STATUS_INITIATOR_PORT_IDENT_SHIFT	= 7,

	SES_STATUS_INITIATOR_PORT_FAIL_BYTE	= 0,
	SES_STATUS_INITIATOR_PORT_FAIL_MASK	= 0x40,
	SES_STATUS_INITIATOR_PORT_FAIL_SHIFT	= 6,

	SES_STATUS_INITIATOR_PORT_REPORT_BYTE	= 1,
	SES_STATUS_INITIATOR_PORT_REPORT_MASK	= 0x01,
	SES_STATUS_INITIATOR_PORT_REPORT_SHIFT	= 0,

	SES_STATUS_INITIATOR_PORT_ENABLED_BYTE	= 2,
	SES_STATUS_INITIATOR_PORT_ENABLED_MASK	= 0x01,
	SES_STATUS_INITIATOR_PORT_ENABLED_SHIFT	= 0
};

#define GEN_SES_STATUS_INITIATOR_PORT_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_initiator_port, SES_STATUS_INITIATOR_PORT,	\
	       LCASE, UCASE)
GEN_SES_STATUS_INITIATOR_PORT_ACCESSORS(ident,   IDENT)
GEN_SES_STATUS_INITIATOR_PORT_ACCESSORS(fail,    FAIL)
GEN_SES_STATUS_INITIATOR_PORT_ACCESSORS(report,  REPORT)
GEN_SES_STATUS_INITIATOR_PORT_ACCESSORS(enabled, ENABLED)
#undef GEN_SES_STATUS_INITIATOR_PORT_ACCESSORS

/*-------------------- Simple Subenclosure Status Element --------------------*/
struct ses_status_simple_subses {
	struct ses_status_common common;
	uint8_t bytes[2];
	uint8_t short_enclosure_status;
};

enum ses_status_simple_subses_field_data {
	SES_STATUS_SIMPlE_SUBSES_IDENT_BYTE	= 0,
	SES_STATUS_SIMPlE_SUBSES_IDENT_MASK	= 0x80,
	SES_STATUS_SIMPlE_SUBSES_IDENT_SHIFT	= 7,

	SES_STATUS_SIMPlE_SUBSES_FAIL_BYTE	= 0,
	SES_STATUS_SIMPlE_SUBSES_FAIL_MASK	= 0x40,
	SES_STATUS_SIMPlE_SUBSES_FAIL_SHIFT	= 6
};

#define GEN_SES_STATUS_SIMPlE_SUBSES_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_simple_subses, SES_STATUS_SIMPlE_SUBSES,	\
		  LCASE, UCASE)
GEN_SES_STATUS_SIMPlE_SUBSES_ACCESSORS(ident, IDENT)
GEN_SES_STATUS_SIMPlE_SUBSES_ACCESSORS(fail,  FAIL)
#undef GEN_SES_STATUS_SIMPlE_SUBSES_ACCESSORS

/*----------------------- SAS Expander Status Element ------------------------*/
struct ses_status_sas_expander {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_sas_expander_field_data {
	SES_STATUS_SAS_EXPANDER_IDENT_BYTE	= 0,
	SES_STATUS_SAS_EXPANDER_IDENT_MASK	= 0x80,
	SES_STATUS_SAS_EXPANDER_IDENT_SHIFT	= 7,

	SES_STATUS_SAS_EXPANDER_FAIL_BYTE	= 0,
	SES_STATUS_SAS_EXPANDER_FAIL_MASK	= 0x40,
	SES_STATUS_SAS_EXPANDER_FAIL_SHIFT	= 6
};

#define GEN_SES_STATUS_SAS_EXPANDER_ACCESSORS(LCASE, UCASE)	\
    GEN_GETTER(ses_status_sas_expander, SES_STATUS_SAS_EXPANDER,	LCASE, UCASE)
GEN_SES_STATUS_SAS_EXPANDER_ACCESSORS(ident, IDENT)
GEN_SES_STATUS_SAS_EXPANDER_ACCESSORS(fail,  FAIL)
#undef GEN_SES_STATUS_SAS_EXPANDER_ACCESSORS

/*----------------------- SAS Connector Status Element -----------------------*/
struct ses_status_sas_connector {
	struct ses_status_common common;
	uint8_t bytes[3];
};

enum ses_status_sas_connector_field_data {
	SES_STATUS_SAS_CONNECTOR_IDENT_BYTE		= 0,
	SES_STATUS_SAS_CONNECTOR_IDENT_MASK		= 0x80,
	SES_STATUS_SAS_CONNECTOR_IDENT_SHIFT		= 7,

	SES_STATUS_SAS_CONNECTOR_TYPE_BYTE		= 0,
	SES_STATUS_SAS_CONNECTOR_TYPE_MASK		= 0x7F,
	SES_STATUS_SAS_CONNECTOR_TYPE_SHIFT		= 0,

	SES_STATUS_SAS_CONNECTOR_PHYS_LINK_BYTE		= 1,
	SES_STATUS_SAS_CONNECTOR_PHYS_LINK_MASK		= 0xFF,
	SES_STATUS_SAS_CONNECTOR_PHYS_LINK_SHIFT	= 0,
	SES_STATUS_SAS_CONNECTOR_PHYS_LINK_ALL		= 0xFF,

	SES_STATUS_SAS_CONNECTOR_FAIL_BYTE		= 2,
	SES_STATUS_SAS_CONNECTOR_FAIL_MASK		= 0x40,
	SES_STATUS_SAS_CONNECTOR_FAIL_SHIFT		= 6,
};

#define GEN_SES_STATUS_SAS_CONNECTOR_ACCESSORS(LCASE, UCASE)		\
    GEN_GETTER(ses_status_sas_connector, SES_STATUS_SAS_CONNECTOR,	\
		  LCASE, UCASE)
GEN_SES_STATUS_SAS_CONNECTOR_ACCESSORS(ident,     IDENT)
GEN_SES_STATUS_SAS_CONNECTOR_ACCESSORS(type,      TYPE)
GEN_SES_STATUS_SAS_CONNECTOR_ACCESSORS(phys_link, PHYS_LINK)
GEN_SES_STATUS_SAS_CONNECTOR_ACCESSORS(fail,      FAIL)
#undef GEN_SES_STATUS_SAS_CONNECTOR_ACCESSORS

/*------------------------- Universal Status Element -------------------------*/
union ses_status_element {
	struct ses_status_common            common;
	struct ses_status_dev_slot          dev_slot;
	struct ses_status_array_dev_slot    array_dev_slot;
	struct ses_status_power_supply      power_supply;
	struct ses_status_cooling           cooling;
	struct ses_status_temp_sensor       temp_sensor;
	struct ses_status_door_lock         door_lock;
	struct ses_status_audible_alarm     audible_alarm;
	struct ses_status_ecc_electronics   ecc_electronics;
	struct ses_status_scc_electronics   scc_electronics;
	struct ses_status_nv_cache          nv_cache;
	struct ses_status_invalid_op_reason invalid_op_reason;
	struct ses_status_ups               ups;
	struct ses_status_display           display;
	struct ses_status_key_pad_entry     key_pad_entry;
	struct ses_status_scsi_port_or_xcvr scsi_port_or_xcvr;
	struct ses_status_language          language;
	struct ses_status_comm_port         comm_port;
	struct ses_status_voltage_sensor    voltage_sensor;
	struct ses_status_current_sensor    current_sensor;
	struct ses_status_target_port       target_port;
	struct ses_status_initiator_port    initiator_port;
	struct ses_status_simple_subses     simple_subses;
	struct ses_status_sas_expander      sas_expander;
	struct ses_status_sas_connector     sas_connector;
	uint8_t				    bytes[4];
};

/*===================== SCSI SES Status Diagnostic Page =====================*/
struct ses_status_page {
	struct ses_page_hdr  hdr;
	union ses_status_element  elements[];
};

enum ses_status_page_field_data {
	SES_STATUS_PAGE_INVOP_MASK	= 0x10,
	SES_STATUS_PAGE_INVOP_SHIFT	= 4,

	SES_STATUS_PAGE_INFO_MASK	= 0x08,
	SES_STATUS_PAGE_INFO_SHIFT	= 3,

	SES_STATUS_PAGE_NON_CRIT_MASK	= 0x04,
	SES_STATUS_PAGE_NON_CRIT_SHIFT	= 2,

	SES_STATUS_PAGE_CRIT_MASK	= 0x02,
	SES_STATUS_PAGE_CRIT_SHIFT	= 1,

	SES_STATUS_PAGE_UNRECOV_MASK	= 0x01,
	SES_STATUS_PAGE_UNRECOV_SHIFT	= 0,

	SES_STATUS_PAGE_CHANGED_MASK	= SES_STATUS_PAGE_INVOP_MASK
					| SES_STATUS_PAGE_INFO_MASK
					| SES_STATUS_PAGE_NON_CRIT_MASK
					| SES_STATUS_PAGE_CRIT_MASK
					| SES_STATUS_PAGE_UNRECOV_MASK,
	SES_STATUS_PAGE_CHANGED_SHIFT	= 0,
};

#define GEN_SES_STATUS_PAGE_ACCESSORS(LCASE, UCASE) \
    GEN_HDR_ACCESSORS(ses_status_page, SES_STATUS_PAGE, LCASE, UCASE)

GEN_SES_STATUS_PAGE_ACCESSORS(invop,    INVOP)
GEN_SES_STATUS_PAGE_ACCESSORS(info,     INFO)
GEN_SES_STATUS_PAGE_ACCESSORS(non_crit, NON_CRIT)
GEN_SES_STATUS_PAGE_ACCESSORS(crit,     CRIT)
GEN_SES_STATUS_PAGE_ACCESSORS(unrecov,  UNRECOV)
GEN_SES_STATUS_PAGE_ACCESSORS(changed,  CHANGED)
#undef GEN_SES_STATUS_PAGE_ACCESSORS

/*================ SCSI SES Element Descriptor Diagnostic Page ===============*/
struct ses_elem_descr {
	uint8_t	reserved[2];
	uint8_t	length[2];
	char	description[];
};

struct ses_elem_descr_page {
	struct ses_page_hdr   hdr;
	struct ses_elem_descr descrs[];
};

/*============ SCSI SES Additional Element Status Diagnostic Page ============*/
struct ses_addl_elem_status_page {
	struct ses_page_hdr   hdr;
};

/*====================== Legacy (Deprecated) Structures ======================*/
struct ses_control_page_hdr {
	uint8_t page_code;
	uint8_t control_flags;
	uint8_t length[2];
	uint8_t gen_code[4];
/* Followed by variable length array of descriptors. */
};

struct ses_status_page_hdr {
	uint8_t page_code;
	uint8_t status_flags;
	uint8_t length[2];
	uint8_t gen_code[4];
/* Followed by variable length array of descriptors. */
};

/* ses_page_hdr.reserved values */
/*
 * Enclosure Status Diagnostic Page:
 * uint8_t	reserved : 3,
 * 		invop : 1,
 * 		info : 1,
 * 		noncritical : 1,
 * 		critical : 1,
 * 		unrecov : 1;
 */
#define	SES_ENCSTAT_UNRECOV		0x01
#define	SES_ENCSTAT_CRITICAL		0x02
#define	SES_ENCSTAT_NONCRITICAL		0x04
#define	SES_ENCSTAT_INFO		0x08
#define	SES_ENCSTAT_INVOP		0x10
/* Status mask: All of the above OR'd together */
#define	SES_STATUS_MASK			0x1f
#define	SES_SET_STATUS_MASK		0xf
/* Element Descriptor Diagnostic Page: unused */
/* Additional Element Status Diagnostic Page: unused */



/* Summary SES Status Defines, Common Status Codes */
#define	SES_OBJSTAT_UNSUPPORTED		0
#define	SES_OBJSTAT_OK			1
#define	SES_OBJSTAT_CRIT		2
#define	SES_OBJSTAT_NONCRIT		3
#define	SES_OBJSTAT_UNRECOV		4
#define	SES_OBJSTAT_NOTINSTALLED	5
#define	SES_OBJSTAT_UNKNOWN		6
#define	SES_OBJSTAT_NOTAVAIL		7
#define	SES_OBJSTAT_NOACCESS		8

/*
 * For control pages, cstat[0] is the same for the
 * enclosure and is common across all device types.
 *
 * If SESCTL_CSEL is set, then PRDFAIL, DISABLE and RSTSWAP
 * are checked, otherwise bits that are specific to the device
 * type in the other 3 bytes of cstat or checked.
 */
#define	SESCTL_CSEL		0x80
#define	SESCTL_PRDFAIL		0x40
#define	SESCTL_DISABLE		0x20
#define	SESCTL_RSTSWAP		0x10


/* Control bits, Device Elements, byte 2 */
#define	SESCTL_DRVLCK	0x40	/* "DO NOT REMOVE" */
#define	SESCTL_RQSINS	0x08	/* RQST INSERT */
#define	SESCTL_RQSRMV	0x04	/* RQST REMOVE */
#define	SESCTL_RQSID	0x02	/* RQST IDENT */
/* Control bits, Device Elements, byte 3 */
#define	SESCTL_RQSFLT	0x20	/* RQST FAULT */
#define	SESCTL_DEVOFF	0x10	/* DEVICE OFF */

/* Control bits, Generic, byte 3 */
#define	SESCTL_RQSTFAIL	0x40
#define	SESCTL_RQSTON	0x20

/*
 * Getting text for an object type is a little
 * trickier because it's string data that can
 * go up to 64 KBytes. Build this union and
 * fill the obj_id with the id of the object who's
 * help text you want, and if text is available,
 * obj_text will be filled in, null terminated.
 */

typedef union {
	unsigned int obj_id;
	char obj_text[1];
} ses_hlptxt;

/*============================================================================*/
struct ses_elm_desc_hdr {
	uint8_t reserved[2];
	uint8_t length[2];
};

/*
 * SES v2 r20 6.1.13 - Element Additional Status diagnostic page
 * Tables 26-28 (general), 29-32 (FC), 33-41 (SAS)
 *
 * Protocol identifier uses definitions in scsi_all.h;
 * SPSP_PROTO_FC, SPSP_PROTO_SAS are the only ones used here.
 */

struct ses_elm_fc_eip_hdr {
	uint8_t num_phys;
	uint8_t reserved[2];
	uint8_t dev_slot_num;
	uint8_t node_name[8];
};

struct ses_elm_fc_noneip_hdr {
	uint8_t num_phys;
	uint8_t reserved;
	uint8_t node_name[8];
};

struct ses_elm_fc_base_hdr {
	uint8_t num_phys;
};

union ses_elm_fc_hdr {
	struct ses_elm_fc_base_hdr	base_hdr;
	struct ses_elm_fc_eip_hdr	eip_hdr;
	struct ses_elm_fc_noneip_hdr	noneip_hdr;
};

struct ses_elm_fc_port {
	uint8_t port_loop_position;
	uint8_t bypass_reason;
#define SES_FC_PORT_BYPASS_UNBYPASSED			0x00

#define	SES_FC_PORT_BYPASS_LINKFAIL_RATE_TOO_HIGH	0x10
#define	SES_FC_PORT_BYPASS_SYNC_LOSS_RATE_TOO_HIGH	0x11
#define	SES_FC_PORT_BYPASS_SIGNAL_LOSS_RATE_TOO_HIGH	0x12
#define	SES_FC_PORT_BYPASS_SEQPROTO_ERR_RATE_TOO_HIGH	0x13
#define	SES_FC_PORT_BYPASS_INVAL_XMIT_RATE_TOO_HIGH	0x14
#define	SES_FC_PORT_BYPASS_CRC_ERR_RATE_TOO_HIGH	0x15

#define	SES_FC_PORT_BYPASS_ERR_RATE_RESERVED_BEGIN	0x16
#define	SES_FC_PORT_BYPASS_ERR_RATE_RESERVED_END	0x1F

#define	SES_FC_PORT_BYPASS_LINKFAIL_COUNT_TOO_HIGH	0x20
#define	SES_FC_PORT_BYPASS_SYNC_LOSS_COUNT_TOO_HIGH	0x21
#define	SES_FC_PORT_BYPASS_SIGNAL_LOSS_COUNT_TOO_HIGH	0x22
#define	SES_FC_PORT_BYPASS_SEQPROTO_ERR_COUNT_TOO_HIGH	0x23
#define	SES_FC_PORT_BYPASS_INVAL_XMIT_COUNT_TOO_HIGH	0x24
#define	SES_FC_PORT_BYPASS_CRC_ERR_COUNT_TOO_HIGH	0x25

#define	SES_FC_PORT_BYPASS_ERR_COUNT_RESERVED_BEGIN	0x26
#define	SES_FC_PORT_BYPASS_ERR_COUNT_RESERVED_END	0x2F

#define	SES_FC_PORT_BYPASS_RESERVED_BEGIN		0x30
#define	SES_FC_PORT_BYPASS_RESERVED_END			0xBF

#define	SES_FC_PORT_BYPASS_VENDOR_SPECIFIC_BEGIN	0xC0
#define	SES_FC_PORT_BYPASS_VENDOR_SPECIFIC_END		0xFF
	uint8_t port_req_hard_addr;
	uint8_t n_port_id[3];
	uint8_t n_port_name[8];
};

struct ses_elm_sas_device_phy {
	uint8_t byte0;
	/*
	 * uint8_t reserved0 : 1,
	 * uint8_t device_type : 3,
	 * uint8_t reserved1 : 4;
	 */

	uint8_t reserved0;

	/* Bit positions for initiator and target port protocols */
#define	SES_SASOBJ_DEV_PHY_SMP		0x2
#define	SES_SASOBJ_DEV_PHY_STP		0x4
#define	SES_SASOBJ_DEV_PHY_SSP		0x8
	/* Select all of the above protocols */
#define	SES_SASOBJ_DEV_PHY_PROTOMASK	0xe
	uint8_t initiator_ports;
	/*
	 * uint8_t reserved0 : 4,
	 * uint8_t ssp : 1,
	 * uint8_t stp : 1,
	 * uint8_t smp : 1,
	 * uint8_t reserved1 : 3;
	 */
	uint8_t target_ports;
	/*
	 * uint8_t sata_port_selector : 1,
	 * uint8_t reserved : 3,
	 * uint8_t ssp : 1,
	 * uint8_t stp : 1,
	 * uint8_t smp : 1,
	 * uint8_t sata_device : 1;
	 */
	uint8_t parent_addr[8];		/* SAS address of parent */
	uint8_t phy_addr[8];		/* SAS address of this phy */
	uint8_t phy_id;
	uint8_t reserved1[7];
};
#ifdef _KERNEL
int ses_elm_sas_dev_phy_sata_dev(struct ses_elm_sas_device_phy *);
int ses_elm_sas_dev_phy_sata_port(struct ses_elm_sas_device_phy *);
int ses_elm_sas_dev_phy_dev_type(struct ses_elm_sas_device_phy *);
#endif	/* _KERNEL */

struct ses_elm_sas_expander_phy {
	uint8_t connector_index;
	uint8_t other_index;
};

struct ses_elm_sas_port_phy {
	uint8_t phy_id;
	uint8_t reserved;
	uint8_t connector_index;
	uint8_t other_index;
	uint8_t phy_addr[8];
};

struct ses_elm_sas_type0_base_hdr {
	uint8_t num_phys;
	uint8_t byte1;
	/*
	 * uint8_t descriptor_type : 2,
	 * uint8_t reserved : 5,
	 * uint8_t not_all_phys : 1;
	 */
#define	SES_SASOBJ_TYPE0_NOT_ALL_PHYS(obj)	\
	((obj)->byte1 & 0x1)
};

struct ses_elm_sas_type0_eip_hdr {
	struct ses_elm_sas_type0_base_hdr base;
	uint8_t reserved;
	uint8_t dev_slot_num;
};

struct ses_elm_sas_type1_expander_hdr {
	uint8_t num_phys;
	uint8_t byte1;
	/*
	 * uint8_t descriptor_type : 2,
	 * uint8_t reserved : 6;
	 */
	uint8_t reserved[2];
	uint8_t sas_addr[8];
};

struct ses_elm_sas_type1_nonexpander_hdr {
	uint8_t num_phys;
	uint8_t byte1;
	/*
	 * uint8_t descriptor_type : 2,
	 * uint8_t reserved : 6;
	 */
	uint8_t reserved[2];
};

/* NB: This is only usable for as long as the headers happen to match */
struct ses_elm_sas_base_hdr {
	uint8_t num_phys;
	uint8_t byte1;
	/*
	 * uint8_t descriptor_type : 2,
	 * uint8_t descr_specific : 6;
	 */
#define	SES_SASOBJ_TYPE_SLOT	0
#define	SES_SASOBJ_TYPE_OTHER	1
};

union ses_elm_sas_hdr {
	struct ses_elm_sas_base_hdr 			base_hdr;
	struct ses_elm_sas_type0_base_hdr		type0_noneip;
	struct ses_elm_sas_type0_eip_hdr		type0_eip;
	struct ses_elm_sas_type1_expander_hdr		type1_exp;
	struct ses_elm_sas_type1_nonexpander_hdr	type1_nonexp;
};
int ses_elm_sas_type0_not_all_phys(union ses_elm_sas_hdr *);
int ses_elm_sas_descr_type(union ses_elm_sas_hdr *);

struct ses_elm_addlstatus_base_hdr {
	uint8_t byte0;
	/*
	 * uint8_t invalid : 1,
	 * uint8_t reserved : 2,
	 * uint8_t eip : 1,
	 * uint8_t proto_id : 4;
	 */
	uint8_t length;
};
int ses_elm_addlstatus_proto(struct ses_elm_addlstatus_base_hdr *);
int ses_elm_addlstatus_eip(struct ses_elm_addlstatus_base_hdr *);
int ses_elm_addlstatus_invalid(struct ses_elm_addlstatus_base_hdr *);

struct ses_elm_addlstatus_eip_hdr {
	struct ses_elm_addlstatus_base_hdr base;
	uint8_t byte2;
#define	SES_ADDL_EIP_EIIOE	1
	uint8_t element_index;
	/* NB: This define (currently) applies to all eip=1 headers */
#define	SES_EIP_HDR_EXTRA_LEN	2
};

union ses_elm_addlstatus_descr_hdr {
	struct ses_elm_addlstatus_base_hdr	base;
	struct ses_elm_addlstatus_eip_hdr	eip;
};

union ses_elm_addlstatus_proto_hdr {
	union ses_elm_fc_hdr	fc;
	union ses_elm_sas_hdr	sas;
};

/*============================= Namespace Cleanup ============================*/
#undef GEN_HDR_ACCESSORS
#undef GEN_ACCESSORS
#undef GEN_HDR_SETTER
#undef GEN_HDR_GETTER
#undef GEN_SETTER
#undef GEN_GETTER
#undef MK_ENUM

#endif	/* _SCSI_SES_H_ */
