/*
 * This module provides an interface to trigger and test firmware loading.
 *
 * It is designed to be used for basic evaluation of the firmware loading
 * subsystem (for example when validating firmware verification). It lacks
 * any extra dependencies, and will not normally be loaded by the system
 * unless explicitly requested by name.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/completion.h>
#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>

#define TEST_FIRMWARE_NAME	"test-firmware.bin"
#define TEST_FIRMWARE_NUM_REQS	4

static DEFINE_MUTEX(test_fw_mutex);
static const struct firmware *test_firmware;

struct test_batched_req {
	u8 idx;
	int rc;
	bool sent;
	const struct firmware *fw;
	const char *name;
	struct completion completion;
	struct task_struct *task;
	struct device *dev;
};

/**
 * test_config - represents configuration for the test for different triggers
 *
 * @name: the name of the firmware file to look for
 * @sync_direct: when the sync trigger is used if this is true
 *	request_firmware_direct() will be used instead.
 * @send_uevent: whether or not to send a uevent for async requests
 * @num_requests: number of requests to try per test case. This is trigger
 *	specific.
 * @reqs: stores all requests information
 * @read_fw_idx: index of thread from which we want to read firmware results
 *	from through the read_fw trigger.
 * @test_result: a test may use this to collect the result from the call
 *	of the request_firmware*() calls used in their tests. In order of
 *	priority we always keep first any setup error. If no setup errors were
 *	found then we move on to the first error encountered while running the
 *	API. Note that for async calls this typically will be a successful
 *	result (0) unless of course you've used bogus parameters, or the system
 *	is out of memory.  In the async case the callback is expected to do a
 *	bit more homework to figure out what happened, unfortunately the only
 *	information passed today on error is the fact that no firmware was
 *	found so we can only assume -ENOENT on async calls if the firmware is
 *	NULL.
 *
 *	Errors you can expect:
 *
 *	API specific:
 *
 *	0:		success for sync, for async it means request was sent
 *	-EINVAL:	invalid parameters or request
 *	-ENOENT:	files not found
 *
 *	System environment:
 *
 *	-ENOMEM:	memory pressure on system
 *	-ENODEV:	out of number of devices to test
 *	-EINVAL:	an unexpected error has occurred
 * @req_firmware: if @sync_direct is true this is set to
 *	request_firmware_direct(), otherwise request_firmware()
 */
struct test_config {
	char *name;
	bool sync_direct;
	bool send_uevent;
	u8 num_requests;
	u8 read_fw_idx;

	/*
	 * These below don't belong her but we'll move them once we create
	 * a struct fw_test_device and stuff the misc_dev under there later.
	 */
	struct test_batched_req *reqs;
	int test_result;
	int (*req_firmware)(const struct firmware **fw, const char *name,
			    struct device *device);
};

static struct test_config *test_fw_config;

static ssize_t test_fw_misc_read(struct file *f, char __user *buf,
				 size_t size, loff_t *offset)
{
	ssize_t rc = 0;

	mutex_lock(&test_fw_mutex);
	if (test_firmware)
		rc = simple_read_from_buffer(buf, size, offset,
					     test_firmware->data,
					     test_firmware->size);
	mutex_unlock(&test_fw_mutex);
	return rc;
}

static const struct file_operations test_fw_fops = {
	.owner          = THIS_MODULE,
	.read           = test_fw_misc_read,
};

static void __test_release_all_firmware(void)
{
	struct test_batched_req *req;
	u8 i;

	if (!test_fw_config->reqs)
		return;

	for (i = 0; i < test_fw_config->num_requests; i++) {
		req = &test_fw_config->reqs[i];
		if (req->fw)
			release_firmware(req->fw);
	}

	vfree(test_fw_config->reqs);
	test_fw_config->reqs = NULL;
}

static void test_release_all_firmware(void)
{
	mutex_lock(&test_fw_mutex);
	__test_release_all_firmware();
	mutex_unlock(&test_fw_mutex);
}


