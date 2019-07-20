// SPDX-License-Identifier: GPL-2.0
/*
 * Documentation/ABI/stable/sysfs-fs-orangefs:
 *
 * What:		/sys/fs/orangefs/perf_counter_reset
 * Date:		June 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 * 			echo a 0 or a 1 into perf_counter_reset to
 * 			reset all the counters in
 * 			/sys/fs/orangefs/perf_counters
 * 			except ones with PINT_PERF_PRESERVE set.
 *
 *
 * What:		/sys/fs/orangefs/perf_counters/...
 * Date:		Jun 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 * 			Counters and settings for various caches.
 * 			Read only.
 *
 *
 * What:		/sys/fs/orangefs/perf_time_interval_secs
 * Date:		Jun 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 *			Length of perf counter intervals in
 *			seconds.
 *
 *
 * What:		/sys/fs/orangefs/perf_history_size
 * Date:		Jun 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 * 			The perf_counters cache statistics have N, or
 * 			perf_history_size, samples. The default is
 * 			one.
 *
 *			Every perf_time_interval_secs the (first)
 *			samples are reset.
 *
 *			If N is greater than one, the "current" set
 *			of samples is reset, and the samples from the
 *			other N-1 intervals remain available.
 *
 *
 * What:		/sys/fs/orangefs/op_timeout_secs
 * Date:		Jun 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 *			Service operation timeout in seconds.
 *
 *
 * What:		/sys/fs/orangefs/slot_timeout_secs
 * Date:		Jun 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 *			"Slot" timeout in seconds. A "slot"
 *			is an indexed buffer in the shared
 *			memory segment used for communication
 *			between the kernel module and userspace.
 *			Slots are requested and waited for,
 *			the wait times out after slot_timeout_secs.
 *
 * What:		/sys/fs/orangefs/cache_timeout_msecs
 * Date:		Mar 2018
 * Contact:		Martin Brandenburg <martin@omnibond.com>
 * Description:
 *			Time in milliseconds between which
 *			orangefs_revalidate_mapping will invalidate the page
 *			cache.
 *
 * What:		/sys/fs/orangefs/dcache_timeout_msecs
 * Date:		Jul 2016
 * Contact:		Martin Brandenburg <martin@omnibond.com>
 * Description:
 *			Time lookup is valid in milliseconds.
 *
 * What:		/sys/fs/orangefs/getattr_timeout_msecs
 * Date:		Jul 2016
 * Contact:		Martin Brandenburg <martin@omnibond.com>
 * Description:
 *			Time getattr is valid in milliseconds.
 *
 * What:		/sys/fs/orangefs/readahead_count
 * Date:		Aug 2016
 * Contact:		Martin Brandenburg <martin@omnibond.com>
 * Description:
 *			Readahead cache buffer count.
 *
 * What:		/sys/fs/orangefs/readahead_size
 * Date:		Aug 2016
 * Contact:		Martin Brandenburg <martin@omnibond.com>
 * Description:
 *			Readahead cache buffer size.
 *
 * What:		/sys/fs/orangefs/readahead_count_size
 * Date:		Aug 2016
 * Contact:		Martin Brandenburg <martin@omnibond.com>
 * Description:
 *			Readahead cache buffer count and size.
 *
 * What:		/sys/fs/orangefs/readahead_readcnt
 * Date:		Jan 2017
 * Contact:		Martin Brandenburg <martin@omnibond.com>
 * Description:
 *			Number of buffers (in multiples of readahead_size)
 *			which can be read ahead for a single file at once.
 *
 * What:		/sys/fs/orangefs/acache/...
 * Date:		Jun 2015
 * Contact:		Martin Brandenburg <martin@omnibond.com>
 * Description:
 * 			Attribute cache configurable settings.
 *
 *
 * What:		/sys/fs/orangefs/ncache/...
 * Date:		Jun 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 * 			Name cache configurable settings.
 *
 *
 * What:		/sys/fs/orangefs/capcache/...
 * Date:		Jun 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 * 			Capability cache configurable settings.
 *
 *
 * What:		/sys/fs/orangefs/ccache/...
 * Date:		Jun 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 * 			Credential cache configurable settings.
 *
 */

#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-sysfs.h"

#define ORANGEFS_KOBJ_ID "orangefs"
#define ACACHE_KOBJ_ID "acache"
#define CAPCACHE_KOBJ_ID "capcache"
#define CCACHE_KOBJ_ID "ccache"
#define NCACHE_KOBJ_ID "ncache"
#define PC_KOBJ_ID "pc"
#define STATS_KOBJ_ID "stats"

