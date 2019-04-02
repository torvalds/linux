/************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of version 2 of the GNU General Public License as
    published by the Free Software Foundation;

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

De macro translation.

************************************************************************/

#include "hpi_internal.h"
#include "hpide.h"

/* De level; 0 quiet; 1 informative, 2 de, 3 verbose de.  */
int hpi_de_level = HPI_DE_LEVEL_DEFAULT;

void hpi_de_init(void)
{
	printk(KERN_INFO "de start\n");
}

int hpi_de_level_set(int level)
{
	int old_level;

	old_level = hpi_de_level;
	hpi_de_level = level;
	return old_level;
}

int hpi_de_level_get(void)
{
	return hpi_de_level;
}

void hpi_de_message(struct hpi_message *phm, char *sz_fileline)
{
	if (phm) {
		printk(KERN_DE "HPI_MSG%d,%d,%d,%d,%d\n", phm->version,
			phm->adapter_index, phm->obj_index, phm->function,
			phm->u.c.attribute);
	}

}

void hpi_de_data(u16 *pdata, u32 len)
{
	u32 i;
	int j;
	int k;
	int lines;
	int cols = 8;

	lines = (len + cols - 1) / cols;
	if (lines > 8)
		lines = 8;

	for (i = 0, j = 0; j < lines; j++) {
		printk(KERN_DE "%p:", (pdata + i));

		for (k = 0; k < cols && i < len; i++, k++)
			printk(KERN_CONT "%s%04x", k == 0 ? "" : " ", pdata[i]);

		printk(KERN_CONT "\n");
	}
}
