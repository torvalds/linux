/* SPDX-License-Identifier: GPL-2.0 */
/*
 * External livepatch interfaces for patch creation tooling
 */

#ifndef _LINUX_LIVEPATCH_EXTERNAL_H_
#define _LINUX_LIVEPATCH_EXTERNAL_H_

#include <linux/types.h>

#define KLP_RELOC_SEC_PREFIX		".klp.rela."
#define KLP_SYM_PREFIX			".klp.sym."

#define __KLP_PRE_PATCH_PREFIX		__klp_pre_patch_callback_
#define __KLP_POST_PATCH_PREFIX		__klp_post_patch_callback_
#define __KLP_PRE_UNPATCH_PREFIX	__klp_pre_unpatch_callback_
#define __KLP_POST_UNPATCH_PREFIX	__klp_post_unpatch_callback_

#define KLP_PRE_PATCH_PREFIX		__stringify(__KLP_PRE_PATCH_PREFIX)
#define KLP_POST_PATCH_PREFIX		__stringify(__KLP_POST_PATCH_PREFIX)
#define KLP_PRE_UNPATCH_PREFIX		__stringify(__KLP_PRE_UNPATCH_PREFIX)
#define KLP_POST_UNPATCH_PREFIX		__stringify(__KLP_POST_UNPATCH_PREFIX)

struct klp_object;

typedef int (*klp_pre_patch_t)(struct klp_object *obj);
typedef void (*klp_post_patch_t)(struct klp_object *obj);
typedef void (*klp_pre_unpatch_t)(struct klp_object *obj);
typedef void (*klp_post_unpatch_t)(struct klp_object *obj);

/**
 * struct klp_callbacks - pre/post live-(un)patch callback structure
 * @pre_patch:		executed before code patching
 * @post_patch:		executed after code patching
 * @pre_unpatch:	executed before code unpatching
 * @post_unpatch:	executed after code unpatching
 * @post_unpatch_enabled:	flag indicating if post-unpatch callback
 *				should run
 *
 * All callbacks are optional.  Only the pre-patch callback, if provided,
 * will be unconditionally executed.  If the parent klp_object fails to
 * patch for any reason, including a non-zero error status returned from
 * the pre-patch callback, no further callbacks will be executed.
 */
struct klp_callbacks {
	klp_pre_patch_t		pre_patch;
	klp_post_patch_t	post_patch;
	klp_pre_unpatch_t	pre_unpatch;
	klp_post_unpatch_t	post_unpatch;
	bool post_unpatch_enabled;
};

/*
 * 'struct klp_{func,object}_ext' are compact "external" representations of
 * 'struct klp_{func,object}'.   They are used by objtool for livepatch
 * generation.  The structs are then read by the livepatch module and converted
 * to the real structs before calling klp_enable_patch().
 *
 * TODO make these the official API for klp_enable_patch().  That should
 * simplify livepatch's interface as well as its data structure lifetime
 * management.
 */
struct klp_func_ext {
	const char *old_name;
	void *new_func;
	unsigned long sympos;
};

struct klp_object_ext {
	const char *name;
	struct klp_func_ext *funcs;
	struct klp_callbacks callbacks;
	unsigned int nr_funcs;
};

#endif /* _LINUX_LIVEPATCH_EXTERNAL_H_ */
