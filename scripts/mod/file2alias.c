/* Simple code to turn various tables in an ELF file into alias definitions.
 * This deals with kernel datastructures where they should be
 * dealt with: in the kernel source.
 *
 * Copyright 2002-2003  Rusty Russell, IBM Corporation
 *           2003       Kai Germaschewski
 *
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include "modpost.h"

/* We use the ELF typedefs for kernel_ulong_t but bite the bullet and
 * use either stdint.h or inttypes.h for the rest. */
#if KERNEL_ELFCLASS == ELFCLASS32
typedef Elf32_Addr	kernel_ulong_t;
#define BITS_PER_LONG 32
#else
typedef Elf64_Addr	kernel_ulong_t;
#define BITS_PER_LONG 64
#endif
#ifdef __sun__
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#include <ctype.h>

typedef uint32_t	__u32;
typedef uint16_t	__u16;
typedef unsigned char	__u8;

/* Big exception to the "don't include kernel headers into userspace, which
 * even potentially has different endianness and word sizes, since 
 * we handle those differences explicitly below */
#include "../../include/linux/mod_devicetable.h"
#include "../../include/linux/input.h"

#define ADD(str, sep, cond, field)                              \
do {                                                            \
        strcat(str, sep);                                       \
        if (cond)                                               \
                sprintf(str + strlen(str),                      \
                        sizeof(field) == 1 ? "%02X" :           \
                        sizeof(field) == 2 ? "%04X" :           \
                        sizeof(field) == 4 ? "%08X" : "",       \
                        field);                                 \
        else                                                    \
                sprintf(str + strlen(str), "*");                \
} while(0)

/* USB is special because the bcdDevice can be matched against a numeric range */
/* Looks like "usb:vNpNdNdcNdscNdpNicNiscNipN" */
static void do_usb_entry(struct usb_device_id *id,
			 unsigned int bcdDevice_initial, int bcdDevice_initial_digits,
			 unsigned char range_lo, unsigned char range_hi,
			 struct module *mod)
{
	char alias[500];
	strcpy(alias, "usb:");
	ADD(alias, "v", id->match_flags&USB_DEVICE_ID_MATCH_VENDOR,
	    id->idVendor);
	ADD(alias, "p", id->match_flags&USB_DEVICE_ID_MATCH_PRODUCT,
	    id->idProduct);

	strcat(alias, "d");
	if (bcdDevice_initial_digits)
		sprintf(alias + strlen(alias), "%0*X",
			bcdDevice_initial_digits, bcdDevice_initial);
	if (range_lo == range_hi)
		sprintf(alias + strlen(alias), "%u", range_lo);
	else if (range_lo > 0 || range_hi < 9)
		sprintf(alias + strlen(alias), "[%u-%u]", range_lo, range_hi);
	if (bcdDevice_initial_digits < (sizeof(id->bcdDevice_lo) * 2 - 1))
		strcat(alias, "*");

	ADD(alias, "dc", id->match_flags&USB_DEVICE_ID_MATCH_DEV_CLASS,
	    id->bDeviceClass);
	ADD(alias, "dsc",
	    id->match_flags&USB_DEVICE_ID_MATCH_DEV_SUBCLASS,
	    id->bDeviceSubClass);
	ADD(alias, "dp",
	    id->match_flags&USB_DEVICE_ID_MATCH_DEV_PROTOCOL,
	    id->bDeviceProtocol);
	ADD(alias, "ic",
	    id->match_flags&USB_DEVICE_ID_MATCH_INT_CLASS,
	    id->bInterfaceClass);
	ADD(alias, "isc",
	    id->match_flags&USB_DEVICE_ID_MATCH_INT_SUBCLASS,
	    id->bInterfaceSubClass);
	ADD(alias, "ip",
	    id->match_flags&USB_DEVICE_ID_MATCH_INT_PROTOCOL,
	    id->bInterfaceProtocol);

	/* Always end in a wildcard, for future extension */
	if (alias[strlen(alias)-1] != '*')
		strcat(alias, "*");
	buf_printf(&mod->dev_table_buf,
		   "MODULE_ALIAS(\"%s\");\n", alias);
}

