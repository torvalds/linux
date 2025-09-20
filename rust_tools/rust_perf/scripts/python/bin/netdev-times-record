#!/bin/bash
perf record -e net:net_dev_xmit -e net:net_dev_queue		\
		-e net:netif_receive_skb -e net:netif_rx		\
		-e skb:consume_skb -e skb:kfree_skb			\
		-e skb:skb_copy_datagram_iovec -e napi:napi_poll	\
		-e irq:irq_handler_entry -e irq:irq_handler_exit	\
		-e irq:softirq_entry -e irq:softirq_exit		\
		-e irq:softirq_raise $@
