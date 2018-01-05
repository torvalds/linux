/*
 * Freescale SSI ALSA SoC Digital Audio Interface (DAI) debugging functions
 *
 * Copyright 2014 Markus Pargmann <mpa@pengutronix.de>, Pengutronix
 *
 * Splitted from fsl_ssi.c
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>

#include "fsl_ssi.h"

void fsl_ssi_dbg_isr(struct fsl_ssi_dbg *dbg, u32 sisr)
{
	if (sisr & SSI_SISR_RFRC)
		dbg->stats.rfrc++;

	if (sisr & SSI_SISR_TFRC)
		dbg->stats.tfrc++;

	if (sisr & SSI_SISR_CMDAU)
		dbg->stats.cmdau++;

	if (sisr & SSI_SISR_CMDDU)
		dbg->stats.cmddu++;

	if (sisr & SSI_SISR_RXT)
		dbg->stats.rxt++;

	if (sisr & SSI_SISR_RDR1)
		dbg->stats.rdr1++;

	if (sisr & SSI_SISR_RDR0)
		dbg->stats.rdr0++;

	if (sisr & SSI_SISR_TDE1)
		dbg->stats.tde1++;

	if (sisr & SSI_SISR_TDE0)
		dbg->stats.tde0++;

	if (sisr & SSI_SISR_ROE1)
		dbg->stats.roe1++;

	if (sisr & SSI_SISR_ROE0)
		dbg->stats.roe0++;

	if (sisr & SSI_SISR_TUE1)
		dbg->stats.tue1++;

	if (sisr & SSI_SISR_TUE0)
		dbg->stats.tue0++;

	if (sisr & SSI_SISR_TFS)
		dbg->stats.tfs++;

	if (sisr & SSI_SISR_RFS)
		dbg->stats.rfs++;

	if (sisr & SSI_SISR_TLS)
		dbg->stats.tls++;

	if (sisr & SSI_SISR_RLS)
		dbg->stats.rls++;

	if (sisr & SSI_SISR_RFF1)
		dbg->stats.rff1++;

	if (sisr & SSI_SISR_RFF0)
		dbg->stats.rff0++;

	if (sisr & SSI_SISR_TFE1)
		dbg->stats.tfe1++;

	if (sisr & SSI_SISR_TFE0)
		dbg->stats.tfe0++;
}

/**
 * Show the statistics of a flag only if its interrupt is enabled
 *
 * Compilers will optimize it to a no-op if the interrupt is disabled
 */
#define SIER_SHOW(flag, name) \
	do { \
		if (SSI_SIER_##flag) \
			seq_printf(s, #name "=%u\n", ssi_dbg->stats.name); \
	} while (0)


/**
 * Display the statistics for the current SSI device
 *
 * To avoid confusion, only show those counts that are enabled
 */
static int fsl_ssi_stats_show(struct seq_file *s, void *unused)
{
	struct fsl_ssi_dbg *ssi_dbg = s->private;

	SIER_SHOW(RFRC_EN, rfrc);
	SIER_SHOW(TFRC_EN, tfrc);
	SIER_SHOW(CMDAU_EN, cmdau);
	SIER_SHOW(CMDDU_EN, cmddu);
	SIER_SHOW(RXT_EN, rxt);
	SIER_SHOW(RDR1_EN, rdr1);
	SIER_SHOW(RDR0_EN, rdr0);
	SIER_SHOW(TDE1_EN, tde1);
	SIER_SHOW(TDE0_EN, tde0);
	SIER_SHOW(ROE1_EN, roe1);
	SIER_SHOW(ROE0_EN, roe0);
	SIER_SHOW(TUE1_EN, tue1);
	SIER_SHOW(TUE0_EN, tue0);
	SIER_SHOW(TFS_EN, tfs);
	SIER_SHOW(RFS_EN, rfs);
	SIER_SHOW(TLS_EN, tls);
	SIER_SHOW(RLS_EN, rls);
	SIER_SHOW(RFF1_EN, rff1);
	SIER_SHOW(RFF0_EN, rff0);
	SIER_SHOW(TFE1_EN, tfe1);
	SIER_SHOW(TFE0_EN, tfe0);

	return 0;
}

static int fsl_ssi_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, fsl_ssi_stats_show, inode->i_private);
}

static const struct file_operations fsl_ssi_stats_ops = {
	.open = fsl_ssi_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int fsl_ssi_debugfs_create(struct fsl_ssi_dbg *ssi_dbg, struct device *dev)
{
	ssi_dbg->dbg_dir = debugfs_create_dir(dev_name(dev), NULL);
	if (!ssi_dbg->dbg_dir)
		return -ENOMEM;

	ssi_dbg->dbg_stats = debugfs_create_file("stats", S_IRUGO,
						 ssi_dbg->dbg_dir, ssi_dbg,
						 &fsl_ssi_stats_ops);
	if (!ssi_dbg->dbg_stats) {
		debugfs_remove(ssi_dbg->dbg_dir);
		return -ENOMEM;
	}

	return 0;
}

void fsl_ssi_debugfs_remove(struct fsl_ssi_dbg *ssi_dbg)
{
	debugfs_remove(ssi_dbg->dbg_stats);
	debugfs_remove(ssi_dbg->dbg_dir);
}
