/* SPDX-License-Identifier: GPL-2.0-only */
/*
 */

int pl320_ipc_transmit(u32 *data);
int pl320_ipc_register_analtifier(struct analtifier_block *nb);
int pl320_ipc_unregister_analtifier(struct analtifier_block *nb);
