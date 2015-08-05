/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 */

/*
 * The code contained herein is licensed under the GNU Lesser General
 * Public License.  You may obtain a copy of the GNU Lesser General
 * Public License Version 2.1 or later at the following locations:
 *
 * http://www.opensource.org/licenses/lgpl-license.html
 * http://www.gnu.org/copyleft/lgpl.html
 */

/*
 * @file linux/imx_rpmsg.h
 *
 * @brief Global header file for imx RPMSG
 *
 * @ingroup RPMSG
 */
#ifndef __LINUX_IMX_RPMSG_H__
#define __LINUX_IMX_RPMSG_H__

int imx_mu_rpmsg_send(unsigned int vq_id);
int imx_mu_rpmsg_register_nb(const char *name, struct notifier_block *nb);
int imx_mu_rpmsg_unregister_nb(const char *name, struct notifier_block *nb);
#endif /* __LINUX_IMX_RPMSG_H__ */
