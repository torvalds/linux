/*-
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $FreeBSD$
 */

#include <linux/module.h>
#include <dev/mlx5/mlx5_fpga_tools/tools.h>
#include <dev/mlx5/mlx5_fpga_tools/tools_char.h>

#if (__FreeBSD_version >= 1100000)
MODULE_DEPEND(mlx5fpga_tools, linuxkpi, 1, 1, 1);
#endif
MODULE_DEPEND(mlx5fpga_tools, mlx5, 1, 1, 1);
MODULE_DEPEND(mlx5fpga_tools, mlx5fpga, 1, 1, 1);
MODULE_VERSION(mlx5fpga_tools, 1);

static void mlx5_fpga_tools_create(struct mlx5_fpga_device *fdev);
static int mlx5_fpga_tools_add(struct mlx5_fpga_device *fdev, u32 vid, u16 pid);
static void mlx5_fpga_tools_remove(struct mlx5_fpga_device *fdev);
static void mlx5_fpga_tools_destroy(struct mlx5_fpga_device *fdev);

struct mlx5_fpga_tools_dev *mlx5_fpga_tools_alloc(struct mlx5_fpga_device *fdev);
void mlx5_fpga_tools_free(struct mlx5_fpga_tools_dev *tdev);

static struct mlx5_fpga_client mlx5_fpga_tools_client = {
	.name = MLX5_FPGA_TOOLS_DRIVER_NAME,
	.create = mlx5_fpga_tools_create,
	.add = mlx5_fpga_tools_add,
	.remove = mlx5_fpga_tools_remove,
	.destroy = mlx5_fpga_tools_destroy,
};

struct mlx5_fpga_tools_dev *mlx5_fpga_tools_alloc(struct mlx5_fpga_device *fdev)
{
	int ret;
	struct mlx5_fpga_tools_dev *tdev;

	tdev = kzalloc(sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		goto out;

	tdev->fdev = fdev;
	sx_init(&tdev->lock, "mlx5fpgat");
	ret = mlx5_fpga_tools_char_add_one(tdev);
	if (ret)
		goto err_free;

	goto out;

err_free:
	kfree(tdev);
	tdev = NULL;

out:
	return tdev;
}

void mlx5_fpga_tools_free(struct mlx5_fpga_tools_dev *tdev)
{
	mlx5_fpga_tools_char_remove_one(tdev);
	kfree(tdev);
}

static void mlx5_fpga_tools_create(struct mlx5_fpga_device *fdev)
{
	struct mlx5_fpga_tools_dev *dev = NULL;

	dev_dbg(mlx5_fpga_dev(fdev), "tools_create\n");

	dev = mlx5_fpga_tools_alloc(fdev);
	if (!dev)
		return;

	mlx5_fpga_client_data_set(fdev, &mlx5_fpga_tools_client, dev);
}

static int mlx5_fpga_tools_add(struct mlx5_fpga_device *fdev, u32 vid, u16 pid)
{
	return 0;
}

static void mlx5_fpga_tools_remove(struct mlx5_fpga_device *fdev)
{
}

static void mlx5_fpga_tools_destroy(struct mlx5_fpga_device *fdev)
{
	struct mlx5_fpga_tools_dev *dev;

	dev_dbg(mlx5_fpga_dev(fdev), "tools_destroy\n");

	dev = mlx5_fpga_client_data_get(fdev, &mlx5_fpga_tools_client);
	if (dev)
		mlx5_fpga_tools_free(dev);
}

static int __init mlx5_fpga_tools_init(void)
{
	int ret = mlx5_fpga_tools_char_init();

	if (ret)
		return ret;
	mlx5_fpga_client_register(&mlx5_fpga_tools_client);
	return 0;
}

static void __exit mlx5_fpga_tools_exit(void)
{
	mlx5_fpga_client_unregister(&mlx5_fpga_tools_client);
	mlx5_fpga_tools_char_deinit();
}

module_init(mlx5_fpga_tools_init);
module_exit(mlx5_fpga_tools_exit);
