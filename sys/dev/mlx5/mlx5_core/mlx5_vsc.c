/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.	 All rights reserved.
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

#include <dev/mlx5/driver.h>
#include <dev/mlx5/device.h>
#include <dev/mlx5/mlx5_core/mlx5_core.h>

#define	MLX5_SEMAPHORE_SPACE_DOMAIN 0xA

struct mlx5_ifc_vsc_space_bits {
	u8 status[0x3];
	u8 reserved0[0xd];
	u8 space[0x10];
};

struct mlx5_ifc_vsc_addr_bits {
	u8 flag[0x1];
	u8 reserved0[0x1];
	u8 address[0x1e];
};

int mlx5_vsc_lock(struct mlx5_core_dev *mdev)
{
	device_t dev = mdev->pdev->dev.bsddev;
	int vsc_addr = mdev->vsc_addr;
	int retries = 0;
	u32 lock_val;
	u32 counter;

	if (!vsc_addr) {
		mlx5_core_warn(mdev, "Unable to acquire vsc lock, vsc_addr not initialized\n");
		return EINVAL;
	}

	while (true) {
		if (retries > MLX5_VSC_MAX_RETRIES)
			return EBUSY;

		if (pci_read_config(dev, vsc_addr + MLX5_VSC_SEMA_OFFSET, 4)) {
			retries++;
			/*
			 * The PRM suggests random 0 - 10ms to prevent multiple
			 * waiters on the same interval in order to avoid starvation
			 */
			DELAY((random() % 11) * 1000);
			continue;
		}

		counter = pci_read_config(dev, vsc_addr + MLX5_VSC_COUNTER_OFFSET, 4);
		pci_write_config(dev, vsc_addr + MLX5_VSC_SEMA_OFFSET, counter, 4);
		lock_val = pci_read_config(dev, vsc_addr + MLX5_VSC_SEMA_OFFSET, 4);

		if (lock_val == counter)
			break;

		retries++;
	}

	return 0;
}

void mlx5_vsc_unlock(struct mlx5_core_dev *mdev)
{
	device_t dev = mdev->pdev->dev.bsddev;
	int vsc_addr = mdev->vsc_addr;

	if (!vsc_addr) {
		mlx5_core_warn(mdev, "Unable to release vsc lock, vsc_addr not initialized\n");
		return;
	}

	pci_write_config(dev, vsc_addr + MLX5_VSC_SEMA_OFFSET, 0, 4);
}

static int mlx5_vsc_wait_on_flag(struct mlx5_core_dev *mdev, u32 expected)
{
	device_t dev = mdev->pdev->dev.bsddev;
	int vsc_addr = mdev->vsc_addr;
	int retries = 0;
	u32 flag;

	while (true) {
		if (retries > MLX5_VSC_MAX_RETRIES)
			return EBUSY;

		flag = pci_read_config(dev, vsc_addr + MLX5_VSC_ADDR_OFFSET, 4);
		if (expected == MLX5_VSC_GET(vsc_addr, &flag, flag))
			break;

		retries++;
		DELAY(10);
	}

	return 0;
}

int mlx5_vsc_set_space(struct mlx5_core_dev *mdev, u16 space)
{
	device_t dev = mdev->pdev->dev.bsddev;
	int vsc_addr = mdev->vsc_addr;
	u32 vsc_space = 0;

	if (!vsc_addr) {
		mlx5_core_warn(mdev, "Unable to set vsc space, vsc_addr not initialized\n");
		return EINVAL;
	}

	MLX5_VSC_SET(vsc_space, &vsc_space, space, space);
	pci_write_config(dev, vsc_addr + MLX5_VSC_SPACE_OFFSET, vsc_space, 4);
	vsc_space = pci_read_config(dev, vsc_addr + MLX5_VSC_SPACE_OFFSET, 4);

	if (MLX5_VSC_GET(vsc_space, &vsc_space, status) != MLX5_VSC_SPACE_SUPPORTED) {
		mlx5_core_warn(mdev, "Space 0x%x is not supported.\n", space);
		return ENOTSUP;
	}

	return 0;
}

