/*
 * kmod stress test driver
 *
 * Copyright (C) 2017 Luis R. Rodriguez <mcgrof@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or at your option any
 * later version; or, when distributed separately from the Linux kernel or
 * when incorporated into other software packages, subject to the following
 * license:
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of copyleft-next (version 0.3.1 or later) as published
 * at http://copyleft-next.org/.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/*
 * This driver provides an interface to trigger and test the kernel's
 * module loader through a series of configurations and a few triggers.
 * To test this driver use the following script as root:
 *
 * tools/testing/selftests/kmod/kmod.sh --help
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/printk.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/device.h>

#define TEST_START_NUM_THREADS	50
#define TEST_START_DRIVER	"test_module"
#define TEST_START_TEST_FS	"xfs"
#define TEST_START_TEST_CASE	TEST_KMOD_DRIVER


static bool force_init_test = false;
module_param(force_init_test, bool_enable_only, 0644);
MODULE_PARM_DESC(force_init_test,
		 "Force kicking a test immediately after driver loads");

/*
 * For device allocation / registration
 */
static DEFINE_MUTEX(reg_dev_mutex);
static LIST_HEAD(reg_test_devs);

/*
 * num_test_devs actually represents the *next* ID of the next
 * device we will allow to create.
 */
static int num_test_devs;

/**
 * enum kmod_test_case - linker table test case
 *
 * If you add a  test case, please be sure to review if you need to se
 * @need_mod_put for your tests case.
 *
 * @TEST_KMOD_DRIVER: stress tests request_module()
 * @TEST_KMOD_FS_TYPE: stress tests get_fs_type()
 */
enum kmod_test_case {
	__TEST_KMOD_INVALID = 0,

	TEST_KMOD_DRIVER,
	TEST_KMOD_FS_TYPE,

	__TEST_KMOD_MAX,
};

struct test_config {
	char *test_driver;
	char *test_fs;
	unsigned int num_threads;
	enum kmod_test_case test_case;
	int test_result;
};

struct kmod_test_device;

/**
 * kmod_test_device_info - thread info
 *
 * @ret_sync: return value if request_module() is used, sync request for
 * 	@TEST_KMOD_DRIVER
 * @fs_sync: return value of get_fs_type() for @TEST_KMOD_FS_TYPE
 * @thread_idx: thread ID
 * @test_dev: test device test is being performed under
 * @need_mod_put: Some tests (get_fs_type() is one) requires putting the module
 *	(module_put(fs_sync->owner)) when done, otherwise you will not be able
 *	to unload the respective modules and re-test. We use this to keep
 *	accounting of when we need this and to help out in case we need to
 *	error out and deal with module_put() on error.
 */
struct kmod_test_device_info {
	int ret_sync;
	struct file_system_type *fs_sync;
	struct task_struct *task_sync;
	unsigned int thread_idx;
	struct kmod_test_device *test_dev;
	bool need_mod_put;
};

/**
 * kmod_test_device - test device to help test kmod
 *
 * @dev_idx: unique ID for test device
 * @config: configuration for the test
 * @misc_dev: we use a misc device under the hood
 * @dev: pointer to misc_dev's own struct device
 * @config_mutex: protects configuration of test
 * @trigger_mutex: the test trigger can only be fired once at a time
 * @thread_lock: protects @done count, and the @info per each thread
 * @done: number of threads which have completed or failed
 * @test_is_oom: when we run out of memory, use this to halt moving forward
 * @kthreads_done: completion used to signal when all work is done
 * @list: needed to be part of the reg_test_devs
 * @info: array of info for each thread
 */
struct kmod_test_device {
	int dev_idx;
	struct test_config config;
	struct miscdevice misc_dev;
	struct device *dev;
	struct mutex config_mutex;
	struct mutex trigger_mutex;
	struct mutex thread_mutex;

	unsigned int done;

	bool test_is_oom;
	struct completion kthreads_done;
	struct list_head list;

	struct kmod_test_device_info *info;
};

