/*
 * include/asm-sh/saturn/smpc.h
 *
 * System Manager / Peripheral Control definitions.
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#ifndef __ASM_SH_SATURN_SMPC_H
#define __ASM_SH_SATURN_SMPC_H

#include <asm/io.h>

#define SMPC_COMMAND	0x2010001f	/* SMPC command register */
#define SMPC_RESULT	0x2010005f	/* SMPC result register */
#define SMPC_STATUS	0x20100063	/* SMPC status register */

#define SMPC_CMD_MSHON	0x0001		/* Master SH On */
#define SMPC_CMD_SSHON	0x0002		/* Slave SH On */
#define SMPC_CMD_SSHOFF	0x0003		/* Slave SH Off */
#define SMPC_CMD_SNDON	0x0004		/* Sound On */
#define SMPC_CMD_SNDOFF	0x0005		/* Sound Off */
#define SMPC_CMD_CDON	0x0006		/* CD On */
#define SMPC_CMD_CDOFF	0x0007		/* CD Off */

static inline void smpc_barrier(void)
{
	while ((ctrl_inb(SMPC_STATUS) & 0x0001) == 0x0001)
		;
}

#endif /* __ASM_SH_SATURN_SMPC_H */

