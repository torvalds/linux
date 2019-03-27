/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include <linux/delay.h>
#include <dev/mlx5/driver.h>
#include <dev/mlx5/mlx5_ifc.h>
#include "mlx5_core.h"

#define	MLX5_HEALTH_POLL_INTERVAL	(2 * HZ)
#define	MAX_MISSES			3

enum {
	MLX5_NIC_IFC_FULL		= 0,
	MLX5_NIC_IFC_DISABLED		= 1,
	MLX5_NIC_IFC_NO_DRAM_NIC	= 2,
	MLX5_NIC_IFC_SW_RESET		= 7,
};

enum {
	MLX5_DROP_NEW_HEALTH_WORK,
	MLX5_DROP_NEW_RECOVERY_WORK,
};

enum  {
	MLX5_SENSOR_NO_ERR		= 0,
	MLX5_SENSOR_PCI_COMM_ERR	= 1,
	MLX5_SENSOR_PCI_ERR		= 2,
	MLX5_SENSOR_NIC_DISABLED	= 3,
	MLX5_SENSOR_NIC_SW_RESET	= 4,
	MLX5_SENSOR_FW_SYND_RFR		= 5,
};

static int mlx5_fw_reset_enable = 1;
SYSCTL_INT(_hw_mlx5, OID_AUTO, fw_reset_enable, CTLFLAG_RWTUN,
    &mlx5_fw_reset_enable, 0,
    "Enable firmware reset");

static int lock_sem_sw_reset(struct mlx5_core_dev *dev)
{
	int ret;

	/* Lock GW access */
	ret = -mlx5_vsc_lock(dev);
	if (ret) {
		mlx5_core_warn(dev, "Timed out locking gateway %d\n", ret);
		return ret;
	}

	ret = -mlx5_vsc_lock_addr_space(dev, MLX5_SEMAPHORE_SW_RESET);
	if (ret) {
		if (ret == -EBUSY)
			mlx5_core_dbg(dev, "SW reset FW semaphore already locked, another function will handle the reset\n");
		else
			mlx5_core_warn(dev, "SW reset semaphore lock return %d\n", ret);
	}

	/* Unlock GW access */
	mlx5_vsc_unlock(dev);

	return ret;
}

static int unlock_sem_sw_reset(struct mlx5_core_dev *dev)
{
	int ret;

	/* Lock GW access */
	ret = -mlx5_vsc_lock(dev);
	if (ret) {
		mlx5_core_warn(dev, "Timed out locking gateway %d\n", ret);
		return ret;
	}

	ret = -mlx5_vsc_unlock_addr_space(dev, MLX5_SEMAPHORE_SW_RESET);

	/* Unlock GW access */
	mlx5_vsc_unlock(dev);

	return ret;
}

static u8 get_nic_mode(struct mlx5_core_dev *dev)
{
	return (ioread32be(&dev->iseg->cmdq_addr_l_sz) >> 8) & 7;
}

static bool sensor_fw_synd_rfr(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct mlx5_health_buffer __iomem *h = health->health;
	u32 rfr = ioread32be(&h->rfr) >> MLX5_RFR_OFFSET;
	u8 synd = ioread8(&h->synd);

	if (rfr && synd)
		mlx5_core_dbg(dev, "FW requests reset, synd: %d\n", synd);
	return rfr && synd;
}

static void mlx5_trigger_cmd_completions(struct mlx5_core_dev *dev)
{
	unsigned long flags;
	u64 vector;

	/* wait for pending handlers to complete */
	synchronize_irq(dev->priv.msix_arr[MLX5_EQ_VEC_CMD].vector);
	spin_lock_irqsave(&dev->cmd.alloc_lock, flags);
	vector = ~dev->cmd.bitmask & ((1ul << (1 << dev->cmd.log_sz)) - 1);
	if (!vector)
		goto no_trig;

	vector |= MLX5_TRIGGERED_CMD_COMP;
	spin_unlock_irqrestore(&dev->cmd.alloc_lock, flags);

	mlx5_core_dbg(dev, "vector 0x%jx\n", (uintmax_t)vector);
	mlx5_cmd_comp_handler(dev, vector, MLX5_CMD_MODE_EVENTS);
	return;

no_trig:
	spin_unlock_irqrestore(&dev->cmd.alloc_lock, flags);
}

