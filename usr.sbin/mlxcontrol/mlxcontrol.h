/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith
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
 *
 *	$FreeBSD$
 */

#include <sys/queue.h>

#define debug(fmt, args...)	printf("%s: " fmt "\n", __func__ , ##args)

struct mlxd_foreach_action 
{
    void	(*func)(int unit, void *arg);
    void	*arg;
};

extern void			mlx_foreach(void (*func)(int unit, void *arg), void *arg);
void				mlxd_foreach_ctrlr(int unit, void *arg);
extern void			mlxd_foreach(void (*func)(int unit, void *arg), void *arg);
extern int			mlxd_find_ctrlr(int unit, int *ctrlr, int *sysdrive);
extern void			mlx_perform(int unit, void (*func)(int fd, void *arg), void *arg);
extern void			mlx_command(int fd, void *arg);
extern int			mlx_enquiry(int unit, struct mlx_enquiry2 *enq);
extern int			mlx_read_configuration(int unit, struct mlx_core_cfg *cfg);
extern int			mlx_scsi_inquiry(int unit, int bus, int target, char **vendor, char **device, char **revision);
extern int			mlx_get_device_state(int fd, int channel, int target, struct mlx_phys_drv *drv);

extern char 	*ctrlrpath(int unit);
extern char	*ctrlrname(int unit);
extern char	*drivepath(int unit);
extern char	*drivename(int unit);
extern int	ctrlrunit(char *str);
extern int	driveunit(char *str);

extern void	mlx_print_phys_drv(struct mlx_phys_drv *drv, int channel, int target, char *prefix, int verbose);

struct conf_phys_drv
{
    TAILQ_ENTRY(conf_phys_drv)	pd_link;
    int				pd_bus;
    int				pd_target;
    struct mlx_phys_drv		pd_drv;
};

struct conf_span 
{
    TAILQ_ENTRY(conf_span)	s_link;
    struct conf_phys_drv	*s_drvs[8];
    struct mlx_sys_drv_span	s_span;
};

struct conf_sys_drv
{
    TAILQ_ENTRY(conf_sys_drv)	sd_link;
    struct conf_span		*sd_spans[4];
    struct mlx_sys_drv		sd_drv;
};

struct conf_config
{
    TAILQ_HEAD(,conf_phys_drv)	cc_phys_drvs;
    TAILQ_HEAD(,conf_span)	cc_spans;
    TAILQ_HEAD(,conf_sys_drv)	cc_sys_drvs;
    struct conf_sys_drv		*cc_drives[32];
    struct mlx_core_cfg		cc_cfg;
};

