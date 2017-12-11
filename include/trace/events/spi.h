/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM spi

#if !defined(_TRACE_SPI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SPI_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(spi_controller,

	TP_PROTO(struct spi_controller *controller),

	TP_ARGS(controller),

	TP_STRUCT__entry(
		__field(        int,           bus_num             )
	),

	TP_fast_assign(
		__entry->bus_num = controller->bus_num;
	),

	TP_printk("spi%d", (int)__entry->bus_num)

);

DEFINE_EVENT(spi_controller, spi_controller_idle,

	TP_PROTO(struct spi_controller *controller),

	TP_ARGS(controller)

);

DEFINE_EVENT(spi_controller, spi_controller_busy,

	TP_PROTO(struct spi_controller *controller),

	TP_ARGS(controller)

);

DECLARE_EVENT_CLASS(spi_message,

	TP_PROTO(struct spi_message *msg),

	TP_ARGS(msg),

	TP_STRUCT__entry(
		__field(        int,            bus_num         )
		__field(        int,            chip_select     )
		__field(        struct spi_message *,   msg     )
	),

	TP_fast_assign(
		__entry->bus_num = msg->spi->controller->bus_num;
		__entry->chip_select = msg->spi->chip_select;
		__entry->msg = msg;
	),

        TP_printk("spi%d.%d %p", (int)__entry->bus_num,
		  (int)__entry->chip_select,
		  (struct spi_message *)__entry->msg)
);

DEFINE_EVENT(spi_message, spi_message_submit,

	TP_PROTO(struct spi_message *msg),

	TP_ARGS(msg)

);

DEFINE_EVENT(spi_message, spi_message_start,

	TP_PROTO(struct spi_message *msg),

	TP_ARGS(msg)

);

TRACE_EVENT(spi_message_done,

	TP_PROTO(struct spi_message *msg),

	TP_ARGS(msg),

	TP_STRUCT__entry(
		__field(        int,            bus_num         )
		__field(        int,            chip_select     )
		__field(        struct spi_message *,   msg     )
		__field(        unsigned,       frame           )
		__field(        unsigned,       actual          )
	),

	TP_fast_assign(
		__entry->bus_num = msg->spi->controller->bus_num;
		__entry->chip_select = msg->spi->chip_select;
		__entry->msg = msg;
		__entry->frame = msg->frame_length;
		__entry->actual = msg->actual_length;
	),

        TP_printk("spi%d.%d %p len=%u/%u", (int)__entry->bus_num,
		  (int)__entry->chip_select,
		  (struct spi_message *)__entry->msg,
                  (unsigned)__entry->actual, (unsigned)__entry->frame)
);

DECLARE_EVENT_CLASS(spi_transfer,

	TP_PROTO(struct spi_message *msg, struct spi_transfer *xfer),

	TP_ARGS(msg, xfer),

	TP_STRUCT__entry(
		__field(        int,            bus_num         )
		__field(        int,            chip_select     )
		__field(        struct spi_transfer *,   xfer   )
		__field(        int,            len             )
	),

	TP_fast_assign(
		__entry->bus_num = msg->spi->controller->bus_num;
		__entry->chip_select = msg->spi->chip_select;
		__entry->xfer = xfer;
		__entry->len = xfer->len;
	),

        TP_printk("spi%d.%d %p len=%d", (int)__entry->bus_num,
		  (int)__entry->chip_select,
		  (struct spi_message *)__entry->xfer,
		  (int)__entry->len)
);

DEFINE_EVENT(spi_transfer, spi_transfer_start,

	TP_PROTO(struct spi_message *msg, struct spi_transfer *xfer),

	TP_ARGS(msg, xfer)

);

DEFINE_EVENT(spi_transfer, spi_transfer_stop,

	TP_PROTO(struct spi_message *msg, struct spi_transfer *xfer),

	TP_ARGS(msg, xfer)

);

#endif /* _TRACE_POWER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