static const char *test_case_str(enum kmod_test_case test_case)
{
	switch (test_case) {
	case TEST_KMOD_DRIVER:
		return "TEST_KMOD_DRIVER";
	case TEST_KMOD_FS_TYPE:
		return "TEST_KMOD_FS_TYPE";
	default:
		return "invalid";
	}
}

static struct miscdevice *dev_to_misc_dev(struct device *dev)
{
	return dev_get_drvdata(dev);
}

static struct kmod_test_device *misc_dev_to_test_dev(struct miscdevice *misc_dev)
{
	return container_of(misc_dev, struct kmod_test_device, misc_dev);
}

static struct kmod_test_device *dev_to_test_dev(struct device *dev)
{
	struct miscdevice *misc_dev;

	misc_dev = dev_to_misc_dev(dev);

	return misc_dev_to_test_dev(misc_dev);
}

/* Must run with thread_mutex held */
static void kmod_test_done_check(struct kmod_test_device *test_dev,
				 unsigned int idx)
{
	struct test_config *config = &test_dev->config;

	test_dev->done++;
	dev_dbg(test_dev->dev, "Done thread count: %u\n", test_dev->done);

	if (test_dev->done == config->num_threads) {
		dev_info(test_dev->dev, "Done: %u threads have all run now\n",
			 test_dev->done);
		dev_info(test_dev->dev, "Last thread to run: %u\n", idx);
		complete(&test_dev->kthreads_done);
	}
}

static void test_kmod_put_module(struct kmod_test_device_info *info)
{
	struct kmod_test_device *test_dev = info->test_dev;
	struct test_config *config = &test_dev->config;

	if (!info->need_mod_put)
		return;

	switch (config->test_case) {
	case TEST_KMOD_DRIVER:
		break;
	case TEST_KMOD_FS_TYPE:
		if (info->fs_sync && info->fs_sync->owner)
			module_put(info->fs_sync->owner);
		break;
	default:
		BUG();
	}

	info->need_mod_put = true;
}

static int run_request(void *data)
{
	struct kmod_test_device_info *info = data;
	struct kmod_test_device *test_dev = info->test_dev;
	struct test_config *config = &test_dev->config;

	switch (config->test_case) {
	case TEST_KMOD_DRIVER:
		info->ret_sync = request_module("%s", config->test_driver);
		break;
	case TEST_KMOD_FS_TYPE:
		info->fs_sync = get_fs_type(config->test_fs);
		info->need_mod_put = true;
		break;
	default:
		/* __trigger_config_run() already checked for test sanity */
		BUG();
		return -EINVAL;
	}

	dev_dbg(test_dev->dev, "Ran thread %u\n", info->thread_idx);

	test_kmod_put_module(info);

	mutex_lock(&test_dev->thread_mutex);
	info->task_sync = NULL;
	kmod_test_done_check(test_dev, info->thread_idx);
	mutex_unlock(&test_dev->thread_mutex);

	return 0;
}

static int tally_work_test(struct kmod_test_device_info *info)
{
	struct kmod_test_device *test_dev = info->test_dev;
	struct test_config *config = &test_dev->config;
	int err_ret = 0;

	switch (config->test_case) {
	case TEST_KMOD_DRIVER:
		/*
		 * Only capture errors, if one is found that's
		 * enough, for now.
		 */
		if (info->ret_sync != 0)
			err_ret = info->ret_sync;
		dev_info(test_dev->dev,
			 "Sync thread %d return status: %d\n",
			 info->thread_idx, info->ret_sync);
		break;
	case TEST_KMOD_FS_TYPE:
		/* For now we make this simple */
		if (!info->fs_sync)
			err_ret = -EINVAL;
		dev_info(test_dev->dev, "Sync thread %u fs: %s\n",
			 info->thread_idx, info->fs_sync ? config->test_fs :
			 "NULL");
		break;
	default:
		BUG();
	}

	return err_ret;
}

