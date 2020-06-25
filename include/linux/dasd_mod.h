/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DASD_MOD_H
#define DASD_MOD_H

#include <asm/dasd.h>

extern int dasd_biodasdinfo(struct gendisk *disk, dasd_information2_t *info);

#endif
