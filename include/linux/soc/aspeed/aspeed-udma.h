#ifndef __ASPEED_UDMA_H__
#define __ASPEED_UDMA_H__

#include <linux/circ_buf.h>

typedef void (*aspeed_udma_cb_t)(int rb_rwptr, void *id);

enum aspeed_udma_ops {
	ASPEED_UDMA_OP_ENABLE,
	ASPEED_UDMA_OP_DISABLE,
	ASPEED_UDMA_OP_RESET,
};

void aspeed_udma_set_tx_wptr(u32 ch_no, u32 wptr);
void aspeed_udma_set_rx_rptr(u32 ch_no, u32 rptr);

void aspeed_udma_tx_chan_ctrl(u32 ch_no, enum aspeed_udma_ops op);
void aspeed_udma_rx_chan_ctrl(u32 ch_no, enum aspeed_udma_ops op);

int aspeed_udma_request_tx_chan(u32 ch_no, dma_addr_t addr,
				struct circ_buf *rb, u32 rb_sz,
				aspeed_udma_cb_t cb, void *id, bool en_tmout);
int aspeed_udma_request_rx_chan(u32 ch_no, dma_addr_t addr,
				struct circ_buf *rb, u32 rb_sz,
				aspeed_udma_cb_t cb, void *id, bool en_tmout);

int aspeed_udma_free_tx_chan(u32 ch_no);
int aspeed_udma_free_rx_chan(u32 ch_no);

#endif