/*
 * XXX: add result option to display if all errors did not match.
 * For now we just keep any error code if one was found.
 *
 * If this ran it means *all* tasks were created fine and we
 * are now just collecting results.
 *
 * Only propagate errors, do not override with a subsequent sucess case.
 */
static void tally_up_work(struct kmod_test_device *test_dev)
{
	struct test_config *config = &test_dev->config;
	struct kmod_test_device_info *info;
	unsigned int idx;
	int err_ret = 0;
	int ret = 0;

	mutex_lock(&test_dev->thread_mutex);

	dev_info(test_dev->dev, "Results:\n");

	for (idx=0; idx < config->num_threads; idx++) {
		info = &test_dev->info[idx];
		ret = tally_work_test(info);
		if (ret)
			err_ret = ret;
	}

	/*
	 * Note: request_module() returns 256 for a module not found even
	 * though modprobe itself returns 1.
	 */
	config->test_result = err_ret;

	mutex_unlock(&test_dev->thread_mutex);
}

static int try_one_request(struct kmod_test_device *test_dev, unsigned int idx)
{
	struct kmod_test_device_info *info = &test_dev->info[idx];
	int fail_ret = -ENOMEM;

	mutex_lock(&test_dev->thread_mutex);

	info->thread_idx = idx;
	info->test_dev = test_dev;
	info->task_sync = kthread_run(run_request, info, "%s-%u",
				      KBUILD_MODNAME, idx);

	if (!info->task_sync || IS_ERR(info->task_sync)) {
		test_dev->test_is_oom = true;
		dev_err(test_dev->dev, "Setting up thread %u failed\n", idx);
		info->task_sync = NULL;
		goto err_out;
	} else
		dev_dbg(test_dev->dev, "Kicked off thread %u\n", idx);

	mutex_unlock(&test_dev->thread_mutex);

	return 0;

err_out:
	info->ret_sync = fail_ret;
	mutex_unlock(&test_dev->thread_mutex);

	return fail_ret;
}

static void test_dev_kmod_stop_tests(struct kmod_test_device *test_dev)
{
	struct test_config *config = &test_dev->config;
	struct kmod_test_device_info *info;
	unsigned int i;

	dev_info(test_dev->dev, "Ending request_module() tests\n");

	mutex_lock(&test_dev->thread_mutex);

	for (i=0; i < config->num_threads; i++) {
		info = &test_dev->info[i];
		if (info->task_sync && !IS_ERR(info->task_sync)) {
			dev_info(test_dev->dev,
				 "Stopping still-running thread %i\n", i);
			kthread_stop(info->task_sync);
		}

		/*
		 * info->task_sync is well protected, it can only be
		 * NULL or a pointer to a struct. If its NULL we either
		 * never ran, or we did and we completed the work. Completed
		 * tasks *always* put the module for us. This is a sanity
		 * check -- just in case.
		 */
		if (info->task_sync && info->need_mod_put)
			test_kmod_put_module(info);
	}

	mutex_unlock(&test_dev->thread_mutex);
}

/*
 * Only wait *iff* we did not run into any errors during all of our thread
 * set up. If run into any issues we stop threads and just bail out with
 * an error to the trigger. This also means we don't need any tally work
 * for any threads which fail.
 */
static int try_requests(struct kmod_test_device *test_dev)
{
	struct test_config *config = &test_dev->config;
	unsigned int idx;
	int ret;
	bool any_error = false;

	for (idx=0; idx < config->num_threads; idx++) {
		if (test_dev->test_is_oom) {
			any_error = true;
			break;
		}

		ret = try_one_request(test_dev, idx);
		if (ret) {
			any_error = true;
			break;
		}
	}

	if (!any_error) {
		test_dev->test_is_oom = false;
		dev_info(test_dev->dev,
			 "No errors were found while initializing threads\n");
		wait_for_completion(&test_dev->kthreads_done);
		tally_up_work(test_dev);
	} else {
		test_dev->test_is_oom = true;
		dev_info(test_dev->dev,
			 "At least one thread failed to start, stop all work\n");
		test_dev_kmod_stop_tests(test_dev);
		return -ENOMEM;
	}

	return 0;
}

