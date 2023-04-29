/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gunyah

#if !defined(_TRACE_GUNYAH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GUNYAH_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/trace_seq.h>
#include <soc/qcom/secure_buffer.h>

#ifndef __GUNYAH_HELPER_FUNCTIONS
#define __GUNYAH_HELPER_FUNCTIONS

#define MAX_ENTRIES_TO_PRINT 4

enum {
	DONATE = 0,
	LEND = 1,
	SHARE = 2
};

static inline const char *__print_acl_arr(struct trace_seq *p, u8 *acl_perms, u16 *acl_vmids,
				int count)
{
	const char *ret;
	int i = 0;

	u8 *perms = acl_perms;
	u16 *vmids = acl_vmids;

	ret = trace_seq_buffer_ptr(p);

	trace_seq_putc(p, '{');

	for (i = 0; i < count; i++) {

		trace_seq_printf(p, "(0x%x,", *vmids);
		trace_seq_printf(p, "%s%s%s)",
			((*perms & 0x4) ? "R" : ""),
			((*perms & 0x2) ? "W" : ""),
			((*perms & 0x1) ? "X" : "")
			);

		perms++;
		vmids++;

		if (i != count-1)
			trace_seq_printf(p, ", ");
	}

	trace_seq_putc(p, '}');
	trace_seq_putc(p, 0);

	return ret;
}
#endif