static void __test_firmware_config_free(void)
{
	__test_release_all_firmware();
	kfree_const(test_fw_config->name);
	test_fw_config->name = NULL;
}

/*
 * XXX: move to kstrncpy() once merged.
 *
 * Users should use kfree_const() when freeing these.
 */
static int __kstrncpy(char **dst, const char *name, size_t count, gfp_t gfp)
{
	*dst = kstrndup(name, count, gfp);
	if (!*dst)
		return -ENOSPC;
	return count;
}

static int __test_firmware_config_init(void)
{
	int ret;

	ret = __kstrncpy(&test_fw_config->name, TEST_FIRMWARE_NAME,
			 strlen(TEST_FIRMWARE_NAME), GFP_KERNEL);
	if (ret < 0)
		goto out;

	test_fw_config->num_requests = TEST_FIRMWARE_NUM_REQS;
	test_fw_config->send_uevent = true;
	test_fw_config->sync_direct = false;
	test_fw_config->req_firmware = request_firmware;
	test_fw_config->test_result = 0;
	test_fw_config->reqs = NULL;

	return 0;

out:
	__test_firmware_config_free();
	return ret;
}

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int ret;

	mutex_lock(&test_fw_mutex);

	__test_firmware_config_free();

	ret = __test_firmware_config_init();
	if (ret < 0) {
		ret = -ENOMEM;
		pr_err("could not alloc settings for config trigger: %d\n",
		       ret);
		goto out;
	}

	pr_info("reset\n");
	ret = count;

out:
	mutex_unlock(&test_fw_mutex);

	return ret;
}
static DEVICE_ATTR_WO(reset);

static ssize_t config_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int len = 0;

	mutex_lock(&test_fw_mutex);

	len += snprintf(buf, PAGE_SIZE,
			"Custom trigger configuration for: %s\n",
			dev_name(dev));

	if (test_fw_config->name)
		len += snprintf(buf+len, PAGE_SIZE,
				"name:\t%s\n",
				test_fw_config->name);
	else
		len += snprintf(buf+len, PAGE_SIZE,
				"name:\tEMTPY\n");

	len += snprintf(buf+len, PAGE_SIZE,
			"num_requests:\t%u\n", test_fw_config->num_requests);

	len += snprintf(buf+len, PAGE_SIZE,
			"send_uevent:\t\t%s\n",
			test_fw_config->send_uevent ?
			"FW_ACTION_HOTPLUG" :
			"FW_ACTION_NOHOTPLUG");
	len += snprintf(buf+len, PAGE_SIZE,
			"sync_direct:\t\t%s\n",
			test_fw_config->sync_direct ? "true" : "false");
	len += snprintf(buf+len, PAGE_SIZE,
			"read_fw_idx:\t%u\n", test_fw_config->read_fw_idx);

	mutex_unlock(&test_fw_mutex);

	return len;
}
static DEVICE_ATTR_RO(config);

static ssize_t config_name_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int ret;

	mutex_lock(&test_fw_mutex);
	kfree_const(test_fw_config->name);
	ret = __kstrncpy(&test_fw_config->name, buf, count, GFP_KERNEL);
	mutex_unlock(&test_fw_mutex);

	return ret;
}

/*
 * As per sysfs_kf_seq_show() the buf is max PAGE_SIZE.
 */
static ssize_t config_test_show_str(char *dst,
				    char *src)
{
	int len;

	mutex_lock(&test_fw_mutex);
	len = snprintf(dst, PAGE_SIZE, "%s\n", src);
	mutex_unlock(&test_fw_mutex);

	return len;
}

static int test_dev_config_update_bool(const char *buf, size_t size,
				       bool *cfg)
{
	int ret;

	mutex_lock(&test_fw_mutex);
	if (strtobool(buf, cfg) < 0)
		ret = -EINVAL;
	else
		ret = size;
	mutex_unlock(&test_fw_mutex);

	return ret;
}

