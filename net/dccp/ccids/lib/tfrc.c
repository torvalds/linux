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

extern int  dccp_li_init(void);
extern void dccp_li_exit(void);
extern int packet_history_init(void);
extern void packet_history_exit(void);

static int __init tfrc_module_init(void)
{
	int rc = dccp_li_init();

	if (rc == 0) {
		rc = packet_history_init();
		if (rc != 0)
			dccp_li_exit();
	}

	return rc;
}

static void __exit tfrc_module_exit(void)
{
	packet_history_exit();
	dccp_li_exit();
}

module_init(tfrc_module_init);
module_exit(tfrc_module_exit);

MODULE_AUTHOR("Gerrit Renker <gerrit@erg.abdn.ac.uk>, "
	      "Ian McDonald <ian.mcdonald@jandi.co.nz>, "
	      "Arnaldo Carvalho de Melo <acme@redhat.com>");
MODULE_DESCRIPTION("DCCP TFRC library");
MODULE_LICENSE("GPL");
