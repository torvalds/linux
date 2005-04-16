#ifndef _ASM_IA64_SECTIONS_H
#define _ASM_IA64_SECTIONS_H

/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm-generic/sections.h>

extern char __per_cpu_start[], __per_cpu_end[], __phys_per_cpu_start[];
extern char __start___vtop_patchlist[], __end___vtop_patchlist[];
extern char __start___mckinley_e9_bundles[], __end___mckinley_e9_bundles[];
extern char __start_gate_section[];
extern char __start_gate_mckinley_e9_patchlist[], __end_gate_mckinley_e9_patchlist[];
extern char __start_gate_vtop_patchlist[], __end_gate_vtop_patchlist[];
extern char __start_gate_fsyscall_patchlist[], __end_gate_fsyscall_patchlist[];
extern char __start_gate_brl_fsys_bubble_down_patchlist[], __end_gate_brl_fsys_bubble_down_patchlist[];
extern char __start_unwind[], __end_unwind[];

#endif /* _ASM_IA64_SECTIONS_H */

