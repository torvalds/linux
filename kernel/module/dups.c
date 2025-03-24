/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * kmod dups - the kernel module autoloader duplicate suppressor
 *
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 */

#define pr_fmt(fmt)     "module: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/binfmts.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/rwsem.h>
#include <linux/ptrace.h>
#include <linux/async.h>
#include <linux/uaccess.h>

#include "internal.h"

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "module."
static bool enable_dups_trace = IS_ENABLED(CONFIG_MODULE_DEBUG_AUTOLOAD_DUPS_TRACE);
module_param(enable_dups_trace, bool_enable_only, 0644);

/*
 * Protects dup_kmod_reqs list, adds / removals with RCU.
 */
static DEFINE_MUTEX(kmod_dup_mutex);
static LIST_HEAD(dup_kmod_reqs);

struct kmod_dup_req {
	struct list_head list;
	char name[MODULE_NAME_LEN];
	struct completion first_req_done;
	struct work_struct complete_work;
	struct delayed_work delete_work;
	int dup_ret;
};

static struct kmod_dup_req *kmod_dup_request_lookup(char *module_name)
{
	struct kmod_dup_req *kmod_req;

	list_for_each_entry_rcu(kmod_req, &dup_kmod_reqs, list,
				lockdep_is_held(&kmod_dup_mutex)) {
		if (strlen(kmod_req->name) == strlen(module_name) &&
		    !memcmp(kmod_req->name, module_name, strlen(module_name))) {
			return kmod_req;
                }
        }

	return NULL;
}

static void kmod_dup_request_delete(struct work_struct *work)
{
	struct kmod_dup_req *kmod_req;
	kmod_req = container_of(to_delayed_work(work), struct kmod_dup_req, delete_work);

	/*
	 * The typical situation is a module successully loaded. In that
	 * situation the module will be present already in userspace. If
	 * new requests come in after that, userspace will already know the
	 * module is loaded so will just return 0 right away. There is still
	 * a small chance right after we delete this entry new request_module()
	 * calls may happen after that, they can happen. These heuristics
	 * are to protect finit_module() abuse for auto-loading, if modules
	 * are still tryign to auto-load even if a module is already loaded,
	 * that's on them, and those inneficiencies should not be fixed by
	 * kmod. The inneficies there are a call to modprobe and modprobe
	 * just returning 0.
	 */
	mutex_lock(&kmod_dup_mutex);
	list_del_rcu(&kmod_req->list);
	synchronize_rcu();
	mutex_unlock(&kmod_dup_mutex);
	kfree(kmod_req);
}

static void kmod_dup_request_complete(struct work_struct *work)
{
	struct kmod_dup_req *kmod_req;

	kmod_req = container_of(work, struct kmod_dup_req, complete_work);

	/*
	 * This will ensure that the kernel will let all the waiters get
	 * informed its time to check the return value. It's time to
	 * go home.
	 */
	complete_all(&kmod_req->first_req_done);

	/*
	 * Now that we have allowed prior request_module() calls to go on
	 * with life, let's schedule deleting this entry. We don't have
	 * to do it right away, but we *eventually* want to do it so to not
	 * let this linger forever as this is just a boot optimization for
	 * possible abuses of vmalloc() incurred by finit_module() thrashing.
	 */
	queue_delayed_work(system_wq, &kmod_req->delete_work, 60 * HZ);
}

bool kmod_dup_request_exists_wait(char *module_name, bool wait, int *dup_ret)
{
	struct kmod_dup_req *kmod_req, *new_kmod_req;
	int ret;

	/*
	 * Pre-allocate the entry in case we have to use it later
	 * to avoid contention with the mutex.
	 */
	new_kmod_req = kzalloc(sizeof(*new_kmod_req), GFP_KERNEL);
	if (!new_kmod_req)
		return false;

	memcpy(new_kmod_req->name, module_name, strlen(module_name));
	INIT_WORK(&new_kmod_req->complete_work, kmod_dup_request_complete);
	INIT_DELAYED_WORK(&new_kmod_req->delete_work, kmod_dup_request_delete);
	init_completion(&new_kmod_req->first_req_done);

	mutex_lock(&kmod_dup_mutex);

	kmod_req = kmod_dup_request_lookup(module_name);
	if (!kmod_req) {
		/*
		 * If the first request that came through for a module
		 * was with request_module_nowait() we cannot wait for it
		 * and share its return value with other users which may
		 * have used request_module() and need a proper return value
		 * so just skip using them as an anchor.
		 *
		 * If a prior request to this one came through with
		 * request_module() though, then a request_module_nowait()
		 * would benefit from duplicate detection.
		 */
		if (!wait) {
			kfree(new_kmod_req);
			pr_debug("New request_module_nowait() for %s -- cannot track duplicates for this request\n", module_name);
			mutex_unlock(&kmod_dup_mutex);
			return false;
		}

		/*
		 * There was no duplicate, just add the request so we can
		 * keep tab on duplicates later.
		 */
		pr_debug("New request_module() for %s\n", module_name);
		list_add_rcu(&new_kmod_req->list, &dup_kmod_reqs);
		mutex_unlock(&kmod_dup_mutex);
		return false;
	}
	mutex_unlock(&kmod_dup_mutex);

	/* We are dealing with a duplicate request now */
	kfree(new_kmod_req);

	/*
	 * To fix these try to use try_then_request_module() instead as that
	 * will check if the component you are looking for is present or not.
	 * You could also just queue a single request to load the module once,
	 * instead of having each and everything you need try to request for
	 * the module.
	 *
	 * Duplicate request_module() calls  can cause quite a bit of wasted
	 * vmalloc() space when racing with userspace.
	 */
	if (enable_dups_trace)
		WARN(1, "module-autoload: duplicate request for module %s\n", module_name);
	else
		pr_warn("module-autoload: duplicate request for module %s\n", module_name);

	if (!wait) {
		/*
		 * If request_module_nowait() was used then the user just
		 * wanted to issue the request and if another module request
		 * was already its way with the same name we don't care for
		 * the return value either. Let duplicate request_module_nowait()
		 * calls bail out right away.
		 */
		*dup_ret = 0;
		return true;
	}

	/*
	 * If a duplicate request_module() was used they *may* care for
	 * the return value, so we have no other option but to wait for
	 * the first caller to complete. If the first caller used
	 * the request_module_nowait() call, subsquent callers will
	 * deal with the comprmise of getting a successful call with this
	 * optimization enabled ...
	 */
	ret = wait_for_completion_state(&kmod_req->first_req_done,
					TASK_KILLABLE);
	if (ret) {
		*dup_ret = ret;
		return true;
	}

	/* Now the duplicate request has the same exact return value as the first request */
	*dup_ret = kmod_req->dup_ret;

	return true;
}

void kmod_dup_request_announce(char *module_name, int ret)
{
	struct kmod_dup_req *kmod_req;

	mutex_lock(&kmod_dup_mutex);

	kmod_req = kmod_dup_request_lookup(module_name);
	if (!kmod_req)
		goto out;

	kmod_req->dup_ret = ret;

	/*
	 * If we complete() here we may allow duplicate threads
	 * to continue before the first one that submitted the
	 * request. We're in no rush also, given that each and
	 * every bounce back to userspace is slow we avoid that
	 * with a slight delay here. So queueue up the completion
	 * and let duplicates suffer, just wait a tad bit longer.
	 * There is no rush. But we also don't want to hold the
	 * caller up forever or introduce any boot delays.
	 */
	queue_work(system_wq, &kmod_req->complete_work);

out:
	mutex_unlock(&kmod_dup_mutex);
}
