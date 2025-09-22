/* $OpenBSD: kstat.c,v 1.5 2025/01/18 12:31:49 mglocker Exp $ */

/*
 * Copyright (c) 2020 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/time.h>

/* for kstat_set_cpu */
#include <sys/proc.h>
#include <sys/sched.h>

#include <sys/kstat.h>

RBT_HEAD(kstat_id_tree, kstat);

static inline int
kstat_id_cmp(const struct kstat *a, const struct kstat *b)
{
	if (a->ks_id > b->ks_id)
		return (1);
	if (a->ks_id < b->ks_id)
		return (-1);

	return (0);
}

RBT_PROTOTYPE(kstat_id_tree, kstat, ks_id_entry, kstat_id_cmp);

RBT_HEAD(kstat_pv_tree, kstat);

static inline int
kstat_pv_cmp(const struct kstat *a, const struct kstat *b)
{
	int rv;

	rv = strcmp(a->ks_provider, b->ks_provider);
	if (rv != 0)
		return (rv);

	if (a->ks_instance > b->ks_instance)
		return (1);
	if (a->ks_instance < b->ks_instance)
		return (-1);

	rv = strcmp(a->ks_name, b->ks_name);
	if (rv != 0)
		return (rv);

	if (a->ks_unit > b->ks_unit)
		return (1);
	if (a->ks_unit < b->ks_unit)
		return (-1);

	return (0);
}

RBT_PROTOTYPE(kstat_pv_tree, kstat, ks_pv_entry, kstat_pv_cmp);

RBT_HEAD(kstat_nm_tree, kstat);

static inline int
kstat_nm_cmp(const struct kstat *a, const struct kstat *b)
{
	int rv;

	rv = strcmp(a->ks_name, b->ks_name);
	if (rv != 0)
		return (rv);

	if (a->ks_unit > b->ks_unit)
		return (1);
	if (a->ks_unit < b->ks_unit)
		return (-1);

	rv = strcmp(a->ks_provider, b->ks_provider);
	if (rv != 0)
		return (rv);

	if (a->ks_instance > b->ks_instance)
		return (1);
	if (a->ks_instance < b->ks_instance)
		return (-1);

	return (0);
}

RBT_PROTOTYPE(kstat_nm_tree, kstat, ks_nm_entry, kstat_nm_cmp);

struct kstat_lock_ops {
	void	(*enter)(void *);
	void	(*leave)(void *);
};

#define kstat_enter(_ks) (_ks)->ks_lock_ops->enter((_ks)->ks_lock)
#define kstat_leave(_ks) (_ks)->ks_lock_ops->leave((_ks)->ks_lock)

const struct kstat_lock_ops kstat_rlock_ops = {
	(void (*)(void *))rw_enter_read,
	(void (*)(void *))rw_exit_read,
};

const struct kstat_lock_ops kstat_wlock_ops = {
	(void (*)(void *))rw_enter_write,
	(void (*)(void *))rw_exit_write,
};

const struct kstat_lock_ops kstat_mutex_ops = {
	(void (*)(void *))mtx_enter,
	(void (*)(void *))mtx_leave,
};

void kstat_cpu_enter(void *);
void kstat_cpu_leave(void *);

const struct kstat_lock_ops kstat_cpu_ops = {
	kstat_cpu_enter,
	kstat_cpu_leave,
};

struct rwlock		kstat_lock = RWLOCK_INITIALIZER("kstat");

/*
 * The global state is versioned so changes to the set of kstats
 * can be detected. This is an int so it can be read atomically on
 * any arch, which is a ridiculous optimisation, really.
 */
unsigned int		kstat_version = 0;

/*
 * kstat structures have a unique identifier so they can be found
 * quickly. Identifiers are 64bit in the hope that it won't wrap
 * during the runtime of a system. The identifiers start at 1 so that
 * 0 can be used as the first value for userland to iterate with.
 */
uint64_t			kstat_next_id = 1;

