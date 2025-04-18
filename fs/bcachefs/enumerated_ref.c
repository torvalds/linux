// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "enumerated_ref.h"
#include "util.h"

#include <linux/completion.h>

#ifdef ENUMERATED_REF_DEBUG
void enumerated_ref_get(struct enumerated_ref *ref, unsigned idx)
{
	BUG_ON(idx >= ref->nr);
	atomic_long_inc(&ref->refs[idx]);
}

bool __enumerated_ref_tryget(struct enumerated_ref *ref, unsigned idx)
{
	BUG_ON(idx >= ref->nr);
	return atomic_long_inc_not_zero(&ref->refs[idx]);
}

bool enumerated_ref_tryget(struct enumerated_ref *ref, unsigned idx)
{
	BUG_ON(idx >= ref->nr);
	return !ref->dying &&
		atomic_long_inc_not_zero(&ref->refs[idx]);
}

void enumerated_ref_put(struct enumerated_ref *ref, unsigned idx)
{
	BUG_ON(idx >= ref->nr);
	long v = atomic_long_dec_return(&ref->refs[idx]);

	BUG_ON(v < 0);
	if (v)
		return;

	for (unsigned i = 0; i < ref->nr; i++)
		if (atomic_long_read(&ref->refs[i]))
			return;

	if (ref->stop_fn)
		ref->stop_fn(ref);
	complete(&ref->stop_complete);
}
#endif

#ifndef ENUMERATED_REF_DEBUG
static void enumerated_ref_kill_cb(struct percpu_ref *percpu_ref)
{
	struct enumerated_ref *ref =
		container_of(percpu_ref, struct enumerated_ref, ref);

	if (ref->stop_fn)
		ref->stop_fn(ref);
	complete(&ref->stop_complete);
}
#endif

void enumerated_ref_stop_async(struct enumerated_ref *ref)
{
	reinit_completion(&ref->stop_complete);

#ifndef ENUMERATED_REF_DEBUG
	percpu_ref_kill(&ref->ref);
#else
	ref->dying = true;
	for (unsigned i = 0; i < ref->nr; i++)
		enumerated_ref_put(ref, i);
#endif
}

void enumerated_ref_stop(struct enumerated_ref *ref,
			 const char * const names[])
{
	enumerated_ref_stop_async(ref);
	while (!wait_for_completion_timeout(&ref->stop_complete, HZ * 10)) {
		struct printbuf buf = PRINTBUF;

		prt_str(&buf, "Waited for 10 seconds to shutdown enumerated ref\n");
		prt_str(&buf, "Outstanding refs:\n");
		enumerated_ref_to_text(&buf, ref, names);
		printk(KERN_ERR "%s", buf.buf);
		printbuf_exit(&buf);
	}
}

void enumerated_ref_start(struct enumerated_ref *ref)
{
#ifndef ENUMERATED_REF_DEBUG
	percpu_ref_reinit(&ref->ref);
#else
	ref->dying = false;
	for (unsigned i = 0; i < ref->nr; i++) {
		BUG_ON(atomic_long_read(&ref->refs[i]));
		atomic_long_inc(&ref->refs[i]);
	}
#endif
}

void enumerated_ref_exit(struct enumerated_ref *ref)
{
#ifndef ENUMERATED_REF_DEBUG
	percpu_ref_exit(&ref->ref);
#else
	kfree(ref->refs);
	ref->refs = NULL;
	ref->nr = 0;
#endif
}

int enumerated_ref_init(struct enumerated_ref *ref, unsigned nr,
			void (*stop_fn)(struct enumerated_ref *))
{
	init_completion(&ref->stop_complete);
	ref->stop_fn = stop_fn;

#ifndef ENUMERATED_REF_DEBUG
	return percpu_ref_init(&ref->ref, enumerated_ref_kill_cb,
			    PERCPU_REF_INIT_DEAD, GFP_KERNEL);
#else
	ref->refs = kzalloc(sizeof(ref->refs[0]) * nr, GFP_KERNEL);
	if (!ref->refs)
		return -ENOMEM;

	ref->nr = nr;
	return 0;
#endif
}

void enumerated_ref_to_text(struct printbuf *out,
			    struct enumerated_ref *ref,
			    const char * const names[])
{
#ifdef ENUMERATED_REF_DEBUG
	bch2_printbuf_tabstop_push(out, 32);

	for (unsigned i = 0; i < ref->nr; i++)
		prt_printf(out, "%s\t%li\n", names[i],
			   atomic_long_read(&ref->refs[i]));
#else
	prt_str(out, "(not in debug mode)\n");
#endif
}