static void do_usb_entry_multi(struct usb_device_id *id, struct module *mod)
{
	unsigned int devlo, devhi;
	unsigned char chi, clo;
	int ndigits;

	id->match_flags = TO_NATIVE(id->match_flags);
	id->idVendor = TO_NATIVE(id->idVendor);
	id->idProduct = TO_NATIVE(id->idProduct);

	devlo = id->match_flags & USB_DEVICE_ID_MATCH_DEV_LO ?
		TO_NATIVE(id->bcdDevice_lo) : 0x0U;
	devhi = id->match_flags & USB_DEVICE_ID_MATCH_DEV_HI ?
		TO_NATIVE(id->bcdDevice_hi) : ~0x0U;

	/*
	 * Some modules (visor) have empty slots as placeholder for
	 * run-time specification that results in catch-all alias
	 */
	if (!(id->idVendor | id->bDeviceClass | id->bInterfaceClass))
		return;

	/* Convert numeric bcdDevice range into fnmatch-able pattern(s) */
	for (ndigits = sizeof(id->bcdDevice_lo) * 2 - 1; devlo <= devhi; ndigits--) {
		clo = devlo & 0xf;
		chi = devhi & 0xf;
		if (chi > 9)	/* it's bcd not hex */
			chi = 9;
		devlo >>= 4;
		devhi >>= 4;

		if (devlo == devhi || !ndigits) {
			do_usb_entry(id, devlo, ndigits, clo, chi, mod);
			break;
		}

		if (clo > 0)
			do_usb_entry(id, devlo++, ndigits, clo, 9, mod);

		if (chi < 9)
			do_usb_entry(id, devhi--, ndigits, 0, chi, mod);
	}
}

static void do_usb_table(void *symval, unsigned long size,
			 struct module *mod)
{
	unsigned int i;
	const unsigned long id_size = sizeof(struct usb_device_id);

	if (size % id_size || size < id_size) {
		fprintf(stderr, "*** Warning: %s ids %lu bad size "
			"(each on %lu)\n", mod->name, size, id_size);
	}
	/* Leave last one: it's the terminator. */
	size -= id_size;

	for (i = 0; i < size; i += id_size)
		do_usb_entry_multi(symval + i, mod);
}

/* Looks like: ieee1394:venNmoNspNverN */
static int do_ieee1394_entry(const char *filename,
			     struct ieee1394_device_id *id, char *alias)
{
	id->match_flags = TO_NATIVE(id->match_flags);
	id->vendor_id = TO_NATIVE(id->vendor_id);
	id->model_id = TO_NATIVE(id->model_id);
	id->specifier_id = TO_NATIVE(id->specifier_id);
	id->version = TO_NATIVE(id->version);

	strcpy(alias, "ieee1394:");
	ADD(alias, "ven", id->match_flags & IEEE1394_MATCH_VENDOR_ID,
	    id->vendor_id);
	ADD(alias, "mo", id->match_flags & IEEE1394_MATCH_MODEL_ID,
	    id->model_id);
	ADD(alias, "sp", id->match_flags & IEEE1394_MATCH_SPECIFIER_ID,
	    id->specifier_id);
	ADD(alias, "ver", id->match_flags & IEEE1394_MATCH_VERSION,
	    id->version);

	return 1;
}

/* Looks like: pci:vNdNsvNsdNbcNscNiN. */
static int do_pci_entry(const char *filename,
			struct pci_device_id *id, char *alias)
{
	/* Class field can be divided into these three. */
	unsigned char baseclass, subclass, interface,
		baseclass_mask, subclass_mask, interface_mask;

	id->vendor = TO_NATIVE(id->vendor);
	id->device = TO_NATIVE(id->device);
	id->subvendor = TO_NATIVE(id->subvendor);
	id->subdevice = TO_NATIVE(id->subdevice);
	id->class = TO_NATIVE(id->class);
	id->class_mask = TO_NATIVE(id->class_mask);

	strcpy(alias, "pci:");
	ADD(alias, "v", id->vendor != PCI_ANY_ID, id->vendor);
	ADD(alias, "d", id->device != PCI_ANY_ID, id->device);
	ADD(alias, "sv", id->subvendor != PCI_ANY_ID, id->subvendor);
	ADD(alias, "sd", id->subdevice != PCI_ANY_ID, id->subdevice);

	baseclass = (id->class) >> 16;
	baseclass_mask = (id->class_mask) >> 16;
	subclass = (id->class) >> 8;
	subclass_mask = (id->class_mask) >> 8;
	interface = id->class;
	interface_mask = id->class_mask;

