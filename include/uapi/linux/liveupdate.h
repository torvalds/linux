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

/* ioctl commands for session file descriptors */
enum {
	LIVEUPDATE_CMD_SESSION_BASE = 0x40,
	LIVEUPDATE_CMD_SESSION_PRESERVE_FD = LIVEUPDATE_CMD_SESSION_BASE,
	LIVEUPDATE_CMD_SESSION_RETRIEVE_FD = 0x41,
	LIVEUPDATE_CMD_SESSION_FINISH = 0x42,
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

/* Session specific IOCTLs */

/**
 * struct liveupdate_session_preserve_fd - ioctl(LIVEUPDATE_SESSION_PRESERVE_FD)
 * @size:  Input; sizeof(struct liveupdate_session_preserve_fd)
 * @fd:    Input; The user-space file descriptor to be preserved.
 * @token: Input; An opaque, unique token for preserved resource.
 *
 * Holds parameters for preserving a file descriptor.
 *
 * User sets the @fd field identifying the file descriptor to preserve
 * (e.g., memfd, kvm, iommufd, VFIO). The kernel validates if this FD type
 * and its dependencies are supported for preservation. If validation passes,
 * the kernel marks the FD internally and *initiates the process* of preparing
 * its state for saving. The actual snapshotting of the state typically occurs
 * during the subsequent %LIVEUPDATE_IOCTL_PREPARE execution phase, though
 * some finalization might occur during freeze.
 * On successful validation and initiation, the kernel uses the @token
 * field with an opaque identifier representing the resource being preserved.
 * This token confirms the FD is targeted for preservation and is required for
 * the subsequent %LIVEUPDATE_SESSION_RETRIEVE_FD call after the live update.
 *
 * Return: 0 on success (validation passed, preservation initiated), negative
 * error code on failure (e.g., unsupported FD type, dependency issue,
 * validation failed).
 */
struct liveupdate_session_preserve_fd {
	__u32		size;
	__s32		fd;
	__aligned_u64	token;
};

#define LIVEUPDATE_SESSION_PRESERVE_FD					\
	_IO(LIVEUPDATE_IOCTL_TYPE, LIVEUPDATE_CMD_SESSION_PRESERVE_FD)

/**
 * struct liveupdate_session_retrieve_fd - ioctl(LIVEUPDATE_SESSION_RETRIEVE_FD)
 * @size:  Input; sizeof(struct liveupdate_session_retrieve_fd)
 * @fd:    Output; The new file descriptor representing the fully restored
 *         kernel resource.
 * @token: Input; An opaque, token that was used to preserve the resource.
 *
 * Retrieve a previously preserved file descriptor.
 *
 * User sets the @token field to the value obtained from a successful
 * %LIVEUPDATE_IOCTL_FD_PRESERVE call before the live update. On success,
 * the kernel restores the state (saved during the PREPARE/FREEZE phases)
 * associated with the token and populates the @fd field with a new file
 * descriptor referencing the restored resource in the current (new) kernel.
 * This operation must be performed *before* signaling completion via
 * %LIVEUPDATE_IOCTL_FINISH.
 *
 * Return: 0 on success, negative error code on failure (e.g., invalid token).
 */
struct liveupdate_session_retrieve_fd {
	__u32		size;
	__s32		fd;
	__aligned_u64	token;
};

#define LIVEUPDATE_SESSION_RETRIEVE_FD					\
	_IO(LIVEUPDATE_IOCTL_TYPE, LIVEUPDATE_CMD_SESSION_RETRIEVE_FD)

/**
 * struct liveupdate_session_finish - ioctl(LIVEUPDATE_SESSION_FINISH)
 * @size:     Input; sizeof(struct liveupdate_session_finish)
 * @reserved: Input; Must be zero. Reserved for future use.
 *
 * Signals the completion of the restoration process for a retrieved session.
 * This is the final operation that should be performed on a session file
 * descriptor after a live update.
 *
 * This ioctl must be called once all required file descriptors for the session
 * have been successfully retrieved (using %LIVEUPDATE_SESSION_RETRIEVE_FD) and
 * are fully restored from the userspace and kernel perspective.
 *
 * Upon success, the kernel releases its ownership of the preserved resources
 * associated with this session. This allows internal resources to be freed,
 * typically by decrementing reference counts on the underlying preserved
 * objects.
 *
 * If this operation fails, the resources remain preserved in memory. Userspace
 * may attempt to call finish again. The resources will otherwise be reset
 * during the next live update cycle.
 *
 * Return: 0 on success, negative error code on failure.
 */
struct liveupdate_session_finish {
	__u32		size;
	__u32		reserved;
};

#define LIVEUPDATE_SESSION_FINISH					\
	_IO(LIVEUPDATE_IOCTL_TYPE, LIVEUPDATE_CMD_SESSION_FINISH)

#endif /* _UAPI_LIVEUPDATE_H */