DECLARE_EVENT_CLASS(gh_rm_mem_accept_donate_lend_share,

	TP_PROTO(u8 mem_type, u8 flags, gh_label_t label,
		struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		struct gh_mem_attr_desc *mem_attr_desc,
		gh_memparcel_handle_t *handle, u16 map_vmid, u8 trans_type),

	TP_ARGS(mem_type, flags, label,
		acl_desc, sgl_desc,
		mem_attr_desc,
		handle, map_vmid, trans_type),

	TP_STRUCT__entry(
		__field(u8, mem_type)
		__field(u8, flags)
		__field(gh_label_t, label)

		/* gh_acl_desc */
		__field(u32, n_acl_entries)

		__dynamic_array(u16, acl_vmid_arr,
			((acl_desc != NULL)	? acl_desc->n_acl_entries : 0))
		__dynamic_array(u8, acl_perm_arr,
			((acl_desc != NULL)	? acl_desc->n_acl_entries : 0))

		/* gh_sgl_desc */
		__field(u16, n_sgl_entries)
		__dynamic_array(u64, sgl_ipa_base_arr,
			((sgl_desc != NULL)	? (sgl_desc->n_sgl_entries > MAX_ENTRIES_TO_PRINT
							? MAX_ENTRIES_TO_PRINT
							: sgl_desc->n_sgl_entries)
						: 0))
		__dynamic_array(u64, sgl_size_arr,
			((sgl_desc != NULL)	? (sgl_desc->n_sgl_entries > MAX_ENTRIES_TO_PRINT
							? MAX_ENTRIES_TO_PRINT
							: sgl_desc->n_sgl_entries)
						: 0))

		/* mem_attr_desc */
		__field(u16, n_mem_attr_entries)
		__dynamic_array(u16, mem_attr_attr_arr,
			((mem_attr_desc != NULL)
				? mem_attr_desc->n_mem_attr_entries : 0))
		__dynamic_array(u16, mem_attr_vmid_arr,
			((mem_attr_desc != NULL)
				? mem_attr_desc->n_mem_attr_entries : 0))

		__field(gh_memparcel_handle_t, handle)
		__field(u16, map_vmid)
		__field(u8, trans_type)

		__field(int, sgl_entries_to_print)
	),

	TP_fast_assign(

		unsigned int i;

		/* gh_acl_desc */
		u16 *acl_vmids_arr_ptr = __get_dynamic_array(acl_vmid_arr);
		u8 *acl_perms_arr_ptr = __get_dynamic_array(acl_perm_arr);

		/* gh_sgl_desc */
		u64 *sgl_ipa_base_arr_ptr = __get_dynamic_array(sgl_ipa_base_arr);
		u64 *sgl_size_arr_ptr = __get_dynamic_array(sgl_size_arr);

		/* mem_attr_desc */
		u16 *mem_attr_attr_arr_ptr = __get_dynamic_array(mem_attr_attr_arr);
		u16 *mem_attr_vmid_arr_ptr = __get_dynamic_array(mem_attr_vmid_arr);

		__entry->mem_type = mem_type;
		__entry->flags = flags;
		__entry->label = label;

		/* gh_acl_desc */
		if (acl_desc != NULL) {
			__entry->n_acl_entries	= acl_desc->n_acl_entries;

			for (i = 0; i < __entry->n_acl_entries; i++) {
				acl_vmids_arr_ptr[i] = acl_desc->acl_entries[i].vmid;
				acl_perms_arr_ptr[i] = acl_desc->acl_entries[i].perms;
			}
		} else {
			__entry->n_acl_entries	= 0;
		}

		/* gh_sgl_desc */
		if (sgl_desc != NULL) {
			__entry->n_sgl_entries	= sgl_desc->n_sgl_entries;

			__entry->sgl_entries_to_print =
					__entry->n_sgl_entries > MAX_ENTRIES_TO_PRINT
							? MAX_ENTRIES_TO_PRINT
							: __entry->n_sgl_entries;

			for (i = 0; i < __entry->sgl_entries_to_print; i++) {
				sgl_ipa_base_arr_ptr[i] = sgl_desc->sgl_entries[i].ipa_base;
				sgl_size_arr_ptr[i] = sgl_desc->sgl_entries[i].size;
			}

		} else {
			__entry->n_sgl_entries = 0;
			__entry->sgl_entries_to_print = 0;
		}

		/* mem_attr_desc */
		if (mem_attr_desc != NULL) {
			__entry->n_mem_attr_entries = mem_attr_desc->n_mem_attr_entries;

			for (i = 0; i < __entry->n_mem_attr_entries; i++) {
				mem_attr_attr_arr_ptr[i] = mem_attr_desc->attr_entries[i].attr;
				mem_attr_vmid_arr_ptr[i] = mem_attr_desc->attr_entries[i].vmid;
			}
		} else {
			__entry->n_mem_attr_entries = 0;
		}

		__entry->handle = *handle;

		__entry->map_vmid = map_vmid;
		__entry->trans_type = trans_type;

	),

	TP_printk("mem_type = %s flags = 0x%x label = %u\t\t"
		"acl_entries = %u acl_arr = %s\t\t"
		"sgl_entries = %u sgl_ipa_base = %s sgl_size = %s\t\t"
		"mem_attr_entries = %u mem_attr_attr = %s mem_attr_vmid = %s\t\t"
		"handle = %u map_vmid = 0x%x trans_type = %s",
		__print_symbolic(__entry->mem_type,
			{ 0, "Normal Memory" },
			{ 1, "IO Memory" }),
		__entry->flags,
		__entry->label,
		__entry->n_acl_entries,
		(__entry->n_acl_entries
			? __print_acl_arr(p, __get_dynamic_array(acl_perm_arr),
				__get_dynamic_array(acl_vmid_arr), __entry->n_acl_entries)
			: "N/A"),
		__entry->n_sgl_entries,
		(__entry->n_sgl_entries
			? __print_array(__get_dynamic_array(sgl_ipa_base_arr),
				__entry->sgl_entries_to_print, sizeof(u64))
			: "N/A"),
		(__entry->n_sgl_entries
			? __print_array(__get_dynamic_array(sgl_size_arr),
				__entry->sgl_entries_to_print, sizeof(u64))
			: "N/A"),
		__entry->n_mem_attr_entries,
		(__entry->n_mem_attr_entries
			? __print_array(__get_dynamic_array(mem_attr_attr_arr),
				__entry->n_mem_attr_entries, sizeof(u16))
			: "N/A"),
		(__entry->n_mem_attr_entries
			? __print_array(__get_dynamic_array(mem_attr_vmid_arr),
				__entry->n_mem_attr_entries, sizeof(u16))
			: "N/A"),
		__entry->handle, __entry->map_vmid,
		__print_symbolic(__entry->trans_type,
			{ 0, "Donate" },
			{ 1, "Lend" },
			{ 2, "Share" })
		)
);

