/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __HID_BPF_H
#define __HID_BPF_H

#include <linux/bpf.h>
#include <linux/mutex.h>
#include <linux/srcu.h>
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
 * a call to hid_bpf_get_data() in order to get a pointer to that field.
 *
 * @hid: the &struct hid_device representing the device itself
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
 *
 * ``hid`` and ``allocated_size`` are read-only, ``size`` and ``retval`` are read-write.
 */
struct hid_bpf_ctx {
	struct hid_device *hid;
	__u32 allocated_size;
	union {
		__s32 retval;
		__s32 size;
	};
};

/*
 * Below is HID internal
 */

#define HID_BPF_MAX_PROGS_PER_DEV 64
#define HID_BPF_FLAG_MASK (((HID_BPF_FLAG_MAX - 1) << 1) - 1)


struct hid_report_enum;

struct hid_ops {
	struct hid_report *(*hid_get_report)(struct hid_report_enum *report_enum, const u8 *data);
	int (*hid_hw_raw_request)(struct hid_device *hdev,
				  unsigned char reportnum, __u8 *buf,
				  size_t len, enum hid_report_type rtype,
				  enum hid_class_request reqtype,
				  u64 source, bool from_bpf);
	int (*hid_hw_output_report)(struct hid_device *hdev, __u8 *buf, size_t len,
				    u64 source, bool from_bpf);
	int (*hid_input_report)(struct hid_device *hid, enum hid_report_type type,
				u8 *data, u32 size, int interrupt, u64 source, bool from_bpf,
				bool lock_already_taken);
	struct module *owner;
	const struct bus_type *bus_type;
};

extern const struct hid_ops *hid_ops;

/**
 * struct hid_bpf_ops - A BPF struct_ops of callbacks allowing to attach HID-BPF
 *			programs to a HID device
 * @hid_id: the HID uniq ID to attach to. This is writeable before ``load()``, and
 *	    cannot be changed after
 * @flags: flags used while attaching the struct_ops to the device. Currently only
 *	   available value is %0 or ``BPF_F_BEFORE``.
 *	   Writeable only before ``load()``
 */
struct hid_bpf_ops {
	/* hid_id needs to stay first so we can easily change it
	 * from userspace.
	 */
	int			hid_id;
	u32			flags;

	/* private: do not show up in the docs */
	struct list_head	list;

	/* public: rest should show up in the docs */

	/**
	 * @hid_device_event: called whenever an event is coming in from the device
	 *
	 * It has the following arguments:
	 *
	 * ``ctx``: The HID-BPF context as &struct hid_bpf_ctx
	 *
	 * Return: %0 on success and keep processing; a positive
	 * value to change the incoming size buffer; a negative
	 * error code to interrupt the processing of this event
	 *
	 * Context: Interrupt context.
	 */
	int (*hid_device_event)(struct hid_bpf_ctx *ctx, enum hid_report_type report_type,
				u64 source);

	/**
	 * @hid_rdesc_fixup: called when the probe function parses the report descriptor
	 * of the HID device
	 *
	 * It has the following arguments:
	 *
	 * ``ctx``: The HID-BPF context as &struct hid_bpf_ctx
	 *
	 * Return: %0 on success and keep processing; a positive
	 * value to change the incoming size buffer; a negative
	 * error code to interrupt the processing of this device
	 */
	int (*hid_rdesc_fixup)(struct hid_bpf_ctx *ctx);

	/**
	 * @hid_hw_request: called whenever a hid_hw_raw_request() call is emitted
	 * on the HID device
	 *
	 * It has the following arguments:
	 *
	 * ``ctx``: The HID-BPF context as &struct hid_bpf_ctx
	 *
	 * ``reportnum``: the report number, as in hid_hw_raw_request()
	 *
	 * ``rtype``: the report type (``HID_INPUT_REPORT``, ``HID_FEATURE_REPORT``,
	 * ``HID_OUTPUT_REPORT``)
	 *
	 * ``reqtype``: the request
	 *
	 * ``source``: a u64 referring to a uniq but identifiable source. If %0, the
	 * kernel itself emitted that call. For hidraw, ``source`` is set
	 * to the associated ``struct file *``.
	 *
	 * Return: %0 to keep processing the request by hid-core; any other value
	 * stops hid-core from processing that event. A positive value should be
	 * returned with the number of bytes returned in the incoming buffer; a
	 * negative error code interrupts the processing of this call.
	 */
	int (*hid_hw_request)(struct hid_bpf_ctx *ctx, unsigned char reportnum,
			       enum hid_report_type rtype, enum hid_class_request reqtype,
			       u64 source);

