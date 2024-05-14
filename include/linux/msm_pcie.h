/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef __MSM_PCIE_H
#define __MSM_PCIE_H

#include <linux/types.h>
#include <linux/pci.h>

enum msm_pcie_config {
	MSM_PCIE_CONFIG_INVALID = 0,
	MSM_PCIE_CONFIG_LINKDOWN = BIT(0),
	MSM_PCIE_CONFIG_NO_RECOVERY = BIT(1),
	MSM_PCIE_CONFIG_NO_L1SS_TO = BIT(2),
	MSM_PCIE_CONFIG_NO_DRV_PC = BIT(3),
	MSM_PCIE_CONFIG_FORCE_SUSP = BIT(4),
};

enum msm_pcie_pm_opt {
	MSM_PCIE_DRV_SUSPEND,
	MSM_PCIE_SUSPEND,
	MSM_PCIE_RESUME,
	MSM_PCIE_DISABLE_PC,
	MSM_PCIE_ENABLE_PC,
	MSM_PCIE_HANDLE_LINKDOWN,
	MSM_PCIE_DRV_PC_CTRL,
};

enum msm_pcie_event {
	MSM_PCIE_EVENT_INVALID = 0,
	MSM_PCIE_EVENT_LINKDOWN = 0x1,
	MSM_PCIE_EVENT_LINKUP = 0x2,
	MSM_PCIE_EVENT_WAKEUP = 0x4,
	MSM_PCIE_EVENT_L1SS_TIMEOUT = BIT(3),
	MSM_PCIE_EVENT_DRV_CONNECT = BIT(4),
	MSM_PCIE_EVENT_DRV_DISCONNECT = BIT(5),
	MSM_PCIE_EVENT_LINK_RECOVER = BIT(6),
};

enum msm_pcie_trigger {
	MSM_PCIE_TRIGGER_CALLBACK,
	MSM_PCIE_TRIGGER_COMPLETION,
};

struct msm_pcie_notify {
	enum msm_pcie_event event;
	void *user;
	void *data;
	u32 options;
};

struct msm_pcie_register_event {
	struct list_head node;
	u32 events;
	void *user;
	enum msm_pcie_trigger mode;
	void (*callback)(struct msm_pcie_notify *notify);
	struct msm_pcie_notify notify;
	struct completion *completion;
	u32 options;
};

void msm_msi_config_access(struct irq_domain *domain, bool allow);
void msm_msi_config(struct irq_domain *domain);
int msm_msi_init(struct device *dev);

#if IS_ENABLED(CONFIG_PCI_MSM)

/**
 * msm_pcie_set_target_link_speed - sets the upper bound of GEN speed PCIe can
 * link up with
 * @rc_idx:		root complex port number that endpoint is connected to
 * @target_link_speed:	new target link speed PCIe can link up with
 * @force:		override DT specified link speed
 *
 * Provide PCIe clients the option to control upper bound of GEN speed PCIe
 * can link up with. Clients may choose only GEN speed within root complex's
 * controller capability or up to what is defined in devicetree,
 * qcom,target-link-speed.
 *
 * Client may also pass 0 for target_link_speed to have PCIe root complex
 * reset and use the default TLS.
 *
 * Return 0 on success, negative value on error
 */
int msm_pcie_set_target_link_speed(u32 rc_idx, u32 target_link_speed,
				   bool force);

/**
 * msm_pcie_allow_l1 - allow PCIe link to re-enter L1
 * @pci_dev:		client's pci device structure
 *
 * This function gives PCIe clients the control to allow the link to re-enter
 * L1. Should only be used after msm_pcie_prevent_l1 has been called.
 */
void msm_pcie_allow_l1(struct pci_dev *pci_dev);

/**
 * msm_pcie_prevent_l1 - keeps PCIe link out of L1
 * @pci_dev:		client's pci device structure
 *
 * This function gives PCIe clients the control to exit and prevent the link
 * from entering L1.
 *
 * Return 0 on success, negative value on error
 */
int msm_pcie_prevent_l1(struct pci_dev *pci_dev);

/**
 * msm_pcie_set_link_bandwidth - updates the number of lanes and speed of PCIe
 * link.
 * @pci_dev:		client's pci device structure
 * @target_link_speed:	gen speed
 * @target_link_width:	number of lanes
 *
 * This function gives PCIe clients the control to update the number of lanes
 * and gen speed of the link.
 *
 * Return: 0 on success, negative value on error
 */
int msm_pcie_set_link_bandwidth(struct pci_dev *pci_dev, u16 target_link_speed,
				u16 target_link_width);

/**
 * msm_pcie_l1ss_timeout_disable - disable L1ss timeout feature
 * @pci_dev:	client's pci device structure
 *
 * This function gives PCIe clients the control to disable L1ss timeout
 * feature.
 */
void msm_pcie_l1ss_timeout_disable(struct pci_dev *pci_dev);

/**
 * msm_pcie_l1ss_timeout_enable - enable L1ss timeout feature
 * @pci_dev:	client's pci device structure
 *
 * This function gives PCIe clients the control to enable L1ss timeout
 * feature.
 */
void msm_pcie_l1ss_timeout_enable(struct pci_dev *pci_dev);

/**
 * msm_pcie_pm_control - control the power state of a PCIe link.
 * @pm_opt:	power management operation
 * @busnr:	bus number of PCIe endpoint
 * @user:	handle of the caller
 * @data:	private data from the caller
 * @options:	options for pm control
 *
 * This function gives PCIe endpoint device drivers the control to change
 * the power state of a PCIe link for their device.
 *
 * Return: 0 on success, negative value on error
 */