	if ((baseclass_mask != 0 && baseclass_mask != 0xFF)
	    || (subclass_mask != 0 && subclass_mask != 0xFF)
	    || (interface_mask != 0 && interface_mask != 0xFF)) {
		fprintf(stderr,
			"*** Warning: Can't handle masks in %s:%04X\n",
			filename, id->class_mask);
		return 0;
	}

	ADD(alias, "bc", baseclass_mask == 0xFF, baseclass);
	ADD(alias, "sc", subclass_mask == 0xFF, subclass);
	ADD(alias, "i", interface_mask == 0xFF, interface);
	return 1;
}

/* looks like: "ccw:tNmNdtNdmN" */ 
static int do_ccw_entry(const char *filename,
			struct ccw_device_id *id, char *alias)
{
	id->match_flags = TO_NATIVE(id->match_flags);
	id->cu_type = TO_NATIVE(id->cu_type);
	id->cu_model = TO_NATIVE(id->cu_model);
	id->dev_type = TO_NATIVE(id->dev_type);
	id->dev_model = TO_NATIVE(id->dev_model);

	strcpy(alias, "ccw:");
	ADD(alias, "t", id->match_flags&CCW_DEVICE_ID_MATCH_CU_TYPE,
	    id->cu_type);
	ADD(alias, "m", id->match_flags&CCW_DEVICE_ID_MATCH_CU_MODEL,
	    id->cu_model);
	ADD(alias, "dt", id->match_flags&CCW_DEVICE_ID_MATCH_DEVICE_TYPE,
	    id->dev_type);
	ADD(alias, "dm", id->match_flags&CCW_DEVICE_ID_MATCH_DEVICE_MODEL,
	    id->dev_model);
	return 1;
}

/* Looks like: "serio:tyNprNidNexN" */
static int do_serio_entry(const char *filename,
			  struct serio_device_id *id, char *alias)
{
	id->type = TO_NATIVE(id->type);
	id->proto = TO_NATIVE(id->proto);
	id->id = TO_NATIVE(id->id);
	id->extra = TO_NATIVE(id->extra);

	strcpy(alias, "serio:");
	ADD(alias, "ty", id->type != SERIO_ANY, id->type);
	ADD(alias, "pr", id->proto != SERIO_ANY, id->proto);
	ADD(alias, "id", id->id != SERIO_ANY, id->id);
	ADD(alias, "ex", id->extra != SERIO_ANY, id->extra);

	return 1;
}

/* looks like: "pnp:dD" */
static int do_pnp_entry(const char *filename,
			struct pnp_device_id *id, char *alias)
{
	sprintf(alias, "pnp:d%s", id->id);
	return 1;
}

/* looks like: "pnp:cCdD..." */
static int do_pnp_card_entry(const char *filename,
			struct pnp_card_device_id *id, char *alias)
{
	int i;

	sprintf(alias, "pnp:c%s", id->id);
	for (i = 0; i < PNP_MAX_DEVICES; i++) {
		if (! *id->devs[i].id)
			break;
		sprintf(alias + strlen(alias), "d%s", id->devs[i].id);
	}
	return 1;
}

/* Looks like: pcmcia:mNcNfNfnNpfnNvaNvbNvcNvdN. */
static int do_pcmcia_entry(const char *filename,
			   struct pcmcia_device_id *id, char *alias)
{
	unsigned int i;

	id->match_flags = TO_NATIVE(id->match_flags);
	id->manf_id = TO_NATIVE(id->manf_id);
	id->card_id = TO_NATIVE(id->card_id);
	id->func_id = TO_NATIVE(id->func_id);
	id->function = TO_NATIVE(id->function);
	id->device_no = TO_NATIVE(id->device_no);

	for (i=0; i<4; i++) {
		id->prod_id_hash[i] = TO_NATIVE(id->prod_id_hash[i]);
       }

       strcpy(alias, "pcmcia:");
       ADD(alias, "m", id->match_flags & PCMCIA_DEV_ID_MATCH_MANF_ID,
	   id->manf_id);
       ADD(alias, "c", id->match_flags & PCMCIA_DEV_ID_MATCH_CARD_ID,
	   id->card_id);
       ADD(alias, "f", id->match_flags & PCMCIA_DEV_ID_MATCH_FUNC_ID,
	   id->func_id);
       ADD(alias, "fn", id->match_flags & PCMCIA_DEV_ID_MATCH_FUNCTION,
	   id->function);
       ADD(alias, "pfn", id->match_flags & PCMCIA_DEV_ID_MATCH_DEVICE_NO,
	   id->device_no);
       ADD(alias, "pa", id->match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID1, id->prod_id_hash[0]);
       ADD(alias, "pb", id->match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID2, id->prod_id_hash[1]);
       ADD(alias, "pc", id->match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID3, id->prod_id_hash[2]);
       ADD(alias, "pd", id->match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID4, id->prod_id_hash[3]);