struct kstat_id_tree	kstat_id_tree = RBT_INITIALIZER();
struct kstat_pv_tree	kstat_pv_tree = RBT_INITIALIZER();
struct kstat_nm_tree	kstat_nm_tree = RBT_INITIALIZER();
struct pool		kstat_pool;

struct rwlock		kstat_default_lock = RWLOCK_INITIALIZER("kstatlk");

int	kstat_read(struct kstat *);
int	kstat_copy(struct kstat *, void *);

int
kstatattach(int num)
{
	/* XXX install system stats here */
	return (0);
}

int
kstatopen(dev_t dev, int flag, int mode, struct proc *p)
{
	return (0);
}

int
kstatclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (0);
}

int
kstatioc_enter(struct kstat_req *ksreq)
{
	int error;

	error = rw_enter(&kstat_lock, RW_READ | RW_INTR);
	if (error != 0)
		return (error);

	if (!ISSET(ksreq->ks_rflags, KSTATIOC_F_IGNVER) &&
	    ksreq->ks_version != kstat_version) {
		error = EINVAL;
		goto error;
	}

	return (0);

error:
	rw_exit(&kstat_lock);
	return (error);
}

int
kstatioc_leave(struct kstat_req *ksreq, struct kstat *ks)
{
	void *buf = NULL;
	size_t klen = 0, ulen = 0;
	struct timespec updated;
	int error = 0;

	if (ks == NULL) {
		error = ENOENT;
		goto error;
	}

	switch (ks->ks_state) {
	case KSTAT_S_CREATED:
		ksreq->ks_updated = ks->ks_created;
		ksreq->ks_interval.tv_sec = 0;
		ksreq->ks_interval.tv_nsec = 0;
		ksreq->ks_datalen = 0;
		ksreq->ks_dataver = 0;
		break;

	case KSTAT_S_INSTALLED:
		ksreq->ks_dataver = ks->ks_dataver;
		ksreq->ks_interval = ks->ks_interval;

		if (ksreq->ks_data == NULL) {
			/* userland doesn't want actual data, so shortcut */ 
			kstat_enter(ks);
			ksreq->ks_datalen = ks->ks_datalen;
			ksreq->ks_updated = ks->ks_updated;
			kstat_leave(ks);
			break;
		}

		klen = ks->ks_datalen; /* KSTAT_F_REALLOC */
		buf = malloc(klen, M_TEMP, M_WAITOK|M_CANFAIL);
		if (buf == NULL) {
			error = ENOMEM;
			goto error;
		}

		kstat_enter(ks);
		error = (*ks->ks_read)(ks);
		if (error == 0) {
			updated = ks->ks_updated;

			/* KSTAT_F_REALLOC */
			KASSERTMSG(ks->ks_datalen == klen,
			    "kstat doesn't support resized data yet");

			error = (*ks->ks_copy)(ks, buf);
		}
		kstat_leave(ks);

		if (error != 0)
			goto error;

		ulen = ksreq->ks_datalen;
		ksreq->ks_datalen = klen; /* KSTAT_F_REALLOC */
		ksreq->ks_updated = updated;
		break;
	default:
		panic("ks %p unexpected state %u", ks, ks->ks_state);
	}

	ksreq->ks_version = kstat_version;
	ksreq->ks_id = ks->ks_id;

	if (strlcpy(ksreq->ks_provider, ks->ks_provider,
	    sizeof(ksreq->ks_provider)) >= sizeof(ksreq->ks_provider))
		panic("kstat %p provider string has grown", ks);
	ksreq->ks_instance = ks->ks_instance;
	if (strlcpy(ksreq->ks_name, ks->ks_name,
	    sizeof(ksreq->ks_name)) >= sizeof(ksreq->ks_name))
		panic("kstat %p name string has grown", ks);
	ksreq->ks_unit = ks->ks_unit;

	ksreq->ks_created = ks->ks_created;
	ksreq->ks_type = ks->ks_type;
	ksreq->ks_state = ks->ks_state;

error:
	rw_exit(&kstat_lock);

	if (buf != NULL) {
		if (error == 0)
			error = copyout(buf, ksreq->ks_data, min(klen, ulen));

		free(buf, M_TEMP, klen);
	}

	return (error);
}

