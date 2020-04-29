// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/snmp.h>
#include <net/tls.h>

#ifdef CONFIG_PROC_FS
static const struct snmp_mib tls_mib_list[] = {
	SNMP_MIB_ITEM("TlsCurrTxSw", LINUX_MIB_TLSCURRTXSW),
	SNMP_MIB_ITEM("TlsCurrRxSw", LINUX_MIB_TLSCURRRXSW),
	SNMP_MIB_ITEM("TlsCurrTxDevice", LINUX_MIB_TLSCURRTXDEVICE),
	SNMP_MIB_ITEM("TlsCurrRxDevice", LINUX_MIB_TLSCURRRXDEVICE),
	SNMP_MIB_ITEM("TlsTxSw", LINUX_MIB_TLSTXSW),
	SNMP_MIB_ITEM("TlsRxSw", LINUX_MIB_TLSRXSW),
	SNMP_MIB_ITEM("TlsTxDevice", LINUX_MIB_TLSTXDEVICE),
	SNMP_MIB_ITEM("TlsRxDevice", LINUX_MIB_TLSRXDEVICE),
	SNMP_MIB_ITEM("TlsDecryptError", LINUX_MIB_TLSDECRYPTERROR),
	SNMP_MIB_ITEM("TlsRxDeviceResync", LINUX_MIB_TLSRXDEVICERESYNC),
	SNMP_MIB_SENTINEL
};

static int tls_statistics_seq_show(struct seq_file *seq, void *v)
{
	unsigned long buf[LINUX_MIB_TLSMAX] = {};
	struct net *net = seq->private;
	int i;

	snmp_get_cpu_field_batch(buf, tls_mib_list, net->mib.tls_statistics);
	for (i = 0; tls_mib_list[i].name; i++)
		seq_printf(seq, "%-32s\t%lu\n", tls_mib_list[i].name, buf[i]);

	return 0;
}
#endif

int __net_init tls_proc_init(struct net *net)
{
	if (!proc_create_net_single("tls_stat", 0444, net->proc_net,
				    tls_statistics_seq_show, NULL))
		return -ENOMEM;
	return 0;
}

void __net_exit tls_proc_fini(struct net *net)
{
	remove_proc_entry("tls_stat", net->proc_net);
}
