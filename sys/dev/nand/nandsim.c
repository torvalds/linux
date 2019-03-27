/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Simulated NAND controller driver */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandsim.h>
#include <dev/nand/nandsim_chip.h>
#include <dev/nand/nandsim_log.h>
#include <dev/nand/nandsim_swap.h>

struct sim_param sim;
struct sim_ctrl_conf ctrls[MAX_SIM_DEV];

static struct cdev *nandsim_dev;
static d_ioctl_t nandsim_ioctl;

static void nandsim_init_sim_param(struct sim_param *);
static int nandsim_create_ctrl(struct sim_ctrl *);
static int nandsim_destroy_ctrl(int);
static int nandsim_ctrl_status(struct sim_ctrl *);
static int nandsim_create_chip(struct sim_chip *);
static int nandsim_destroy_chip(struct sim_ctrl_chip *);
static int nandsim_chip_status(struct sim_chip *);
static int nandsim_start_ctrl(int);
static int nandsim_stop_ctrl(int);
static int nandsim_inject_error(struct sim_error *);
static int nandsim_get_block_state(struct sim_block_state *);
static int nandsim_set_block_state(struct sim_block_state *);
static int nandsim_modify(struct sim_mod *);
static int nandsim_dump(struct sim_dump *);
static int nandsim_restore(struct sim_dump *);
static int nandsim_freeze(struct sim_ctrl_chip *);
static void nandsim_print_log(struct sim_log *);
static struct nandsim_chip *get_nandsim_chip(uint8_t, uint8_t);

static struct cdevsw nandsim_cdevsw = {
	.d_version =    D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_ioctl =      nandsim_ioctl,
	.d_name =       "nandsim",
};

int
nandsim_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
    int flags, struct thread *td)
{
	int ret = 0;

	switch (cmd) {
	case NANDSIM_SIM_PARAM:
		nandsim_init_sim_param((struct sim_param *)data);
		break;
	case NANDSIM_CREATE_CTRL:
		ret = nandsim_create_ctrl((struct sim_ctrl *)data);
		break;
	case NANDSIM_DESTROY_CTRL:
		ret = nandsim_destroy_ctrl(*(int *)data);
		break;
	case NANDSIM_STATUS_CTRL:
		ret = nandsim_ctrl_status((struct sim_ctrl *)data);
		break;
	case NANDSIM_CREATE_CHIP:
		ret = nandsim_create_chip((struct sim_chip *)data);
		break;
	case NANDSIM_DESTROY_CHIP:
		ret = nandsim_destroy_chip((struct sim_ctrl_chip *)data);
		break;
	case NANDSIM_STATUS_CHIP:
		ret = nandsim_chip_status((struct sim_chip *)data);
		break;
	case NANDSIM_MODIFY:
		ret = nandsim_modify((struct sim_mod *)data);
		break;
	case NANDSIM_START_CTRL:
		ret = nandsim_start_ctrl(*(int *)data);
		break;
	case NANDSIM_STOP_CTRL:
		ret = nandsim_stop_ctrl(*(int *)data);
		break;
	case NANDSIM_INJECT_ERROR:
		ret = nandsim_inject_error((struct sim_error *)data);
		break;
	case NANDSIM_SET_BLOCK_STATE:
		ret = nandsim_set_block_state((struct sim_block_state *)data);
		break;
	case NANDSIM_GET_BLOCK_STATE:
		ret = nandsim_get_block_state((struct sim_block_state *)data);
		break;
	case NANDSIM_PRINT_LOG:
		nandsim_print_log((struct sim_log *)data);
		break;
	case NANDSIM_DUMP:
		ret = nandsim_dump((struct sim_dump *)data);
		break;
	case NANDSIM_RESTORE:
		ret = nandsim_restore((struct sim_dump *)data);
		break;
	case NANDSIM_FREEZE:
		ret = nandsim_freeze((struct sim_ctrl_chip *)data);
		break;
	default:
		ret = EINVAL;
		break;
	}

	return (ret);
}

