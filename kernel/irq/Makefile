# SPDX-License-Identifier: GPL-2.0

obj-y := irqdesc.o handle.o manage.o spurious.o resend.o chip.o dummychip.o devres.o
obj-$(CONFIG_IRQ_TIMINGS) += timings.o
ifeq ($(CONFIG_TEST_IRQ_TIMINGS),y)
	CFLAGS_timings.o += -DDEBUG
endif
obj-$(CONFIG_GENERIC_IRQ_CHIP) += generic-chip.o
obj-$(CONFIG_GENERIC_IRQ_PROBE) += autoprobe.o
obj-$(CONFIG_IRQ_DOMAIN) += irqdomain.o
obj-$(CONFIG_IRQ_SIM) += irq_sim.o
obj-$(CONFIG_PROC_FS) += proc.o
obj-$(CONFIG_GENERIC_PENDING_IRQ) += migration.o
obj-$(CONFIG_GENERIC_IRQ_MIGRATION) += cpuhotplug.o
obj-$(CONFIG_PM_SLEEP) += pm.o
obj-$(CONFIG_GENERIC_MSI_IRQ) += msi.o
obj-$(CONFIG_GENERIC_IRQ_IPI) += ipi.o
obj-$(CONFIG_SMP) += affinity.o
obj-$(CONFIG_GENERIC_IRQ_DEBUGFS) += debugfs.o
obj-$(CONFIG_GENERIC_IRQ_MATRIX_ALLOCATOR) += matrix.o