       return 1;
}



static int do_of_entry (const char *filename, struct of_device_id *of, char *alias)
{
    char *tmp;
    sprintf (alias, "of:N%sT%sC%s",
                    of->name[0] ? of->name : "*",
                    of->type[0] ? of->type : "*",
                    of->compatible[0] ? of->compatible : "*");

    /* Replace all whitespace with underscores */
    for (tmp = alias; tmp && *tmp; tmp++)
        if (isspace (*tmp))
            *tmp = '_';

    return 1;
}

static int do_vio_entry(const char *filename, struct vio_device_id *vio,
		char *alias)
{
	char *tmp;

	sprintf(alias, "vio:T%sS%s", vio->type[0] ? vio->type : "*",
			vio->compat[0] ? vio->compat : "*");

	/* Replace all whitespace with underscores */
	for (tmp = alias; tmp && *tmp; tmp++)
		if (isspace (*tmp))
			*tmp = '_';

	return 1;
}

static int do_i2c_entry(const char *filename, struct i2c_device_id *i2c, char *alias)
{
	strcpy(alias, "i2c:");
	ADD(alias, "id", 1, i2c->id);
	return 1;
}

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static void do_input(char *alias,
		     kernel_ulong_t *arr, unsigned int min, unsigned int max)
{
	unsigned int i;
	for (i = min; i < max; i++) {
		if (arr[i/BITS_PER_LONG] & (1 << (i%BITS_PER_LONG)))
			sprintf(alias+strlen(alias), "%X,*", i);
	}
}

/* input:b0v0p0e0-eXkXrXaXmXlXsXfXwX where X is comma-separated %02X. */
static int do_input_entry(const char *filename, struct input_device_id *id,
			  char *alias)
{
	sprintf(alias, "input:");

	ADD(alias, "b", id->flags&INPUT_DEVICE_ID_MATCH_BUS, id->id.bustype);
	ADD(alias, "v", id->flags&INPUT_DEVICE_ID_MATCH_VENDOR, id->id.vendor);
	ADD(alias, "p", id->flags&INPUT_DEVICE_ID_MATCH_PRODUCT,
	    id->id.product);
	ADD(alias, "e", id->flags&INPUT_DEVICE_ID_MATCH_VERSION,
	    id->id.version);

	sprintf(alias + strlen(alias), "-e*");
	if (id->flags&INPUT_DEVICE_ID_MATCH_EVBIT)
		do_input(alias, id->evbit, 0, EV_MAX);
	sprintf(alias + strlen(alias), "k*");
	if (id->flags&INPUT_DEVICE_ID_MATCH_KEYBIT)
		do_input(alias, id->keybit, KEY_MIN_INTERESTING, KEY_MAX);
	sprintf(alias + strlen(alias), "r*");
	if (id->flags&INPUT_DEVICE_ID_MATCH_RELBIT)
		do_input(alias, id->relbit, 0, REL_MAX);
	sprintf(alias + strlen(alias), "a*");
	if (id->flags&INPUT_DEVICE_ID_MATCH_ABSBIT)
		do_input(alias, id->absbit, 0, ABS_MAX);
	sprintf(alias + strlen(alias), "m*");
	if (id->flags&INPUT_DEVICE_ID_MATCH_MSCIT)
		do_input(alias, id->mscbit, 0, MSC_MAX);
	sprintf(alias + strlen(alias), "l*");
	if (id->flags&INPUT_DEVICE_ID_MATCH_LEDBIT)
		do_input(alias, id->ledbit, 0, LED_MAX);
	sprintf(alias + strlen(alias), "s*");
	if (id->flags&INPUT_DEVICE_ID_MATCH_SNDBIT)
		do_input(alias, id->sndbit, 0, SND_MAX);
	sprintf(alias + strlen(alias), "f*");
	if (id->flags&INPUT_DEVICE_ID_MATCH_FFBIT)
		do_input(alias, id->ffbit, 0, FF_MAX);
	sprintf(alias + strlen(alias), "w*");
	if (id->flags&INPUT_DEVICE_ID_MATCH_SWBIT)
		do_input(alias, id->swbit, 0, SW_MAX);
	return 1;
}

