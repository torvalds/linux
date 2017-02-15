#undef TRACE_SYSTEM
#define TRACE_SYSTEM bpf

#if !defined(_TRACE_BPF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BPF_H

#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/fs.h>
#include <linux/tracepoint.h>

#define __PROG_TYPE_MAP(FN)	\
	FN(SOCKET_FILTER)	\
	FN(KPROBE)		\
	FN(SCHED_CLS)		\
	FN(SCHED_ACT)		\
	FN(TRACEPOINT)		\
	FN(XDP)			\
	FN(PERF_EVENT)		\
	FN(CGROUP_SKB)		\
	FN(CGROUP_SOCK)		\
	FN(LWT_IN)		\
	FN(LWT_OUT)		\
	FN(LWT_XMIT)

#define __MAP_TYPE_MAP(FN)	\
	FN(HASH)		\
	FN(ARRAY)		\
	FN(PROG_ARRAY)		\
	FN(PERF_EVENT_ARRAY)	\
	FN(PERCPU_HASH)		\
	FN(PERCPU_ARRAY)	\
	FN(STACK_TRACE)		\
	FN(CGROUP_ARRAY)	\
	FN(LRU_HASH)		\
	FN(LRU_PERCPU_HASH)	\
	FN(LPM_TRIE)

#define __PROG_TYPE_TP_FN(x)	\
	TRACE_DEFINE_ENUM(BPF_PROG_TYPE_##x);
#define __PROG_TYPE_SYM_FN(x)	\
	{ BPF_PROG_TYPE_##x, #x },
#define __PROG_TYPE_SYM_TAB	\
	__PROG_TYPE_MAP(__PROG_TYPE_SYM_FN) { -1, 0 }
__PROG_TYPE_MAP(__PROG_TYPE_TP_FN)

#define __MAP_TYPE_TP_FN(x)	\
	TRACE_DEFINE_ENUM(BPF_MAP_TYPE_##x);
#define __MAP_TYPE_SYM_FN(x)	\
	{ BPF_MAP_TYPE_##x, #x },
#define __MAP_TYPE_SYM_TAB	\
	__MAP_TYPE_MAP(__MAP_TYPE_SYM_FN) { -1, 0 }
__MAP_TYPE_MAP(__MAP_TYPE_TP_FN)

DECLARE_EVENT_CLASS(bpf_prog_event,

	TP_PROTO(const struct bpf_prog *prg),

	TP_ARGS(prg),

	TP_STRUCT__entry(
		__array(u8, prog_tag, 8)
		__field(u32, type)
	),

	TP_fast_assign(
		BUILD_BUG_ON(sizeof(__entry->prog_tag) != sizeof(prg->tag));
		memcpy(__entry->prog_tag, prg->tag, sizeof(prg->tag));
		__entry->type = prg->type;
	),

	TP_printk("prog=%s type=%s",
		  __print_hex_str(__entry->prog_tag, 8),
		  __print_symbolic(__entry->type, __PROG_TYPE_SYM_TAB))
);

DEFINE_EVENT(bpf_prog_event, bpf_prog_get_type,

	TP_PROTO(const struct bpf_prog *prg),

	TP_ARGS(prg)
);

DEFINE_EVENT(bpf_prog_event, bpf_prog_put_rcu,

	TP_PROTO(const struct bpf_prog *prg),

	TP_ARGS(prg)
);

TRACE_EVENT(bpf_prog_load,

	TP_PROTO(const struct bpf_prog *prg, int ufd),

	TP_ARGS(prg, ufd),

	TP_STRUCT__entry(
		__array(u8, prog_tag, 8)
		__field(u32, type)
		__field(int, ufd)
	),

	TP_fast_assign(
		BUILD_BUG_ON(sizeof(__entry->prog_tag) != sizeof(prg->tag));
		memcpy(__entry->prog_tag, prg->tag, sizeof(prg->tag));
		__entry->type = prg->type;
		__entry->ufd  = ufd;
	),

	TP_printk("prog=%s type=%s ufd=%d",
		  __print_hex_str(__entry->prog_tag, 8),
		  __print_symbolic(__entry->type, __PROG_TYPE_SYM_TAB),
		  __entry->ufd)
);

TRACE_EVENT(bpf_map_create,

	TP_PROTO(const struct bpf_map *map, int ufd),

	TP_ARGS(map, ufd),

	TP_STRUCT__entry(
		__field(u32, type)
		__field(u32, size_key)
		__field(u32, size_value)
		__field(u32, max_entries)
		__field(u32, flags)
		__field(int, ufd)
	),

	TP_fast_assign(
		__entry->type        = map->map_type;
		__entry->size_key    = map->key_size;
		__entry->size_value  = map->value_size;
		__entry->max_entries = map->max_entries;
		__entry->flags       = map->map_flags;
		__entry->ufd         = ufd;
	),

	TP_printk("map type=%s ufd=%d key=%u val=%u max=%u flags=%x",
		  __print_symbolic(__entry->type, __MAP_TYPE_SYM_TAB),
		  __entry->ufd, __entry->size_key, __entry->size_value,
		  __entry->max_entries, __entry->flags)
);

DECLARE_EVENT_CLASS(bpf_obj_prog,

	TP_PROTO(const struct bpf_prog *prg, int ufd,
		 const struct filename *pname),

	TP_ARGS(prg, ufd, pname),

	TP_STRUCT__entry(
		__array(u8, prog_tag, 8)
		__field(int, ufd)
		__string(path, pname->name)
	),

	TP_fast_assign(
		BUILD_BUG_ON(sizeof(__entry->prog_tag) != sizeof(prg->tag));
		memcpy(__entry->prog_tag, prg->tag, sizeof(prg->tag));
		__assign_str(path, pname->name);
		__entry->ufd = ufd;
	),

	TP_printk("prog=%s path=%s ufd=%d",
		  __print_hex_str(__entry->prog_tag, 8),
		  __get_str(path), __entry->ufd)
);

DEFINE_EVENT(bpf_obj_prog, bpf_obj_pin_prog,

	TP_PROTO(const struct bpf_prog *prg, int ufd,
		 const struct filename *pname),

	TP_ARGS(prg, ufd, pname)
);

DEFINE_EVENT(bpf_obj_prog, bpf_obj_get_prog,

	TP_PROTO(const struct bpf_prog *prg, int ufd,
		 const struct filename *pname),

	TP_ARGS(prg, ufd, pname)
);

DECLARE_EVENT_CLASS(bpf_obj_map,

	TP_PROTO(const struct bpf_map *map, int ufd,
		 const struct filename *pname),

	TP_ARGS(map, ufd, pname),

	TP_STRUCT__entry(
		__field(u32, type)
		__field(int, ufd)
		__string(path, pname->name)
	),

	TP_fast_assign(
		__assign_str(path, pname->name);
		__entry->type = map->map_type;
		__entry->ufd  = ufd;
	),

	TP_printk("map type=%s ufd=%d path=%s",
		  __print_symbolic(__entry->type, __MAP_TYPE_SYM_TAB),
		  __entry->ufd, __get_str(path))
);

DEFINE_EVENT(bpf_obj_map, bpf_obj_pin_map,

	TP_PROTO(const struct bpf_map *map, int ufd,
		 const struct filename *pname),

	TP_ARGS(map, ufd, pname)
);

DEFINE_EVENT(bpf_obj_map, bpf_obj_get_map,

	TP_PROTO(const struct bpf_map *map, int ufd,
		 const struct filename *pname),

	TP_ARGS(map, ufd, pname)
);

DECLARE_EVENT_CLASS(bpf_map_keyval,

	TP_PROTO(const struct bpf_map *map, int ufd,
		 const void *key, const void *val),

	TP_ARGS(map, ufd, key, val),

	TP_STRUCT__entry(
		__field(u32, type)
		__field(u32, key_len)
		__dynamic_array(u8, key, map->key_size)
		__field(bool, key_trunc)
		__field(u32, val_len)
		__dynamic_array(u8, val, map->value_size)
		__field(bool, val_trunc)
		__field(int, ufd)
	),

	TP_fast_assign(
		memcpy(__get_dynamic_array(key), key, map->key_size);
		memcpy(__get_dynamic_array(val), val, map->value_size);
		__entry->type      = map->map_type;
		__entry->key_len   = min(map->key_size, 16U);
		__entry->key_trunc = map->key_size != __entry->key_len;
		__entry->val_len   = min(map->value_size, 16U);
		__entry->val_trunc = map->value_size != __entry->val_len;
		__entry->ufd       = ufd;
	),

	TP_printk("map type=%s ufd=%d key=[%s%s] val=[%s%s]",
		  __print_symbolic(__entry->type, __MAP_TYPE_SYM_TAB),
		  __entry->ufd,
		  __print_hex(__get_dynamic_array(key), __entry->key_len),
		  __entry->key_trunc ? " ..." : "",
		  __print_hex(__get_dynamic_array(val), __entry->val_len),
		  __entry->val_trunc ? " ..." : "")
);

DEFINE_EVENT(bpf_map_keyval, bpf_map_lookup_elem,

	TP_PROTO(const struct bpf_map *map, int ufd,
		 const void *key, const void *val),

	TP_ARGS(map, ufd, key, val)
);

DEFINE_EVENT(bpf_map_keyval, bpf_map_update_elem,

	TP_PROTO(const struct bpf_map *map, int ufd,
		 const void *key, const void *val),

	TP_ARGS(map, ufd, key, val)
);

TRACE_EVENT(bpf_map_delete_elem,

	TP_PROTO(const struct bpf_map *map, int ufd,
		 const void *key),

	TP_ARGS(map, ufd, key),

	TP_STRUCT__entry(
		__field(u32, type)
		__field(u32, key_len)
		__dynamic_array(u8, key, map->key_size)
		__field(bool, key_trunc)
		__field(int, ufd)
	),

	TP_fast_assign(
		memcpy(__get_dynamic_array(key), key, map->key_size);
		__entry->type      = map->map_type;
		__entry->key_len   = min(map->key_size, 16U);
		__entry->key_trunc = map->key_size != __entry->key_len;
		__entry->ufd       = ufd;
	),

	TP_printk("map type=%s ufd=%d key=[%s%s]",
		  __print_symbolic(__entry->type, __MAP_TYPE_SYM_TAB),
		  __entry->ufd,
		  __print_hex(__get_dynamic_array(key), __entry->key_len),
		  __entry->key_trunc ? " ..." : "")
);

TRACE_EVENT(bpf_map_next_key,

	TP_PROTO(const struct bpf_map *map, int ufd,
		 const void *key, const void *key_next),

	TP_ARGS(map, ufd, key, key_next),

	TP_STRUCT__entry(
		__field(u32, type)
		__field(u32, key_len)
		__dynamic_array(u8, key, map->key_size)
		__dynamic_array(u8, nxt, map->key_size)
		__field(bool, key_trunc)
		__field(int, ufd)
	),

	TP_fast_assign(
		memcpy(__get_dynamic_array(key), key, map->key_size);
		memcpy(__get_dynamic_array(nxt), key_next, map->key_size);
		__entry->type      = map->map_type;
		__entry->key_len   = min(map->key_size, 16U);
		__entry->key_trunc = map->key_size != __entry->key_len;
		__entry->ufd       = ufd;
	),

	TP_printk("map type=%s ufd=%d key=[%s%s] next=[%s%s]",
		  __print_symbolic(__entry->type, __MAP_TYPE_SYM_TAB),
		  __entry->ufd,
		  __print_hex(__get_dynamic_array(key), __entry->key_len),
		  __entry->key_trunc ? " ..." : "",
		  __print_hex(__get_dynamic_array(nxt), __entry->key_len),
		  __entry->key_trunc ? " ..." : "")
);

#endif /* _TRACE_BPF_H */

#include <trace/define_trace.h>
