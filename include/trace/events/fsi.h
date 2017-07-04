
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fsi

#if !defined(_TRACE_FSI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FSI_H

#include <linux/tracepoint.h>

TRACE_EVENT(fsi_master_read,
	TP_PROTO(const struct fsi_master *master, int link, int id,
			uint32_t addr, size_t size),
	TP_ARGS(master, link, id, addr, size),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(int,	link)
		__field(int,	id)
		__field(__u32,	addr)
		__field(size_t,	size)
	),
	TP_fast_assign(
		__entry->master_idx = master->idx;
		__entry->link = link;
		__entry->id = id;
		__entry->addr = addr;
		__entry->size = size;
	),
	TP_printk("fsi%d:%02d:%02d %08x[%zd]",
		__entry->master_idx,
		__entry->link,
		__entry->id,
		__entry->addr,
		__entry->size
	)
);

TRACE_EVENT(fsi_master_write,
	TP_PROTO(const struct fsi_master *master, int link, int id,
			uint32_t addr, size_t size, const void *data),
	TP_ARGS(master, link, id, addr, size, data),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(int,	link)
		__field(int,	id)
		__field(__u32,	addr)
		__field(size_t,	size)
		__field(__u32,	data)
	),
	TP_fast_assign(
		__entry->master_idx = master->idx;
		__entry->link = link;
		__entry->id = id;
		__entry->addr = addr;
		__entry->size = size;
		__entry->data = 0;
		memcpy(&__entry->data, data, size);
	),
	TP_printk("fsi%d:%02d:%02d %08x[%zd] <= {%*ph}",
		__entry->master_idx,
		__entry->link,
		__entry->id,
		__entry->addr,
		__entry->size,
		(int)__entry->size, &__entry->data
	)
);

TRACE_EVENT(fsi_master_rw_result,
	TP_PROTO(const struct fsi_master *master, int link, int id,
			uint32_t addr, size_t size,
			bool write, const void *data, int ret),
	TP_ARGS(master, link, id, addr, size, write, data, ret),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(int,	link)
		__field(int,	id)
		__field(__u32,	addr)
		__field(size_t,	size)
		__field(bool,	write)
		__field(__u32,	data)
		__field(int,	ret)
	),
	TP_fast_assign(
		__entry->master_idx = master->idx;
		__entry->link = link;
		__entry->id = id;
		__entry->addr = addr;
		__entry->size = size;
		__entry->write = write;
		__entry->data = 0;
		__entry->ret = ret;
		if (__entry->write || !__entry->ret)
			memcpy(&__entry->data, data, size);
	),
	TP_printk("fsi%d:%02d:%02d %08x[%zd] %s {%*ph} ret %d",
		__entry->master_idx,
		__entry->link,
		__entry->id,
		__entry->addr,
		__entry->size,
		__entry->write ? "<=" : "=>",
		(int)__entry->size, &__entry->data,
		__entry->ret
	)
);

TRACE_EVENT(fsi_master_break,
	TP_PROTO(const struct fsi_master *master, int link),
	TP_ARGS(master, link),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(int,	link)
	),
	TP_fast_assign(
		__entry->master_idx = master->idx;
		__entry->link = link;
	),
	TP_printk("fsi%d:%d",
		__entry->master_idx,
		__entry->link
	)
);


#endif /* _TRACE_FSI_H */

#include <trace/define_trace.h>
