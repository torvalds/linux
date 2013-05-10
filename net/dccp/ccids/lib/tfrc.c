/*
 * TFRC library initialisation
 *
 * Copyright (c) 2007 The University of Aberdeen, Scotland, UK
 * Copyright (c) 2007 Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "tfrc.h"

#ifdef CONFIG_IP_DCCP_TFRC_DEBUG
int tfrc_debug;
module_param(tfrc_debug, bool, 0644);
MODULE_PARM_DESC(tfrc_debug, "Enable TFRC debug messages");
#endif

int __init tfrc_lib_init(void)
{
	int rc = tfrc_li_init();

	if (rc)
		goto out;

	rc = tfrc_tx_packet_history_init();
	if (rc)
		goto out_free_loss_intervals;

	rc = tfrc_rx_packet_history_init();
	if (rc)
		goto out_free_tx_history;
	return 0;

out_free_tx_history:
	tfrc_tx_packet_history_exit();
out_free_loss_intervals:
	tfrc_li_exit();
out:
	return rc;
}

void tfrc_lib_exit(void)
{
	tfrc_rx_packet_history_exit();
	tfrc_tx_packet_history_exit();
	tfrc_li_exit();
}