int
kstatioc_find_id(struct kstat_req *ksreq)
{
	struct kstat *ks, key;
	int error;

	error = kstatioc_enter(ksreq);
	if (error != 0)
		return (error);

	key.ks_id = ksreq->ks_id;

	ks = RBT_FIND(kstat_id_tree, &kstat_id_tree, &key);

	return (kstatioc_leave(ksreq, ks));
}

int
kstatioc_nfind_id(struct kstat_req *ksreq)
{
	struct kstat *ks, key;
	int error;

	error = kstatioc_enter(ksreq);
	if (error != 0)
		return (error);

	key.ks_id = ksreq->ks_id;

	ks = RBT_NFIND(kstat_id_tree, &kstat_id_tree, &key);

	return (kstatioc_leave(ksreq, ks));
}

int
kstatioc_find_pv(struct kstat_req *ksreq)
{
	struct kstat *ks, key;
	int error;

	error = kstatioc_enter(ksreq);
	if (error != 0)
		return (error);

	key.ks_provider = ksreq->ks_provider;
	key.ks_instance = ksreq->ks_instance;
	key.ks_name = ksreq->ks_name;
	key.ks_unit = ksreq->ks_unit;

	ks = RBT_FIND(kstat_pv_tree, &kstat_pv_tree, &key);

	return (kstatioc_leave(ksreq, ks));
}

int
kstatioc_nfind_pv(struct kstat_req *ksreq)
{
	struct kstat *ks, key;
	int error;

	error = kstatioc_enter(ksreq);
	if (error != 0)
		return (error);

	key.ks_provider = ksreq->ks_provider;
	key.ks_instance = ksreq->ks_instance;
	key.ks_name = ksreq->ks_name;
	key.ks_unit = ksreq->ks_unit;

	ks = RBT_NFIND(kstat_pv_tree, &kstat_pv_tree, &key);

	return (kstatioc_leave(ksreq, ks));
}

int
kstatioc_find_nm(struct kstat_req *ksreq)
{
	struct kstat *ks, key;
	int error;

	error = kstatioc_enter(ksreq);
	if (error != 0)
		return (error);

	key.ks_name = ksreq->ks_name;
	key.ks_unit = ksreq->ks_unit;
	key.ks_provider = ksreq->ks_provider;
	key.ks_instance = ksreq->ks_instance;

	ks = RBT_FIND(kstat_nm_tree, &kstat_nm_tree, &key);

	return (kstatioc_leave(ksreq, ks));
}

int
kstatioc_nfind_nm(struct kstat_req *ksreq)
{
	struct kstat *ks, key;
	int error;

	error = kstatioc_enter(ksreq);
	if (error != 0)
		return (error);

	key.ks_name = ksreq->ks_name;
	key.ks_unit = ksreq->ks_unit;
	key.ks_provider = ksreq->ks_provider;
	key.ks_instance = ksreq->ks_instance;

	ks = RBT_NFIND(kstat_nm_tree, &kstat_nm_tree, &key);

	return (kstatioc_leave(ksreq, ks));
}

int
kstatioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct kstat_req *ksreq = (struct kstat_req *)data;
	int error = 0;

	KERNEL_UNLOCK();

	switch (cmd) {
	case KSTATIOC_VERSION:
		*(unsigned int *)data = kstat_version;
		break;

	case KSTATIOC_FIND_ID:
		error = kstatioc_find_id(ksreq);
		break;
	case KSTATIOC_NFIND_ID:
		error = kstatioc_nfind_id(ksreq);
		break;
	case KSTATIOC_FIND_PROVIDER:
		error = kstatioc_find_pv(ksreq);
		break;
	case KSTATIOC_NFIND_PROVIDER:
		error = kstatioc_nfind_pv(ksreq);
		break;
	case KSTATIOC_FIND_NAME:
		error = kstatioc_find_nm(ksreq);
		break;
	case KSTATIOC_NFIND_NAME:
		error = kstatioc_nfind_nm(ksreq);
		break;

	default:
		error = ENOTTY;
		break;
	}

	KERNEL_LOCK();

	return (error);
}

