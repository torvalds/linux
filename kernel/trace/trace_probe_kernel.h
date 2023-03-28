/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRACE_PROBE_KERNEL_H_
#define __TRACE_PROBE_KERNEL_H_

#define FAULT_STRING "(fault)"

/*
 * This depends on trace_probe.h, but can not include it due to
 * the way trace_probe_tmpl.h is used by trace_kprobe.c and trace_eprobe.c.
 * Which means that any other user must include trace_probe.h before including
 * this file.
 */
/* Return the length of string -- including null terminal byte */
static nokprobe_inline int
fetch_store_strlen_user(unsigned long addr)
{
	const void __user *uaddr =  (__force const void __user *)addr;
	int ret;

	ret = strnlen_user_nofault(uaddr, MAX_STRING_SIZE);
	/*
	 * strnlen_user_nofault returns zero on fault, insert the
	 * FAULT_STRING when that occurs.
	 */
	if (ret <= 0)
		return strlen(FAULT_STRING) + 1;
	return ret;
}

/* Return the length of string -- including null terminal byte */
static nokprobe_inline int
fetch_store_strlen(unsigned long addr)
{
	int ret, len = 0;
	u8 c;

#ifdef CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE
	if (addr < TASK_SIZE)
		return fetch_store_strlen_user(addr);
#endif

	do {
		ret = copy_from_kernel_nofault(&c, (u8 *)addr + len, 1);
		len++;
	} while (c && ret == 0 && len < MAX_STRING_SIZE);

	/* For faults, return enough to hold the FAULT_STRING */
	return (ret < 0) ? strlen(FAULT_STRING) + 1 : len;
}

static nokprobe_inline void set_data_loc(int ret, void *dest, void *__dest, void *base, int len)
{
	if (ret >= 0) {
		*(u32 *)dest = make_data_loc(ret, __dest - base);
	} else {
		strscpy(__dest, FAULT_STRING, len);
		ret = strlen(__dest) + 1;
	}
}

/*
 * Fetch a null-terminated string from user. Caller MUST set *(u32 *)buf
 * with max length and relative data location.
 */
static nokprobe_inline int
fetch_store_string_user(unsigned long addr, void *dest, void *base)
{
	const void __user *uaddr =  (__force const void __user *)addr;
	int maxlen = get_loc_len(*(u32 *)dest);
	void *__dest;
	long ret;

	if (unlikely(!maxlen))
		return -ENOMEM;

	__dest = get_loc_data(dest, base);

	ret = strncpy_from_user_nofault(__dest, uaddr, maxlen);
	set_data_loc(ret, dest, __dest, base, maxlen);

	return ret;
}

/*
 * Fetch a null-terminated string. Caller MUST set *(u32 *)buf with max
 * length and relative data location.
 */
static nokprobe_inline int
fetch_store_string(unsigned long addr, void *dest, void *base)
{
	int maxlen = get_loc_len(*(u32 *)dest);
	void *__dest;
	long ret;

#ifdef CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE
	if ((unsigned long)addr < TASK_SIZE)
		return fetch_store_string_user(addr, dest, base);
#endif

	if (unlikely(!maxlen))
		return -ENOMEM;

	__dest = get_loc_data(dest, base);

	/*
	 * Try to get string again, since the string can be changed while
	 * probing.
	 */
	ret = strncpy_from_kernel_nofault(__dest, (void *)addr, maxlen);
	set_data_loc(ret, dest, __dest, base, maxlen);

	return ret;
}

static nokprobe_inline int
probe_mem_read_user(void *dest, void *src, size_t size)
{
	const void __user *uaddr =  (__force const void __user *)src;

	return copy_from_user_nofault(dest, uaddr, size);
}

static nokprobe_inline int
probe_mem_read(void *dest, void *src, size_t size)
{
#ifdef CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE
	if ((unsigned long)src < TASK_SIZE)
		return probe_mem_read_user(dest, src, size);
#endif
	return copy_from_kernel_nofault(dest, src, size);
}

#endif /* __TRACE_PROBE_KERNEL_H_ */