static int run_test_driver(struct kmod_test_device *test_dev)
{
	struct test_config *config = &test_dev->config;

	dev_info(test_dev->dev, "Test case: %s (%u)\n",
		 test_case_str(config->test_case),
		 config->test_case);
	dev_info(test_dev->dev, "Test driver to load: %s\n",
		 config->test_driver);
	dev_info(test_dev->dev, "Number of threads to run: %u\n",
		 config->num_threads);
	dev_info(test_dev->dev, "Thread IDs will range from 0 - %u\n",
		 config->num_threads - 1);

	return try_requests(test_dev);
}

static int run_test_fs_type(struct kmod_test_device *test_dev)
{
	struct test_config *config = &test_dev->config;

	dev_info(test_dev->dev, "Test case: %s (%u)\n",
		 test_case_str(config->test_case),
		 config->test_case);
	dev_info(test_dev->dev, "Test filesystem to load: %s\n",
		 config->test_fs);
	dev_info(test_dev->dev, "Number of threads to run: %u\n",
		 config->num_threads);
	dev_info(test_dev->dev, "Thread IDs will range from 0 - %u\n",
		 config->num_threads - 1);

	return try_requests(test_dev);
}

static ssize_t config_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;
	int len = 0;

	mutex_lock(&test_dev->config_mutex);

	len += snprintf(buf, PAGE_SIZE,
			"Custom trigger configuration for: %s\n",
			dev_name(dev));

	len += snprintf(buf+len, PAGE_SIZE - len,
			"Number of threads:\t%u\n",
			config->num_threads);

	len += snprintf(buf+len, PAGE_SIZE - len,
			"Test_case:\t%s (%u)\n",
			test_case_str(config->test_case),
			config->test_case);

	if (config->test_driver)
		len += snprintf(buf+len, PAGE_SIZE - len,
				"driver:\t%s\n",
				config->test_driver);
	else
		len += snprintf(buf+len, PAGE_SIZE - len,
				"driver:\tEMPTY\n");

	if (config->test_fs)
		len += snprintf(buf+len, PAGE_SIZE - len,
				"fs:\t%s\n",
				config->test_fs);
	else
		len += snprintf(buf+len, PAGE_SIZE - len,
				"fs:\tEMPTY\n");

	mutex_unlock(&test_dev->config_mutex);

	return len;
}
static DEVICE_ATTR_RO(config);

/*
 * This ensures we don't allow kicking threads through if our configuration
 * is faulty.
 */
static int __trigger_config_run(struct kmod_test_device *test_dev)
{
	struct test_config *config = &test_dev->config;

	test_dev->done = 0;

	switch (config->test_case) {
	case TEST_KMOD_DRIVER:
		return run_test_driver(test_dev);
	case TEST_KMOD_FS_TYPE:
		return run_test_fs_type(test_dev);
	default:
		dev_warn(test_dev->dev,
			 "Invalid test case requested: %u\n",
			 config->test_case);
		return -EINVAL;
	}
}

static int trigger_config_run(struct kmod_test_device *test_dev)
{
	struct test_config *config = &test_dev->config;
	int ret;

	mutex_lock(&test_dev->trigger_mutex);
	mutex_lock(&test_dev->config_mutex);

	ret = __trigger_config_run(test_dev);
	if (ret < 0)
		goto out;
	dev_info(test_dev->dev, "General test result: %d\n",
		 config->test_result);

	/*
	 * We must return 0 after a trigger even unless something went
	 * wrong with the setup of the test. If the test setup went fine
	 * then userspace must just check the result of config->test_result.
	 * One issue with relying on the return from a call in the kernel
	 * is if the kernel returns a possitive value using this trigger
	 * will not return the value to userspace, it would be lost.
	 *
	 * By not relying on capturing the return value of tests we are using
	 * through the trigger it also us to run tests with set -e and only
	 * fail when something went wrong with the driver upon trigger
	 * requests.
	 */
	ret = 0;

out:
	mutex_unlock(&test_dev->config_mutex);
	mutex_unlock(&test_dev->trigger_mutex);

	return ret;
}

