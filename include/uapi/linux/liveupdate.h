/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

/*
 * Userspace interface for /dev/liveupdate
 * Live Update Orchestrator
 *
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#ifndef _UAPI_LIVEUPDATE_H
#define _UAPI_LIVEUPDATE_H

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * DOC: General ioctl format
 *
 * The ioctl interface follows a general format to allow for extensibility. Each
 * ioctl is passed in a structure pointer as the argument providing the size of
 * the structure in the first u32. The kernel checks that any structure space
 * beyond what it understands is 0. This allows userspace to use the backward
 * compatible portion while consistently using the newer, larger, structures.
 *
 * ioctls use a standard meaning for common errnos:
 *
 *  - ENOTTY: The IOCTL number itself is not supported at all
 *  - E2BIG: The IOCTL number is supported, but the provided structure has
 *    non-zero in a part the kernel does not understand.
 *  - EOPNOTSUPP: The IOCTL number is supported, and the structure is
 *    understood, however a known field has a value the kernel does not
 *    understand or support.
 *  - EINVAL: Everything about the IOCTL was understood, but a field is not
 *    correct.
 *  - ENOENT: A provided token does not exist.
 *  - ENOMEM: Out of memory.
 *  - EOVERFLOW: Mathematics overflowed.
 *
 * As well as additional errnos, within specific ioctls.
 */

/* The ioctl type, documented in ioctl-number.rst */
#define LIVEUPDATE_IOCTL_TYPE		0xBA

/* The maximum length of session name including null termination */
#define LIVEUPDATE_SESSION_NAME_LENGTH 64

/* The /dev/liveupdate ioctl commands */
enum {
	LIVEUPDATE_CMD_BASE = 0x00,
	LIVEUPDATE_CMD_CREATE_SESSION = LIVEUPDATE_CMD_BASE,
	LIVEUPDATE_CMD_RETRIEVE_SESSION = 0x01,
};

/**
 * struct liveupdate_ioctl_create_session - ioctl(LIVEUPDATE_IOCTL_CREATE_SESSION)
 * @size:	Input; sizeof(struct liveupdate_ioctl_create_session)
 * @fd:		Output; The new file descriptor for the created session.
 * @name:	Input; A null-terminated string for the session name, max
 *		length %LIVEUPDATE_SESSION_NAME_LENGTH including termination
 *		character.
 *
 * Creates a new live update session for managing preserved resources.
 * This ioctl can only be called on the main /dev/liveupdate device.
 *
 * Return: 0 on success, negative error code on failure.
 */
struct liveupdate_ioctl_create_session {
	__u32		size;
	__s32		fd;
	__u8		name[LIVEUPDATE_SESSION_NAME_LENGTH];
};

#define LIVEUPDATE_IOCTL_CREATE_SESSION					\
	_IO(LIVEUPDATE_IOCTL_TYPE, LIVEUPDATE_CMD_CREATE_SESSION)

/**
 * struct liveupdate_ioctl_retrieve_session - ioctl(LIVEUPDATE_IOCTL_RETRIEVE_SESSION)
 * @size:    Input; sizeof(struct liveupdate_ioctl_retrieve_session)
 * @fd:      Output; The new file descriptor for the retrieved session.
 * @name:    Input; A null-terminated string identifying the session to retrieve.
 *           The name must exactly match the name used when the session was
 *           created in the previous kernel.
 *
 * Retrieves a handle (a new file descriptor) for a preserved session by its
 * name. This is the primary mechanism for a userspace agent to regain control
 * of its preserved resources after a live update.
 *
 * The userspace application provides the null-terminated `name` of a session
 * it created before the live update. If a preserved session with a matching
 * name is found, the kernel instantiates it and returns a new file descriptor
 * in the `fd` field. This new session FD can then be used for all file-specific
 * operations, such as restoring individual file descriptors with
 * LIVEUPDATE_SESSION_RETRIEVE_FD.
 *
 * It is the responsibility of the userspace application to know the names of
 * the sessions it needs to retrieve. If no session with the given name is
 * found, the ioctl will fail with -ENOENT.
 *
 * This ioctl can only be called on the main /dev/liveupdate device when the
 * system is in the LIVEUPDATE_STATE_UPDATED state.
 */
struct liveupdate_ioctl_retrieve_session {
	__u32		size;
	__s32		fd;
	__u8		name[LIVEUPDATE_SESSION_NAME_LENGTH];
};

#define LIVEUPDATE_IOCTL_RETRIEVE_SESSION \
	_IO(LIVEUPDATE_IOCTL_TYPE, LIVEUPDATE_CMD_RETRIEVE_SESSION)

#endif /* _UAPI_LIVEUPDATE_H */
