/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2026 Google LLC
 */

#ifndef __LINUX_REVOCABLE_H
#define __LINUX_REVOCABLE_H

#include <linux/compiler.h>
#include <linux/cleanup.h>

struct device;
struct revocable;
struct revocable_provider;

struct revocable_provider *revocable_provider_alloc(void *res);
void revocable_provider_revoke(struct revocable_provider *rp);
struct revocable_provider *devm_revocable_provider_alloc(struct device *dev,
							 void *res);

struct revocable *revocable_alloc(struct revocable_provider *rp);
void revocable_free(struct revocable *rev);
void *revocable_try_access(struct revocable *rev) __acquires(&rev->rp->srcu);
void revocable_withdraw_access(struct revocable *rev) __releases(&rev->rp->srcu);

DEFINE_FREE(access_rev, struct revocable *, if (_T) revocable_withdraw_access(_T))

/**
 * REVOCABLE_TRY_ACCESS_WITH() - A helper for accessing revocable resource
 * @_rev: The consumer's ``struct revocable *`` handle.
 * @_res: A pointer variable that will be assigned the resource.
 *
 * The macro simplifies the access-release cycle for consumers, ensuring that
 * revocable_withdraw_access() is always called, even in the case of an early
 * exit.
 *
 * It creates a local variable in the current scope.  @_res is populated with
 * the result of revocable_try_access().  The consumer code **must** check if
 * @_res is ``NULL`` before using it.  The revocable_withdraw_access() function
 * is automatically called when the scope is exited.
 *
 * Note: It shares the same issue with guard() in cleanup.h.  No goto statements
 * are allowed before the helper.  Otherwise, the compiler fails with
 * "jump bypasses initialization of variable with __attribute__((cleanup))".
 */
#define REVOCABLE_TRY_ACCESS_WITH(_rev, _res)					\
	struct revocable *__UNIQUE_ID(name) __free(access_rev) = _rev;		\
	_res = revocable_try_access(_rev)

#define _REVOCABLE_TRY_ACCESS_SCOPED(_rev, _label, _res)			\
	for (struct revocable *__UNIQUE_ID(name) __free(access_rev) = _rev;	\
	     (_res = revocable_try_access(_rev)) || true; ({ goto _label; }))	\
		if (0) {							\
_label:										\
			break;							\
		} else

/**
 * REVOCABLE_TRY_ACCESS_SCOPED() - A helper for accessing revocable resource
 * @_rev: The consumer's ``struct revocable *`` handle.
 * @_res: A pointer variable that will be assigned the resource.
 *
 * Similar to REVOCABLE_TRY_ACCESS_WITH() but with an explicit scope from a
 * temporary ``for`` loop.
 */
#define REVOCABLE_TRY_ACCESS_SCOPED(_rev, _res)					\
	_REVOCABLE_TRY_ACCESS_SCOPED(_rev, __UNIQUE_ID(label), _res)

#endif /* __LINUX_REVOCABLE_H */