static ssize_t
trigger_config_store(struct device *dev,
		     struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	int ret;

	if (test_dev->test_is_oom)
		return -ENOMEM;

	/* For all intents and purposes we don't care what userspace
	 * sent this trigger, we care only that we were triggered.
	 * We treat the return value only for caputuring issues with
	 * the test setup. At this point all the test variables should
	 * have been allocated so typically this should never fail.
	 */
	ret = trigger_config_run(test_dev);
	if (unlikely(ret < 0))
		goto out;

	/*
	 * Note: any return > 0 will be treated as success
	 * and the error value will not be available to userspace.
	 * Do not rely on trying to send to userspace a test value
	 * return value as possitive return errors will be lost.
	 */
	if (WARN_ON(ret > 0))
		return -EINVAL;

	ret = count;
out:
	return ret;
}
static DEVICE_ATTR_WO(trigger_config);

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

static int config_copy_test_driver_name(struct test_config *config,
				    const char *name,
				    size_t count)
{
	return __kstrncpy(&config->test_driver, name, count, GFP_KERNEL);
}


static int config_copy_test_fs(struct test_config *config, const char *name,
			       size_t count)
{
	return __kstrncpy(&config->test_fs, name, count, GFP_KERNEL);
}

static void __kmod_config_free(struct test_config *config)
{
	if (!config)
		return;

	kfree_const(config->test_driver);
	config->test_driver = NULL;

	kfree_const(config->test_fs);
	config->test_fs = NULL;
}

static void kmod_config_free(struct kmod_test_device *test_dev)
{
	struct test_config *config;

	if (!test_dev)
		return;

	config = &test_dev->config;

	mutex_lock(&test_dev->config_mutex);
	__kmod_config_free(config);
	mutex_unlock(&test_dev->config_mutex);
}

static ssize_t config_test_driver_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;
	int copied;

	mutex_lock(&test_dev->config_mutex);

	kfree_const(config->test_driver);
	config->test_driver = NULL;

	copied = config_copy_test_driver_name(config, buf, count);
	mutex_unlock(&test_dev->config_mutex);

	return copied;
}

/*
 * As per sysfs_kf_seq_show() the buf is max PAGE_SIZE.
 */
static ssize_t config_test_show_str(struct mutex *config_mutex,
				    char *dst,
				    char *src)
{
	int len;

	mutex_lock(config_mutex);
	len = snprintf(dst, PAGE_SIZE, "%s\n", src);
	mutex_unlock(config_mutex);

	return len;
}

static ssize_t config_test_driver_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;

	return config_test_show_str(&test_dev->config_mutex, buf,
				    config->test_driver);
}
static DEVICE_ATTR_RW(config_test_driver);

static ssize_t config_test_fs_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;
	int copied;

	mutex_lock(&test_dev->config_mutex);

	kfree_const(config->test_fs);
	config->test_fs = NULL;

	copied = config_copy_test_fs(config, buf, count);
	mutex_unlock(&test_dev->config_mutex);

	return copied;
}

static ssize_t config_test_fs_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;

	return config_test_show_str(&test_dev->config_mutex, buf,
				    config->test_fs);
}
static DEVICE_ATTR_RW(config_test_fs);