static bool sensor_pci_no_comm(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct mlx5_health_buffer __iomem *h = health->health;
	bool err = ioread32be(&h->fw_ver) == 0xffffffff;

	return err;
}

static bool sensor_nic_disabled(struct mlx5_core_dev *dev)
{
	return get_nic_mode(dev) == MLX5_NIC_IFC_DISABLED;
}

static bool sensor_nic_sw_reset(struct mlx5_core_dev *dev)
{
	return get_nic_mode(dev) == MLX5_NIC_IFC_SW_RESET;
}

static u32 check_fatal_sensors(struct mlx5_core_dev *dev)
{
	if (sensor_pci_no_comm(dev))
		return MLX5_SENSOR_PCI_COMM_ERR;
	if (pci_channel_offline(dev->pdev))
		return MLX5_SENSOR_PCI_ERR;
	if (sensor_nic_disabled(dev))
		return MLX5_SENSOR_NIC_DISABLED;
	if (sensor_nic_sw_reset(dev))
		return MLX5_SENSOR_NIC_SW_RESET;
	if (sensor_fw_synd_rfr(dev))
		return MLX5_SENSOR_FW_SYND_RFR;

	return MLX5_SENSOR_NO_ERR;
}

static void reset_fw_if_needed(struct mlx5_core_dev *dev)
{
	bool supported;
	u32 cmdq_addr, fatal_error;

	if (!mlx5_fw_reset_enable)
		return;
	supported = (ioread32be(&dev->iseg->initializing) >>
	    MLX5_FW_RESET_SUPPORTED_OFFSET) & 1;
	if (!supported)
		return;

	/* The reset only needs to be issued by one PF. The health buffer is
	 * shared between all functions, and will be cleared during a reset.
	 * Check again to avoid a redundant 2nd reset. If the fatal erros was
	 * PCI related a reset won't help.
	 */
	fatal_error = check_fatal_sensors(dev);
	if (fatal_error == MLX5_SENSOR_PCI_COMM_ERR ||
	    fatal_error == MLX5_SENSOR_NIC_DISABLED ||
	    fatal_error == MLX5_SENSOR_NIC_SW_RESET) {
		mlx5_core_warn(dev, "Not issuing FW reset. Either it's already done or won't help.\n");
		return;
	}

	mlx5_core_warn(dev, "Issuing FW Reset\n");
	/* Write the NIC interface field to initiate the reset, the command
	 * interface address also resides here, don't overwrite it.
	 */
	cmdq_addr = ioread32be(&dev->iseg->cmdq_addr_l_sz);
	iowrite32be((cmdq_addr & 0xFFFFF000) |
		    MLX5_NIC_IFC_SW_RESET << MLX5_NIC_IFC_OFFSET,
		    &dev->iseg->cmdq_addr_l_sz);
}

#define MLX5_CRDUMP_WAIT_MS	60000
#define MLX5_FW_RESET_WAIT_MS	1000
#define MLX5_NIC_STATE_POLL_MS	5
void mlx5_enter_error_state(struct mlx5_core_dev *dev, bool force)
{
	unsigned long end, delay_ms = MLX5_CRDUMP_WAIT_MS;
	u32 fatal_error;
	int lock = -EBUSY;

	fatal_error = check_fatal_sensors(dev);

	if (fatal_error || force) {
		if (xchg(&dev->state, MLX5_DEVICE_STATE_INTERNAL_ERROR) ==
		    MLX5_DEVICE_STATE_INTERNAL_ERROR)
			return;
		if (!force)
			mlx5_core_err(dev, "internal state error detected\n");
		mlx5_trigger_cmd_completions(dev);
	}

	mutex_lock(&dev->intf_state_mutex);

	if (force)
		goto err_state_done;

	if (fatal_error == MLX5_SENSOR_FW_SYND_RFR) {
		/* Get cr-dump and reset FW semaphore */
		if (mlx5_core_is_pf(dev))
			lock = lock_sem_sw_reset(dev);

		/* Execute cr-dump and SW reset */
		if (lock != -EBUSY) {
			mlx5_fwdump(dev);
			reset_fw_if_needed(dev);
			delay_ms = MLX5_FW_RESET_WAIT_MS;
		}
	}

	/* Recover from SW reset */
	end = jiffies + msecs_to_jiffies(delay_ms);
	do {
		if (sensor_nic_disabled(dev))
			break;

		msleep(MLX5_NIC_STATE_POLL_MS);
	} while (!time_after(jiffies, end));

	if (!sensor_nic_disabled(dev)) {
		dev_err(&dev->pdev->dev, "NIC IFC still %d after %lums.\n",
			get_nic_mode(dev), delay_ms);
	}

	/* Release FW semaphore if you are the lock owner */
	if (!lock)
		unlock_sem_sw_reset(dev);

	mlx5_core_err(dev, "system error event triggered\n");

err_state_done:
	mlx5_core_event(dev, MLX5_DEV_EVENT_SYS_ERROR, 1);
	mutex_unlock(&dev->intf_state_mutex);
}

