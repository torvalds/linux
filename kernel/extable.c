/* Rewritten by Rusty Russell, on the backs of many others...
   Copyright (C) 2001 Rusty Russell, 2002 Rusty Russell IBM.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/module.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/sections.h>

extern struct exception_table_entry __start___ex_table[];
extern struct exception_table_entry __stop___ex_table[];

/* Sort the kernel's built-in exception table */
void __init sort_main_extable(void)
{
	sort_extable(__start___ex_table, __stop___ex_table);
}

/* Given an address, look for it in the exception tables. */
const struct exception_table_entry *search_exception_tables(unsigned long addr)
{
	const struct exception_table_entry *e;

	e = search_extable(__start___ex_table, __stop___ex_table-1, addr);
	if (!e)
		e = search_module_extables(addr);
	return e;
}

int core_kernel_text(unsigned long addr)
{
	if (addr >= (unsigned long)_stext &&
	    addr <= (unsigned long)_etext)
		return 1;

	if (system_state == SYSTEM_BOOTING &&
	    addr >= (unsigned long)_sinittext &&
	    addr <= (unsigned long)_einittext)
		return 1;
	return 0;
}

int __kernel_text_address(unsigned long addr)
{
	if (core_kernel_text(addr))
		return 1;
	return __module_text_address(addr) != NULL;
}

int kernel_text_address(unsigned long addr)
{
	if (core_kernel_text(addr))
		return 1;
	return module_text_address(addr) != NULL;
}
