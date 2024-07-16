/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MTD device concatenation layer definitions
 *
 * Copyright © 2002      Robert Kaiser <rkaiser@sysgo.de>
 */

#ifndef MTD_CONCAT_H
#define MTD_CONCAT_H


struct mtd_info *mtd_concat_create(
    struct mtd_info *subdev[],  /* subdevices to concatenate */
    int num_devs,               /* number of subdevices      */
    const char *name);          /* name for the new device   */

void mtd_concat_destroy(struct mtd_info *mtd);

#endif