static void mlx5_handle_bad_state(struct mlx5_core_dev *dev)
{
	u8 nic_mode = get_nic_mode(dev);

	if (nic_mode == MLX5_NIC_IFC_SW_RESET) {
		/* The IFC mode field is 3 bits, so it will read 0x7 in two cases:
		 * 1. PCI has been disabled (ie. PCI-AER, PF driver unloaded
		 *    and this is a VF), this is not recoverable by SW reset.
		 *    Logging of this is handled elsewhere.
		 * 2. FW reset has been issued by another function, driver can
		 *    be reloaded to recover after the mode switches to
		 *    MLX5_NIC_IFC_DISABLED.
		 */
		if (dev->priv.health.fatal_error != MLX5_SENSOR_PCI_COMM_ERR)
			mlx5_core_warn(dev, "NIC SW reset is already progress\n");
		else
			mlx5_core_warn(dev, "Communication with FW over the PCI link is down\n");
	} else {
		mlx5_core_warn(dev, "NIC mode %d\n", nic_mode);
	}

	mlx5_disable_device(dev);
}

#define MLX5_FW_RESET_WAIT_MS	1000
#define MLX5_NIC_STATE_POLL_MS	5
static void health_recover(struct work_struct *work)
{
	unsigned long end = jiffies + msecs_to_jiffies(MLX5_FW_RESET_WAIT_MS);
	struct mlx5_core_health *health;
	struct delayed_work *dwork;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	bool recover = true;
	u8 nic_mode;

	dwork = container_of(work, struct delayed_work, work);
	health = container_of(dwork, struct mlx5_core_health, recover_work);
	priv = container_of(health, struct mlx5_priv, health);
	dev = container_of(priv, struct mlx5_core_dev, priv);

	mtx_lock(&Giant);	/* XXX newbus needs this */

	if (sensor_pci_no_comm(dev)) {
		dev_err(&dev->pdev->dev, "health recovery flow aborted, PCI reads still not working\n");
		recover = false;
	}

	nic_mode = get_nic_mode(dev);
	while (nic_mode != MLX5_NIC_IFC_DISABLED &&
	       !time_after(jiffies, end)) {
		msleep(MLX5_NIC_STATE_POLL_MS);
		nic_mode = get_nic_mode(dev);
	}

	if (nic_mode != MLX5_NIC_IFC_DISABLED) {
		dev_err(&dev->pdev->dev, "health recovery flow aborted, unexpected NIC IFC mode %d.\n",
			nic_mode);
		recover = false;
	}

	if (recover) {
		dev_err(&dev->pdev->dev, "starting health recovery flow\n");
		mlx5_recover_device(dev);
	}

	mtx_unlock(&Giant);
}

/* How much time to wait until health resetting the driver (in msecs) */
#define MLX5_RECOVERY_DELAY_MSECS 60000
#define MLX5_RECOVERY_NO_DELAY 0
static unsigned long get_recovery_delay(struct mlx5_core_dev *dev)
{
	return dev->priv.health.fatal_error == MLX5_SENSOR_PCI_ERR ||
		dev->priv.health.fatal_error == MLX5_SENSOR_PCI_COMM_ERR	?
		MLX5_RECOVERY_DELAY_MSECS : MLX5_RECOVERY_NO_DELAY;
}

