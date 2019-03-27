/*-
 * Copyright (c) 2018, Mellanox Technologies, Ltd.  All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <dev/mlx5/driver.h>
#include <dev/mlx5/device.h>
#include <dev/mlx5/mlx5_core/mlx5_core.h>
#include <dev/mlx5/mlx5io.h>

extern const struct mlx5_crspace_regmap mlx5_crspace_regmap_mt4117[];
extern const struct mlx5_crspace_regmap mlx5_crspace_regmap_mt4115[];
extern const struct mlx5_crspace_regmap mlx5_crspace_regmap_connectx5[];

struct mlx5_dump_data {
	const struct mlx5_crspace_regmap *rege;
	uint32_t *dump;
	unsigned dump_size;
	int dump_valid;
	struct mtx dump_lock;
};

static MALLOC_DEFINE(M_MLX5_DUMP, "MLX5DUMP", "MLX5 Firmware dump");

static unsigned
mlx5_fwdump_getsize(const struct mlx5_crspace_regmap *rege)
{
	const struct mlx5_crspace_regmap *r;
	unsigned sz;

	for (sz = 0, r = rege; r->cnt != 0; r++)
		sz += r->cnt;
	return (sz);
}

static void
mlx5_fwdump_destroy_dd(struct mlx5_dump_data *dd)
{

	mtx_destroy(&dd->dump_lock);
	free(dd->dump, M_MLX5_DUMP);
	free(dd, M_MLX5_DUMP);
}

void
mlx5_fwdump_prep(struct mlx5_core_dev *mdev)
{
	struct mlx5_dump_data *dd;
	int error;

	error = mlx5_vsc_find_cap(mdev);
	if (error != 0) {
		/* Inability to create a firmware dump is not fatal. */
		device_printf((&mdev->pdev->dev)->bsddev, "WARN: "
		    "mlx5_fwdump_prep failed %d\n", error);
		return;
	}
	dd = malloc(sizeof(struct mlx5_dump_data), M_MLX5_DUMP, M_WAITOK);
	switch (pci_get_device(mdev->pdev->dev.bsddev)) {
	case 0x1013:
		dd->rege = mlx5_crspace_regmap_mt4115;
		break;
	case 0x1015:
		dd->rege = mlx5_crspace_regmap_mt4117;
		break;
	case 0x1017:
	case 0x1019:
		dd->rege = mlx5_crspace_regmap_connectx5;
		break;
	default:
		free(dd, M_MLX5_DUMP);
		return; /* silently fail, do not prevent driver attach */
	}
	dd->dump_size = mlx5_fwdump_getsize(dd->rege);
	dd->dump = malloc(dd->dump_size * sizeof(uint32_t), M_MLX5_DUMP,
	    M_WAITOK | M_ZERO);
	dd->dump_valid = 0;
	mtx_init(&dd->dump_lock, "mlx5dmp", NULL, MTX_DEF | MTX_NEW);
	if (atomic_cmpset_rel_ptr((uintptr_t *)&mdev->dump_data, 0,
	    (uintptr_t)dd) == 0)
		mlx5_fwdump_destroy_dd(dd);
}

void
mlx5_fwdump(struct mlx5_core_dev *mdev)
{
	struct mlx5_dump_data *dd;
	const struct mlx5_crspace_regmap *r;
	uint32_t i, ri;
	int error;

	dev_info(&mdev->pdev->dev, "Issuing FW dump\n");
	dd = (struct mlx5_dump_data *)atomic_load_acq_ptr((uintptr_t *)
	    &mdev->dump_data);
	if (dd == NULL)
		return;
	mtx_lock(&dd->dump_lock);
	if (dd->dump_valid) {
		/* only one dump */
		dev_warn(&mdev->pdev->dev,
		    "Only one FW dump can be captured aborting FW dump\n");
		goto failed;
	}

	/* mlx5_vsc already warns, be silent. */
	error = mlx5_vsc_lock(mdev);
	if (error != 0)
		goto failed;
	error = mlx5_vsc_set_space(mdev, MLX5_VSC_DOMAIN_PROTECTED_CRSPACE);
	if (error != 0)
		goto unlock_vsc;
	for (i = 0, r = dd->rege; r->cnt != 0; r++) {
		for (ri = 0; ri < r->cnt; ri++) {
			error = mlx5_vsc_read(mdev, r->addr + ri * 4,
			    &dd->dump[i]);
			if (error != 0)
				goto unlock_vsc;
			i++;
		}
	}
	atomic_store_rel_int(&dd->dump_valid, 1);
unlock_vsc:
	mlx5_vsc_unlock(mdev);
failed:
	mtx_unlock(&dd->dump_lock);
}