static int trigger_config_run_type(struct kmod_test_device *test_dev,
				   enum kmod_test_case test_case,
				   const char *test_str)
{
	int copied = 0;
	struct test_config *config = &test_dev->config;

	mutex_lock(&test_dev->config_mutex);

	switch (test_case) {
	case TEST_KMOD_DRIVER:
		kfree_const(config->test_driver);
		config->test_driver = NULL;
		copied = config_copy_test_driver_name(config, test_str,
						      strlen(test_str));
		break;
	case TEST_KMOD_FS_TYPE:
		kfree_const(config->test_fs);
		config->test_fs = NULL;
		copied = config_copy_test_fs(config, test_str,
					     strlen(test_str));
		break;
	default:
		mutex_unlock(&test_dev->config_mutex);
		return -EINVAL;
	}

	config->test_case = test_case;

	mutex_unlock(&test_dev->config_mutex);

	if (copied <= 0 || copied != strlen(test_str)) {
		test_dev->test_is_oom = true;
		return -ENOMEM;
	}

	test_dev->test_is_oom = false;

	return trigger_config_run(test_dev);
}

static void free_test_dev_info(struct kmod_test_device *test_dev)
{
	vfree(test_dev->info);
	test_dev->info = NULL;
}

static int kmod_config_sync_info(struct kmod_test_device *test_dev)
{
	struct test_config *config = &test_dev->config;

	free_test_dev_info(test_dev);
	test_dev->info =
		vzalloc(array_size(sizeof(struct kmod_test_device_info),
				   config->num_threads));
	if (!test_dev->info)
		return -ENOMEM;

	return 0;
}

/*
 * Old kernels may not have this, if you want to port this code to
 * test it on older kernels.
 */
#ifdef get_kmod_umh_limit
static unsigned int kmod_init_test_thread_limit(void)
{
	return get_kmod_umh_limit();
}
#else
static unsigned int kmod_init_test_thread_limit(void)
{
	return TEST_START_NUM_THREADS;
}
#endif

static int __kmod_config_init(struct kmod_test_device *test_dev)
{
	struct test_config *config = &test_dev->config;
	int ret = -ENOMEM, copied;

	__kmod_config_free(config);

	copied = config_copy_test_driver_name(config, TEST_START_DRIVER,
					      strlen(TEST_START_DRIVER));
	if (copied != strlen(TEST_START_DRIVER))
		goto err_out;

	copied = config_copy_test_fs(config, TEST_START_TEST_FS,
				     strlen(TEST_START_TEST_FS));
	if (copied != strlen(TEST_START_TEST_FS))
		goto err_out;

	config->num_threads = kmod_init_test_thread_limit();
	config->test_result = 0;
	config->test_case = TEST_START_TEST_CASE;

	ret = kmod_config_sync_info(test_dev);
	if (ret)
		goto err_out;

	test_dev->test_is_oom = false;

	return 0;

err_out:
	test_dev->test_is_oom = true;
	WARN_ON(test_dev->test_is_oom);

	__kmod_config_free(config);

	return ret;
}

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	int ret;

	mutex_lock(&test_dev->trigger_mutex);
	mutex_lock(&test_dev->config_mutex);

	ret = __kmod_config_init(test_dev);
	if (ret < 0) {
		ret = -ENOMEM;
		dev_err(dev, "could not alloc settings for config trigger: %d\n",
		       ret);
		goto out;
	}

	dev_info(dev, "reset\n");
	ret = count;

out:
	mutex_unlock(&test_dev->config_mutex);
	mutex_unlock(&test_dev->trigger_mutex);

	return ret;
}
static DEVICE_ATTR_WO(reset);

static int test_dev_config_update_uint_sync(struct kmod_test_device *test_dev,
					    const char *buf, size_t size,
					    unsigned int *config,
					    int (*test_sync)(struct kmod_test_device *test_dev))
{
	int ret;
	unsigned int val;
	unsigned int old_val;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&test_dev->config_mutex);

	old_val = *config;
	*(unsigned int *)config = val;

	ret = test_sync(test_dev);
	if (ret) {
		*(unsigned int *)config = old_val;

		ret = test_sync(test_dev);
		WARN_ON(ret);

		mutex_unlock(&test_dev->config_mutex);
		return -EINVAL;
	}

	mutex_unlock(&test_dev->config_mutex);
	/* Always return full write size even if we didn't consume all */
	return size;
}

