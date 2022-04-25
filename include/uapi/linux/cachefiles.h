/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_CACHEFILES_H
#define _LINUX_CACHEFILES_H

#include <linux/types.h>

/*
 * Fscache ensures that the maximum length of cookie key is 255. The volume key
 * is controlled by netfs, and generally no bigger than 255.
 */
#define CACHEFILES_MSG_MAX_SIZE	1024

enum cachefiles_opcode {
	CACHEFILES_OP_OPEN,
};

/*
 * Message Header
 *
 * @msg_id	a unique ID identifying this message
 * @opcode	message type, CACHEFILE_OP_*
 * @len		message length, including message header and following data
 * @object_id	a unique ID identifying a cache file
 * @data	message type specific payload
 */
struct cachefiles_msg {
	__u32 msg_id;
	__u32 opcode;
	__u32 len;
	__u32 object_id;
	__u8  data[];
};

/*
 * @data contains the volume_key followed directly by the cookie_key. volume_key
 * is a NUL-terminated string; @volume_key_size indicates the size of the volume
 * key in bytes. cookie_key is binary data, which is netfs specific;
 * @cookie_key_size indicates the size of the cookie key in bytes.
 *
 * @fd identifies an anon_fd referring to the cache file.
 */
struct cachefiles_open {
	__u32 volume_key_size;
	__u32 cookie_key_size;
	__u32 fd;
	__u32 flags;
	__u8  data[];
};

#endif