int mlx5_vsc_write(struct mlx5_core_dev *mdev, u32 addr, const u32 *data)
{
	device_t dev = mdev->pdev->dev.bsddev;
	int vsc_addr = mdev->vsc_addr;
	u32 in = 0;
	int err;

	if (!vsc_addr) {
		mlx5_core_warn(mdev, "Unable to call vsc write, vsc_addr not initialized\n");
		return EINVAL;
	}

	MLX5_VSC_SET(vsc_addr, &in, address, addr);
	MLX5_VSC_SET(vsc_addr, &in, flag, 1);
	pci_write_config(dev, vsc_addr + MLX5_VSC_DATA_OFFSET, *data, 4);
	pci_write_config(dev, vsc_addr + MLX5_VSC_ADDR_OFFSET, in, 4);

	err = mlx5_vsc_wait_on_flag(mdev, 0);
	if (err)
		mlx5_core_warn(mdev, "Failed waiting for write flag!\n");

	return err;
}

int mlx5_vsc_read(struct mlx5_core_dev *mdev, u32 addr, u32 *data)
{
	device_t dev = mdev->pdev->dev.bsddev;
	int vsc_addr = mdev->vsc_addr;
	int err;
	u32 in;

	if (!vsc_addr) {
		mlx5_core_warn(mdev, "Unable to call vsc read, vsc_addr not initialized\n");
		return EINVAL;
	}

	MLX5_VSC_SET(vsc_addr, &in, address, addr);
	pci_write_config(dev, vsc_addr + MLX5_VSC_ADDR_OFFSET, in, 4);

	err = mlx5_vsc_wait_on_flag(mdev, 1);
	if (err) {
		mlx5_core_warn(mdev, "Failed waiting for read complete flag!\n");
		return err;
	}

	*data = pci_read_config(dev, vsc_addr + MLX5_VSC_DATA_OFFSET, 4);

	return 0;
}

int mlx5_vsc_lock_addr_space(struct mlx5_core_dev *mdev, u32 addr)
{
	device_t dev = mdev->pdev->dev.bsddev;
	int vsc_addr = mdev->vsc_addr;
	u32 data;
	int ret;
	u32 id;

	ret = mlx5_vsc_set_space(mdev, MLX5_SEMAPHORE_SPACE_DOMAIN);
	if (ret)
		return ret;

	/* Get a unique ID based on the counter */
	id = pci_read_config(dev, vsc_addr + MLX5_VSC_COUNTER_OFFSET, 4);

	/* Try to modify lock */
	ret = mlx5_vsc_write(mdev, addr, &id);
	if (ret)
		return ret;

	/* Verify */
	ret = mlx5_vsc_read(mdev, addr, &data);
	if (ret)
		return ret;
	if (data != id)
		return EBUSY;

	return 0;
}

int mlx5_vsc_unlock_addr_space(struct mlx5_core_dev *mdev, u32 addr)
{
	u32 data = 0;
	int ret;

	ret = mlx5_vsc_set_space(mdev, MLX5_SEMAPHORE_SPACE_DOMAIN);
	if (ret)
		return ret;

	/* Try to modify lock */
	ret = mlx5_vsc_write(mdev, addr, &data);
	if (ret)
		return ret;

	/* Verify */
	ret = mlx5_vsc_read(mdev, addr, &data);
	if (ret)
		return ret;
	if (data != 0)
		return EBUSY;

	return 0;
}

int mlx5_vsc_find_cap(struct mlx5_core_dev *mdev)
{
	int *capreg = &mdev->vsc_addr;
	int err;

	err = pci_find_cap(mdev->pdev->dev.bsddev, PCIY_VENDOR, capreg);

	if (err)
		*capreg = 0;

	return err;
}