DEFINE_EVENT(gh_rm_mem_accept_donate_lend_share, gh_rm_mem_accept,

	TP_PROTO(u8 mem_type, u8 flags, gh_label_t label,
		struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		struct gh_mem_attr_desc *mem_attr_desc,
		gh_memparcel_handle_t *handle, u16 map_vmid, u8 trans_type),

	TP_ARGS(mem_type, flags, label,
		acl_desc, sgl_desc,
		mem_attr_desc,
		handle, map_vmid, trans_type)
);

DEFINE_EVENT(gh_rm_mem_accept_donate_lend_share, gh_rm_mem_donate,

	TP_PROTO(u8 mem_type, u8 flags, gh_label_t label,
		struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		struct gh_mem_attr_desc *mem_attr_desc,
		gh_memparcel_handle_t *handle, u16 map_vmid, u8 trans_type),

	TP_ARGS(mem_type, flags, label,
		acl_desc, sgl_desc,
		mem_attr_desc,
		handle, map_vmid, trans_type)
);

DEFINE_EVENT(gh_rm_mem_accept_donate_lend_share, gh_rm_mem_lend,

	TP_PROTO(u8 mem_type, u8 flags, gh_label_t label,
		struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		struct gh_mem_attr_desc *mem_attr_desc,
		gh_memparcel_handle_t *handle, u16 map_vmid, u8 trans_type),

	TP_ARGS(mem_type, flags, label,
		acl_desc, sgl_desc,
		mem_attr_desc,
		handle, map_vmid, trans_type)
);

DEFINE_EVENT(gh_rm_mem_accept_donate_lend_share, gh_rm_mem_share,

	TP_PROTO(u8 mem_type, u8 flags, gh_label_t label,
		struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		struct gh_mem_attr_desc *mem_attr_desc,
		gh_memparcel_handle_t *handle, u16 map_vmid, u8 trans_type),

	TP_ARGS(mem_type, flags, label,
		acl_desc, sgl_desc,
		mem_attr_desc,
		handle, map_vmid, trans_type)
);

TRACE_EVENT(gh_rm_mem_accept_reply,

	TP_PROTO(struct gh_sgl_desc *sgl_desc),

	TP_ARGS(sgl_desc),

	TP_STRUCT__entry(

		__field(u16, n_sgl_entries)

		__dynamic_array(u64, sgl_ipa_base_arr,
				((sgl_desc != NULL)
					? (sgl_desc->n_sgl_entries > MAX_ENTRIES_TO_PRINT
							? MAX_ENTRIES_TO_PRINT
							: sgl_desc->n_sgl_entries)
					: 0))
		__dynamic_array(u64, sgl_size_arr,
				((sgl_desc != NULL)
					? (sgl_desc->n_sgl_entries > MAX_ENTRIES_TO_PRINT
							? MAX_ENTRIES_TO_PRINT
							: sgl_desc->n_sgl_entries)
					: 0))
		__field(int, sgl_entries_to_print)
		__field(bool, is_error)
	),

	TP_fast_assign(

		unsigned int i;

		u64 *sgl_ipa_base_arr_ptr = __get_dynamic_array(sgl_ipa_base_arr);
		u64 *sgl_size_arr_ptr = __get_dynamic_array(sgl_size_arr);

		__entry->is_error = IS_ERR(sgl_desc);

		if (sgl_desc != NULL && __entry->is_error == false) {
			__entry->n_sgl_entries	= sgl_desc->n_sgl_entries;

			__entry->sgl_entries_to_print =
				__entry->n_sgl_entries > MAX_ENTRIES_TO_PRINT
						? MAX_ENTRIES_TO_PRINT
						: __entry->n_sgl_entries;

			for (i = 0; i < __entry->sgl_entries_to_print; i++) {
				sgl_ipa_base_arr_ptr[i] = sgl_desc->sgl_entries[i].ipa_base;
				sgl_size_arr_ptr[i] = sgl_desc->sgl_entries[i].size;
			}

		} else {
			__entry->n_sgl_entries = 0;
			__entry->sgl_entries_to_print = 0;
		}

	),

	TP_printk("sgl_entries = %u sgl_ipa_base = %s sgl_size = %s\t\t",
		__entry->n_sgl_entries,
		((__entry->n_sgl_entries && __entry->is_error == false)
				? __print_array(__get_dynamic_array(sgl_ipa_base_arr),
						__entry->sgl_entries_to_print, sizeof(u64))
				: "N/A"),
		((__entry->n_sgl_entries && __entry->is_error == false)
				? __print_array(__get_dynamic_array(sgl_size_arr),
						__entry->sgl_entries_to_print, sizeof(u64))
				: "N/A")
		)
);