static void
nandsim_init_sim_param(struct sim_param *param)
{

	if (!param)
		return;

	nand_debug(NDBG_SIM,"log level:%d output %d", param->log_level,
	    param->log_output);
	nandsim_log_level = param->log_level;
	nandsim_log_output = param->log_output;
}

static int
nandsim_create_ctrl(struct sim_ctrl *ctrl)
{
	struct sim_ctrl_conf *sim_ctrl;

	nand_debug(NDBG_SIM,"create controller num:%d cs:%d",ctrl->num,
	    ctrl->num_cs);

	if (ctrl->num >= MAX_SIM_DEV) {
		return (EINVAL);
	}

	sim_ctrl = &ctrls[ctrl->num];
	if(sim_ctrl->created)
		return (EEXIST);

	sim_ctrl->num = ctrl->num;
	sim_ctrl->num_cs = ctrl->num_cs;
	sim_ctrl->ecc = ctrl->ecc;
	memcpy(sim_ctrl->ecc_layout, ctrl->ecc_layout,
	    MAX_ECC_BYTES * sizeof(ctrl->ecc_layout[0]));
	strlcpy(sim_ctrl->filename, ctrl->filename,
	    FILENAME_SIZE);
	sim_ctrl->created = 1;

	return (0);
}

static int
nandsim_destroy_ctrl(int ctrl_num)
{

	nand_debug(NDBG_SIM,"destroy controller num:%d", ctrl_num);

	if (ctrl_num >= MAX_SIM_DEV) {
		return (EINVAL);
	}

	if (!ctrls[ctrl_num].created) {
		return (ENODEV);
	}

	if (ctrls[ctrl_num].running) {
		return (EBUSY);
	}

	memset(&ctrls[ctrl_num], 0, sizeof(ctrls[ctrl_num]));

	return (0);
}

static int
nandsim_ctrl_status(struct sim_ctrl *ctrl)
{

	nand_debug(NDBG_SIM,"status controller num:%d cs:%d",ctrl->num,
	    ctrl->num_cs);

	if (ctrl->num >= MAX_SIM_DEV) {
		return (EINVAL);
	}

	ctrl->num_cs = ctrls[ctrl->num].num_cs;
	ctrl->ecc = ctrls[ctrl->num].ecc;
	memcpy(ctrl->ecc_layout, ctrls[ctrl->num].ecc_layout,
	    MAX_ECC_BYTES * sizeof(ctrl->ecc_layout[0]));
	strlcpy(ctrl->filename, ctrls[ctrl->num].filename,
	    FILENAME_SIZE);
	ctrl->running = ctrls[ctrl->num].running;
	ctrl->created = ctrls[ctrl->num].created;

	return (0);
}

static int
nandsim_create_chip(struct sim_chip *chip)
{
	struct sim_chip *sim_chip;

	nand_debug(NDBG_SIM,"create chip num:%d at ctrl:%d", chip->num,
	    chip->ctrl_num);

	if (chip->ctrl_num >= MAX_SIM_DEV ||
	    chip->num >= MAX_CTRL_CS) {
		return (EINVAL);
	}

	if (ctrls[chip->ctrl_num].chips[chip->num]) {
		return (EEXIST);
	}

	sim_chip = malloc(sizeof(*sim_chip), M_NANDSIM,
	    M_WAITOK);
	if (sim_chip == NULL) {
		return (ENOMEM);
	}

	memcpy(sim_chip, chip, sizeof(*sim_chip));
	ctrls[chip->ctrl_num].chips[chip->num] = sim_chip;
	sim_chip->created = 1;

	return (0);
}

static int
nandsim_destroy_chip(struct sim_ctrl_chip *chip)
{
	struct sim_ctrl_conf *ctrl_conf;

	nand_debug(NDBG_SIM,"destroy chip num:%d at ctrl:%d", chip->chip_num,
	    chip->ctrl_num);

	if (chip->ctrl_num >= MAX_SIM_DEV ||
	    chip->chip_num >= MAX_CTRL_CS)
		return (EINVAL);

	ctrl_conf = &ctrls[chip->ctrl_num];

	if (!ctrl_conf->created || !ctrl_conf->chips[chip->chip_num])
		return (ENODEV);

	if (ctrl_conf->running)
		return (EBUSY);

	free(ctrl_conf->chips[chip->chip_num], M_NANDSIM);
	ctrl_conf->chips[chip->chip_num] = NULL;

	return (0);
}

