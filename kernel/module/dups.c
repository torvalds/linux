/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * kmod dups - the kernel module autoloader duplicate suppressor
 *
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 */

#define pr_fmt(fmt)     "module: " fmt

#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/param.h>
#include <linux/printk.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "internal.h"

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "module."
static bool enable_dups_trace = IS_ENABLED(CONFIG_MODULE_DEBUG_AUTOLOAD_DUPS_TRACE);
module_param(enable_dups_trace, bool_enable_only, 0644);

/* A mutex-protected list of active kmod requests. */
static DEFINE_MUTEX(kmod_dup_mutex);
static LIST_HEAD(dup_kmod_reqs);

struct kmod_dup_req {
	refcount_t refcount;
	struct list_head list;
	char name[MODULE_NAME_LEN];
	struct completion first_req_done;
	struct delayed_work delete_work;
	int dup_ret;
};

static void get_kmod_req(struct kmod_dup_req *kmod_req)
{
	refcount_inc(&kmod_req->refcount);
}

static void put_kmod_req(struct kmod_dup_req *kmod_req)
{
	if (refcount_dec_and_test(&kmod_req->refcount))
		kfree(kmod_req);
}

DEFINE_FREE(put_kmod_req, struct kmod_dup_req *, if (_T) put_kmod_req(_T))

static struct kmod_dup_req *kmod_dup_request_lookup(char *module_name)
{
	struct kmod_dup_req *kmod_req;

	lockdep_assert_held(&kmod_dup_mutex);

	list_for_each_entry(kmod_req, &dup_kmod_reqs, list) {
		if (!strcmp(kmod_req->name, module_name))
			return kmod_req;
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
	scoped_guard(mutex, &kmod_dup_mutex)
		list_del(&kmod_req->list);

	put_kmod_req(kmod_req);
}

static struct kmod_dup_req *alloc_kmod_req(const char *module_name)
{
	struct kmod_dup_req *kmod_req = kzalloc_obj(*kmod_req);

	if (!kmod_req)
		return NULL;

	refcount_set(&kmod_req->refcount, 1);
	strscpy(kmod_req->name, module_name);
	INIT_DELAYED_WORK(&kmod_req->delete_work, kmod_dup_request_delete);
	init_completion(&kmod_req->first_req_done);
	return kmod_req;
}

bool kmod_dup_request_exists_wait(char *module_name, bool wait, int *dup_ret)
{
	struct kmod_dup_req *kmod_req __free(put_kmod_req) = NULL;
	int ret;

	scoped_guard(mutex, &kmod_dup_mutex) {
		struct kmod_dup_req *new_kmod_req;

		kmod_req = kmod_dup_request_lookup(module_name);
		if (kmod_req) {
			get_kmod_req(kmod_req);
			break;
		}

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
			pr_debug("New request_module_nowait() for %s -- cannot track duplicates for this request\n", module_name);
			return false;
		}

		/*
		 * There was no duplicate, just add the request so we can
		 * keep tab on duplicates later.
		 */
		pr_debug("New request_module() for %s\n", module_name);
		new_kmod_req = alloc_kmod_req(module_name);
		if (!new_kmod_req)
			return false;
		list_add(&new_kmod_req->list, &dup_kmod_reqs);
		return false;
	}

	/* We are dealing with a duplicate request now */

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

	/*
	 * Look for a kmod_dup_req previously added in
	 * kmod_dup_request_exists_wait(). Note that a request_module_nowait()
	 * without its own kmod_dup_req entry can announce a result of
	 * a concurrent request_module() call.
	 */
	scoped_guard(mutex, &kmod_dup_mutex) {
		kmod_req = kmod_dup_request_lookup(module_name);
		if (!kmod_req || completion_done(&kmod_req->first_req_done))
			return;

		kmod_req->dup_ret = ret;

		/* Inform all duplicate waiters to check the return value. */
		complete_all(&kmod_req->first_req_done);
	}

	/*
	 * Now that we have allowed prior request_module() calls to go on
	 * with life, let's schedule deleting this entry. We don't have
	 * to do it right away, but we *eventually* want to do it so to not
	 * let this linger forever as this is just a boot optimization for
	 * possible abuses of vmalloc() incurred by finit_module() thrashing.
	 */
	queue_delayed_work(system_dfl_wq, &kmod_req->delete_work, 60 * HZ);
}