void
mlx5_fwdump_clean(struct mlx5_core_dev *mdev)
{
	struct mlx5_dump_data *dd;

	for (;;) {
		dd = mdev->dump_data;
		if (dd == NULL)
			return;
		if (atomic_cmpset_ptr((uintptr_t *)&mdev->dump_data,
		    (uintptr_t)dd, 0) == 1) {
			mlx5_fwdump_destroy_dd(dd);
			return;
		}
	}
}

static int
mlx5_dbsf_to_core(const struct mlx5_fwdump_addr *devaddr,
    struct mlx5_core_dev **mdev)
{
	device_t dev;
	struct pci_dev *pdev;

	dev = pci_find_dbsf(devaddr->domain, devaddr->bus, devaddr->slot,
	    devaddr->func);
	if (dev == NULL)
		return (ENOENT);
	if (device_get_devclass(dev) != mlx5_core_driver.bsdclass)
		return (EINVAL);
	pdev = device_get_softc(dev);
	*mdev = pci_get_drvdata(pdev);
	if (*mdev == NULL)
		return (ENOENT);
	return (0);
}

static int
mlx5_fwdump_copyout(struct mlx5_dump_data *dd, struct mlx5_fwdump_get *fwg)
{
	const struct mlx5_crspace_regmap *r;
	struct mlx5_fwdump_reg rv, *urv;
	uint32_t i, ri;
	int error;

	if (dd == NULL)
		return (ENOENT);
	if (fwg->buf == NULL) {
		fwg->reg_filled = dd->dump_size;
		return (0);
	}
	if (atomic_load_acq_int(&dd->dump_valid) == 0)
		return (ENOENT);

	urv = fwg->buf;
	for (i = 0, r = dd->rege; r->cnt != 0; r++) {
		for (ri = 0; ri < r->cnt; ri++) {
			if (i >= fwg->reg_cnt)
				goto out;
			rv.addr = r->addr + ri * 4;
			rv.val = dd->dump[i];
			error = copyout(&rv, urv, sizeof(rv));
			if (error != 0)
				return (error);
			urv++;
			i++;
		}
	}
out:
	fwg->reg_filled = i;
	return (0);
}

static int
mlx5_fwdump_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct mlx5_core_dev *mdev;
	struct mlx5_fwdump_get *fwg;
	struct mlx5_fwdump_addr *devaddr;
	struct mlx5_dump_data *dd;
	int error;

	error = 0;
	switch (cmd) {
	case MLX5_FWDUMP_GET:
		if ((fflag & FREAD) == 0) {
			error = EBADF;
			break;
		}
		fwg = (struct mlx5_fwdump_get *)data;
		devaddr = &fwg->devaddr;
		error = mlx5_dbsf_to_core(devaddr, &mdev);
		if (error != 0)
			break;
		error = mlx5_fwdump_copyout(mdev->dump_data, fwg);
		break;
	case MLX5_FWDUMP_RESET:
		if ((fflag & FWRITE) == 0) {
			error = EBADF;
			break;
		}
		devaddr = (struct mlx5_fwdump_addr *)data;
		error = mlx5_dbsf_to_core(devaddr, &mdev);
		if (error != 0)
			break;
		dd = mdev->dump_data;
		if (dd != NULL)
			atomic_store_rel_int(&dd->dump_valid, 0);
		else
			error = ENOENT;
		break;
	case MLX5_FWDUMP_FORCE:
		if ((fflag & FWRITE) == 0) {
			error = EBADF;
			break;
		}
		devaddr = (struct mlx5_fwdump_addr *)data;
		error = mlx5_dbsf_to_core(devaddr, &mdev);
		if (error != 0)
			break;
		mlx5_fwdump(mdev);
		break;
	default:
		error = ENOTTY;
		break;
	}
	return (error);
}

static struct cdevsw mlx5_fwdump_devsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	mlx5_fwdump_ioctl,
};

static struct cdev *mlx5_fwdump_dev;

int
mlx5_fwdump_init(void)
{
	struct make_dev_args mda;
	int error;

	make_dev_args_init(&mda);
	mda.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
	mda.mda_devsw = &mlx5_fwdump_devsw;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_OPERATOR;
	mda.mda_mode = 0640;
	error = make_dev_s(&mda, &mlx5_fwdump_dev, "mlx5ctl");
	return (-error);
}

void
mlx5_fwdump_fini(void)
{

	if (mlx5_fwdump_dev != NULL)
		destroy_dev(mlx5_fwdump_dev);

}