static int
nandsim_chip_status(struct sim_chip *chip)
{
	struct sim_ctrl_conf *ctrl_conf;

	nand_debug(NDBG_SIM,"status for chip num:%d at ctrl:%d", chip->num,
	    chip->ctrl_num);

	if (chip->ctrl_num >= MAX_SIM_DEV &&
	    chip->num >= MAX_CTRL_CS)
		return (EINVAL);

	ctrl_conf = &ctrls[chip->ctrl_num];
	if (!ctrl_conf->chips[chip->num])
		chip->created = 0;
	else
		memcpy(chip, ctrl_conf->chips[chip->num], sizeof(*chip));

	return (0);
}

static int
nandsim_start_ctrl(int num)
{
	device_t nexus, ndev;
	devclass_t nexus_devclass;
	int ret = 0;

	nand_debug(NDBG_SIM,"start ctlr num:%d", num);

	if (num >= MAX_SIM_DEV)
		return (EINVAL);

	if (!ctrls[num].created)
		return (ENODEV);

	if (ctrls[num].running)
		return (EBUSY);

	/* We will add our device as a child of the nexus0 device */
	if (!(nexus_devclass = devclass_find("nexus")) ||
	    !(nexus = devclass_get_device(nexus_devclass, 0)))
		return (EFAULT);

	/*
	 * Create a newbus device representing this frontend instance
	 *
	 * XXX powerpc nexus doesn't implement bus_add_child, so child
	 * must be added by device_add_child().
	 */
#if defined(__powerpc__)
	ndev = device_add_child(nexus, "nandsim", num);
#else
	ndev = BUS_ADD_CHILD(nexus, 0, "nandsim", num);
#endif
	if (!ndev)
		return (EFAULT);

	mtx_lock(&Giant);
	ret = device_probe_and_attach(ndev);
	mtx_unlock(&Giant);

	if (ret == 0) {
		ctrls[num].sim_ctrl_dev = ndev;
		ctrls[num].running = 1;
	}

	return (ret);
}

static int
nandsim_stop_ctrl(int num)
{
	device_t nexus;
	devclass_t nexus_devclass;
	int ret = 0;

	nand_debug(NDBG_SIM,"stop controller num:%d", num);

	if (num >= MAX_SIM_DEV) {
		return (EINVAL);
	}

	if (!ctrls[num].created || !ctrls[num].running) {
		return (ENODEV);
	}

	/* We will add our device as a child of the nexus0 device */
	if (!(nexus_devclass = devclass_find("nexus")) ||
	    !(nexus = devclass_get_device(nexus_devclass, 0))) {
		return (ENODEV);
	}

	mtx_lock(&Giant);
	if (ctrls[num].sim_ctrl_dev) {
		ret = device_delete_child(nexus, ctrls[num].sim_ctrl_dev);
		ctrls[num].sim_ctrl_dev = NULL;
	}
	mtx_unlock(&Giant);

	ctrls[num].running = 0;

	return (ret);
}

static struct nandsim_chip *
get_nandsim_chip(uint8_t ctrl_num, uint8_t chip_num)
{
	struct nandsim_softc *sc;

	if (!ctrls[ctrl_num].sim_ctrl_dev)
		return (NULL);

	sc = device_get_softc(ctrls[ctrl_num].sim_ctrl_dev);
	return (sc->chips[chip_num]);
}