static ssize_t
test_dev_config_show_bool(char *buf,
			  bool config)
{
	bool val;

	mutex_lock(&test_fw_mutex);
	val = config;
	mutex_unlock(&test_fw_mutex);

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t test_dev_config_show_int(char *buf, int cfg)
{
	int val;

	mutex_lock(&test_fw_mutex);
	val = cfg;
	mutex_unlock(&test_fw_mutex);

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static int test_dev_config_update_u8(const char *buf, size_t size, u8 *cfg)
{
	int ret;
	long new;

	ret = kstrtol(buf, 10, &new);
	if (ret)
		return ret;

	if (new > U8_MAX)
		return -EINVAL;

	mutex_lock(&test_fw_mutex);
	*(u8 *)cfg = new;
	mutex_unlock(&test_fw_mutex);

	/* Always return full write size even if we didn't consume all */
	return size;
}

static ssize_t test_dev_config_show_u8(char *buf, u8 cfg)
{
	u8 val;

	mutex_lock(&test_fw_mutex);
	val = cfg;
	mutex_unlock(&test_fw_mutex);

	return snprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t config_name_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return config_test_show_str(buf, test_fw_config->name);
}
static DEVICE_ATTR_RW(config_name);

static ssize_t config_num_requests_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	int rc;

	mutex_lock(&test_fw_mutex);
	if (test_fw_config->reqs) {
		pr_err("Must call release_all_firmware prior to changing config\n");
		rc = -EINVAL;
		mutex_unlock(&test_fw_mutex);
		goto out;
	}
	mutex_unlock(&test_fw_mutex);

	rc = test_dev_config_update_u8(buf, count,
				       &test_fw_config->num_requests);

out:
	return rc;
}

static ssize_t config_num_requests_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return test_dev_config_show_u8(buf, test_fw_config->num_requests);
}
static DEVICE_ATTR_RW(config_num_requests);

static ssize_t config_sync_direct_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int rc = test_dev_config_update_bool(buf, count,
					     &test_fw_config->sync_direct);

	if (rc == count)
		test_fw_config->req_firmware = test_fw_config->sync_direct ?
				       request_firmware_direct :
				       request_firmware;
	return rc;
}

static ssize_t config_sync_direct_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	return test_dev_config_show_bool(buf, test_fw_config->sync_direct);
}
static DEVICE_ATTR_RW(config_sync_direct);

static ssize_t config_send_uevent_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	return test_dev_config_update_bool(buf, count,
					   &test_fw_config->send_uevent);
}

static ssize_t config_send_uevent_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	return test_dev_config_show_bool(buf, test_fw_config->send_uevent);
}
static DEVICE_ATTR_RW(config_send_uevent);

static ssize_t config_read_fw_idx_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	return test_dev_config_update_u8(buf, count,
					 &test_fw_config->read_fw_idx);
}

static ssize_t config_read_fw_idx_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	return test_dev_config_show_u8(buf, test_fw_config->read_fw_idx);
}
static DEVICE_ATTR_RW(config_read_fw_idx);


static ssize_t trigger_request_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int rc;
	char *name;

	name = kstrndup(buf, count, GFP_KERNEL);
	if (!name)
		return -ENOSPC;

	pr_info("loading '%s'\n", name);

	mutex_lock(&test_fw_mutex);
	release_firmware(test_firmware);
	test_firmware = NULL;
	rc = request_firmware(&test_firmware, name, dev);
	if (rc) {
		pr_info("load of '%s' failed: %d\n", name, rc);
		goto out;
	}
	pr_info("loaded: %zu\n", test_firmware->size);
	rc = count;

out:
	mutex_unlock(&test_fw_mutex);

	kfree(name);

	return rc;
}
static DEVICE_ATTR_WO(trigger_request);

static DECLARE_COMPLETION(async_fw_done);

static void trigger_async_request_cb(const struct firmware *fw, void *context)
{
	test_firmware = fw;
	complete(&async_fw_done);
}

