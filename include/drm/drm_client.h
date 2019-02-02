/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _DRM_CLIENT_H_
#define _DRM_CLIENT_H_

#include <linux/types.h>

struct drm_client_dev;
struct drm_device;
struct drm_file;
struct drm_framebuffer;
struct drm_gem_object;
struct drm_minor;
struct module;

/**
 * struct drm_client_funcs - DRM client callbacks
 */
struct drm_client_funcs {
	/**
	 * @owner: The module owner
	 */
	struct module *owner;

	/**
	 * @unregister:
	 *
	 * Called when &drm_device is unregistered. The client should respond by
	 * releasing its resources using drm_client_release().
	 *
	 * This callback is optional.
	 */
	void (*unregister)(struct drm_client_dev *client);

	/**
	 * @restore:
	 *
	 * Called on drm_lastclose(). The first client instance in the list that
	 * returns zero gets the privilege to restore and no more clients are
	 * called. This callback is not called after @unregister has been called.
	 *
	 * This callback is optional.
	 */
	int (*restore)(struct drm_client_dev *client);

	/**
	 * @hotplug:
	 *
	 * Called on drm_kms_helper_hotplug_event().
	 * This callback is not called after @unregister has been called.
	 *
	 * This callback is optional.
	 */
	int (*hotplug)(struct drm_client_dev *client);
};

/**
 * struct drm_client_dev - DRM client instance
 */
struct drm_client_dev {
	/**
	 * @dev: DRM device
	 */
	struct drm_device *dev;

	/**
	 * @name: Name of the client.
	 */
	const char *name;

	/**
	 * @list:
	 *
	 * List of all clients of a DRM device, linked into
	 * &drm_device.clientlist. Protected by &drm_device.clientlist_mutex.
	 */
	struct list_head list;

	/**
	 * @funcs: DRM client functions (optional)
	 */
	const struct drm_client_funcs *funcs;

	/**
	 * @file: DRM file
	 */
	struct drm_file *file;
};

int drm_client_init(struct drm_device *dev, struct drm_client_dev *client,
		    const char *name, const struct drm_client_funcs *funcs);
void drm_client_release(struct drm_client_dev *client);
void drm_client_add(struct drm_client_dev *client);

void drm_client_dev_unregister(struct drm_device *dev);
void drm_client_dev_hotplug(struct drm_device *dev);
void drm_client_dev_restore(struct drm_device *dev);

/**
 * struct drm_client_buffer - DRM client buffer
 */
struct drm_client_buffer {
	/**
	 * @client: DRM client
	 */
	struct drm_client_dev *client;

	/**
	 * @handle: Buffer handle
	 */
	u32 handle;

	/**
	 * @pitch: Buffer pitch
	 */
	u32 pitch;

	/**
	 * @gem: GEM object backing this buffer
	 */
	struct drm_gem_object *gem;

	/**
	 * @vaddr: Virtual address for the buffer
	 */
	void *vaddr;

	/**
	 * @fb: DRM framebuffer
	 */
	struct drm_framebuffer *fb;
};

struct drm_client_buffer *
drm_client_framebuffer_create(struct drm_client_dev *client, u32 width, u32 height, u32 format);
void drm_client_framebuffer_delete(struct drm_client_buffer *buffer);

int drm_client_debugfs_init(struct drm_minor *minor);

#endif
