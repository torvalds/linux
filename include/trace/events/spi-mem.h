/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM spi-mem

#undef TRACE_SYSTEM_VAR
#define TRACE_SYSTEM_VAR spi_mem

#if !defined(_TRACE_SPI_MEM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SPI_MEM_H

#include <linux/tracepoint.h>
#include <linux/spi/spi-mem.h>

#define decode_dtr(dtr) \
	__print_symbolic(dtr, \
		{ 0, "S" }, \
		{ 1, "D" })

TRACE_EVENT(spi_mem_start_op,
	TP_PROTO(struct spi_mem *mem, const struct spi_mem_op *op),
	TP_ARGS(mem, op),

	TP_STRUCT__entry(
		__string(name, mem->name)
		__dynamic_array(u8, op, 1 + op->addr.nbytes + op->dummy.nbytes)
		__dynamic_array(u8, data, op->data.dir == SPI_MEM_DATA_OUT ?
					  min(op->data.nbytes, 64) : 0)
		__field(u32, data_len)
		__field(u32, max_freq)
		__field(u8, cmd_buswidth)
		__field(bool, cmd_dtr)
		__field(u8, addr_buswidth)
		__field(bool, addr_dtr)
		__field(u8, dummy_nbytes)
		__field(u8, data_buswidth)
		__field(bool, data_dtr)
	),

	TP_fast_assign(
		int i;

		__assign_str(name);
		__entry->max_freq = op->max_freq ?: mem->spi->max_speed_hz;

		__entry->cmd_buswidth = op->cmd.buswidth;
		__entry->cmd_dtr = op->cmd.dtr;
		*((u8 *)__get_dynamic_array(op)) = op->cmd.opcode;

		__entry->addr_buswidth = op->addr.buswidth;
		__entry->addr_dtr = op->addr.dtr;
		for (i = 0; i < op->addr.nbytes; i++)
			((u8 *)__get_dynamic_array(op))[i + 1] =
				op->addr.val >> (8 * (op->addr.nbytes - i - 1));

		memset(((u8 *)__get_dynamic_array(op)) + op->addr.nbytes + 1,
		       0xff, op->dummy.nbytes);

		__entry->data_len = op->data.nbytes;
		__entry->data_buswidth = op->data.buswidth;
		__entry->data_dtr = op->data.dtr;
		if (op->data.dir == SPI_MEM_DATA_OUT)
			memcpy(__get_dynamic_array(data), op->data.buf.out,
			       __get_dynamic_array_len(data));
	),

	TP_printk("%s %u%s-%u%s-%u%s @%u Hz op=[%*phD] len=%u tx=[%*phD]",
		__get_str(name),
		__entry->cmd_buswidth, decode_dtr(__entry->cmd_dtr),
		__entry->addr_buswidth, decode_dtr(__entry->addr_dtr),
		__entry->data_buswidth, decode_dtr(__entry->data_dtr),
		__entry->max_freq,
		__get_dynamic_array_len(op), __get_dynamic_array(op),
		__entry->data_len,
		__get_dynamic_array_len(data), __get_dynamic_array(data))
);

TRACE_EVENT(spi_mem_stop_op,
	TP_PROTO(struct spi_mem *mem, const struct spi_mem_op *op),
	TP_ARGS(mem, op),

	TP_STRUCT__entry(
		__string(name, mem->name)
		__dynamic_array(u8, data, op->data.dir == SPI_MEM_DATA_IN ?
					  min(op->data.nbytes, 64) : 0)
		__field(u32, data_len)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->data_len = op->data.nbytes;
		if (op->data.dir == SPI_MEM_DATA_IN)
			memcpy(__get_dynamic_array(data), op->data.buf.in,
			       __get_dynamic_array_len(data));
	),

	TP_printk("%s len=%u rx=[%*phD]",
		__get_str(name),
		__entry->data_len,
		__get_dynamic_array_len(data), __get_dynamic_array(data))
);


#endif /* _TRACE_SPI_MEM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