static ssize_t trigger_async_request_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int rc;
	char *name;

	name = kstrndup(buf, count, GFP_KERNEL);
	if (!name)
		return -ENOSPC;

	pr_info("loading '%s'\n", name);

	mutex_lock(&test_fw_mutex);
	release_firmware(test_firmware);
	test_firmware = NULL;
	rc = request_firmware_nowait(THIS_MODULE, 1, name, dev, GFP_KERNEL,
				     NULL, trigger_async_request_cb);
	if (rc) {
		pr_info("async load of '%s' failed: %d\n", name, rc);
		kfree(name);
		goto out;
	}
	/* Free 'name' ASAP, to test for race conditions */
	kfree(name);

	wait_for_completion(&async_fw_done);

	if (test_firmware) {
		pr_info("loaded: %zu\n", test_firmware->size);
		rc = count;
	} else {
		pr_err("failed to async load firmware\n");
		rc = -ENODEV;
	}

out:
	mutex_unlock(&test_fw_mutex);

	return rc;
}
static DEVICE_ATTR_WO(trigger_async_request);

static ssize_t trigger_custom_fallback_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	int rc;
	char *name;

	name = kstrndup(buf, count, GFP_KERNEL);
	if (!name)
		return -ENOSPC;

	pr_info("loading '%s' using custom fallback mechanism\n", name);

	mutex_lock(&test_fw_mutex);
	release_firmware(test_firmware);
	test_firmware = NULL;
	rc = request_firmware_nowait(THIS_MODULE, FW_ACTION_NOHOTPLUG, name,
				     dev, GFP_KERNEL, NULL,
				     trigger_async_request_cb);
	if (rc) {
		pr_info("async load of '%s' failed: %d\n", name, rc);
		kfree(name);
		goto out;
	}
	/* Free 'name' ASAP, to test for race conditions */
	kfree(name);

	wait_for_completion(&async_fw_done);

	if (test_firmware) {
		pr_info("loaded: %zu\n", test_firmware->size);
		rc = count;
	} else {
		pr_err("failed to async load firmware\n");
		rc = -ENODEV;
	}

out:
	mutex_unlock(&test_fw_mutex);

	return rc;
}
static DEVICE_ATTR_WO(trigger_custom_fallback);

static int test_fw_run_batch_request(void *data)
{
	struct test_batched_req *req = data;

	if (!req) {
		test_fw_config->test_result = -EINVAL;
		return -EINVAL;
	}

	req->rc = test_fw_config->req_firmware(&req->fw, req->name, req->dev);
	if (req->rc) {
		pr_info("#%u: batched sync load failed: %d\n",
			req->idx, req->rc);
		if (!test_fw_config->test_result)
			test_fw_config->test_result = req->rc;
	} else if (req->fw) {
		req->sent = true;
		pr_info("#%u: batched sync loaded %zu\n",
			req->idx, req->fw->size);
	}
	complete(&req->completion);

	req->task = NULL;

	return 0;
}

/*
 * We use a kthread as otherwise the kernel serializes all our sync requests
 * and we would not be able to mimic batched requests on a sync call. Batched
 * requests on a sync call can for instance happen on a device driver when
 * multiple cards are used and firmware loading happens outside of probe.
 */
static ssize_t trigger_batched_requests_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct test_batched_req *req;
	int rc;
	u8 i;

	mutex_lock(&test_fw_mutex);

	test_fw_config->reqs =
		vzalloc(array3_size(sizeof(struct test_batched_req),
				    test_fw_config->num_requests, 2));
	if (!test_fw_config->reqs) {
		rc = -ENOMEM;
		goto out_unlock;
	}

	pr_info("batched sync firmware loading '%s' %u times\n",
		test_fw_config->name, test_fw_config->num_requests);

	for (i = 0; i < test_fw_config->num_requests; i++) {
		req = &test_fw_config->reqs[i];
		req->fw = NULL;
		req->idx = i;
		req->name = test_fw_config->name;
		req->dev = dev;
		init_completion(&req->completion);
		req->task = kthread_run(test_fw_run_batch_request, req,
					     "%s-%u", KBUILD_MODNAME, req->idx);
		if (!req->task || IS_ERR(req->task)) {
			pr_err("Setting up thread %u failed\n", req->idx);
			req->task = NULL;
			rc = -ENOMEM;
			goto out_bail;
		}
	}

	rc = count;

	/*
	 * We require an explicit release to enable more time and delay of
	 * calling release_firmware() to improve our chances of forcing a
	 * batched request. If we instead called release_firmware() right away
	 * then we might miss on an opportunity of having a successful firmware
	 * request pass on the opportunity to be come a batched request.
	 */

