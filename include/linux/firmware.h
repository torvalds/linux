/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FIRMWARE_H
#define _LINUX_FIRMWARE_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/gfp.h>

#define FW_ACTION_NOUEVENT 0
#define FW_ACTION_UEVENT 1

struct firmware {
	size_t size;
	const u8 *data;

	/* firmware loader private fields */
	void *priv;
};

struct module;
struct device;

/*
 * Built-in firmware functionality is only available if FW_LOADER=y, but not
 * FW_LOADER=m
 */
#ifdef CONFIG_FW_LOADER
bool firmware_request_builtin(struct firmware *fw, const char *name);
#else
static inline bool firmware_request_builtin(struct firmware *fw,
					    const char *name)
{
	return false;
}
#endif

#if IS_REACHABLE(CONFIG_FW_LOADER)
int request_firmware(const struct firmware **fw, const char *name,
		     struct device *device);
int firmware_request_nowarn(const struct firmware **fw, const char *name,
			    struct device *device);
int firmware_request_platform(const struct firmware **fw, const char *name,
			      struct device *device);
int request_firmware_nowait(
	struct module *module, bool uevent,
	const char *name, struct device *device, gfp_t gfp, void *context,
	void (*cont)(const struct firmware *fw, void *context));
int request_firmware_direct(const struct firmware **fw, const char *name,
			    struct device *device);
int request_firmware_into_buf(const struct firmware **firmware_p,
	const char *name, struct device *device, void *buf, size_t size);
int request_partial_firmware_into_buf(const struct firmware **firmware_p,
				      const char *name, struct device *device,
				      void *buf, size_t size, size_t offset);

void release_firmware(const struct firmware *fw);
#else
static inline int request_firmware(const struct firmware **fw,
				   const char *name,
				   struct device *device)
{
	return -EINVAL;
}

static inline int firmware_request_nowarn(const struct firmware **fw,
					  const char *name,
					  struct device *device)
{
	return -EINVAL;
}

static inline int firmware_request_platform(const struct firmware **fw,
					    const char *name,
					    struct device *device)
{
	return -EINVAL;
}

static inline int request_firmware_nowait(
	struct module *module, bool uevent,
	const char *name, struct device *device, gfp_t gfp, void *context,
	void (*cont)(const struct firmware *fw, void *context))
{
	return -EINVAL;
}

static inline void release_firmware(const struct firmware *fw)
{
}

static inline int request_firmware_direct(const struct firmware **fw,
					  const char *name,
					  struct device *device)
{
	return -EINVAL;
}

static inline int request_firmware_into_buf(const struct firmware **firmware_p,
	const char *name, struct device *device, void *buf, size_t size)
{
	return -EINVAL;
}

static inline int request_partial_firmware_into_buf
					(const struct firmware **firmware_p,
					 const char *name,
					 struct device *device,
					 void *buf, size_t size, size_t offset)
{
	return -EINVAL;
}

#endif

int firmware_request_cache(struct device *device, const char *name);

#endif