void
kstat_init(void)
{
	static int initialized = 0;

	if (initialized)
		return;

	pool_init(&kstat_pool, sizeof(struct kstat), 0, IPL_NONE,
	    PR_WAITOK | PR_RWLOCK, "kstatmem", NULL);

	initialized = 1;
}

int
kstat_strcheck(const char *str)
{
	size_t i, l;

	l = strlen(str);
	if (l == 0 || l >= KSTAT_STRLEN)
		return (-1);
	for (i = 0; i < l; i++) {
		int ch = str[i];
		if (ch >= 'a' && ch <= 'z')
			continue;
		if (ch >= 'A' && ch <= 'Z')
			continue;
		if (ch >= '0' && ch <= '9')
			continue;
		switch (ch) {
		case '-':
		case '_':
		case '.':
			break;
		default:
			return (-1);
		}
	}

	return (0);
}

struct kstat *
kstat_create(const char *provider, unsigned int instance,
    const char *name, unsigned int unit,
    unsigned int type, unsigned int flags)
{
	struct kstat *ks, *oks;

	if (kstat_strcheck(provider) == -1)
		panic("invalid provider string");
	if (kstat_strcheck(name) == -1)
		panic("invalid name string");

	kstat_init();

	ks = pool_get(&kstat_pool, PR_WAITOK|PR_ZERO);

	ks->ks_provider = provider;
	ks->ks_instance = instance;
	ks->ks_name = name;
	ks->ks_unit = unit;
	ks->ks_flags = flags;
	ks->ks_type = type;
	ks->ks_state = KSTAT_S_CREATED;

	getnanouptime(&ks->ks_created);
	ks->ks_updated = ks->ks_created;

	ks->ks_lock = &kstat_default_lock;
	ks->ks_lock_ops = &kstat_wlock_ops;
	ks->ks_read = kstat_read;
	ks->ks_copy = kstat_copy;

	rw_enter_write(&kstat_lock);
	ks->ks_id = kstat_next_id;

	oks = RBT_INSERT(kstat_pv_tree, &kstat_pv_tree, ks);
	if (oks == NULL) {
		/* commit */
		kstat_next_id++;
		kstat_version++;

		oks = RBT_INSERT(kstat_nm_tree, &kstat_nm_tree, ks);
		if (oks != NULL)
			panic("kstat name collision! (%llu)", ks->ks_id);

		oks = RBT_INSERT(kstat_id_tree, &kstat_id_tree, ks);
		if (oks != NULL)
			panic("kstat id collision! (%llu)", ks->ks_id);
	}
	rw_exit_write(&kstat_lock);

	if (oks != NULL) {
		pool_put(&kstat_pool, ks);
		return (NULL);
	}

	return (ks);
}

void
kstat_set_rlock(struct kstat *ks, struct rwlock *rwl)
{
	KASSERT(ks->ks_state == KSTAT_S_CREATED);

	ks->ks_lock = rwl;
	ks->ks_lock_ops = &kstat_rlock_ops;
}

void
kstat_set_wlock(struct kstat *ks, struct rwlock *rwl)
{
	KASSERT(ks->ks_state == KSTAT_S_CREATED);

	ks->ks_lock = rwl;
	ks->ks_lock_ops = &kstat_wlock_ops;
}

void
kstat_set_mutex(struct kstat *ks, struct mutex *mtx)
{
	KASSERT(ks->ks_state == KSTAT_S_CREATED);

	ks->ks_lock = mtx;
	ks->ks_lock_ops = &kstat_mutex_ops;
}

void
kstat_cpu_enter(void *p)
{
	struct cpu_info *ci = p;
	sched_peg_curproc(ci);
}