static void
nandsim_print_log(struct sim_log *sim_log)
{
	struct nandsim_softc *sc;
	int len1, len2;

	if (!ctrls[sim_log->ctrl_num].sim_ctrl_dev)
		return;

	sc = device_get_softc(ctrls[sim_log->ctrl_num].sim_ctrl_dev);
	if (sc->log_buff) {
		len1 = strlen(&sc->log_buff[sc->log_idx + 1]);
		if (len1 >= sim_log->len)
			len1 = sim_log->len;
		copyout(&sc->log_buff[sc->log_idx + 1], sim_log->log, len1);
		len2 = strlen(sc->log_buff);
		if (len2 >= (sim_log->len - len1))
			len2 = (sim_log->len - len1);
		copyout(sc->log_buff, &sim_log->log[len1], len2);
		sim_log->len = len1 + len2;
	}
}

static int
nandsim_inject_error(struct sim_error *error)
{
	struct nandsim_chip *chip;
	struct block_space *bs;
	struct onfi_params *param;
	int page, page_size, block, offset;

	nand_debug(NDBG_SIM,"inject error for chip %d at ctrl %d\n",
	    error->chip_num, error->ctrl_num);

	if (error->ctrl_num >= MAX_SIM_DEV ||
	    error->chip_num >= MAX_CTRL_CS)
		return (EINVAL);

	if (!ctrls[error->ctrl_num].created || !ctrls[error->ctrl_num].running)
		return (ENODEV);

	chip = get_nandsim_chip(error->ctrl_num, error->chip_num);
	param = &chip->params;
	page_size = param->bytes_per_page + param->spare_bytes_per_page;
	block = error->page_num / param->pages_per_block;
	page = error->page_num % param->pages_per_block;

	bs = get_bs(chip->swap, block, 1);
	if (!bs)
		return (EINVAL);

	offset = (page * page_size) + error->column;
	memset(&bs->blk_ptr[offset], error->pattern, error->len);

	return (0);
}

static int
nandsim_set_block_state(struct sim_block_state *bs)
{
	struct onfi_params *params;
	struct nandsim_chip *chip;
	int blocks;

	nand_debug(NDBG_SIM,"set block state for %d:%d block %d\n",
	    bs->chip_num, bs->ctrl_num, bs->block_num);

	if (bs->ctrl_num >= MAX_SIM_DEV ||
	    bs->chip_num >= MAX_CTRL_CS)
		return (EINVAL);

	chip = get_nandsim_chip(bs->ctrl_num, bs->chip_num);
	params = &chip->params;
	blocks = params->luns * params->blocks_per_lun;

	if (bs->block_num > blocks)
		return (EINVAL);

	chip->blk_state[bs->block_num].is_bad = bs->state;

	if (bs->wearout >= 0)
		chip->blk_state[bs->block_num].wear_lev = bs->wearout;

	return (0);
}

static int
nandsim_get_block_state(struct sim_block_state *bs)
{
	struct onfi_params *params;
	struct nandsim_chip *chip;
	int blocks;

	if (bs->ctrl_num >= MAX_SIM_DEV ||
	    bs->chip_num >= MAX_CTRL_CS)
		return (EINVAL);

	nand_debug(NDBG_SIM,"get block state for %d:%d block %d\n",
	    bs->chip_num, bs->ctrl_num, bs->block_num);

	chip = get_nandsim_chip(bs->ctrl_num, bs->chip_num);
	params = &chip->params;
	blocks = params->luns * params->blocks_per_lun;

	if (bs->block_num > blocks)
		return (EINVAL);

	bs->state = chip->blk_state[bs->block_num].is_bad;
	bs->wearout = chip->blk_state[bs->block_num].wear_lev;

	return (0);
}

static int
nandsim_dump(struct sim_dump *dump)
{
	struct nandsim_chip *chip;
	struct block_space *bs;
	int blk_size;

	nand_debug(NDBG_SIM,"dump chip %d %d\n", dump->ctrl_num, dump->chip_num);

	if (dump->ctrl_num >= MAX_SIM_DEV ||
	    dump->chip_num >= MAX_CTRL_CS)
		return (EINVAL);

	chip = get_nandsim_chip(dump->ctrl_num, dump->chip_num);
	blk_size = chip->cg.block_size +
	    (chip->cg.oob_size * chip->cg.pgs_per_blk);

	bs = get_bs(chip->swap, dump->block_num, 0);
	if (!bs)
		return (EINVAL);

	if (dump->len > blk_size)
		dump->len = blk_size;

	copyout(bs->blk_ptr, dump->data, dump->len);

	return (0);
}

