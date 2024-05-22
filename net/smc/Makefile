# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(src)
obj-$(CONFIG_SMC)	+= smc.o
obj-$(CONFIG_SMC_DIAG)	+= smc_diag.o
smc-y := af_smc.o smc_pnet.o smc_ib.o smc_clc.o smc_core.o smc_wr.o smc_llc.o
smc-y += smc_cdc.o smc_tx.o smc_rx.o smc_close.o smc_ism.o smc_netlink.o smc_stats.o
smc-y += smc_tracepoint.o
smc-$(CONFIG_SYSCTL) += smc_sysctl.o
smc-$(CONFIG_SMC_LO) += smc_loopback.o