static void health_care(struct work_struct *work)
{
	struct mlx5_core_health *health;
	unsigned long recover_delay;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	unsigned long flags;

	health = container_of(work, struct mlx5_core_health, work);
	priv = container_of(health, struct mlx5_priv, health);
	dev = container_of(priv, struct mlx5_core_dev, priv);

	mlx5_core_warn(dev, "handling bad device here\n");
	mlx5_handle_bad_state(dev);
	recover_delay = msecs_to_jiffies(get_recovery_delay(dev));

	spin_lock_irqsave(&health->wq_lock, flags);
	if (!test_bit(MLX5_DROP_NEW_RECOVERY_WORK, &health->flags)) {
		mlx5_core_warn(dev, "Scheduling recovery work with %lums delay\n",
			       recover_delay);
		schedule_delayed_work(&health->recover_work, recover_delay);
	} else {
		dev_err(&dev->pdev->dev,
			"new health works are not permitted at this stage\n");
	}
	spin_unlock_irqrestore(&health->wq_lock, flags);
}

static int get_next_poll_jiffies(void)
{
	unsigned long next;

	get_random_bytes(&next, sizeof(next));
	next %= HZ;
	next += jiffies + MLX5_HEALTH_POLL_INTERVAL;

	return next;
}

void mlx5_trigger_health_work(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long flags;

	spin_lock_irqsave(&health->wq_lock, flags);
	if (!test_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags))
		queue_work(health->wq, &health->work);
	else
		dev_err(&dev->pdev->dev,
			"new health works are not permitted at this stage\n");
	spin_unlock_irqrestore(&health->wq_lock, flags);
}

static const char *hsynd_str(u8 synd)
{
	switch (synd) {
	case MLX5_HEALTH_SYNDR_FW_ERR:
		return "firmware internal error";
	case MLX5_HEALTH_SYNDR_IRISC_ERR:
		return "irisc not responding";
	case MLX5_HEALTH_SYNDR_HW_UNRECOVERABLE_ERR:
		return "unrecoverable hardware error";
	case MLX5_HEALTH_SYNDR_CRC_ERR:
		return "firmware CRC error";
	case MLX5_HEALTH_SYNDR_FETCH_PCI_ERR:
		return "ICM fetch PCI error";
	case MLX5_HEALTH_SYNDR_HW_FTL_ERR:
		return "HW fatal error\n";
	case MLX5_HEALTH_SYNDR_ASYNC_EQ_OVERRUN_ERR:
		return "async EQ buffer overrun";
	case MLX5_HEALTH_SYNDR_EQ_ERR:
		return "EQ error";
	case MLX5_HEALTH_SYNDR_EQ_INV:
		return "Invalid EQ referenced";
	case MLX5_HEALTH_SYNDR_FFSER_ERR:
		return "FFSER error";
	case MLX5_HEALTH_SYNDR_HIGH_TEMP:
		return "High temprature";
	default:
		return "unrecognized error";
	}
}

static void print_health_info(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct mlx5_health_buffer __iomem *h = health->health;
	char fw_str[18];
	u32 fw;
	int i;

	/* If the syndrom is 0, the device is OK and no need to print buffer */
	if (!ioread8(&h->synd))
		return;

	for (i = 0; i < ARRAY_SIZE(h->assert_var); i++)
		printf("mlx5_core: INFO: ""assert_var[%d] 0x%08x\n", i, ioread32be(h->assert_var + i));

	printf("mlx5_core: INFO: ""assert_exit_ptr 0x%08x\n", ioread32be(&h->assert_exit_ptr));
	printf("mlx5_core: INFO: ""assert_callra 0x%08x\n", ioread32be(&h->assert_callra));
	snprintf(fw_str, sizeof(fw_str), "%d.%d.%d", fw_rev_maj(dev), fw_rev_min(dev), fw_rev_sub(dev));
	printf("mlx5_core: INFO: ""fw_ver %s\n", fw_str);
	printf("mlx5_core: INFO: ""hw_id 0x%08x\n", ioread32be(&h->hw_id));
	printf("mlx5_core: INFO: ""irisc_index %d\n", ioread8(&h->irisc_index));
	printf("mlx5_core: INFO: ""synd 0x%x: %s\n", ioread8(&h->synd), hsynd_str(ioread8(&h->synd)));
	printf("mlx5_core: INFO: ""ext_synd 0x%04x\n", ioread16be(&h->ext_synd));
	fw = ioread32be(&h->fw_ver);
	printf("mlx5_core: INFO: ""raw fw_ver 0x%08x\n", fw);
}