out_bail:
	for (i = 0; i < test_fw_config->num_requests; i++) {
		req = &test_fw_config->reqs[i];
		if (req->task || req->sent)
			wait_for_completion(&req->completion);
	}

	/* Override any worker error if we had a general setup error */
	if (rc < 0)
		test_fw_config->test_result = rc;

out_unlock:
	mutex_unlock(&test_fw_mutex);

	return rc;
}
static DEVICE_ATTR_WO(trigger_batched_requests);

/*
 * We wait for each callback to return with the lock held, no need to lock here
 */
static void trigger_batched_cb(const struct firmware *fw, void *context)
{
	struct test_batched_req *req = context;

	if (!req) {
		test_fw_config->test_result = -EINVAL;
		return;
	}

	/* forces *some* batched requests to queue up */
	if (!req->idx)
		ssleep(2);

	req->fw = fw;

	/*
	 * Unfortunately the firmware API gives us nothing other than a null FW
	 * if the firmware was not found on async requests.  Best we can do is
	 * just assume -ENOENT. A better API would pass the actual return
	 * value to the callback.
	 */
	if (!fw && !test_fw_config->test_result)
		test_fw_config->test_result = -ENOENT;

	complete(&req->completion);
}

static
ssize_t trigger_batched_requests_async_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct test_batched_req *req;
	bool send_uevent;
	int rc;
	u8 i;

	mutex_lock(&test_fw_mutex);

	test_fw_config->reqs =
		vzalloc(array3_size(sizeof(struct test_batched_req),
				    test_fw_config->num_requests, 2));
	if (!test_fw_config->reqs) {
		rc = -ENOMEM;
		goto out;
	}

	pr_info("batched loading '%s' custom fallback mechanism %u times\n",
		test_fw_config->name, test_fw_config->num_requests);

	send_uevent = test_fw_config->send_uevent ? FW_ACTION_HOTPLUG :
		FW_ACTION_NOHOTPLUG;

	for (i = 0; i < test_fw_config->num_requests; i++) {
		req = &test_fw_config->reqs[i];
		req->name = test_fw_config->name;
		req->fw = NULL;
		req->idx = i;
		init_completion(&req->completion);
		rc = request_firmware_nowait(THIS_MODULE, send_uevent,
					     req->name,
					     dev, GFP_KERNEL, req,
					     trigger_batched_cb);
		if (rc) {
			pr_info("#%u: batched async load failed setup: %d\n",
				i, rc);
			req->rc = rc;
			goto out_bail;
		} else
			req->sent = true;
	}

	rc = count;

out_bail:

	/*
	 * We require an explicit release to enable more time and delay of
	 * calling release_firmware() to improve our chances of forcing a
	 * batched request. If we instead called release_firmware() right away
	 * then we might miss on an opportunity of having a successful firmware
	 * request pass on the opportunity to be come a batched request.
	 */

	for (i = 0; i < test_fw_config->num_requests; i++) {
		req = &test_fw_config->reqs[i];
		if (req->sent)
			wait_for_completion(&req->completion);
	}

	/* Override any worker error if we had a general setup error */
	if (rc < 0)
		test_fw_config->test_result = rc;

out:
	mutex_unlock(&test_fw_mutex);

	return rc;
}
static DEVICE_ATTR_WO(trigger_batched_requests_async);