/* Ignore any prefix, eg. v850 prepends _ */
static inline int sym_is(const char *symbol, const char *name)
{
	const char *match;

	match = strstr(symbol, name);
	if (!match)
		return 0;
	return match[strlen(symbol)] == '\0';
}

static void do_table(void *symval, unsigned long size,
		     unsigned long id_size,
		     void *function,
		     struct module *mod)
{
	unsigned int i;
	char alias[500];
	int (*do_entry)(const char *, void *entry, char *alias) = function;

	if (size % id_size || size < id_size) {
		fprintf(stderr, "*** Warning: %s ids %lu bad size "
			"(each on %lu)\n", mod->name, size, id_size);
	}
	/* Leave last one: it's the terminator. */
	size -= id_size;

	for (i = 0; i < size; i += id_size) {
		if (do_entry(mod->name, symval+i, alias)) {
			/* Always end in a wildcard, for future extension */
			if (alias[strlen(alias)-1] != '*')
				strcat(alias, "*");
			buf_printf(&mod->dev_table_buf,
				   "MODULE_ALIAS(\"%s\");\n", alias);
		}
	}
}

/* Create MODULE_ALIAS() statements.
 * At this time, we cannot write the actual output C source yet,
 * so we write into the mod->dev_table_buf buffer. */
void handle_moddevtable(struct module *mod, struct elf_info *info,
			Elf_Sym *sym, const char *symname)
{
	void *symval;

	/* We're looking for a section relative symbol */
	if (!sym->st_shndx || sym->st_shndx >= info->hdr->e_shnum)
		return;

	symval = (void *)info->hdr
		+ info->sechdrs[sym->st_shndx].sh_offset
		+ sym->st_value;

	if (sym_is(symname, "__mod_pci_device_table"))
		do_table(symval, sym->st_size, sizeof(struct pci_device_id),
			 do_pci_entry, mod);
	else if (sym_is(symname, "__mod_usb_device_table"))
		/* special case to handle bcdDevice ranges */
		do_usb_table(symval, sym->st_size, mod);
	else if (sym_is(symname, "__mod_ieee1394_device_table"))
		do_table(symval, sym->st_size, sizeof(struct ieee1394_device_id),
			 do_ieee1394_entry, mod);
	else if (sym_is(symname, "__mod_ccw_device_table"))
		do_table(symval, sym->st_size, sizeof(struct ccw_device_id),
			 do_ccw_entry, mod);
	else if (sym_is(symname, "__mod_serio_device_table"))
		do_table(symval, sym->st_size, sizeof(struct serio_device_id),
			 do_serio_entry, mod);
	else if (sym_is(symname, "__mod_pnp_device_table"))
		do_table(symval, sym->st_size, sizeof(struct pnp_device_id),
			 do_pnp_entry, mod);
	else if (sym_is(symname, "__mod_pnp_card_device_table"))
		do_table(symval, sym->st_size, sizeof(struct pnp_card_device_id),
			 do_pnp_card_entry, mod);
	else if (sym_is(symname, "__mod_pcmcia_device_table"))
		do_table(symval, sym->st_size, sizeof(struct pcmcia_device_id),
			 do_pcmcia_entry, mod);
        else if (sym_is(symname, "__mod_of_device_table"))
		do_table(symval, sym->st_size, sizeof(struct of_device_id),
			 do_of_entry, mod);
        else if (sym_is(symname, "__mod_vio_device_table"))
		do_table(symval, sym->st_size, sizeof(struct vio_device_id),
			 do_vio_entry, mod);
	else if (sym_is(symname, "__mod_i2c_device_table"))
		do_table(symval, sym->st_size, sizeof(struct i2c_device_id),
			 do_i2c_entry, mod);
	else if (sym_is(symname, "__mod_input_device_table"))
		do_table(symval, sym->st_size, sizeof(struct input_device_id),
			 do_input_entry, mod);
}

/* Now add out buffered information to the generated C source */
void add_moddevtable(struct buffer *buf, struct module *mod)
{
	buf_printf(buf, "\n");
	buf_write(buf, mod->dev_table_buf.p, mod->dev_table_buf.pos);
	free(mod->dev_table_buf.p);
}