static int
nandsim_restore(struct sim_dump *dump)
{
	struct nandsim_chip *chip;
	struct block_space *bs;
	int blk_size;

	nand_debug(NDBG_SIM,"restore chip %d %d\n", dump->ctrl_num,
	    dump->chip_num);

	if (dump->ctrl_num >= MAX_SIM_DEV ||
	    dump->chip_num >= MAX_CTRL_CS)
		return (EINVAL);

	chip = get_nandsim_chip(dump->ctrl_num, dump->chip_num);
	blk_size = chip->cg.block_size +
	    (chip->cg.oob_size * chip->cg.pgs_per_blk);

	bs = get_bs(chip->swap, dump->block_num, 1);
	if (!bs)
		return (EINVAL);

	if (dump->len > blk_size)
		dump->len = blk_size;


	copyin(dump->data, bs->blk_ptr, dump->len);

	return (0);
}

static int
nandsim_freeze(struct sim_ctrl_chip *ctrl_chip)
{
	struct nandsim_chip *chip;

	if (ctrl_chip->ctrl_num >= MAX_SIM_DEV ||
	    ctrl_chip->chip_num >= MAX_CTRL_CS)
		return (EINVAL);

	chip = get_nandsim_chip(ctrl_chip->ctrl_num, ctrl_chip->chip_num);
	nandsim_chip_freeze(chip);

	return (0);
}

static int
nandsim_modify(struct sim_mod *mod)
{
	struct sim_chip *sim_conf = NULL;
	struct nandsim_chip *sim_chip = NULL;

	nand_debug(NDBG_SIM,"modify ctlr %d chip %d", mod->ctrl_num,
	    mod->chip_num);

	if (mod->field != SIM_MOD_LOG_LEVEL) {
		if (mod->ctrl_num >= MAX_SIM_DEV ||
		    mod->chip_num >= MAX_CTRL_CS)
			return (EINVAL);

		sim_conf = ctrls[mod->ctrl_num].chips[mod->chip_num];
		sim_chip = get_nandsim_chip(mod->ctrl_num, mod->chip_num);
	}

	switch (mod->field) {
	case SIM_MOD_LOG_LEVEL:
		nandsim_log_level = mod->new_value;
		break;
	case SIM_MOD_ERASE_TIME:
		sim_conf->erase_time = sim_chip->erase_delay = mod->new_value;
		break;
	case SIM_MOD_PROG_TIME:
		sim_conf->prog_time = sim_chip->prog_delay = mod->new_value;
		break;
	case SIM_MOD_READ_TIME:
		sim_conf->read_time = sim_chip->read_delay = mod->new_value;
		break;
	case SIM_MOD_ERROR_RATIO:
		sim_conf->error_ratio = mod->new_value;
		sim_chip->error_ratio = mod->new_value;
		break;
	default:
		break;
	}

	return (0);
}
static int
nandsim_modevent(module_t mod __unused, int type, void *data __unused)
{
	struct sim_ctrl_chip chip_ctrl;
	int i, j;

	switch (type) {
	case MOD_LOAD:
		nandsim_dev = make_dev(&nandsim_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0600, "nandsim.ioctl");
		break;
	case MOD_UNLOAD:
		for (i = 0; i < MAX_SIM_DEV; i++) {
			nandsim_stop_ctrl(i);
			chip_ctrl.ctrl_num = i;
			for (j = 0; j < MAX_CTRL_CS; j++) {
				chip_ctrl.chip_num = j;
				nandsim_destroy_chip(&chip_ctrl);
			}
			nandsim_destroy_ctrl(i);
		}
		destroy_dev(nandsim_dev);
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

DEV_MODULE(nandsim, nandsim_modevent, NULL);
MODULE_VERSION(nandsim, 1);
MODULE_DEPEND(nandsim, nand, 1, 1, 1);
MODULE_DEPEND(nandsim, alq, 1, 1, 1);