/*
 * Every item calls orangefs_attr_show and orangefs_attr_store through
 * orangefs_sysfs_ops. They look at the orangefs_attributes further below to
 * call one of sysfs_int_show, sysfs_int_store, sysfs_service_op_show, or
 * sysfs_service_op_store.
 */

struct orangefs_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj,
			struct orangefs_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj,
			 struct orangefs_attribute *attr,
			 const char *buf,
			 size_t count);
};

static ssize_t orangefs_attr_show(struct kobject *kobj,
				  struct attribute *attr,
				  char *buf)
{
	struct orangefs_attribute *attribute;

	attribute = container_of(attr, struct orangefs_attribute, attr);
	if (!attribute->show)
		return -EIO;
	return attribute->show(kobj, attribute, buf);
}

static ssize_t orangefs_attr_store(struct kobject *kobj,
				   struct attribute *attr,
				   const char *buf,
				   size_t len)
{
	struct orangefs_attribute *attribute;

	if (!strcmp(kobj->name, PC_KOBJ_ID) ||
	    !strcmp(kobj->name, STATS_KOBJ_ID))
		return -EPERM;

	attribute = container_of(attr, struct orangefs_attribute, attr);
	if (!attribute->store)
		return -EIO;
	return attribute->store(kobj, attribute, buf, len);
}

static const struct sysfs_ops orangefs_sysfs_ops = {
	.show = orangefs_attr_show,
	.store = orangefs_attr_store,
};

static ssize_t sysfs_int_show(struct kobject *kobj,
    struct orangefs_attribute *attr, char *buf)
{
	int rc = -EIO;

	gossip_debug(GOSSIP_SYSFS_DEBUG, "sysfs_int_show: id:%s:\n",
	    kobj->name);

	if (!strcmp(kobj->name, ORANGEFS_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "op_timeout_secs")) {
			rc = scnprintf(buf,
				       PAGE_SIZE,
				       "%d\n",
				       op_timeout_secs);
			goto out;
		} else if (!strcmp(attr->attr.name,
				   "slot_timeout_secs")) {
			rc = scnprintf(buf,
				       PAGE_SIZE,
				       "%d\n",
				       slot_timeout_secs);
			goto out;
		} else if (!strcmp(attr->attr.name,
				   "cache_timeout_msecs")) {
			rc = scnprintf(buf,
				       PAGE_SIZE,
				       "%d\n",
				       orangefs_cache_timeout_msecs);
			goto out;
		} else if (!strcmp(attr->attr.name,
				   "dcache_timeout_msecs")) {
			rc = scnprintf(buf,
				       PAGE_SIZE,
				       "%d\n",
				       orangefs_dcache_timeout_msecs);
			goto out;
		} else if (!strcmp(attr->attr.name,
				   "getattr_timeout_msecs")) {
			rc = scnprintf(buf,
				       PAGE_SIZE,
				       "%d\n",
				       orangefs_getattr_timeout_msecs);
			goto out;
		} else {
			goto out;
		}

	} else if (!strcmp(kobj->name, STATS_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "reads")) {
			rc = scnprintf(buf,
				       PAGE_SIZE,
				       "%lu\n",
				       orangefs_stats.reads);
			goto out;
		} else if (!strcmp(attr->attr.name, "writes")) {
			rc = scnprintf(buf,
				       PAGE_SIZE,
				       "%lu\n",
				       orangefs_stats.writes);
			goto out;
		} else {
			goto out;
		}
	}

out:

	return rc;
}

static ssize_t sysfs_int_store(struct kobject *kobj,
    struct orangefs_attribute *attr, const char *buf, size_t count)
{
	int rc = 0;

	gossip_debug(GOSSIP_SYSFS_DEBUG,
		     "sysfs_int_store: start attr->attr.name:%s: buf:%s:\n",
		     attr->attr.name, buf);

	if (!strcmp(attr->attr.name, "op_timeout_secs")) {
		rc = kstrtoint(buf, 0, &op_timeout_secs);
		goto out;
	} else if (!strcmp(attr->attr.name, "slot_timeout_secs")) {
		rc = kstrtoint(buf, 0, &slot_timeout_secs);
		goto out;
	} else if (!strcmp(attr->attr.name, "cache_timeout_msecs")) {
		rc = kstrtoint(buf, 0, &orangefs_cache_timeout_msecs);
		goto out;
	} else if (!strcmp(attr->attr.name, "dcache_timeout_msecs")) {
		rc = kstrtoint(buf, 0, &orangefs_dcache_timeout_msecs);
		goto out;
	} else if (!strcmp(attr->attr.name, "getattr_timeout_msecs")) {
		rc = kstrtoint(buf, 0, &orangefs_getattr_timeout_msecs);
		goto out;
	} else {
		goto out;
	}

out:
	if (rc)
		rc = -EINVAL;
	else
		rc = count;

