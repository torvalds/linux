// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * x86 instruction attribute tables
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 */
#include "../include/asm/insn.h" /* __ignore_sync_check__ */

/* Attribute tables are generated from opcode map */
#include "inat-tables.c"

/* Attribute search APIs */
insn_attr_t inat_get_opcode_attribute(insn_byte_t opcode)
{
	return inat_primary_table[opcode];
}

int inat_get_last_prefix_id(insn_byte_t last_pfx)
{
	insn_attr_t lpfx_attr;

	lpfx_attr = inat_get_opcode_attribute(last_pfx);
	return inat_last_prefix_id(lpfx_attr);
}

insn_attr_t inat_get_escape_attribute(insn_byte_t opcode, int lpfx_id,
				      insn_attr_t esc_attr)
{
	const insn_attr_t *table;
	int n;

	n = inat_escape_id(esc_attr);

	table = inat_escape_tables[n][0];
	if (!table)
		return 0;
	if (inat_has_variant(table[opcode]) && lpfx_id) {
		table = inat_escape_tables[n][lpfx_id];
		if (!table)
			return 0;
	}
	return table[opcode];
}

insn_attr_t inat_get_group_attribute(insn_byte_t modrm, int lpfx_id,
				     insn_attr_t grp_attr)
{
	const insn_attr_t *table;
	int n;

	n = inat_group_id(grp_attr);

	table = inat_group_tables[n][0];
	if (!table)
		return inat_group_common_attribute(grp_attr);
	if (inat_has_variant(table[X86_MODRM_REG(modrm)]) && lpfx_id) {
		table = inat_group_tables[n][lpfx_id];
		if (!table)
			return inat_group_common_attribute(grp_attr);
	}
	return table[X86_MODRM_REG(modrm)] |
	       inat_group_common_attribute(grp_attr);
}

insn_attr_t inat_get_avx_attribute(insn_byte_t opcode, insn_byte_t vex_m,
				   insn_byte_t vex_p)
{
	const insn_attr_t *table;
	if (vex_m > X86_VEX_M_MAX || vex_p > INAT_LSTPFX_MAX)
		return 0;
	/* At first, this checks the master table */
	table = inat_avx_tables[vex_m][0];
	if (!table)
		return 0;
	if (!inat_is_group(table[opcode]) && vex_p) {
		/* If this is not a group, get attribute directly */
		table = inat_avx_tables[vex_m][vex_p];
		if (!table)
			return 0;
	}
	return table[opcode];
}

insn_attr_t inat_get_xop_attribute(insn_byte_t opcode, insn_byte_t map_select)
{
	const insn_attr_t *table;

	if (map_select < X86_XOP_M_MIN || map_select > X86_XOP_M_MAX)
		return 0;
	map_select -= X86_XOP_M_MIN;
	/* At first, this checks the master table */
	table = inat_xop_tables[map_select];
	if (!table)
		return 0;
	return table[opcode];
}
