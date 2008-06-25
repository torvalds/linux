/*
 * TFRC: main module holding the pieces of the TFRC library together
 *
 * Copyright (c) 2007 The University of Aberdeen, Scotland, UK
 * Copyright (c) 2007 Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include "tfrc.h"

#ifdef CONFIG_IP_DCCP_TFRC_DEBUG
int tfrc_debug;
module_param(tfrc_debug, bool, 0444);
MODULE_PARM_DESC(tfrc_debug, "Enable debug messages");
#endif

static int __init tfrc_module_init(void)
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

static void __exit tfrc_module_exit(void)
{
	tfrc_rx_packet_history_exit();
	tfrc_tx_packet_history_exit();
	tfrc_li_exit();
}

module_init(tfrc_module_init);
module_exit(tfrc_module_exit);

MODULE_AUTHOR("Gerrit Renker <gerrit@erg.abdn.ac.uk>, "
	      "Ian McDonald <ian.mcdonald@jandi.co.nz>, "
	      "Arnaldo Carvalho de Melo <acme@redhat.com>");
MODULE_DESCRIPTION("DCCP TFRC library");
MODULE_LICENSE("GPL");