DECLARE_EVENT_CLASS(gh_rm_mem_release_reclaim,

	TP_PROTO(gh_memparcel_handle_t handle, u8 flags),

	TP_ARGS(handle, flags),

	TP_STRUCT__entry(
		__field(gh_memparcel_handle_t, handle)
		__field(u8, flags)
	),

	TP_fast_assign(
		__entry->handle	= handle;
		__entry->flags = flags;
	),

	TP_printk("handle_s = %u flags = 0x%x",
		__entry->handle,
		__entry->flags
		)
);

DEFINE_EVENT(gh_rm_mem_release_reclaim, gh_rm_mem_release,

	TP_PROTO(gh_memparcel_handle_t handle, u8 flags),

	TP_ARGS(handle, flags)
);


DEFINE_EVENT(gh_rm_mem_release_reclaim, gh_rm_mem_reclaim,

	TP_PROTO(gh_memparcel_handle_t handle, u8 flags),

	TP_ARGS(handle, flags)
);

TRACE_EVENT(gh_rm_mem_call_return,

	TP_PROTO(gh_memparcel_handle_t handle, int return_val),

	TP_ARGS(handle, return_val),

	TP_STRUCT__entry(
		__field(gh_memparcel_handle_t, handle)
		__field(int, return_val)
	),

	TP_fast_assign(
		__entry->handle	= handle;
		__entry->return_val	= return_val;

	),

	TP_printk("handle = %u, return_value = %d", __entry->handle, __entry->return_val)
);

TRACE_EVENT(gh_rm_mem_notify,

	TP_PROTO(gh_memparcel_handle_t handle, u8 flags, gh_label_t mem_info_tag,
		struct gh_notify_vmid_desc *vmid_desc),

	TP_ARGS(handle, flags, mem_info_tag, vmid_desc),

	TP_STRUCT__entry(
		__field(gh_memparcel_handle_t, handle)
		__field(u8, flags)
		__field(gh_label_t, mem_info_tag)

		__field(u16, n_vmid_entries)
		__dynamic_array(u16, entry_vmid_arr,
			((vmid_desc != NULL) ? vmid_desc->n_vmid_entries : 0))
	),

	TP_fast_assign(

		unsigned int i;

		/* vmid_desc */
		u16 *entry_vmid_arr_ptr = __get_dynamic_array(entry_vmid_arr);

		__entry->handle = handle;
		__entry->flags = flags;
		__entry->mem_info_tag = mem_info_tag;

		if (vmid_desc != NULL) {
			__entry->n_vmid_entries	= vmid_desc->n_vmid_entries;

			for (i = 0; i < __entry->n_vmid_entries; i++)
				entry_vmid_arr_ptr[i] = vmid_desc->vmid_entries[i].vmid;

		} else {
			__entry->n_vmid_entries	= 0;
		}

	),

	TP_printk("handle = %u flags = 0x%x mem_info_tag = %u\t\t"
		"vmid_entries = %u entry_vmid_arr = %s",
		__entry->handle,
		__entry->flags,
		__entry->mem_info_tag,
		__entry->n_vmid_entries,
		(__entry->n_vmid_entries
			? __print_array(__get_dynamic_array(entry_vmid_arr),
					__entry->n_vmid_entries, sizeof(u16))
			: "N/A")
		)
);


#endif /* _TRACE_GUNYAH_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
