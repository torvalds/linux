/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Benjamin Tissoires
 */

#ifndef __HID_BPF_HELPERS_H
#define __HID_BPF_HELPERS_H

/* "undefine" structs and enums in vmlinux.h, because we "override" them below */
#define hid_bpf_ctx hid_bpf_ctx___not_used
#define hid_bpf_ops hid_bpf_ops___not_used
#define hid_report_type hid_report_type___not_used
#define hid_class_request hid_class_request___not_used
#define hid_bpf_attach_flags hid_bpf_attach_flags___not_used
#define HID_INPUT_REPORT         HID_INPUT_REPORT___not_used
#define HID_OUTPUT_REPORT        HID_OUTPUT_REPORT___not_used
#define HID_FEATURE_REPORT       HID_FEATURE_REPORT___not_used
#define HID_REPORT_TYPES         HID_REPORT_TYPES___not_used
#define HID_REQ_GET_REPORT       HID_REQ_GET_REPORT___not_used
#define HID_REQ_GET_IDLE         HID_REQ_GET_IDLE___not_used
#define HID_REQ_GET_PROTOCOL     HID_REQ_GET_PROTOCOL___not_used
#define HID_REQ_SET_REPORT       HID_REQ_SET_REPORT___not_used
#define HID_REQ_SET_IDLE         HID_REQ_SET_IDLE___not_used
#define HID_REQ_SET_PROTOCOL     HID_REQ_SET_PROTOCOL___not_used

#include "vmlinux.h"

#undef hid_bpf_ctx
#undef hid_bpf_ops
#undef hid_report_type
#undef hid_class_request
#undef hid_bpf_attach_flags
#undef HID_INPUT_REPORT
#undef HID_OUTPUT_REPORT
#undef HID_FEATURE_REPORT
#undef HID_REPORT_TYPES
#undef HID_REQ_GET_REPORT
#undef HID_REQ_GET_IDLE
#undef HID_REQ_GET_PROTOCOL
#undef HID_REQ_SET_REPORT
#undef HID_REQ_SET_IDLE
#undef HID_REQ_SET_PROTOCOL

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <linux/const.h>

enum hid_report_type {
	HID_INPUT_REPORT		= 0,
	HID_OUTPUT_REPORT		= 1,
	HID_FEATURE_REPORT		= 2,

	HID_REPORT_TYPES,
};

struct hid_bpf_ctx {
	struct hid_device *hid;
	__u32 allocated_size;
	union {
		__s32 retval;
		__s32 size;
	};
} __attribute__((preserve_access_index));

enum hid_class_request {
	HID_REQ_GET_REPORT		= 0x01,
	HID_REQ_GET_IDLE		= 0x02,
	HID_REQ_GET_PROTOCOL		= 0x03,
	HID_REQ_SET_REPORT		= 0x09,
	HID_REQ_SET_IDLE		= 0x0A,
	HID_REQ_SET_PROTOCOL		= 0x0B,
};

struct hid_bpf_ops {
	int			hid_id;
	u32			flags;
	struct list_head	list;
	int (*hid_device_event)(struct hid_bpf_ctx *ctx, enum hid_report_type report_type,
				u64 source);
	int (*hid_rdesc_fixup)(struct hid_bpf_ctx *ctx);
	int (*hid_hw_request)(struct hid_bpf_ctx *ctx, unsigned char reportnum,
			       enum hid_report_type rtype, enum hid_class_request reqtype,
			       u64 source);
	int (*hid_hw_output_report)(struct hid_bpf_ctx *ctx, u64 source);
	struct hid_device *hdev;
};

#define BIT(n) (1U << n)

#ifndef BPF_F_BEFORE
#define BPF_F_BEFORE BIT(3)
#endif

#define HID_QUIRK_IGNORE_SPECIAL_DRIVER		BIT(22)

/* following are kfuncs exported by HID for HID-BPF */
extern __u8 *hid_bpf_get_data(struct hid_bpf_ctx *ctx,
			      unsigned int offset,
			      const size_t __sz) __ksym;
extern struct hid_bpf_ctx *hid_bpf_allocate_context(unsigned int hid_id) __ksym;
extern void hid_bpf_release_context(struct hid_bpf_ctx *ctx) __ksym;
extern int hid_bpf_hw_request(struct hid_bpf_ctx *ctx,
			      __u8 *data,
			      size_t buf__sz,
			      enum hid_report_type type,
			      enum hid_class_request reqtype) __ksym;
extern int hid_bpf_hw_output_report(struct hid_bpf_ctx *ctx,
				    __u8 *buf, size_t buf__sz) __ksym;
extern int hid_bpf_input_report(struct hid_bpf_ctx *ctx,
				enum hid_report_type type,
				__u8 *data,
				size_t buf__sz) __ksym;
extern int hid_bpf_try_input_report(struct hid_bpf_ctx *ctx,
				    enum hid_report_type type,
				    __u8 *data,
				    size_t buf__sz) __ksym;

/* bpf_wq implementation */
extern int bpf_wq_init(struct bpf_wq *wq, void *p__map, unsigned int flags) __weak __ksym;
extern int bpf_wq_start(struct bpf_wq *wq, unsigned int flags) __weak __ksym;
extern int bpf_wq_set_callback_impl(struct bpf_wq *wq,
		int (callback_fn)(void *map, int *key, void *wq),
		unsigned int flags__k, void *aux__ign) __ksym;
#define bpf_wq_set_callback(timer, cb, flags) \
	bpf_wq_set_callback_impl(timer, cb, flags, NULL)

#endif /* __HID_BPF_HELPERS_H */
