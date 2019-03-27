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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <dev/mlx5/mlx5io.h>
#include <dev/mlx5/mlx5_fpga_tools/tools_char.h>

#define CHUNK_SIZE (128 * 1024)

struct tools_context {
	struct mlx5_fpga_tools_dev *tdev;
	enum mlx5_fpga_access_type access_type;
};

static void
tools_char_ctx_dtor(void *data)
{

	free(data, M_DEVBUF);
}

static int
tools_char_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct tools_context *context;

	context = malloc(sizeof(*context), M_DEVBUF, M_WAITOK);
	context->tdev = dev->si_drv1;
	context->access_type = MLX5_FPGA_ACCESS_TYPE_DONTCARE;
	devfs_set_cdevpriv(context, tools_char_ctx_dtor);
	return (0);
}

static int
mem_read(struct mlx5_fpga_tools_dev *tdev, void *buf, size_t count,
    u64 address, enum mlx5_fpga_access_type access_type, size_t *retcnt)
{
	int ret;

	ret = sx_xlock_sig(&tdev->lock);
	if (ret != 0)
		return (ret);
	ret = mlx5_fpga_mem_read(tdev->fdev, count, address, buf, access_type);
	sx_xunlock(&tdev->lock);
	if (ret < 0) {
		dev_dbg(mlx5_fpga_dev(tdev->fdev),
			"Failed to read %zu bytes at address 0x%jx: %d\n",
			count, (uintmax_t)address, ret);
		return (-ret);
	}
	*retcnt = ret;
	return (0);
}

static int
mem_write(struct mlx5_fpga_tools_dev *tdev, void *buf, size_t count,
    u64 address, enum mlx5_fpga_access_type access_type, size_t *retcnt)
{
	int ret;

	ret = sx_xlock_sig(&tdev->lock);
	if (ret != 0)
		return (ret);
	ret = mlx5_fpga_mem_write(tdev->fdev, count, address, buf, access_type);
	sx_xunlock(&tdev->lock);
	if (ret < 0) {
		dev_dbg(mlx5_fpga_dev(tdev->fdev),
			"Failed to write %zu bytes at address 0x%jx: %d\n",
			count, (uintmax_t)address, ret);
		return (-ret);
	}
	*retcnt = ret;
	return (0);
}

static void
tools_char_llseek(struct tools_context *context, struct uio *uio, ssize_t *len)
{
	uint64_t fbase, fsize;
	size_t llen;

	llen = uio->uio_resid;
	if (llen < 1) {
		*len = 0;
		return;
	}
	if (llen > CHUNK_SIZE)
		llen = CHUNK_SIZE;
	fbase = mlx5_fpga_ddr_base_get(context->tdev->fdev);
	fsize = mlx5_fpga_ddr_size_get(context->tdev->fdev);
	if (uio->uio_offset > fbase)
		*len = 0;
	else if (uio->uio_offset + *len > fbase + fsize)
		*len = fbase + fsize - uio->uio_offset;
	else
		*len = llen;
}

static int
tools_char_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct tools_context *context;
	void *kbuf;
	size_t len, len1;
	int ret;

	ret = devfs_get_cdevpriv((void **)&context);
	if (ret != 0)
		return (ret);
	dev_dbg(mlx5_fpga_dev(context->tdev->fdev),
	    "tools char device reading %zu bytes at 0x%jx\n", uio->uio_resid,
	     (uintmax_t)uio->uio_offset);

	tools_char_llseek(context, uio, &len);
	if (len == 0)
		return (0);

	kbuf = malloc(len, M_DEVBUF, M_WAITOK);
	ret = mem_read(context->tdev, kbuf, len, uio->uio_offset,
	    context->access_type, &len1);
	if (ret == 0)
		ret = uiomove(kbuf, len1, uio);
	free(kbuf, M_DEVBUF);
	return (ret);
}

static int
tools_char_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct tools_context *context;
	void *kbuf;
	off_t of;
	size_t len, len1;
	int ret;

	ret = devfs_get_cdevpriv((void **)&context);
	if (ret != 0)
		return (ret);
	dev_dbg(mlx5_fpga_dev(context->tdev->fdev),
	    "tools char device reading %zu bytes at 0x%jx\n", uio->uio_resid,
	    (uintmax_t)uio->uio_offset);

	tools_char_llseek(context, uio, &len);
	if (len == 0)
		return (0);

	kbuf = malloc(len, M_DEVBUF, M_WAITOK);
	len1 = uio->uio_resid;
	of = uio->uio_offset;

	ret = uiomove(kbuf, len, uio);
	if (ret == 0) {
		len1 -= uio->uio_resid;
		ret = mem_write(context->tdev, kbuf, len, of,
		    context->access_type, &len1);
	}
	free(kbuf, M_DEVBUF);
	return (ret);
}

CTASSERT(MLX5_FPGA_CAP_ARR_SZ == MLX5_ST_SZ_DW(fpga_cap));