	return rc;
}

/*
 * obtain attribute values from userspace with a service operation.
 */
static ssize_t sysfs_service_op_show(struct kobject *kobj,
    struct orangefs_attribute *attr, char *buf)
{
	struct orangefs_kernel_op_s *new_op = NULL;
	int rc = 0;
	char *ser_op_type = NULL;
	__u32 op_alloc_type;

	gossip_debug(GOSSIP_SYSFS_DEBUG,
		     "sysfs_service_op_show: id:%s:\n",
		     kobj->name);

	if (strcmp(kobj->name, PC_KOBJ_ID))
		op_alloc_type = ORANGEFS_VFS_OP_PARAM;
	else
		op_alloc_type = ORANGEFS_VFS_OP_PERF_COUNT;

	new_op = op_alloc(op_alloc_type);
	if (!new_op)
		return -ENOMEM;

	/* Can't do a service_operation if the client is not running... */
	rc = is_daemon_in_service();
	if (rc) {
		pr_info_ratelimited("%s: Client not running :%d:\n",
			__func__,
			is_daemon_in_service());
		goto out;
	}

	if (strcmp(kobj->name, PC_KOBJ_ID))
		new_op->upcall.req.param.type = ORANGEFS_PARAM_REQUEST_GET;

	if (!strcmp(kobj->name, ORANGEFS_KOBJ_ID)) {
		/* Drop unsupported requests first. */
		if (!(orangefs_features & ORANGEFS_FEATURE_READAHEAD) &&
		    (!strcmp(attr->attr.name, "readahead_count") ||
		    !strcmp(attr->attr.name, "readahead_size") ||
		    !strcmp(attr->attr.name, "readahead_count_size") ||
		    !strcmp(attr->attr.name, "readahead_readcnt"))) {
			rc = -EINVAL;
			goto out;
		}

		if (!strcmp(attr->attr.name, "perf_history_size"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_PERF_HISTORY_SIZE;
		else if (!strcmp(attr->attr.name,
				 "perf_time_interval_secs"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_PERF_TIME_INTERVAL_SECS;
		else if (!strcmp(attr->attr.name,
				 "perf_counter_reset"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_PERF_RESET;

		else if (!strcmp(attr->attr.name,
				 "readahead_count"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_READAHEAD_COUNT;

		else if (!strcmp(attr->attr.name,
				 "readahead_size"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_READAHEAD_SIZE;

		else if (!strcmp(attr->attr.name,
				 "readahead_count_size"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_READAHEAD_COUNT_SIZE;

		else if (!strcmp(attr->attr.name,
				 "readahead_readcnt"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_READAHEAD_READCNT;
	} else if (!strcmp(kobj->name, ACACHE_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "timeout_msecs"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_ACACHE_TIMEOUT_MSECS;

		if (!strcmp(attr->attr.name, "hard_limit"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_ACACHE_HARD_LIMIT;

		if (!strcmp(attr->attr.name, "soft_limit"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_ACACHE_SOFT_LIMIT;

		if (!strcmp(attr->attr.name, "reclaim_percentage"))
			new_op->upcall.req.param.op =
			  ORANGEFS_PARAM_REQUEST_OP_ACACHE_RECLAIM_PERCENTAGE;

	} else if (!strcmp(kobj->name, CAPCACHE_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "timeout_secs"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_TIMEOUT_SECS;

		if (!strcmp(attr->attr.name, "hard_limit"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_HARD_LIMIT;

		if (!strcmp(attr->attr.name, "soft_limit"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_SOFT_LIMIT;

		if (!strcmp(attr->attr.name, "reclaim_percentage"))
			new_op->upcall.req.param.op =
			  ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_RECLAIM_PERCENTAGE;

	} else if (!strcmp(kobj->name, CCACHE_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "timeout_secs"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_CCACHE_TIMEOUT_SECS;

		if (!strcmp(attr->attr.name, "hard_limit"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_CCACHE_HARD_LIMIT;

		if (!strcmp(attr->attr.name, "soft_limit"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_CCACHE_SOFT_LIMIT;

		if (!strcmp(attr->attr.name, "reclaim_percentage"))
			new_op->upcall.req.param.op =
			  ORANGEFS_PARAM_REQUEST_OP_CCACHE_RECLAIM_PERCENTAGE;

	} else if (!strcmp(kobj->name, NCACHE_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "timeout_msecs"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_NCACHE_TIMEOUT_MSECS;

		if (!strcmp(attr->attr.name, "hard_limit"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_NCACHE_HARD_LIMIT;

		if (!strcmp(attr->attr.name, "soft_limit"))
			new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_NCACHE_SOFT_LIMIT;

		if (!strcmp(attr->attr.name, "reclaim_percentage"))
			new_op->upcall.req.param.op =
			  ORANGEFS_PARAM_REQUEST_OP_NCACHE_RECLAIM_PERCENTAGE;

	} else if (!strcmp(kobj->name, PC_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, ACACHE_KOBJ_ID))
			new_op->upcall.req.perf_count.type =
				ORANGEFS_PERF_COUNT_REQUEST_ACACHE;

		if (!strcmp(attr->attr.name, CAPCACHE_KOBJ_ID))
			new_op->upcall.req.perf_count.type =
				ORANGEFS_PERF_COUNT_REQUEST_CAPCACHE;

		if (!strcmp(attr->attr.name, NCACHE_KOBJ_ID))
			new_op->upcall.req.perf_count.type =
				ORANGEFS_PERF_COUNT_REQUEST_NCACHE;

	} else {
		gossip_err("sysfs_service_op_show: unknown kobj_id:%s:\n",
			   kobj->name);
		rc = -EINVAL;
		goto out;
	}


	if (strcmp(kobj->name, PC_KOBJ_ID))
		ser_op_type = "orangefs_param";
	else
		ser_op_type = "orangefs_perf_count";

	/*
	 * The service_operation will return an errno return code on
	 * error, and zero on success.
	 */
	rc = service_operation(new_op, ser_op_type, ORANGEFS_OP_INTERRUPTIBLE);

out:
	if (!rc) {
		if (strcmp(kobj->name, PC_KOBJ_ID)) {
			if (new_op->upcall.req.param.op ==
			    ORANGEFS_PARAM_REQUEST_OP_READAHEAD_COUNT_SIZE) {
				rc = scnprintf(buf, PAGE_SIZE, "%d %d\n",
				    (int)new_op->downcall.resp.param.u.
				    value32[0],
				    (int)new_op->downcall.resp.param.u.
				    value32[1]);
			} else {
				rc = scnprintf(buf, PAGE_SIZE, "%d\n",
				    (int)new_op->downcall.resp.param.u.value64);
			}
		} else {
			rc = scnprintf(
				buf,
				PAGE_SIZE,
				"%s",
				new_op->downcall.resp.perf_count.buffer);
		}
	}

	op_release(new_op);

	return rc;

}

/*
 * pass attribute values back to userspace with a service operation.
 *
 * We have to do a memory allocation, an sscanf and a service operation.
 * And we have to evaluate what the user entered, to make sure the
 * value is within the range supported by the attribute. So, there's
 * a lot of return code checking and mapping going on here.
 *
 * We want to return 1 if we think everything went OK, and
 * EINVAL if not.
 */
static ssize_t sysfs_service_op_store(struct kobject *kobj,
    struct orangefs_attribute *attr, const char *buf, size_t count)
{
	struct orangefs_kernel_op_s *new_op = NULL;
	int val = 0;
	int rc = 0;

	gossip_debug(GOSSIP_SYSFS_DEBUG,
		     "sysfs_service_op_store: id:%s:\n",
		     kobj->name);

	new_op = op_alloc(ORANGEFS_VFS_OP_PARAM);
	if (!new_op)
		return -EINVAL; /* sic */

	/* Can't do a service_operation if the client is not running... */
	rc = is_daemon_in_service();
	if (rc) {
		pr_info("%s: Client not running :%d:\n",
			__func__,
			is_daemon_in_service());
		goto out;
	}

	/*
	 * The value we want to send back to userspace is in buf, unless this
	 * there are two parameters, which is specially handled below.
	 */
	if (strcmp(kobj->name, ORANGEFS_KOBJ_ID) ||
	    strcmp(attr->attr.name, "readahead_count_size")) {
		rc = kstrtoint(buf, 0, &val);
		if (rc)
			goto out;
	}

	new_op->upcall.req.param.type = ORANGEFS_PARAM_REQUEST_SET;

	if (!strcmp(kobj->name, ORANGEFS_KOBJ_ID)) {
		/* Drop unsupported requests first. */
		if (!(orangefs_features & ORANGEFS_FEATURE_READAHEAD) &&
		    (!strcmp(attr->attr.name, "readahead_count") ||
		    !strcmp(attr->attr.name, "readahead_size") ||
		    !strcmp(attr->attr.name, "readahead_count_size") ||
		    !strcmp(attr->attr.name, "readahead_readcnt"))) {
			rc = -EINVAL;
			goto out;
		}

		if (!strcmp(attr->attr.name, "perf_history_size")) {
			if (val > 0) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_PERF_HISTORY_SIZE;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name,
				   "perf_time_interval_secs")) {
			if (val > 0) {
				new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_PERF_TIME_INTERVAL_SECS;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name,
				   "perf_counter_reset")) {
			if ((val == 0) || (val == 1)) {
				new_op->upcall.req.param.op =
					ORANGEFS_PARAM_REQUEST_OP_PERF_RESET;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name,
				   "readahead_count")) {
			if ((val >= 0)) {
				new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_READAHEAD_COUNT;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name,
				   "readahead_size")) {
			if ((val >= 0)) {
				new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_READAHEAD_SIZE;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name,
				   "readahead_count_size")) {
			int val1, val2;
			rc = sscanf(buf, "%d %d", &val1, &val2);
			if (rc < 2) {
				rc = 0;
				goto out;
			}
			if ((val1 >= 0) && (val2 >= 0)) {
				new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_READAHEAD_COUNT_SIZE;
			} else {
				rc = 0;
				goto out;
			}
			new_op->upcall.req.param.u.value32[0] = val1;
			new_op->upcall.req.param.u.value32[1] = val2;
			goto value_set;
		} else if (!strcmp(attr->attr.name,
				   "readahead_readcnt")) {
			if ((val >= 0)) {
				new_op->upcall.req.param.op =
				ORANGEFS_PARAM_REQUEST_OP_READAHEAD_READCNT;
			} else {
				rc = 0;
				goto out;
			}
		}

	} else if (!strcmp(kobj->name, ACACHE_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "hard_limit")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_ACACHE_HARD_LIMIT;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name, "soft_limit")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_ACACHE_SOFT_LIMIT;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name,
				   "reclaim_percentage")) {
			if ((val > -1) && (val < 101)) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_ACACHE_RECLAIM_PERCENTAGE;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name, "timeout_msecs")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_ACACHE_TIMEOUT_MSECS;
			} else {
				rc = 0;
				goto out;
			}
		}

	} else if (!strcmp(kobj->name, CAPCACHE_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "hard_limit")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_HARD_LIMIT;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name, "soft_limit")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_SOFT_LIMIT;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name,
				   "reclaim_percentage")) {
			if ((val > -1) && (val < 101)) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_RECLAIM_PERCENTAGE;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name, "timeout_secs")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_TIMEOUT_SECS;
			} else {
				rc = 0;
				goto out;
			}
		}

	} else if (!strcmp(kobj->name, CCACHE_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "hard_limit")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_CCACHE_HARD_LIMIT;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name, "soft_limit")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_CCACHE_SOFT_LIMIT;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name,
				   "reclaim_percentage")) {
			if ((val > -1) && (val < 101)) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_CCACHE_RECLAIM_PERCENTAGE;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name, "timeout_secs")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_CCACHE_TIMEOUT_SECS;
			} else {
				rc = 0;
				goto out;
			}
		}

	} else if (!strcmp(kobj->name, NCACHE_KOBJ_ID)) {
		if (!strcmp(attr->attr.name, "hard_limit")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_NCACHE_HARD_LIMIT;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name, "soft_limit")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_NCACHE_SOFT_LIMIT;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name,
				   "reclaim_percentage")) {
			if ((val > -1) && (val < 101)) {
				new_op->upcall.req.param.op =
					ORANGEFS_PARAM_REQUEST_OP_NCACHE_RECLAIM_PERCENTAGE;
			} else {
				rc = 0;
				goto out;
			}
		} else if (!strcmp(attr->attr.name, "timeout_msecs")) {
			if (val > -1) {
				new_op->upcall.req.param.op =
				  ORANGEFS_PARAM_REQUEST_OP_NCACHE_TIMEOUT_MSECS;
			} else {
				rc = 0;
				goto out;
			}
		}

	} else {
		gossip_err("sysfs_service_op_store: unknown kobj_id:%s:\n",
			   kobj->name);
		rc = -EINVAL;
		goto out;
	}

	new_op->upcall.req.param.u.value64 = val;
value_set:

	/*
	 * The service_operation will return a errno return code on
	 * error, and zero on success.
	 */
	rc = service_operation(new_op, "orangefs_param", ORANGEFS_OP_INTERRUPTIBLE);

	if (rc < 0) {
		gossip_err("sysfs_service_op_store: service op returned:%d:\n",
			rc);
		rc = 0;
	} else {
		rc = count;
	}

out:
	op_release(new_op);

	if (rc == -ENOMEM || rc == 0)
		rc = -EINVAL;

	return rc;
}

static struct orangefs_attribute op_timeout_secs_attribute =
	__ATTR(op_timeout_secs, 0664, sysfs_int_show, sysfs_int_store);

static struct orangefs_attribute slot_timeout_secs_attribute =
	__ATTR(slot_timeout_secs, 0664, sysfs_int_show, sysfs_int_store);

static struct orangefs_attribute cache_timeout_msecs_attribute =
	__ATTR(cache_timeout_msecs, 0664, sysfs_int_show, sysfs_int_store);

static struct orangefs_attribute dcache_timeout_msecs_attribute =
	__ATTR(dcache_timeout_msecs, 0664, sysfs_int_show, sysfs_int_store);

static struct orangefs_attribute getattr_timeout_msecs_attribute =
	__ATTR(getattr_timeout_msecs, 0664, sysfs_int_show, sysfs_int_store);

static struct orangefs_attribute readahead_count_attribute =
	__ATTR(readahead_count, 0664, sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute readahead_size_attribute =
	__ATTR(readahead_size, 0664, sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute readahead_count_size_attribute =
	__ATTR(readahead_count_size, 0664, sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute readahead_readcnt_attribute =
	__ATTR(readahead_readcnt, 0664, sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute perf_counter_reset_attribute =
	__ATTR(perf_counter_reset,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute perf_history_size_attribute =
	__ATTR(perf_history_size,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute perf_time_interval_secs_attribute =
	__ATTR(perf_time_interval_secs,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct attribute *orangefs_default_attrs[] = {
	&op_timeout_secs_attribute.attr,
	&slot_timeout_secs_attribute.attr,
	&cache_timeout_msecs_attribute.attr,
	&dcache_timeout_msecs_attribute.attr,
	&getattr_timeout_msecs_attribute.attr,
	&readahead_count_attribute.attr,
	&readahead_size_attribute.attr,
	&readahead_count_size_attribute.attr,
	&readahead_readcnt_attribute.attr,
	&perf_counter_reset_attribute.attr,
	&perf_history_size_attribute.attr,
	&perf_time_interval_secs_attribute.attr,
	NULL,
};

static struct kobj_type orangefs_ktype = {
	.sysfs_ops = &orangefs_sysfs_ops,
	.default_attrs = orangefs_default_attrs,
};

static struct orangefs_attribute acache_hard_limit_attribute =
	__ATTR(hard_limit,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute acache_reclaim_percent_attribute =
	__ATTR(reclaim_percentage,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute acache_soft_limit_attribute =
	__ATTR(soft_limit,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute acache_timeout_msecs_attribute =
	__ATTR(timeout_msecs,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct attribute *acache_orangefs_default_attrs[] = {
	&acache_hard_limit_attribute.attr,
	&acache_reclaim_percent_attribute.attr,
	&acache_soft_limit_attribute.attr,
	&acache_timeout_msecs_attribute.attr,
	NULL,
};

static struct kobj_type acache_orangefs_ktype = {
	.sysfs_ops = &orangefs_sysfs_ops,
	.default_attrs = acache_orangefs_default_attrs,
};

static struct orangefs_attribute capcache_hard_limit_attribute =
	__ATTR(hard_limit,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute capcache_reclaim_percent_attribute =
	__ATTR(reclaim_percentage,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute capcache_soft_limit_attribute =
	__ATTR(soft_limit,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute capcache_timeout_secs_attribute =
	__ATTR(timeout_secs,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct attribute *capcache_orangefs_default_attrs[] = {
	&capcache_hard_limit_attribute.attr,
	&capcache_reclaim_percent_attribute.attr,
	&capcache_soft_limit_attribute.attr,
	&capcache_timeout_secs_attribute.attr,
	NULL,
};

static struct kobj_type capcache_orangefs_ktype = {
	.sysfs_ops = &orangefs_sysfs_ops,
	.default_attrs = capcache_orangefs_default_attrs,
};

static struct orangefs_attribute ccache_hard_limit_attribute =
	__ATTR(hard_limit,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute ccache_reclaim_percent_attribute =
	__ATTR(reclaim_percentage,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute ccache_soft_limit_attribute =
	__ATTR(soft_limit,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute ccache_timeout_secs_attribute =
	__ATTR(timeout_secs,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct attribute *ccache_orangefs_default_attrs[] = {
	&ccache_hard_limit_attribute.attr,
	&ccache_reclaim_percent_attribute.attr,
	&ccache_soft_limit_attribute.attr,
	&ccache_timeout_secs_attribute.attr,
	NULL,
};

static struct kobj_type ccache_orangefs_ktype = {
	.sysfs_ops = &orangefs_sysfs_ops,
	.default_attrs = ccache_orangefs_default_attrs,
};

static struct orangefs_attribute ncache_hard_limit_attribute =
	__ATTR(hard_limit,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute ncache_reclaim_percent_attribute =
	__ATTR(reclaim_percentage,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute ncache_soft_limit_attribute =
	__ATTR(soft_limit,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct orangefs_attribute ncache_timeout_msecs_attribute =
	__ATTR(timeout_msecs,
	       0664,
	       sysfs_service_op_show,
	       sysfs_service_op_store);

static struct attribute *ncache_orangefs_default_attrs[] = {
	&ncache_hard_limit_attribute.attr,
	&ncache_reclaim_percent_attribute.attr,
	&ncache_soft_limit_attribute.attr,
	&ncache_timeout_msecs_attribute.attr,
	NULL,
};

static struct kobj_type ncache_orangefs_ktype = {
	.sysfs_ops = &orangefs_sysfs_ops,
	.default_attrs = ncache_orangefs_default_attrs,
};

static struct orangefs_attribute pc_acache_attribute =
	__ATTR(acache,
	       0664,
	       sysfs_service_op_show,
	       NULL);

static struct orangefs_attribute pc_capcache_attribute =
	__ATTR(capcache,
	       0664,
	       sysfs_service_op_show,
	       NULL);

static struct orangefs_attribute pc_ncache_attribute =
	__ATTR(ncache,
	       0664,
	       sysfs_service_op_show,
	       NULL);

static struct attribute *pc_orangefs_default_attrs[] = {
	&pc_acache_attribute.attr,
	&pc_capcache_attribute.attr,
	&pc_ncache_attribute.attr,
	NULL,
};

static struct kobj_type pc_orangefs_ktype = {
	.sysfs_ops = &orangefs_sysfs_ops,
	.default_attrs = pc_orangefs_default_attrs,
};

static struct orangefs_attribute stats_reads_attribute =
	__ATTR(reads,
	       0664,
	       sysfs_int_show,
	       NULL);

static struct orangefs_attribute stats_writes_attribute =
	__ATTR(writes,
	       0664,
	       sysfs_int_show,
	       NULL);

static struct attribute *stats_orangefs_default_attrs[] = {
	&stats_reads_attribute.attr,
	&stats_writes_attribute.attr,
	NULL,
};

static struct kobj_type stats_orangefs_ktype = {
	.sysfs_ops = &orangefs_sysfs_ops,
	.default_attrs = stats_orangefs_default_attrs,
};

static struct kobject *orangefs_obj;
static struct kobject *acache_orangefs_obj;
static struct kobject *capcache_orangefs_obj;
static struct kobject *ccache_orangefs_obj;
static struct kobject *ncache_orangefs_obj;
static struct kobject *pc_orangefs_obj;
static struct kobject *stats_orangefs_obj;

int orangefs_sysfs_init(void)
{
	int rc = -EINVAL;

	gossip_debug(GOSSIP_SYSFS_DEBUG, "orangefs_sysfs_init: start\n");

	/* create /sys/fs/orangefs. */
	orangefs_obj = kzalloc(sizeof(*orangefs_obj), GFP_KERNEL);
	if (!orangefs_obj)
		goto out;

	rc = kobject_init_and_add(orangefs_obj,
				  &orangefs_ktype,
				  fs_kobj,
				  ORANGEFS_KOBJ_ID);

	if (rc)
		goto ofs_obj_bail;

	kobject_uevent(orangefs_obj, KOBJ_ADD);

	/* create /sys/fs/orangefs/acache. */
	acache_orangefs_obj = kzalloc(sizeof(*acache_orangefs_obj), GFP_KERNEL);
	if (!acache_orangefs_obj) {
		rc = -EINVAL;
		goto ofs_obj_bail;
	}

	rc = kobject_init_and_add(acache_orangefs_obj,
				  &acache_orangefs_ktype,
				  orangefs_obj,
				  ACACHE_KOBJ_ID);

	if (rc)
		goto acache_obj_bail;

	kobject_uevent(acache_orangefs_obj, KOBJ_ADD);

	/* create /sys/fs/orangefs/capcache. */
	capcache_orangefs_obj =
		kzalloc(sizeof(*capcache_orangefs_obj), GFP_KERNEL);
	if (!capcache_orangefs_obj) {
		rc = -EINVAL;
		goto acache_obj_bail;
	}

	rc = kobject_init_and_add(capcache_orangefs_obj,
				  &capcache_orangefs_ktype,
				  orangefs_obj,
				  CAPCACHE_KOBJ_ID);
	if (rc)
		goto capcache_obj_bail;

	kobject_uevent(capcache_orangefs_obj, KOBJ_ADD);

	/* create /sys/fs/orangefs/ccache. */
	ccache_orangefs_obj =
		kzalloc(sizeof(*ccache_orangefs_obj), GFP_KERNEL);
	if (!ccache_orangefs_obj) {
		rc = -EINVAL;
		goto capcache_obj_bail;
	}

	rc = kobject_init_and_add(ccache_orangefs_obj,
				  &ccache_orangefs_ktype,
				  orangefs_obj,
				  CCACHE_KOBJ_ID);
	if (rc)
		goto ccache_obj_bail;

	kobject_uevent(ccache_orangefs_obj, KOBJ_ADD);

	/* create /sys/fs/orangefs/ncache. */
	ncache_orangefs_obj = kzalloc(sizeof(*ncache_orangefs_obj), GFP_KERNEL);
	if (!ncache_orangefs_obj) {
		rc = -EINVAL;
		goto ccache_obj_bail;
	}

	rc = kobject_init_and_add(ncache_orangefs_obj,
				  &ncache_orangefs_ktype,
				  orangefs_obj,
				  NCACHE_KOBJ_ID);

	if (rc)
		goto ncache_obj_bail;

	kobject_uevent(ncache_orangefs_obj, KOBJ_ADD);

	/* create /sys/fs/orangefs/perf_counters. */
	pc_orangefs_obj = kzalloc(sizeof(*pc_orangefs_obj), GFP_KERNEL);
	if (!pc_orangefs_obj) {
		rc = -EINVAL;
		goto ncache_obj_bail;
	}

	rc = kobject_init_and_add(pc_orangefs_obj,
				  &pc_orangefs_ktype,
				  orangefs_obj,
				  "perf_counters");

	if (rc)
		goto pc_obj_bail;

	kobject_uevent(pc_orangefs_obj, KOBJ_ADD);

	/* create /sys/fs/orangefs/stats. */
	stats_orangefs_obj = kzalloc(sizeof(*stats_orangefs_obj), GFP_KERNEL);
	if (!stats_orangefs_obj) {
		rc = -EINVAL;
		goto pc_obj_bail;
	}

	rc = kobject_init_and_add(stats_orangefs_obj,
				  &stats_orangefs_ktype,
				  orangefs_obj,
				  STATS_KOBJ_ID);

	if (rc)
		goto stats_obj_bail;

	kobject_uevent(stats_orangefs_obj, KOBJ_ADD);
	goto out;

stats_obj_bail:
		kobject_put(stats_orangefs_obj);
pc_obj_bail:
		kobject_put(pc_orangefs_obj);
ncache_obj_bail:
		kobject_put(ncache_orangefs_obj);
ccache_obj_bail:
		kobject_put(ccache_orangefs_obj);
capcache_obj_bail:
		kobject_put(capcache_orangefs_obj);
acache_obj_bail:
		kobject_put(acache_orangefs_obj);
ofs_obj_bail:
		kobject_put(orangefs_obj);
out:
	return rc;
}

void orangefs_sysfs_exit(void)
{
	gossip_debug(GOSSIP_SYSFS_DEBUG, "orangefs_sysfs_exit: start\n");
	kobject_put(acache_orangefs_obj);
	kobject_put(capcache_orangefs_obj);
	kobject_put(ccache_orangefs_obj);
	kobject_put(ncache_orangefs_obj);
	kobject_put(pc_orangefs_obj);
	kobject_put(stats_orangefs_obj);
	kobject_put(orangefs_obj);
}