static int test_dev_config_update_uint_range(struct kmod_test_device *test_dev,
					     const char *buf, size_t size,
					     unsigned int *config,
					     unsigned int min,
					     unsigned int max)
{
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val < min || val > max)
		return -EINVAL;

	mutex_lock(&test_dev->config_mutex);
	*config = val;
	mutex_unlock(&test_dev->config_mutex);

	/* Always return full write size even if we didn't consume all */
	return size;
}

static int test_dev_config_update_int(struct kmod_test_device *test_dev,
				      const char *buf, size_t size,
				      int *config)
{
	int val;
	int ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&test_dev->config_mutex);
	*config = val;
	mutex_unlock(&test_dev->config_mutex);
	/* Always return full write size even if we didn't consume all */
	return size;
}

static ssize_t test_dev_config_show_int(struct kmod_test_device *test_dev,
					char *buf,
					int config)
{
	int val;

	mutex_lock(&test_dev->config_mutex);
	val = config;
	mutex_unlock(&test_dev->config_mutex);

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t test_dev_config_show_uint(struct kmod_test_device *test_dev,
					 char *buf,
					 unsigned int config)
{
	unsigned int val;

	mutex_lock(&test_dev->config_mutex);
	val = config;
	mutex_unlock(&test_dev->config_mutex);

	return snprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t test_result_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;

	return test_dev_config_update_int(test_dev, buf, count,
					  &config->test_result);
}

static ssize_t config_num_threads_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;

	return test_dev_config_update_uint_sync(test_dev, buf, count,
						&config->num_threads,
						kmod_config_sync_info);
}

static ssize_t config_num_threads_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;

	return test_dev_config_show_int(test_dev, buf, config->num_threads);
}
static DEVICE_ATTR_RW(config_num_threads);

static ssize_t config_test_case_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;

	return test_dev_config_update_uint_range(test_dev, buf, count,
						 &config->test_case,
						 __TEST_KMOD_INVALID + 1,
						 __TEST_KMOD_MAX - 1);
}

static ssize_t config_test_case_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;

	return test_dev_config_show_uint(test_dev, buf, config->test_case);
}
static DEVICE_ATTR_RW(config_test_case);

static ssize_t test_result_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct kmod_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;

	return test_dev_config_show_int(test_dev, buf, config->test_result);
}
static DEVICE_ATTR_RW(test_result);

#define TEST_KMOD_DEV_ATTR(name)		&dev_attr_##name.attr

static struct attribute *test_dev_attrs[] = {
	TEST_KMOD_DEV_ATTR(trigger_config),
	TEST_KMOD_DEV_ATTR(config),
	TEST_KMOD_DEV_ATTR(reset),

	TEST_KMOD_DEV_ATTR(config_test_driver),
	TEST_KMOD_DEV_ATTR(config_test_fs),
	TEST_KMOD_DEV_ATTR(config_num_threads),
	TEST_KMOD_DEV_ATTR(config_test_case),
	TEST_KMOD_DEV_ATTR(test_result),

	NULL,
};

ATTRIBUTE_GROUPS(test_dev);

static int kmod_config_init(struct kmod_test_device *test_dev)
{
	int ret;

	mutex_lock(&test_dev->config_mutex);
	ret = __kmod_config_init(test_dev);
	mutex_unlock(&test_dev->config_mutex);

	return ret;
}

static struct kmod_test_device *alloc_test_dev_kmod(int idx)
{
	int ret;
	struct kmod_test_device *test_dev;
	struct miscdevice *misc_dev;

	test_dev = vzalloc(sizeof(struct kmod_test_device));
	if (!test_dev)
		goto err_out;

	mutex_init(&test_dev->config_mutex);
	mutex_init(&test_dev->trigger_mutex);
	mutex_init(&test_dev->thread_mutex);

	init_completion(&test_dev->kthreads_done);

	ret = kmod_config_init(test_dev);
	if (ret < 0) {
		pr_err("Cannot alloc kmod_config_init()\n");
		goto err_out_free;
	}