static int
tools_char_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct tools_context *context;
	struct mlx5_fpga_device *fdev;
	struct mlx5_fpga_query query;
	struct mlx5_fpga_temperature *temperature;
	enum mlx5_fpga_connect *connect;
	u32 fpga_cap[MLX5_ST_SZ_DW(fpga_cap)] = {0};
	int arg, err;

	err = devfs_get_cdevpriv((void **)&context);
	if (err != 0)
		return (err);
	fdev = context->tdev->fdev;
	if (fdev == NULL)
		return (ENXIO);

	switch (cmd) {
	case MLX5_FPGA_ACCESS_TYPE:
		arg = *(int *)data;
		if (arg > MLX5_FPGA_ACCESS_TYPE_MAX) {
			dev_err(mlx5_fpga_dev(fdev),
			    "unknown access type %u\n", arg);
			err = EINVAL;
			break;
		}
		context->access_type = arg;
		break;
	case MLX5_FPGA_LOAD:
		arg = *(int *)data;
		if (arg > MLX5_FPGA_IMAGE_MAX) {
			dev_err(mlx5_fpga_dev(fdev),
				"unknown image type %u\n", arg);
			err = EINVAL;
			break;
		}
		err = mlx5_fpga_device_reload(fdev, arg);
		break;
	case MLX5_FPGA_RESET:
		err = mlx5_fpga_device_reload(fdev, MLX5_FPGA_IMAGE_MAX + 1);
		break;
	case MLX5_FPGA_IMAGE_SEL:
		arg = *(int *)data;
		if (arg > MLX5_FPGA_IMAGE_MAX) {
			dev_err(mlx5_fpga_dev(fdev),
			    "unknown image type %u\n", arg);
			err = EINVAL;
			break;
		}
		err = mlx5_fpga_flash_select(fdev, arg);
		break;
	case MLX5_FPGA_QUERY:
		mlx5_fpga_device_query(fdev, &query);
		bcopy(&query, data, sizeof(query));
		err = 0;
		break;
	case MLX5_FPGA_CAP:
		mlx5_fpga_get_cap(fdev, fpga_cap);
		bcopy(&fpga_cap, data, sizeof(fpga_cap));
		err = 0;
		break;
	case MLX5_FPGA_TEMPERATURE:
		temperature = (struct mlx5_fpga_temperature *)data;
		mlx5_fpga_temperature(fdev, temperature);
		err = 0; /* XXXKIB */
		break;
	case MLX5_FPGA_CONNECT:
		connect = (enum mlx5_fpga_connect *)data;
		mlx5_fpga_connectdisconnect(fdev, connect);
		err = 0; /* XXXKIB */
 		break;
	default:
		dev_err(mlx5_fpga_dev(fdev),
			"unknown ioctl command %#08lx\n", cmd);
		err = ENOTTY;
	}
	return (err);
}

static struct cdevsw mlx5_tools_char_cdevsw = {
	.d_version =	D_VERSION,
	.d_name =	"mlx5_tools_char",
	.d_open =	tools_char_open,
	.d_read =	tools_char_read,
	.d_write =	tools_char_write,
	.d_ioctl =	tools_char_ioctl,
};

int
mlx5_fpga_tools_char_add_one(struct mlx5_fpga_tools_dev *tdev)
{
	struct make_dev_args mda;
	struct cdev *cd;
	device_t bdev;
	int ret;

	make_dev_args_init(&mda);
	mda.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
	mda.mda_devsw = &mlx5_tools_char_cdevsw;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_OPERATOR;
	mda.mda_mode = 0660;
	mda.mda_si_drv1 = tdev;
	bdev = mlx5_fpga_dev(tdev->fdev)->bsddev;
	ret = make_dev_s(&mda, &cd,
	    "%04x:%02x:%02x.%x" MLX5_FPGA_TOOLS_NAME_SUFFIX,
	    pci_get_domain(bdev), pci_get_bus(bdev), pci_get_slot(bdev),
	    pci_get_function(bdev));
	if (ret != 0) {
		tdev->char_device = NULL;
		dev_err(mlx5_fpga_dev(tdev->fdev),
		    "Failed to create a char device: %d\n", ret);
		return (-ret);
	}
	tdev->char_device = cd;

	dev_dbg(mlx5_fpga_dev(tdev->fdev), "tools char device %s created\n",
	    cd->si_name);
	return (0);
}

void mlx5_fpga_tools_char_remove_one(struct mlx5_fpga_tools_dev *tdev)
{

	dev_err(mlx5_fpga_dev(tdev->fdev), "tools char device %s destroyed\n",
	    ((struct cdev *)(tdev->char_device))->si_name);
	destroy_dev((struct cdev *)(tdev->char_device));
}

int
mlx5_fpga_tools_char_init(void)
{

	return (0);
}

void
mlx5_fpga_tools_char_deinit(void)
{
}
