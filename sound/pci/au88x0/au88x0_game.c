/*
 *  Manuel Jander.
 *
 *  Based on the work of:
 *  Vojtech Pavlik
 *  Raymond Ingles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 *
 * Based 90% on Vojtech Pavlik pcigame driver.
 * Merged and modified by Manuel Jander, for the OpenVortex
 * driver. (email: mjander@embedded.cl).
 */

#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <sound/core.h>
#include "au88x0.h"
#include <linux/gameport.h>
#include <linux/export.h>

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))

#define VORTEX_GAME_DWAIT	20	/* 20 ms */

static unsigned char vortex_game_read(struct gameport *gameport)
{
	vortex_t *vortex = gameport_get_port_data(gameport);
	return hwread(vortex->mmio, VORTEX_GAME_LEGACY);
}

static void vortex_game_trigger(struct gameport *gameport)
{
	vortex_t *vortex = gameport_get_port_data(gameport);
	hwwrite(vortex->mmio, VORTEX_GAME_LEGACY, 0xff);
}

static int
vortex_game_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	vortex_t *vortex = gameport_get_port_data(gameport);
	int i;

	*buttons = (~hwread(vortex->mmio, VORTEX_GAME_LEGACY) >> 4) & 0xf;

	for (i = 0; i < 4; i++) {
		axes[i] =
		    hwread(vortex->mmio, VORTEX_GAME_AXIS + (i * AXIS_SIZE));
		if (axes[i] == AXIS_RANGE)
			axes[i] = -1;
	}
	return 0;
}

static int vortex_game_open(struct gameport *gameport, int mode)
{
	vortex_t *vortex = gameport_get_port_data(gameport);

	switch (mode) {
	case GAMEPORT_MODE_COOKED:
		hwwrite(vortex->mmio, VORTEX_CTRL2,
			hwread(vortex->mmio,
			       VORTEX_CTRL2) | CTRL2_GAME_ADCMODE);
		msleep(VORTEX_GAME_DWAIT);
		return 0;
	case GAMEPORT_MODE_RAW:
		hwwrite(vortex->mmio, VORTEX_CTRL2,
			hwread(vortex->mmio,
			       VORTEX_CTRL2) & ~CTRL2_GAME_ADCMODE);
		return 0;
	default:
		return -1;
	}

	return 0;
}

static int __devinit vortex_gameport_register(vortex_t * vortex)
{
	struct gameport *gp;

	vortex->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR "vortex: cannot allocate memory for gameport\n");
		return -ENOMEM;
	};

	gameport_set_name(gp, "AU88x0 Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(vortex->pci_dev));
	gameport_set_dev_parent(gp, &vortex->pci_dev->dev);

	gp->read = vortex_game_read;
	gp->trigger = vortex_game_trigger;
	gp->cooked_read = vortex_game_cooked_read;
	gp->open = vortex_game_open;

	gameport_set_port_data(gp, vortex);
	gp->fuzz = 64;

	gameport_register_port(gp);

	return 0;
}

static void vortex_gameport_unregister(vortex_t * vortex)
{
	if (vortex->gameport) {
		gameport_unregister_port(vortex->gameport);
		vortex->gameport = NULL;
	}
}

#else
static inline int vortex_gameport_register(vortex_t * vortex) { return -ENOSYS; }
static inline void vortex_gameport_unregister(vortex_t * vortex) { }
#endif