static void poll_health(unsigned long data)
{
	struct mlx5_core_dev *dev = (struct mlx5_core_dev *)data;
	struct mlx5_core_health *health = &dev->priv.health;
	u32 fatal_error;
	u32 count;

	if (dev->state != MLX5_DEVICE_STATE_UP)
		return;

	if (dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR)
		goto out;

	count = ioread32be(health->health_counter);
	if (count == health->prev)
		++health->miss_counter;
	else
		health->miss_counter = 0;

	health->prev = count;
	if (health->miss_counter == MAX_MISSES) {
		mlx5_core_err(dev, "device's health compromised - reached miss count\n");
		print_health_info(dev);
	}

	fatal_error = check_fatal_sensors(dev);

	if (fatal_error && !health->fatal_error) {
		mlx5_core_err(dev, "Fatal error %u detected\n", fatal_error);
		dev->priv.health.fatal_error = fatal_error;
		print_health_info(dev);
		mlx5_trigger_health_work(dev);
	}

out:
	mod_timer(&health->timer, get_next_poll_jiffies());
}

void mlx5_start_health_poll(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	init_timer(&health->timer);
	health->fatal_error = MLX5_SENSOR_NO_ERR;
	clear_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
	clear_bit(MLX5_DROP_NEW_RECOVERY_WORK, &health->flags);
	health->health = &dev->iseg->health;
	health->health_counter = &dev->iseg->health_counter;

	setup_timer(&health->timer, poll_health, (unsigned long)dev);
	mod_timer(&health->timer,
		  round_jiffies(jiffies + MLX5_HEALTH_POLL_INTERVAL));
}

void mlx5_stop_health_poll(struct mlx5_core_dev *dev, bool disable_health)
{
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long flags;

	if (disable_health) {
		spin_lock_irqsave(&health->wq_lock, flags);
		set_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
		set_bit(MLX5_DROP_NEW_RECOVERY_WORK, &health->flags);
		spin_unlock_irqrestore(&health->wq_lock, flags);
	}

	del_timer_sync(&health->timer);
}

void mlx5_drain_health_wq(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long flags;

	spin_lock_irqsave(&health->wq_lock, flags);
	set_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
	set_bit(MLX5_DROP_NEW_RECOVERY_WORK, &health->flags);
	spin_unlock_irqrestore(&health->wq_lock, flags);
	cancel_delayed_work_sync(&health->recover_work);
	cancel_work_sync(&health->work);
}

void mlx5_drain_health_recovery(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long flags;

	spin_lock_irqsave(&health->wq_lock, flags);
	set_bit(MLX5_DROP_NEW_RECOVERY_WORK, &health->flags);
	spin_unlock_irqrestore(&health->wq_lock, flags);
	cancel_delayed_work_sync(&dev->priv.health.recover_work);
}

void mlx5_health_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	destroy_workqueue(health->wq);
}

#define HEALTH_NAME "mlx5_health"
int mlx5_health_init(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health;
	char *name;
	int len;

	health = &dev->priv.health;
	len = strlen(HEALTH_NAME) + strlen(dev_name(&dev->pdev->dev));
	name = kmalloc(len + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	snprintf(name, len, "%s:%s", HEALTH_NAME, dev_name(&dev->pdev->dev));
	health->wq = create_singlethread_workqueue(name);
	kfree(name);
	if (!health->wq)
		return -ENOMEM;

	spin_lock_init(&health->wq_lock);
	INIT_WORK(&health->work, health_care);
	INIT_DELAYED_WORK(&health->recover_work, health_recover);

	return 0;
}
