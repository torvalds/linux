/* $OpenBSD: kstat.h,v 1.6 2025/05/13 21:20:10 kettenis Exp $ */

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

#ifndef _SYS_KSTAT_H_
#define _SYS_KSTAT_H_

#include <sys/ioccom.h>

#define KSTAT_STRLEN		32

#define KSTAT_T_RAW		0
#define KSTAT_T_KV		1
#define KSTAT_T_COUNTERS	2

struct kstat_req {
	unsigned int		 ks_rflags;
#define KSTATIOC_F_IGNVER		(1 << 0)
	/* the current version of the kstat subsystem */
	unsigned int		 ks_version;

	uint64_t		 ks_id;

	char			 ks_provider[KSTAT_STRLEN];
	unsigned int		 ks_instance;
	char			 ks_name[KSTAT_STRLEN];
	unsigned int		 ks_unit;

	struct timespec		 ks_created;
	struct timespec		 ks_updated;
	struct timespec		 ks_interval;
	unsigned int		 ks_type;
	unsigned int		 ks_state;

	void			*ks_data;
	size_t			 ks_datalen;
	unsigned int		 ks_dataver;
};

/* ioctls */

#define KSTATIOC_VERSION	_IOR('k', 1, unsigned int)
#define KSTATIOC_FIND_ID	_IOWR('k', 2, struct kstat_req)
#define KSTATIOC_NFIND_ID	_IOWR('k', 3, struct kstat_req)
#define KSTATIOC_FIND_PROVIDER	_IOWR('k', 4, struct kstat_req)
#define KSTATIOC_NFIND_PROVIDER	_IOWR('k', 5, struct kstat_req)
#define KSTATIOC_FIND_NAME	_IOWR('k', 6, struct kstat_req)
#define KSTATIOC_NFIND_NAME	_IOWR('k', 7, struct kstat_req)

/* named data */

#define KSTAT_KV_NAMELEN	16
#define KSTAT_KV_ALIGN		sizeof(uint64_t)

enum kstat_kv_type {
	KSTAT_KV_T_NULL,
	KSTAT_KV_T_BOOL,
	KSTAT_KV_T_COUNTER64,
	KSTAT_KV_T_COUNTER32,
	KSTAT_KV_T_UINT64,
	KSTAT_KV_T_INT64,
	KSTAT_KV_T_UINT32,
	KSTAT_KV_T_INT32,
	KSTAT_KV_T_ISTR,	/* inline string */
	KSTAT_KV_T_STR,		/* trailing string */
	KSTAT_KV_T_BYTES,	/* trailing bytes */
	KSTAT_KV_T_TEMP,	/* temperature (uK) */
	KSTAT_KV_T_COUNTER16,
	KSTAT_KV_T_UINT16,
	KSTAT_KV_T_INT16,
	KSTAT_KV_T_FREQ,	/* frequency (Hz) */
	KSTAT_KV_T_VOLTS_DC,	/* voltage (uV DC) */
	KSTAT_KV_T_VOLTS_AC,	/* voltage (uV AC) */
	KSTAT_KV_T_AMPS,	/* current (uA) */
	KSTAT_KV_T_WATTS,	/* power (uW) */
};

/* units only apply to integer types */
enum kstat_kv_unit {
	KSTAT_KV_U_NONE = 0,
	KSTAT_KV_U_PACKETS,	/* packets */
	KSTAT_KV_U_BYTES,	/* bytes */
	KSTAT_KV_U_CYCLES,	/* cycles */
};

struct kstat_kv {
	char			 kv_key[KSTAT_KV_NAMELEN];
	union {
		char			v_istr[16];
		unsigned int		v_bool;	
		uint64_t		v_u64;
		int64_t			v_s64;
		uint32_t		v_u32;
		int32_t			v_s32;
		uint16_t		v_u16;
		int16_t			v_s16;
		size_t			v_len;
	}			 kv_v;
	enum kstat_kv_type	 kv_type;
	enum kstat_kv_unit	 kv_unit;
} __aligned(KSTAT_KV_ALIGN);