	test_dev->dev_idx = idx;
	misc_dev = &test_dev->misc_dev;

	misc_dev->minor = MISC_DYNAMIC_MINOR;
	misc_dev->name = kasprintf(GFP_KERNEL, "test_kmod%d", idx);
	if (!misc_dev->name) {
		pr_err("Cannot alloc misc_dev->name\n");
		goto err_out_free_config;
	}
	misc_dev->groups = test_dev_groups;

	return test_dev;

err_out_free_config:
	free_test_dev_info(test_dev);
	kmod_config_free(test_dev);
err_out_free:
	vfree(test_dev);
	test_dev = NULL;
err_out:
	return NULL;
}

static void free_test_dev_kmod(struct kmod_test_device *test_dev)
{
	if (test_dev) {
		kfree_const(test_dev->misc_dev.name);
		test_dev->misc_dev.name = NULL;
		free_test_dev_info(test_dev);
		kmod_config_free(test_dev);
		vfree(test_dev);
		test_dev = NULL;
	}
}

static struct kmod_test_device *register_test_dev_kmod(void)
{
	struct kmod_test_device *test_dev = NULL;
	int ret;

	mutex_lock(&reg_dev_mutex);

	/* int should suffice for number of devices, test for wrap */
	if (num_test_devs + 1 == INT_MAX) {
		pr_err("reached limit of number of test devices\n");
		goto out;
	}

	test_dev = alloc_test_dev_kmod(num_test_devs);
	if (!test_dev)
		goto out;

	ret = misc_register(&test_dev->misc_dev);
	if (ret) {
		pr_err("could not register misc device: %d\n", ret);
		free_test_dev_kmod(test_dev);
		goto out;
	}

	test_dev->dev = test_dev->misc_dev.this_device;
	list_add_tail(&test_dev->list, &reg_test_devs);
	dev_info(test_dev->dev, "interface ready\n");

	num_test_devs++;

out:
	mutex_unlock(&reg_dev_mutex);

	return test_dev;

}

static int __init test_kmod_init(void)
{
	struct kmod_test_device *test_dev;
	int ret;

	test_dev = register_test_dev_kmod();
	if (!test_dev) {
		pr_err("Cannot add first test kmod device\n");
		return -ENODEV;
	}

	/*
	 * With some work we might be able to gracefully enable
	 * testing with this driver built-in, for now this seems
	 * rather risky. For those willing to try have at it,
	 * and enable the below. Good luck! If that works, try
	 * lowering the init level for more fun.
	 */
	if (force_init_test) {
		ret = trigger_config_run_type(test_dev,
					      TEST_KMOD_DRIVER, "tun");
		if (WARN_ON(ret))
			return ret;
		ret = trigger_config_run_type(test_dev,
					      TEST_KMOD_FS_TYPE, "btrfs");
		if (WARN_ON(ret))
			return ret;
	}

	return 0;
}
late_initcall(test_kmod_init);

static
void unregister_test_dev_kmod(struct kmod_test_device *test_dev)
{
	mutex_lock(&test_dev->trigger_mutex);
	mutex_lock(&test_dev->config_mutex);

	test_dev_kmod_stop_tests(test_dev);

	dev_info(test_dev->dev, "removing interface\n");
	misc_deregister(&test_dev->misc_dev);

	mutex_unlock(&test_dev->config_mutex);
	mutex_unlock(&test_dev->trigger_mutex);

	free_test_dev_kmod(test_dev);
}

static void __exit test_kmod_exit(void)
{
	struct kmod_test_device *test_dev, *tmp;

	mutex_lock(&reg_dev_mutex);
	list_for_each_entry_safe(test_dev, tmp, &reg_test_devs, list) {
		list_del(&test_dev->list);
		unregister_test_dev_kmod(test_dev);
	}
	mutex_unlock(&reg_dev_mutex);
}
module_exit(test_kmod_exit);

MODULE_AUTHOR("Luis R. Rodriguez <mcgrof@kernel.org>");
MODULE_LICENSE("GPL");
