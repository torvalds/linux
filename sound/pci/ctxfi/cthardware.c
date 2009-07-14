/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	cthardware.c
 *
 * @Brief
 * This file contains the implementation of hardware access methord.
 *
 * @Author	Liu Chun
 * @Date 	Jun 26 2008
 *
 */

#include "cthardware.h"
#include "cthw20k1.h"
#include "cthw20k2.h"
#include <linux/bug.h>

int __devinit create_hw_obj(struct pci_dev *pci, enum CHIPTYP chip_type,
			    enum CTCARDS model, struct hw **rhw)
{
	int err;

	switch (chip_type) {
	case ATC20K1:
		err = create_20k1_hw_obj(rhw);
		break;
	case ATC20K2:
		err = create_20k2_hw_obj(rhw);
		break;
	default:
		err = -ENODEV;
		break;
	}
	if (err)
		return err;

	(*rhw)->pci = pci;
	(*rhw)->chip_type = chip_type;
	(*rhw)->model = model;

	return 0;
}

int destroy_hw_obj(struct hw *hw)
{
	int err;

	switch (hw->pci->device) {
	case 0x0005:	/* 20k1 device */
		err = destroy_20k1_hw_obj(hw);
		break;
	case 0x000B:	/* 20k2 device */
		err = destroy_20k2_hw_obj(hw);
		break;
	default:
		err = -ENODEV;
		break;
	}

	return err;
}

unsigned int get_field(unsigned int data, unsigned int field)
{
	int i;

	BUG_ON(!field);
	/* @field should always be greater than 0 */
	for (i = 0; !(field & (1 << i)); )
		i++;

	return (data & field) >> i;
}

void set_field(unsigned int *data, unsigned int field, unsigned int value)
{
	int i;

	BUG_ON(!field);
	/* @field should always be greater than 0 */
	for (i = 0; !(field & (1 << i)); )
		i++;

	*data = (*data & (~field)) | ((value << i) & field);
}

