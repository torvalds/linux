# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I $(src)		# needed for trace events

obj-$(CONFIG_RV) += rv.o
obj-$(CONFIG_RV_MON_WIP) += monitors/wip/wip.o
obj-$(CONFIG_RV_MON_WWNR) += monitors/wwnr/wwnr.o
obj-$(CONFIG_RV_MON_SCHED) += monitors/sched/sched.o
obj-$(CONFIG_RV_MON_SCO) += monitors/sco/sco.o
obj-$(CONFIG_RV_MON_SNROC) += monitors/snroc/snroc.o
obj-$(CONFIG_RV_MON_SCPD) += monitors/scpd/scpd.o
obj-$(CONFIG_RV_MON_SNEP) += monitors/snep/snep.o
obj-$(CONFIG_RV_MON_RTAPP) += monitors/rtapp/rtapp.o
obj-$(CONFIG_RV_MON_PAGEFAULT) += monitors/pagefault/pagefault.o
obj-$(CONFIG_RV_MON_SLEEP) += monitors/sleep/sleep.o
obj-$(CONFIG_RV_MON_STS) += monitors/sts/sts.o
obj-$(CONFIG_RV_MON_NRP) += monitors/nrp/nrp.o
obj-$(CONFIG_RV_MON_SSSW) += monitors/sssw/sssw.o
obj-$(CONFIG_RV_MON_OPID) += monitors/opid/opid.o
# Add new monitors here
obj-$(CONFIG_RV_REACTORS) += rv_reactors.o
obj-$(CONFIG_RV_REACT_PRINTK) += reactor_printk.o
obj-$(CONFIG_RV_REACT_PANIC) += reactor_panic.o
