/*
 *  Copyright Intel Corporation (C) 2017. All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Reset binding definitions for Altera Arria10 MAX5 System Resource Chip
 *
 * Adapted from altr,rst-mgr-a10.h
 */

#ifndef _DT_BINDINGS_RESET_ALTR_RST_MGR_A10SR_H
#define _DT_BINDINGS_RESET_ALTR_RST_MGR_A10SR_H

/* Peripheral PHY resets */
#define A10SR_RESET_ENET_HPS	0
#define A10SR_RESET_PCIE	1
#define A10SR_RESET_FILE	2
#define A10SR_RESET_BQSPI	3
#define A10SR_RESET_USB		4

#define A10SR_RESET_NUM		5

#endif