static ssize_t test_result_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return test_dev_config_show_int(buf, test_fw_config->test_result);
}
static DEVICE_ATTR_RO(test_result);

static ssize_t release_all_firmware_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	test_release_all_firmware();
	return count;
}
static DEVICE_ATTR_WO(release_all_firmware);

static ssize_t read_firmware_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct test_batched_req *req;
	u8 idx;
	ssize_t rc = 0;

	mutex_lock(&test_fw_mutex);

	idx = test_fw_config->read_fw_idx;
	if (idx >= test_fw_config->num_requests) {
		rc = -ERANGE;
		goto out;
	}

	if (!test_fw_config->reqs) {
		rc = -EINVAL;
		goto out;
	}

	req = &test_fw_config->reqs[idx];
	if (!req->fw) {
		pr_err("#%u: failed to async load firmware\n", idx);
		rc = -ENOENT;
		goto out;
	}

	pr_info("#%u: loaded %zu\n", idx, req->fw->size);

	if (req->fw->size > PAGE_SIZE) {
		pr_err("Testing interface must use PAGE_SIZE firmware for now\n");
		rc = -EINVAL;
		goto out;
	}
	memcpy(buf, req->fw->data, req->fw->size);

	rc = req->fw->size;
out:
	mutex_unlock(&test_fw_mutex);

	return rc;
}
static DEVICE_ATTR_RO(read_firmware);

#define TEST_FW_DEV_ATTR(name)          &dev_attr_##name.attr

static struct attribute *test_dev_attrs[] = {
	TEST_FW_DEV_ATTR(reset),

	TEST_FW_DEV_ATTR(config),
	TEST_FW_DEV_ATTR(config_name),
	TEST_FW_DEV_ATTR(config_num_requests),
	TEST_FW_DEV_ATTR(config_sync_direct),
	TEST_FW_DEV_ATTR(config_send_uevent),
	TEST_FW_DEV_ATTR(config_read_fw_idx),

	/* These don't use the config at all - they could be ported! */
	TEST_FW_DEV_ATTR(trigger_request),
	TEST_FW_DEV_ATTR(trigger_async_request),
	TEST_FW_DEV_ATTR(trigger_custom_fallback),

	/* These use the config and can use the test_result */
	TEST_FW_DEV_ATTR(trigger_batched_requests),
	TEST_FW_DEV_ATTR(trigger_batched_requests_async),

	TEST_FW_DEV_ATTR(release_all_firmware),
	TEST_FW_DEV_ATTR(test_result),
	TEST_FW_DEV_ATTR(read_firmware),
	NULL,
};

ATTRIBUTE_GROUPS(test_dev);

static struct miscdevice test_fw_misc_device = {
	.minor          = MISC_DYNAMIC_MINOR,
	.name           = "test_firmware",
	.fops           = &test_fw_fops,
	.groups 	= test_dev_groups,
};

static int __init test_firmware_init(void)
{
	int rc;

	test_fw_config = kzalloc(sizeof(struct test_config), GFP_KERNEL);
	if (!test_fw_config)
		return -ENOMEM;

	rc = __test_firmware_config_init();
	if (rc)
		return rc;

	rc = misc_register(&test_fw_misc_device);
	if (rc) {
		kfree(test_fw_config);
		pr_err("could not register misc device: %d\n", rc);
		return rc;
	}

	pr_warn("interface ready\n");

	return 0;
}

module_init(test_firmware_init);

static void __exit test_firmware_exit(void)
{
	mutex_lock(&test_fw_mutex);
	release_firmware(test_firmware);
	misc_deregister(&test_fw_misc_device);
	__test_firmware_config_free();
	kfree(test_fw_config);
	mutex_unlock(&test_fw_mutex);

	pr_warn("removed interface\n");
}

module_exit(test_firmware_exit);

MODULE_AUTHOR("Kees Cook <keescook@chromium.org>");
MODULE_LICENSE("GPL");
