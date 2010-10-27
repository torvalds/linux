/************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2010  AudioScience Inc. <support@audioscience.com>

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

Debug macro translation.

************************************************************************/

#include "hpi_internal.h"
#include "hpidebug.h"

/* Debug level; 0 quiet; 1 informative, 2 debug, 3 verbose debug.  */
int hpi_debug_level = HPI_DEBUG_LEVEL_DEFAULT;

void hpi_debug_init(void)
{
	printk(KERN_INFO "debug start\n");
}

int hpi_debug_level_set(int level)
{
	int old_level;

	old_level = hpi_debug_level;
	hpi_debug_level = level;
	return old_level;
}

int hpi_debug_level_get(void)
{
	return hpi_debug_level;
}

#ifdef HPIOS_DEBUG_PRINT
/* implies OS has no printf-like function */
#include <stdarg.h>

void hpi_debug_printf(char *fmt, ...)
{
	va_list arglist;
	char buffer[128];

	va_start(arglist, fmt);

	if (buffer[0])
		HPIOS_DEBUG_PRINT(buffer);
	va_end(arglist);
}
#endif

struct treenode {
	void *array;
	unsigned int num_elements;
};

#define make_treenode_from_array(nodename, array) \
static void *tmp_strarray_##nodename[] = array; \
static struct treenode nodename = { \
	&tmp_strarray_##nodename, \
	ARRAY_SIZE(tmp_strarray_##nodename) \
};

#define get_treenode_elem(node_ptr, idx, type)  \
	(&(*((type *)(node_ptr)->array)[idx]))

make_treenode_from_array(hpi_control_type_strings, HPI_CONTROL_TYPE_STRINGS)

	make_treenode_from_array(hpi_subsys_strings, HPI_SUBSYS_STRINGS)
	make_treenode_from_array(hpi_adapter_strings, HPI_ADAPTER_STRINGS)
	make_treenode_from_array(hpi_istream_strings, HPI_ISTREAM_STRINGS)
	make_treenode_from_array(hpi_ostream_strings, HPI_OSTREAM_STRINGS)
	make_treenode_from_array(hpi_mixer_strings, HPI_MIXER_STRINGS)
	make_treenode_from_array(hpi_node_strings,
	{
	"NODE is invalid object"})

	make_treenode_from_array(hpi_control_strings, HPI_CONTROL_STRINGS)
	make_treenode_from_array(hpi_nvmemory_strings, HPI_OBJ_STRINGS)
	make_treenode_from_array(hpi_digitalio_strings, HPI_DIGITALIO_STRINGS)
	make_treenode_from_array(hpi_watchdog_strings, HPI_WATCHDOG_STRINGS)
	make_treenode_from_array(hpi_clock_strings, HPI_CLOCK_STRINGS)
	make_treenode_from_array(hpi_profile_strings, HPI_PROFILE_STRINGS)
	make_treenode_from_array(hpi_asyncevent_strings, HPI_ASYNCEVENT_STRINGS)
#define HPI_FUNCTION_STRINGS \
{ \
  &hpi_subsys_strings,\
  &hpi_adapter_strings,\
  &hpi_ostream_strings,\
  &hpi_istream_strings,\
  &hpi_mixer_strings,\
  &hpi_node_strings,\
  &hpi_control_strings,\
  &hpi_nvmemory_strings,\
  &hpi_digitalio_strings,\
  &hpi_watchdog_strings,\
  &hpi_clock_strings,\
  &hpi_profile_strings,\
  &hpi_control_strings, \
  &hpi_asyncevent_strings \
}
	make_treenode_from_array(hpi_function_strings, HPI_FUNCTION_STRINGS)

	compile_time_assert(HPI_OBJ_MAXINDEX == 14, obj_list_doesnt_match);

static char *hpi_function_string(unsigned int function)
{
	unsigned int object;
	struct treenode *tmp;

	object = function / HPI_OBJ_FUNCTION_SPACING;
	function = function - object * HPI_OBJ_FUNCTION_SPACING;

	if (object == 0 || object == HPI_OBJ_NODE
		|| object > hpi_function_strings.num_elements)
		return "invalid object";

	tmp = get_treenode_elem(&hpi_function_strings, object - 1,
		struct treenode *);

	if (function == 0 || function > tmp->num_elements)
		return "invalid function";

	return get_treenode_elem(tmp, function - 1, char *);
}

void hpi_debug_message(struct hpi_message *phm, char *sz_fileline)
{
	if (phm) {
		if ((phm->object <= HPI_OBJ_MAXINDEX) && phm->object) {
			u16 index = 0;
			u16 attrib = 0;
			int is_control = 0;

			index = phm->obj_index;
			switch (phm->object) {
			case HPI_OBJ_ADAPTER:
			case HPI_OBJ_PROFILE:
				break;
			case HPI_OBJ_MIXER:
				if (phm->function ==
					HPI_MIXER_GET_CONTROL_BY_INDEX)
					index = phm->u.m.control_index;
				break;
			case HPI_OBJ_OSTREAM:
			case HPI_OBJ_ISTREAM:
				break;

			case HPI_OBJ_CONTROLEX:
			case HPI_OBJ_CONTROL:
				if (phm->version == 1)
					attrib = HPI_CTL_ATTR(UNIVERSAL, 1);
				else
					attrib = phm->u.c.attribute;
				is_control = 1;
				break;
			default:
				break;
			}

			if (is_control && (attrib & 0xFF00)) {
				int control_type = (attrib & 0xFF00) >> 8;
				int attr_index = HPI_CTL_ATTR_INDEX(attrib);
				/* note the KERN facility level
				   is in szFileline already */
				printk("%s adapter %d %s "
					"ctrl_index x%04x %s %d\n",
					sz_fileline, phm->adapter_index,
					hpi_function_string(phm->function),
					index,
					get_treenode_elem
					(&hpi_control_type_strings,
						control_type, char *),
					attr_index);

			} else
				printk("%s adapter %d %s "
					"idx x%04x attr x%04x \n",
					sz_fileline, phm->adapter_index,
					hpi_function_string(phm->function),
					index, attrib);
		} else {
			printk("adap=%d, invalid obj=%d, func=0x%x\n",
				phm->adapter_index, phm->object,
				phm->function);
		}
	} else
		printk(KERN_ERR
			"NULL message pointer to hpi_debug_message!\n");
}

void hpi_debug_data(u16 *pdata, u32 len)
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
		printk(KERN_DEBUG "%p:", (pdata + i));

		for (k = 0; k < cols && i < len; i++, k++)
			printk("%s%04x", k == 0 ? "" : " ", pdata[i]);

		printk("\n");
	}
}