int msm_pcie_pm_control(enum msm_pcie_pm_opt pm_opt, u32 busnr, void *user,
			void *data, u32 options);

/**
 * msm_pcie_register_event - register an event with PCIe bus driver.
 * @reg:	event structure
 *
 * This function gives PCIe endpoint device drivers an option to register
 * events with PCIe bus driver.
 *
 * Return: 0 on success, negative value on error
 */
int msm_pcie_register_event(struct msm_pcie_register_event *reg);

/**
 * msm_pcie_deregister_event - deregister an event with PCIe bus driver.
 * @reg:	event structure
 *
 * This function gives PCIe endpoint device drivers an option to deregister
 * events with PCIe bus driver.
 *
 * Return: 0 on success, negative value on error
 */
int msm_pcie_deregister_event(struct msm_pcie_register_event *reg);

/**
 * msm_pcie_enumerate - enumerate Endpoints.
 * @rc_idx:	RC that Endpoints connect to.
 *
 * This function enumerates Endpoints connected to RC.
 *
 * Return: 0 on success, negative value on error
 */
int msm_pcie_enumerate(u32 rc_idx);

/**
 * msm_pcie_deenumerate - deenumerates the Endpoints.
 * @rc_idx:	RC that Endpoints connect to.
 *
 * This function de-enumerates Endpoints connected to RC.
 *
 * Return: 0 on success, negative value on error
 */
int msm_pcie_deenumerate(u32 rc_idx);

/*
 * msm_pcie_debug_info - run a PCIe specific debug testcase.
 * @dev:	pci device structure
 * @option:	specifies which PCIe debug testcase to execute
 * @base:	PCIe specific range
 * @offset:	offset of destination register
 * @mask:	mask the bit(s) of destination register
 * @value:	value to be written to destination register
 *
 * This function gives PCIe endpoint device drivers the control to
 * run a debug testcase.
 *
 * Return: 0 on success, negative value on error
 */
int msm_pcie_debug_info(struct pci_dev *dev, u32 option, u32 base,
			u32 offset, u32 mask, u32 value);

/*
 * msm_pcie_reg_dump - dump pcie regsters for debug
 * @pci_dev:	pci device structure
 * @buffer:	destination buffer address
 * @len:		length of buffer
 *
 * This functions dumps PCIE registers for debug. Sould be used when
 * link is already enabled
 */
int msm_pcie_reg_dump(struct pci_dev *pci_dev, u8 *buff, u32 len);

/*
 * msm_pcie_dsp_link_control - enable/disable DSP link
 * @pci_dev:	pci device structure, endpoint of this DSP
 * @link_enable true to enable, false to disable
 *
 * This function enable(include training)/disable link between PCIe
 * switch DSP and endpoint attached.
 * Return: 0 on success, negative value on error
 */
int msm_pcie_dsp_link_control(struct pci_dev *pci_dev,
				    bool link_enable);

/*
 * msm_pcie_fmd_enable - deassert perst and enable FMD bit
 * @pci_dev:	pci device structure
 *
 * This function will de-assert PERST if PERST is already in assert state
 * and set fmd_enable  bit, after that no further perst assert/de-assert
 * are allowed.
 */
int msm_pcie_fmd_enable(struct pci_dev *pci_dev);

#else /* !CONFIG_PCI_MSM */
static inline int msm_pcie_pm_control(enum msm_pcie_pm_opt pm_opt, u32 busnr,
			void *user, void *data, u32 options)
{
	return -ENODEV;
}

static inline int msm_pcie_set_target_link_speed(u32 rc_idx,
						u32 target_link_speed)
{
	return -ENODEV;
}

static inline void msm_pcie_allow_l1(struct pci_dev *pci_dev)
{
}

static inline int msm_pcie_prevent_l1(struct pci_dev *pci_dev)
{
	return -ENODEV;
}

static inline int msm_pcie_l1ss_timeout_disable(struct pci_dev *pci_dev)
{
	return -ENODEV;
}

static inline int msm_pcie_l1ss_timeout_enable(struct pci_dev *pci_dev)
{
	return -ENODEV;
}

static inline int msm_pcie_register_event(struct msm_pcie_register_event *reg)
{
	return -ENODEV;
}

static inline int msm_pcie_deregister_event(struct msm_pcie_register_event *reg)
{
	return -ENODEV;
}

static inline int msm_pcie_enumerate(u32 rc_idx)
{
	return -ENODEV;
}

static inline int msm_pcie_deenumerate(u32 rc_idx)
{
	return -ENODEV;
}

static inline int msm_pcie_debug_info(struct pci_dev *dev, u32 option, u32 base,
			u32 offset, u32 mask, u32 value)
{
	return -ENODEV;
}

static inline int msm_pcie_reg_dump(struct pci_dev *pci_dev, u8 *buff, u32 len)
{
	return -ENODEV;
}

static inline int msm_pcie_dsp_link_control(struct pci_dev *pci_dev,
						  bool link_enable)
{
	return -ENODEV;
}

static inline int msm_pcie_fmd_enable(struct pci_dev *pci_dev)
{
	return -ENODEV;
}
#endif /* CONFIG_PCI_MSM */

#endif /* __MSM_PCIE_H */