	/**
	 * @hid_hw_output_report: called whenever a hid_hw_output_report() call is emitted
	 * on the HID device
	 *
	 * It has the following arguments:
	 *
	 * ``ctx``: The HID-BPF context as &struct hid_bpf_ctx
	 *
	 * ``source``: a u64 referring to a uniq but identifiable source. If %0, the
	 * kernel itself emitted that call. For hidraw, ``source`` is set
	 * to the associated ``struct file *``.
	 *
	 * Return: %0 to keep processing the request by hid-core; any other value
	 * stops hid-core from processing that event. A positive value should be
	 * returned with the number of bytes written to the device; a negative error
	 * code interrupts the processing of this call.
	 */
	int (*hid_hw_output_report)(struct hid_bpf_ctx *ctx, u64 source);


	/* private: do not show up in the docs */
	struct hid_device *hdev;
};

/* stored in each device */
struct hid_bpf {
	u8 *device_data;		/* allocated when a bpf program of type
					 * SEC(f.../hid_bpf_device_event) has been attached
					 * to this HID device
					 */
	u32 allocated_data;
	bool destroyed;			/* prevents the assignment of any progs */

	struct hid_bpf_ops *rdesc_ops;
	struct list_head prog_list;
	struct mutex prog_list_lock;	/* protects prog_list update */
	struct srcu_struct srcu;	/* protects prog_list read-only access */
};

#ifdef CONFIG_HID_BPF
u8 *dispatch_hid_bpf_device_event(struct hid_device *hid, enum hid_report_type type, u8 *data,
				  u32 *size, int interrupt, u64 source, bool from_bpf);
int dispatch_hid_bpf_raw_requests(struct hid_device *hdev,
				  unsigned char reportnum, __u8 *buf,
				  u32 size, enum hid_report_type rtype,
				  enum hid_class_request reqtype,
				  u64 source, bool from_bpf);
int dispatch_hid_bpf_output_report(struct hid_device *hdev, __u8 *buf, u32 size,
				   u64 source, bool from_bpf);
int hid_bpf_connect_device(struct hid_device *hdev);
void hid_bpf_disconnect_device(struct hid_device *hdev);
void hid_bpf_destroy_device(struct hid_device *hid);
int hid_bpf_device_init(struct hid_device *hid);
const u8 *call_hid_bpf_rdesc_fixup(struct hid_device *hdev, const u8 *rdesc, unsigned int *size);
#else /* CONFIG_HID_BPF */
static inline u8 *dispatch_hid_bpf_device_event(struct hid_device *hid, enum hid_report_type type,
						u8 *data, u32 *size, int interrupt,
						u64 source, bool from_bpf) { return data; }
static inline int dispatch_hid_bpf_raw_requests(struct hid_device *hdev,
						unsigned char reportnum, u8 *buf,
						u32 size, enum hid_report_type rtype,
						enum hid_class_request reqtype,
						u64 source, bool from_bpf) { return 0; }
static inline int dispatch_hid_bpf_output_report(struct hid_device *hdev, __u8 *buf, u32 size,
						 u64 source, bool from_bpf) { return 0; }
static inline int hid_bpf_connect_device(struct hid_device *hdev) { return 0; }
static inline void hid_bpf_disconnect_device(struct hid_device *hdev) {}
static inline void hid_bpf_destroy_device(struct hid_device *hid) {}
static inline int hid_bpf_device_init(struct hid_device *hid) { return 0; }
static inline const u8 *call_hid_bpf_rdesc_fixup(struct hid_device *hdev, const u8 *rdesc,
						 unsigned int *size) { return rdesc; }

#endif /* CONFIG_HID_BPF */

#endif /* __HID_BPF_H */