void
kstat_cpu_leave(void *p)
{
	sched_unpeg_curproc();
}

void
kstat_set_cpu(struct kstat *ks, struct cpu_info *ci)
{
	KASSERT(ks->ks_state == KSTAT_S_CREATED);

	ks->ks_lock = ci;
	ks->ks_lock_ops = &kstat_cpu_ops;
}

int
kstat_read_nop(struct kstat *ks)
{
	return (0);
}

void
kstat_install(struct kstat *ks)
{
	if (!ISSET(ks->ks_flags, KSTAT_F_REALLOC)) {
		KASSERTMSG(ks->ks_copy != NULL || ks->ks_data != NULL,
		    "kstat %p %s:%u:%s:%u must provide ks_copy or ks_data", ks,
		    ks->ks_provider, ks->ks_instance, ks->ks_name, ks->ks_unit);
		KASSERT(ks->ks_datalen > 0);
	}

	rw_enter_write(&kstat_lock);
	ks->ks_state = KSTAT_S_INSTALLED;
	rw_exit_write(&kstat_lock);
}

void
kstat_remove(struct kstat *ks)
{
	rw_enter_write(&kstat_lock);
	KASSERTMSG(ks->ks_state == KSTAT_S_INSTALLED,
	    "kstat %p %s:%u:%s:%u is not installed", ks,
	    ks->ks_provider, ks->ks_instance, ks->ks_name, ks->ks_unit);
	  
	ks->ks_state = KSTAT_S_CREATED;
	rw_exit_write(&kstat_lock);
}

void
kstat_destroy(struct kstat *ks)
{
	rw_enter_write(&kstat_lock);
	RBT_REMOVE(kstat_id_tree, &kstat_id_tree, ks);
	RBT_REMOVE(kstat_pv_tree, &kstat_pv_tree, ks);
	RBT_REMOVE(kstat_nm_tree, &kstat_nm_tree, ks);
	kstat_version++;
	rw_exit_write(&kstat_lock);

	pool_put(&kstat_pool, ks);
}

int
kstat_read(struct kstat *ks)
{
	getnanouptime(&ks->ks_updated);
	return (0);
}

int
kstat_copy(struct kstat *ks, void *buf)
{
	memcpy(buf, ks->ks_data, ks->ks_datalen);
	return (0);
}

RBT_GENERATE(kstat_id_tree, kstat, ks_id_entry, kstat_id_cmp);
RBT_GENERATE(kstat_pv_tree, kstat, ks_pv_entry, kstat_pv_cmp);
RBT_GENERATE(kstat_nm_tree, kstat, ks_nm_entry, kstat_nm_cmp);

void
kstat_kv_init(struct kstat_kv *kv, const char *name, enum kstat_kv_type type)
{
	memset(kv, 0, sizeof(*kv));
	strlcpy(kv->kv_key, name, sizeof(kv->kv_key)); /* XXX truncated? */
	kv->kv_type = type;
	kv->kv_unit = KSTAT_KV_U_NONE;
}

void
kstat_kv_unit_init(struct kstat_kv *kv, const char *name,
    enum kstat_kv_type type, enum kstat_kv_unit unit)
{
	switch (type) {
	case KSTAT_KV_T_COUNTER64:
	case KSTAT_KV_T_COUNTER32:
	case KSTAT_KV_T_COUNTER16:
	case KSTAT_KV_T_UINT64:
	case KSTAT_KV_T_INT64:
	case KSTAT_KV_T_UINT32:
	case KSTAT_KV_T_INT32:
	case KSTAT_KV_T_UINT16:
	case KSTAT_KV_T_INT16:
		break;
	default:
		panic("kv unit init %s: unit for non-integer type", name);
	}

	memset(kv, 0, sizeof(*kv));
	strlcpy(kv->kv_key, name, sizeof(kv->kv_key)); /* XXX truncated? */
	kv->kv_type = type;
	kv->kv_unit = unit;
}
