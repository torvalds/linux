/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __HID_BPF_H
#define __HID_BPF_H

#include <linux/bpf.h>
#include <linux/spinlock.h>
#include <uapi/linux/hid.h>

struct hid_device;

/*
 * The following is the user facing HID BPF API.
 *
 * Extra care should be taken when editing this part, as
 * it might break existing out of the tree bpf programs.
 */

/**
 * struct hid_bpf_ctx - User accessible data for all HID programs
 *
 * ``data`` is not directly accessible from the context. We need to issue
 * a call to ``hid_bpf_get_data()`` in order to get a pointer to that field.
 *
 * All of these fields are currently read-only.
 *
 * @index: program index in the jump table. No special meaning (a smaller index
 *         doesn't mean the program will be executed before another program with
 *         a bigger index).
 * @hid: the ``struct hid_device`` representing the device itself
 * @report_type: used for ``hid_bpf_device_event()``
 * @allocated_size: Allocated size of data.
 *
 *                  This is how much memory is available and can be requested
 *                  by the HID program.
 *                  Note that for ``HID_BPF_RDESC_FIXUP``, that memory is set to
 *                  ``4096`` (4 KB)
 * @size: Valid data in the data field.
 *
 *        Programs can get the available valid size in data by fetching this field.
 *        Programs can also change this value by returning a positive number in the
 *        program.
 *        To discard the event, return a negative error code.
 *
 *        ``size`` must always be less or equal than ``allocated_size`` (it is enforced
 *        once all BPF programs have been run).
 * @retval: Return value of the previous program.
 */
struct hid_bpf_ctx {
	__u32 index;
	const struct hid_device *hid;
	__u32 allocated_size;
	enum hid_report_type report_type;
	union {
		__s32 retval;
		__s32 size;
	};
};

/**
 * enum hid_bpf_attach_flags - flags used when attaching a HIF-BPF program
 *
 * @HID_BPF_FLAG_NONE: no specific flag is used, the kernel choses where to
 *                     insert the program
 * @HID_BPF_FLAG_INSERT_HEAD: insert the given program before any other program
 *                            currently attached to the device. This doesn't
 *                            guarantee that this program will always be first
 * @HID_BPF_FLAG_MAX: sentinel value, not to be used by the callers
 */
enum hid_bpf_attach_flags {
	HID_BPF_FLAG_NONE = 0,
	HID_BPF_FLAG_INSERT_HEAD = _BITUL(0),
	HID_BPF_FLAG_MAX,
};

/* Following functions are tracepoints that BPF programs can attach to */
int hid_bpf_device_event(struct hid_bpf_ctx *ctx);
int hid_bpf_rdesc_fixup(struct hid_bpf_ctx *ctx);

/*
 * Below is HID internal
 */

/* internal function to call eBPF programs, not to be used by anybody */
int __hid_bpf_tail_call(struct hid_bpf_ctx *ctx);

#define HID_BPF_MAX_PROGS_PER_DEV 64
#define HID_BPF_FLAG_MASK (((HID_BPF_FLAG_MAX - 1) << 1) - 1)

/* types of HID programs to attach to */
enum hid_bpf_prog_type {
	HID_BPF_PROG_TYPE_UNDEF = -1,
	HID_BPF_PROG_TYPE_DEVICE_EVENT,			/* an event is emitted from the device */
	HID_BPF_PROG_TYPE_RDESC_FIXUP,
	HID_BPF_PROG_TYPE_MAX,
};

struct hid_report_enum;

struct hid_bpf_ops {
	struct hid_report *(*hid_get_report)(struct hid_report_enum *report_enum, const u8 *data);
	int (*hid_hw_raw_request)(struct hid_device *hdev,
				  unsigned char reportnum, __u8 *buf,
				  size_t len, enum hid_report_type rtype,
				  enum hid_class_request reqtype);
	struct module *owner;
	const struct bus_type *bus_type;
};

extern struct hid_bpf_ops *hid_bpf_ops;

struct hid_bpf_prog_list {
	u16 prog_idx[HID_BPF_MAX_PROGS_PER_DEV];
	u8 prog_cnt;
};

/* stored in each device */
struct hid_bpf {
	u8 *device_data;		/* allocated when a bpf program of type
					 * SEC(f.../hid_bpf_device_event) has been attached
					 * to this HID device
					 */
	u32 allocated_data;

	struct hid_bpf_prog_list __rcu *progs[HID_BPF_PROG_TYPE_MAX];	/* attached BPF progs */
	bool destroyed;			/* prevents the assignment of any progs */

	spinlock_t progs_lock;		/* protects RCU update of progs */
};

/* specific HID-BPF link when a program is attached to a device */
struct hid_bpf_link {
	struct bpf_link link;
	int hid_table_index;
};

#ifdef CONFIG_HID_BPF
u8 *dispatch_hid_bpf_device_event(struct hid_device *hid, enum hid_report_type type, u8 *data,
				  u32 *size, int interrupt);
int hid_bpf_connect_device(struct hid_device *hdev);
void hid_bpf_disconnect_device(struct hid_device *hdev);
void hid_bpf_destroy_device(struct hid_device *hid);
void hid_bpf_device_init(struct hid_device *hid);
u8 *call_hid_bpf_rdesc_fixup(struct hid_device *hdev, u8 *rdesc, unsigned int *size);
#else /* CONFIG_HID_BPF */
static inline u8 *dispatch_hid_bpf_device_event(struct hid_device *hid, enum hid_report_type type,
						u8 *data, u32 *size, int interrupt) { return data; }
static inline int hid_bpf_connect_device(struct hid_device *hdev) { return 0; }
static inline void hid_bpf_disconnect_device(struct hid_device *hdev) {}
static inline void hid_bpf_destroy_device(struct hid_device *hid) {}
static inline void hid_bpf_device_init(struct hid_device *hid) {}
static inline u8 *call_hid_bpf_rdesc_fixup(struct hid_device *hdev, u8 *rdesc, unsigned int *size)
{
	return kmemdup(rdesc, *size, GFP_KERNEL);
}

#endif /* CONFIG_HID_BPF */

#endif /* __HID_BPF_H */