#define kstat_kv_istr(_kv)	(_kv)->kv_v.v_istr
#define kstat_kv_bool(_kv)	(_kv)->kv_v.v_bool
#define kstat_kv_u64(_kv)	(_kv)->kv_v.v_u64
#define kstat_kv_s64(_kv)	(_kv)->kv_v.v_s64
#define kstat_kv_u32(_kv)	(_kv)->kv_v.v_u32
#define kstat_kv_s32(_kv)	(_kv)->kv_v.v_s32
#define kstat_kv_u16(_kv)	(_kv)->kv_v.v_u16
#define kstat_kv_s16(_kv)	(_kv)->kv_v.v_s16
#define kstat_kv_len(_kv)	(_kv)->kv_v.v_len
#define kstat_kv_temp(_kv)	(_kv)->kv_v.v_u64
#define kstat_kv_freq(_kv)	(_kv)->kv_v.v_u64
#define kstat_kv_volts(_kv)	(_kv)->kv_v.v_u64
#define kstat_kv_amps(_kv)	(_kv)->kv_v.v_u64
#define kstat_kv_watts(_kv)	(_kv)->kv_v.v_u64

#ifdef _KERNEL

#include <sys/tree.h>

struct kstat_lock_ops;
struct rwlock;

struct kstat {
	uint64_t		  ks_id;

	const char		 *ks_provider;
	unsigned int		  ks_instance;
	const char		 *ks_name;
	unsigned int		  ks_unit;

	unsigned int		  ks_type;
	unsigned int		  ks_flags;
#define KSTAT_F_REALLOC			(1 << 0)
	unsigned int		  ks_state;
#define KSTAT_S_CREATED			0
#define KSTAT_S_INSTALLED		1

	struct timespec		  ks_created;
	RBT_ENTRY(kstat)	  ks_id_entry;
	RBT_ENTRY(kstat)	  ks_pv_entry;
	RBT_ENTRY(kstat)	  ks_nm_entry;

	/* the driver can update these between kstat creation and install */
	unsigned int		  ks_dataver;
	void			 *ks_softc;
	void			 *ks_ptr;
	int			(*ks_read)(struct kstat *);
	int			(*ks_copy)(struct kstat *, void *);

	const struct kstat_lock_ops *
				  ks_lock_ops;
	void			 *ks_lock;

	/* the data that is updated by ks_read */
	void			 *ks_data;
	size_t			  ks_datalen;
	struct timespec		  ks_updated;
	struct timespec		  ks_interval;
};

struct kstat	*kstat_create(const char *, unsigned int,
		     const char *, unsigned int,
		     unsigned int, unsigned int);

void		 kstat_set_rlock(struct kstat *, struct rwlock *);
void		 kstat_set_wlock(struct kstat *, struct rwlock *);
void		 kstat_set_mutex(struct kstat *, struct mutex *);
void		 kstat_set_cpu(struct kstat *, struct cpu_info *);

int		 kstat_read_nop(struct kstat *);

void		 kstat_install(struct kstat *);
void		 kstat_remove(struct kstat *);
void		 kstat_destroy(struct kstat *);

/*
 * kstat_kv api
 */

#define KSTAT_KV_UNIT_INITIALIZER(_key, _type, _unit) {	\
	.kv_key = (_key),				\
	.kv_type = (_type),				\
	.kv_unit = (_unit),				\
}

#define KSTAT_KV_INITIALIZER(_key, _type)		\
    KSTAT_KV_UNIT_INITIALIZER((_key), (_type), KSTAT_KV_U_NONE)

void	kstat_kv_init(struct kstat_kv *, const char *, enum kstat_kv_type);
void	kstat_kv_unit_init(struct kstat_kv *, const char *,
	    enum kstat_kv_type, enum kstat_kv_unit);

#endif /* _KERNEL */

#endif /* _SYS_KSTAT_H_ */
