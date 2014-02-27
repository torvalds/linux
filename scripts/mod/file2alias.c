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
#include "devicetable-offsets.h"

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
#include <stdbool.h>

typedef uint32_t	__u32;
typedef uint16_t	__u16;
typedef unsigned char	__u8;

/* Big exception to the "don't include kernel headers into userspace, which
 * even potentially has different endianness and word sizes, since
 * we handle those differences explicitly below */
#include "../../include/linux/mod_devicetable.h"

/* This array collects all instances that use the generic do_table */
struct devtable {
	const char *device_id; /* name of table, __mod_<name>_device_table. */
	unsigned long id_size;
	void *function;
};

#define ___cat(a,b) a ## b
#define __cat(a,b) ___cat(a,b)

/* we need some special handling for this host tool running eventually on
 * Darwin. The Mach-O section handling is a bit different than ELF section
 * handling. The differnces in detail are:
 *  a) we have segments which have sections
 *  b) we need a API call to get the respective section symbols */
#if defined(__MACH__)
#include <mach-o/getsect.h>

#define INIT_SECTION(name)  do {					\
		unsigned long name ## _len;				\
		char *__cat(pstart_,name) = getsectdata("__TEXT",	\
			#name, &__cat(name,_len));			\
		char *__cat(pstop_,name) = __cat(pstart_,name) +	\
			__cat(name, _len);				\
		__cat(__start_,name) = (void *)__cat(pstart_,name);	\
		__cat(__stop_,name) = (void *)__cat(pstop_,name);	\
	} while (0)
#define SECTION(name)   __attribute__((section("__TEXT, " #name)))

struct devtable **__start___devtable, **__stop___devtable;
#else
#define INIT_SECTION(name) /* no-op for ELF */
#define SECTION(name)   __attribute__((section(#name)))

/* We construct a table of pointers in an ELF section (pointers generally
 * go unpadded by gcc).  ld creates boundary syms for us. */
extern struct devtable *__start___devtable[], *__stop___devtable[];
#endif /* __MACH__ */

#if __GNUC__ == 3 && __GNUC_MINOR__ < 3
# define __used			__attribute__((__unused__))
#else
# define __used			__attribute__((__used__))
#endif

/* Define a variable f that holds the value of field f of struct devid
 * based at address m.
 */
#define DEF_FIELD(m, devid, f) \
	typeof(((struct devid *)0)->f) f = TO_NATIVE(*(typeof(f) *)((m) + OFF_##devid##_##f))
/* Define a variable f that holds the address of field f of struct devid
 * based at address m.  Due to the way typeof works, for a field of type
 * T[N] the variable has type T(*)[N], _not_ T*.
 */
#define DEF_FIELD_ADDR(m, devid, f) \
	typeof(((struct devid *)0)->f) *f = ((m) + OFF_##devid##_##f)

/* Add a table entry.  We test function type matches while we're here. */
#define ADD_TO_DEVTABLE(device_id, type, function) \
	static struct devtable __cat(devtable,__LINE__) = {	\
		device_id + 0*sizeof((function)((const char *)NULL,	\
						(void *)NULL,		\
						(char *)NULL)),		\
		SIZE_##type, (function) };				\
	static struct devtable *SECTION(__devtable) __used \
		__cat(devtable_ptr,__LINE__) = &__cat(devtable,__LINE__)

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

/* Always end in a wildcard, for future extension */
static inline void add_wildcard(char *str)
{
	int len = strlen(str);

	if (str[len - 1] != '*')
		strcat(str + len, "*");
}

/**
 * Check that sizeof(device_id type) are consistent with size of section
 * in .o file. If in-consistent then userspace and kernel does not agree
 * on actual size which is a bug.
 * Also verify that the final entry in the table is all zeros.
 * Ignore both checks if build host differ from target host and size differs.
 **/
static void device_id_check(const char *modname, const char *device_id,
			    unsigned long size, unsigned long id_size,
			    void *symval)
{
	int i;

	if (size % id_size || size < id_size) {
		fatal("%s: sizeof(struct %s_device_id)=%lu is not a modulo "
		      "of the size of section __mod_%s_device_table=%lu.\n"
		      "Fix definition of struct %s_device_id "
		      "in mod_devicetable.h\n",
		      modname, device_id, id_size, device_id, size, device_id);
	}
	/* Verify last one is a terminator */
	for (i = 0; i < id_size; i++ ) {
		if (*(uint8_t*)(symval+size-id_size+i)) {
			fprintf(stderr,"%s: struct %s_device_id is %lu bytes.  "
				"The last of %lu is:\n",
				modname, device_id, id_size, size / id_size);
			for (i = 0; i < id_size; i++ )
				fprintf(stderr,"0x%02x ",
					*(uint8_t*)(symval+size-id_size+i) );
			fprintf(stderr,"\n");
			fatal("%s: struct %s_device_id is not terminated "
				"with a NULL entry!\n", modname, device_id);
		}
	}
}

/* USB is special because the bcdDevice can be matched against a numeric range */
/* Looks like "usb:vNpNdNdcNdscNdpNicNiscNipNinN" */
static void do_usb_entry(void *symval,
			 unsigned int bcdDevice_initial, int bcdDevice_initial_digits,
			 unsigned char range_lo, unsigned char range_hi,
			 unsigned char max, struct module *mod)
{
	char alias[500];
	DEF_FIELD(symval, usb_device_id, match_flags);
	DEF_FIELD(symval, usb_device_id, idVendor);
	DEF_FIELD(symval, usb_device_id, idProduct);
	DEF_FIELD(symval, usb_device_id, bcdDevice_lo);
	DEF_FIELD(symval, usb_device_id, bDeviceClass);
	DEF_FIELD(symval, usb_device_id, bDeviceSubClass);
	DEF_FIELD(symval, usb_device_id, bDeviceProtocol);
	DEF_FIELD(symval, usb_device_id, bInterfaceClass);
	DEF_FIELD(symval, usb_device_id, bInterfaceSubClass);
	DEF_FIELD(symval, usb_device_id, bInterfaceProtocol);
	DEF_FIELD(symval, usb_device_id, bInterfaceNumber);

	strcpy(alias, "usb:");
	ADD(alias, "v", match_flags&USB_DEVICE_ID_MATCH_VENDOR,
	    idVendor);
	ADD(alias, "p", match_flags&USB_DEVICE_ID_MATCH_PRODUCT,
	    idProduct);

	strcat(alias, "d");
	if (bcdDevice_initial_digits)
		sprintf(alias + strlen(alias), "%0*X",
			bcdDevice_initial_digits, bcdDevice_initial);
	if (range_lo == range_hi)
		sprintf(alias + strlen(alias), "%X", range_lo);
	else if (range_lo > 0 || range_hi < max) {
		if (range_lo > 0x9 || range_hi < 0xA)
			sprintf(alias + strlen(alias),
				"[%X-%X]",
				range_lo,
				range_hi);
		else {
			sprintf(alias + strlen(alias),
				range_lo < 0x9 ? "[%X-9" : "[%X",
				range_lo);
			sprintf(alias + strlen(alias),
				range_hi > 0xA ? "A-%X]" : "%X]",
				range_hi);
		}
	}
	if (bcdDevice_initial_digits < (sizeof(bcdDevice_lo) * 2 - 1))
		strcat(alias, "*");

	ADD(alias, "dc", match_flags&USB_DEVICE_ID_MATCH_DEV_CLASS,
	    bDeviceClass);
	ADD(alias, "dsc", match_flags&USB_DEVICE_ID_MATCH_DEV_SUBCLASS,
	    bDeviceSubClass);
	ADD(alias, "dp", match_flags&USB_DEVICE_ID_MATCH_DEV_PROTOCOL,
	    bDeviceProtocol);
	ADD(alias, "ic", match_flags&USB_DEVICE_ID_MATCH_INT_CLASS,
	    bInterfaceClass);
	ADD(alias, "isc", match_flags&USB_DEVICE_ID_MATCH_INT_SUBCLASS,
	    bInterfaceSubClass);
	ADD(alias, "ip", match_flags&USB_DEVICE_ID_MATCH_INT_PROTOCOL,
	    bInterfaceProtocol);
	ADD(alias, "in", match_flags&USB_DEVICE_ID_MATCH_INT_NUMBER,
	    bInterfaceNumber);

	add_wildcard(alias);
	buf_printf(&mod->dev_table_buf,
		   "MODULE_ALIAS(\"%s\");\n", alias);
}

/* Handles increment/decrement of BCD formatted integers */
/* Returns the previous value, so it works like i++ or i-- */
static unsigned int incbcd(unsigned int *bcd,
			   int inc,
			   unsigned char max,
			   size_t chars)
{
	unsigned int init = *bcd, i, j;
	unsigned long long c, dec = 0;

	/* If bcd is not in BCD format, just increment */
	if (max > 0x9) {
		*bcd += inc;
		return init;
	}

	/* Convert BCD to Decimal */
	for (i=0 ; i < chars ; i++) {
		c = (*bcd >> (i << 2)) & 0xf;
		c = c > 9 ? 9 : c; /* force to bcd just in case */
		for (j=0 ; j < i ; j++)
			c = c * 10;
		dec += c;
	}

	/* Do our increment/decrement */
	dec += inc;
	*bcd  = 0;

	/* Convert back to BCD */
	for (i=0 ; i < chars ; i++) {
		for (c=1,j=0 ; j < i ; j++)
			c = c * 10;
		c = (dec / c) % 10;
		*bcd += c << (i << 2);
	}
	return init;
}

static void do_usb_entry_multi(void *symval, struct module *mod)
{
	unsigned int devlo, devhi;
	unsigned char chi, clo, max;
	int ndigits;

	DEF_FIELD(symval, usb_device_id, match_flags);
	DEF_FIELD(symval, usb_device_id, idVendor);
	DEF_FIELD(symval, usb_device_id, idProduct);
	DEF_FIELD(symval, usb_device_id, bcdDevice_lo);
	DEF_FIELD(symval, usb_device_id, bcdDevice_hi);
	DEF_FIELD(symval, usb_device_id, bDeviceClass);
	DEF_FIELD(symval, usb_device_id, bInterfaceClass);

	devlo = match_flags & USB_DEVICE_ID_MATCH_DEV_LO ?
		bcdDevice_lo : 0x0U;
	devhi = match_flags & USB_DEVICE_ID_MATCH_DEV_HI ?
		bcdDevice_hi : ~0x0U;

	/* Figure out if this entry is in bcd or hex format */
	max = 0x9; /* Default to decimal format */
	for (ndigits = 0 ; ndigits < sizeof(bcdDevice_lo) * 2 ; ndigits++) {
		clo = (devlo >> (ndigits << 2)) & 0xf;
		chi = ((devhi > 0x9999 ? 0x9999 : devhi) >> (ndigits << 2)) & 0xf;
		if (clo > max || chi > max) {
			max = 0xf;
			break;
		}
	}

	/*
	 * Some modules (visor) have empty slots as placeholder for
	 * run-time specification that results in catch-all alias
	 */
	if (!(idVendor | idProduct | bDeviceClass | bInterfaceClass))
		return;

	/* Convert numeric bcdDevice range into fnmatch-able pattern(s) */
	for (ndigits = sizeof(bcdDevice_lo) * 2 - 1; devlo <= devhi; ndigits--) {
		clo = devlo & 0xf;
		chi = devhi & 0xf;
		if (chi > max)	/* If we are in bcd mode, truncate if necessary */
			chi = max;
		devlo >>= 4;
		devhi >>= 4;

		if (devlo == devhi || !ndigits) {
			do_usb_entry(symval, devlo, ndigits, clo, chi, max, mod);
			break;
		}

		if (clo > 0x0)
			do_usb_entry(symval,
				     incbcd(&devlo, 1, max,
					    sizeof(bcdDevice_lo) * 2),
				     ndigits, clo, max, max, mod);

		if (chi < max)
			do_usb_entry(symval,
				     incbcd(&devhi, -1, max,
					    sizeof(bcdDevice_lo) * 2),
				     ndigits, 0x0, chi, max, mod);
	}
}

static void do_usb_table(void *symval, unsigned long size,
			 struct module *mod)
{
	unsigned int i;
	const unsigned long id_size = SIZE_usb_device_id;

	device_id_check(mod->name, "usb", size, id_size, symval);

	/* Leave last one: it's the terminator. */
	size -= id_size;

	for (i = 0; i < size; i += id_size)
		do_usb_entry_multi(symval + i, mod);
}

/* Looks like: hid:bNvNpN */
static int do_hid_entry(const char *filename,
			     void *symval, char *alias)
{
	DEF_FIELD(symval, hid_device_id, bus);
	DEF_FIELD(symval, hid_device_id, group);
	DEF_FIELD(symval, hid_device_id, vendor);
	DEF_FIELD(symval, hid_device_id, product);

	sprintf(alias, "hid:");
	ADD(alias, "b", bus != HID_BUS_ANY, bus);
	ADD(alias, "g", group != HID_GROUP_ANY, group);
	ADD(alias, "v", vendor != HID_ANY_ID, vendor);
	ADD(alias, "p", product != HID_ANY_ID, product);

	return 1;
}
ADD_TO_DEVTABLE("hid", hid_device_id, do_hid_entry);

/* Looks like: ieee1394:venNmoNspNverN */
static int do_ieee1394_entry(const char *filename,
			     void *symval, char *alias)
{
	DEF_FIELD(symval, ieee1394_device_id, match_flags);
	DEF_FIELD(symval, ieee1394_device_id, vendor_id);
	DEF_FIELD(symval, ieee1394_device_id, model_id);
	DEF_FIELD(symval, ieee1394_device_id, specifier_id);
	DEF_FIELD(symval, ieee1394_device_id, version);

	strcpy(alias, "ieee1394:");
	ADD(alias, "ven", match_flags & IEEE1394_MATCH_VENDOR_ID,
	    vendor_id);
	ADD(alias, "mo", match_flags & IEEE1394_MATCH_MODEL_ID,
	    model_id);
	ADD(alias, "sp", match_flags & IEEE1394_MATCH_SPECIFIER_ID,
	    specifier_id);
	ADD(alias, "ver", match_flags & IEEE1394_MATCH_VERSION,
	    version);

	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("ieee1394", ieee1394_device_id, do_ieee1394_entry);

/* Looks like: pci:vNdNsvNsdNbcNscNiN. */
static int do_pci_entry(const char *filename,
			void *symval, char *alias)
{
	/* Class field can be divided into these three. */
	unsigned char baseclass, subclass, interface,
		baseclass_mask, subclass_mask, interface_mask;

	DEF_FIELD(symval, pci_device_id, vendor);
	DEF_FIELD(symval, pci_device_id, device);
	DEF_FIELD(symval, pci_device_id, subvendor);
	DEF_FIELD(symval, pci_device_id, subdevice);
	DEF_FIELD(symval, pci_device_id, class);
	DEF_FIELD(symval, pci_device_id, class_mask);

	strcpy(alias, "pci:");
	ADD(alias, "v", vendor != PCI_ANY_ID, vendor);
	ADD(alias, "d", device != PCI_ANY_ID, device);
	ADD(alias, "sv", subvendor != PCI_ANY_ID, subvendor);
	ADD(alias, "sd", subdevice != PCI_ANY_ID, subdevice);

	baseclass = (class) >> 16;
	baseclass_mask = (class_mask) >> 16;
	subclass = (class) >> 8;
	subclass_mask = (class_mask) >> 8;
	interface = class;
	interface_mask = class_mask;

	if ((baseclass_mask != 0 && baseclass_mask != 0xFF)
	    || (subclass_mask != 0 && subclass_mask != 0xFF)
	    || (interface_mask != 0 && interface_mask != 0xFF)) {
		warn("Can't handle masks in %s:%04X\n",
		     filename, class_mask);
		return 0;
	}

	ADD(alias, "bc", baseclass_mask == 0xFF, baseclass);
	ADD(alias, "sc", subclass_mask == 0xFF, subclass);
	ADD(alias, "i", interface_mask == 0xFF, interface);
	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("pci", pci_device_id, do_pci_entry);

/* looks like: "ccw:tNmNdtNdmN" */
static int do_ccw_entry(const char *filename,
			void *symval, char *alias)
{
	DEF_FIELD(symval, ccw_device_id, match_flags);
	DEF_FIELD(symval, ccw_device_id, cu_type);
	DEF_FIELD(symval, ccw_device_id, cu_model);
	DEF_FIELD(symval, ccw_device_id, dev_type);
	DEF_FIELD(symval, ccw_device_id, dev_model);

	strcpy(alias, "ccw:");
	ADD(alias, "t", match_flags&CCW_DEVICE_ID_MATCH_CU_TYPE,
	    cu_type);
	ADD(alias, "m", match_flags&CCW_DEVICE_ID_MATCH_CU_MODEL,
	    cu_model);
	ADD(alias, "dt", match_flags&CCW_DEVICE_ID_MATCH_DEVICE_TYPE,
	    dev_type);
	ADD(alias, "dm", match_flags&CCW_DEVICE_ID_MATCH_DEVICE_MODEL,
	    dev_model);
	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("ccw", ccw_device_id, do_ccw_entry);

/* looks like: "ap:tN" */
static int do_ap_entry(const char *filename,
		       void *symval, char *alias)
{
	DEF_FIELD(symval, ap_device_id, dev_type);

	sprintf(alias, "ap:t%02X*", dev_type);
	return 1;
}
ADD_TO_DEVTABLE("ap", ap_device_id, do_ap_entry);

/* looks like: "css:tN" */
static int do_css_entry(const char *filename,
			void *symval, char *alias)
{
	DEF_FIELD(symval, css_device_id, type);

	sprintf(alias, "css:t%01X", type);
	return 1;
}
ADD_TO_DEVTABLE("css", css_device_id, do_css_entry);

/* Looks like: "serio:tyNprNidNexN" */
static int do_serio_entry(const char *filename,
			  void *symval, char *alias)
{
	DEF_FIELD(symval, serio_device_id, type);
	DEF_FIELD(symval, serio_device_id, proto);
	DEF_FIELD(symval, serio_device_id, id);
	DEF_FIELD(symval, serio_device_id, extra);

	strcpy(alias, "serio:");
	ADD(alias, "ty", type != SERIO_ANY, type);
	ADD(alias, "pr", proto != SERIO_ANY, proto);
	ADD(alias, "id", id != SERIO_ANY, id);
	ADD(alias, "ex", extra != SERIO_ANY, extra);

	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("serio", serio_device_id, do_serio_entry);

/* looks like: "acpi:ACPI0003 or acpi:PNP0C0B" or "acpi:LNXVIDEO" */
static int do_acpi_entry(const char *filename,
			void *symval, char *alias)
{
	DEF_FIELD_ADDR(symval, acpi_device_id, id);
	sprintf(alias, "acpi*:%s:*", *id);
	return 1;
}
ADD_TO_DEVTABLE("acpi", acpi_device_id, do_acpi_entry);

/* looks like: "pnp:dD" */
static void do_pnp_device_entry(void *symval, unsigned long size,
				struct module *mod)
{
	const unsigned long id_size = SIZE_pnp_device_id;
	const unsigned int count = (size / id_size)-1;
	unsigned int i;

	device_id_check(mod->name, "pnp", size, id_size, symval);

	for (i = 0; i < count; i++) {
		DEF_FIELD_ADDR(symval + i*id_size, pnp_device_id, id);
		char acpi_id[sizeof(*id)];
		int j;

		buf_printf(&mod->dev_table_buf,
			   "MODULE_ALIAS(\"pnp:d%s*\");\n", *id);

		/* fix broken pnp bus lowercasing */
		for (j = 0; j < sizeof(acpi_id); j++)
			acpi_id[j] = toupper((*id)[j]);
		buf_printf(&mod->dev_table_buf,
			   "MODULE_ALIAS(\"acpi*:%s:*\");\n", acpi_id);
	}
}

/* looks like: "pnp:dD" for every device of the card */
static void do_pnp_card_entries(void *symval, unsigned long size,
				struct module *mod)
{
	const unsigned long id_size = SIZE_pnp_card_device_id;
	const unsigned int count = (size / id_size)-1;
	unsigned int i;

	device_id_check(mod->name, "pnp", size, id_size, symval);

	for (i = 0; i < count; i++) {
		unsigned int j;
		DEF_FIELD_ADDR(symval + i*id_size, pnp_card_device_id, devs);

		for (j = 0; j < PNP_MAX_DEVICES; j++) {
			const char *id = (char *)(*devs)[j].id;
			int i2, j2;
			int dup = 0;

			if (!id[0])
				break;

			/* find duplicate, already added value */
			for (i2 = 0; i2 < i && !dup; i2++) {
				DEF_FIELD_ADDR(symval + i2*id_size, pnp_card_device_id, devs);

				for (j2 = 0; j2 < PNP_MAX_DEVICES; j2++) {
					const char *id2 = (char *)(*devs)[j2].id;

					if (!id2[0])
						break;

					if (!strcmp(id, id2)) {
						dup = 1;
						break;
					}
				}
			}

			/* add an individual alias for every device entry */
			if (!dup) {
				char acpi_id[PNP_ID_LEN];
				int k;

				buf_printf(&mod->dev_table_buf,
					   "MODULE_ALIAS(\"pnp:d%s*\");\n", id);

				/* fix broken pnp bus lowercasing */
				for (k = 0; k < sizeof(acpi_id); k++)
					acpi_id[k] = toupper(id[k]);
				buf_printf(&mod->dev_table_buf,
					   "MODULE_ALIAS(\"acpi*:%s:*\");\n", acpi_id);
			}
		}
	}
}

/* Looks like: pcmcia:mNcNfNfnNpfnNvaNvbNvcNvdN. */
static int do_pcmcia_entry(const char *filename,
			   void *symval, char *alias)
{
	unsigned int i;
	DEF_FIELD(symval, pcmcia_device_id, match_flags);
	DEF_FIELD(symval, pcmcia_device_id, manf_id);
	DEF_FIELD(symval, pcmcia_device_id, card_id);
	DEF_FIELD(symval, pcmcia_device_id, func_id);
	DEF_FIELD(symval, pcmcia_device_id, function);
	DEF_FIELD(symval, pcmcia_device_id, device_no);
	DEF_FIELD_ADDR(symval, pcmcia_device_id, prod_id_hash);

	for (i=0; i<4; i++) {
		(*prod_id_hash)[i] = TO_NATIVE((*prod_id_hash)[i]);
	}

	strcpy(alias, "pcmcia:");
	ADD(alias, "m", match_flags & PCMCIA_DEV_ID_MATCH_MANF_ID,
	    manf_id);
	ADD(alias, "c", match_flags & PCMCIA_DEV_ID_MATCH_CARD_ID,
	    card_id);
	ADD(alias, "f", match_flags & PCMCIA_DEV_ID_MATCH_FUNC_ID,
	    func_id);
	ADD(alias, "fn", match_flags & PCMCIA_DEV_ID_MATCH_FUNCTION,
	    function);
	ADD(alias, "pfn", match_flags & PCMCIA_DEV_ID_MATCH_DEVICE_NO,
	    device_no);
	ADD(alias, "pa", match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID1, (*prod_id_hash)[0]);
	ADD(alias, "pb", match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID2, (*prod_id_hash)[1]);
	ADD(alias, "pc", match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID3, (*prod_id_hash)[2]);
	ADD(alias, "pd", match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID4, (*prod_id_hash)[3]);

	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("pcmcia", pcmcia_device_id, do_pcmcia_entry);

static int do_of_entry (const char *filename, void *symval, char *alias)
{
    int len;
    char *tmp;
    DEF_FIELD_ADDR(symval, of_device_id, name);
    DEF_FIELD_ADDR(symval, of_device_id, type);
    DEF_FIELD_ADDR(symval, of_device_id, compatible);

    len = sprintf (alias, "of:N%sT%s",
                    (*name)[0] ? *name : "*",
                    (*type)[0] ? *type : "*");

    if (compatible[0])
        sprintf (&alias[len], "%sC%s",
                     (*type)[0] ? "*" : "",
                     *compatible);

    /* Replace all whitespace with underscores */
    for (tmp = alias; tmp && *tmp; tmp++)
        if (isspace (*tmp))
            *tmp = '_';

    add_wildcard(alias);
    return 1;
}
ADD_TO_DEVTABLE("of", of_device_id, do_of_entry);

static int do_vio_entry(const char *filename, void *symval,
		char *alias)
{
	char *tmp;
	DEF_FIELD_ADDR(symval, vio_device_id, type);
	DEF_FIELD_ADDR(symval, vio_device_id, compat);

	sprintf(alias, "vio:T%sS%s", (*type)[0] ? *type : "*",
			(*compat)[0] ? *compat : "*");

	/* Replace all whitespace with underscores */
	for (tmp = alias; tmp && *tmp; tmp++)
		if (isspace (*tmp))
			*tmp = '_';

	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("vio", vio_device_id, do_vio_entry);

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static void do_input(char *alias,
		     kernel_ulong_t *arr, unsigned int min, unsigned int max)
{
	unsigned int i;

	for (i = min / BITS_PER_LONG; i < max / BITS_PER_LONG + 1; i++)
		arr[i] = TO_NATIVE(arr[i]);
	for (i = min; i < max; i++)
		if (arr[i / BITS_PER_LONG] & (1L << (i%BITS_PER_LONG)))
			sprintf(alias + strlen(alias), "%X,*", i);
}

/* input:b0v0p0e0-eXkXrXaXmXlXsXfXwX where X is comma-separated %02X. */
static int do_input_entry(const char *filename, void *symval,
			  char *alias)
{
	DEF_FIELD(symval, input_device_id, flags);
	DEF_FIELD(symval, input_device_id, bustype);
	DEF_FIELD(symval, input_device_id, vendor);
	DEF_FIELD(symval, input_device_id, product);
	DEF_FIELD(symval, input_device_id, version);
	DEF_FIELD_ADDR(symval, input_device_id, evbit);
	DEF_FIELD_ADDR(symval, input_device_id, keybit);
	DEF_FIELD_ADDR(symval, input_device_id, relbit);
	DEF_FIELD_ADDR(symval, input_device_id, absbit);
	DEF_FIELD_ADDR(symval, input_device_id, mscbit);
	DEF_FIELD_ADDR(symval, input_device_id, ledbit);
	DEF_FIELD_ADDR(symval, input_device_id, sndbit);
	DEF_FIELD_ADDR(symval, input_device_id, ffbit);
	DEF_FIELD_ADDR(symval, input_device_id, swbit);

	sprintf(alias, "input:");

	ADD(alias, "b", flags & INPUT_DEVICE_ID_MATCH_BUS, bustype);
	ADD(alias, "v", flags & INPUT_DEVICE_ID_MATCH_VENDOR, vendor);
	ADD(alias, "p", flags & INPUT_DEVICE_ID_MATCH_PRODUCT, product);
	ADD(alias, "e", flags & INPUT_DEVICE_ID_MATCH_VERSION, version);

	sprintf(alias + strlen(alias), "-e*");
	if (flags & INPUT_DEVICE_ID_MATCH_EVBIT)
		do_input(alias, *evbit, 0, INPUT_DEVICE_ID_EV_MAX);
	sprintf(alias + strlen(alias), "k*");
	if (flags & INPUT_DEVICE_ID_MATCH_KEYBIT)
		do_input(alias, *keybit,
			 INPUT_DEVICE_ID_KEY_MIN_INTERESTING,
			 INPUT_DEVICE_ID_KEY_MAX);
	sprintf(alias + strlen(alias), "r*");
	if (flags & INPUT_DEVICE_ID_MATCH_RELBIT)
		do_input(alias, *relbit, 0, INPUT_DEVICE_ID_REL_MAX);
	sprintf(alias + strlen(alias), "a*");
	if (flags & INPUT_DEVICE_ID_MATCH_ABSBIT)
		do_input(alias, *absbit, 0, INPUT_DEVICE_ID_ABS_MAX);
	sprintf(alias + strlen(alias), "m*");
	if (flags & INPUT_DEVICE_ID_MATCH_MSCIT)
		do_input(alias, *mscbit, 0, INPUT_DEVICE_ID_MSC_MAX);
	sprintf(alias + strlen(alias), "l*");
	if (flags & INPUT_DEVICE_ID_MATCH_LEDBIT)
		do_input(alias, *ledbit, 0, INPUT_DEVICE_ID_LED_MAX);
	sprintf(alias + strlen(alias), "s*");
	if (flags & INPUT_DEVICE_ID_MATCH_SNDBIT)
		do_input(alias, *sndbit, 0, INPUT_DEVICE_ID_SND_MAX);
	sprintf(alias + strlen(alias), "f*");
	if (flags & INPUT_DEVICE_ID_MATCH_FFBIT)
		do_input(alias, *ffbit, 0, INPUT_DEVICE_ID_FF_MAX);
	sprintf(alias + strlen(alias), "w*");
	if (flags & INPUT_DEVICE_ID_MATCH_SWBIT)
		do_input(alias, *swbit, 0, INPUT_DEVICE_ID_SW_MAX);
	return 1;
}
ADD_TO_DEVTABLE("input", input_device_id, do_input_entry);

static int do_eisa_entry(const char *filename, void *symval,
		char *alias)
{
	DEF_FIELD_ADDR(symval, eisa_device_id, sig);
	if (sig[0])
		sprintf(alias, EISA_DEVICE_MODALIAS_FMT "*", *sig);
	else
		strcat(alias, "*");
	return 1;
}
ADD_TO_DEVTABLE("eisa", eisa_device_id, do_eisa_entry);

/* Looks like: parisc:tNhvNrevNsvN */
static int do_parisc_entry(const char *filename, void *symval,
		char *alias)
{
	DEF_FIELD(symval, parisc_device_id, hw_type);
	DEF_FIELD(symval, parisc_device_id, hversion);
	DEF_FIELD(symval, parisc_device_id, hversion_rev);
	DEF_FIELD(symval, parisc_device_id, sversion);

	strcpy(alias, "parisc:");
	ADD(alias, "t", hw_type != PA_HWTYPE_ANY_ID, hw_type);
	ADD(alias, "hv", hversion != PA_HVERSION_ANY_ID, hversion);
	ADD(alias, "rev", hversion_rev != PA_HVERSION_REV_ANY_ID, hversion_rev);
	ADD(alias, "sv", sversion != PA_SVERSION_ANY_ID, sversion);

	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("parisc", parisc_device_id, do_parisc_entry);

/* Looks like: sdio:cNvNdN. */
static int do_sdio_entry(const char *filename,
			void *symval, char *alias)
{
	DEF_FIELD(symval, sdio_device_id, class);
	DEF_FIELD(symval, sdio_device_id, vendor);
	DEF_FIELD(symval, sdio_device_id, device);

	strcpy(alias, "sdio:");
	ADD(alias, "c", class != (__u8)SDIO_ANY_ID, class);
	ADD(alias, "v", vendor != (__u16)SDIO_ANY_ID, vendor);
	ADD(alias, "d", device != (__u16)SDIO_ANY_ID, device);
	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("sdio", sdio_device_id, do_sdio_entry);

/* Looks like: ssb:vNidNrevN. */
static int do_ssb_entry(const char *filename,
			void *symval, char *alias)
{
	DEF_FIELD(symval, ssb_device_id, vendor);
	DEF_FIELD(symval, ssb_device_id, coreid);
	DEF_FIELD(symval, ssb_device_id, revision);

	strcpy(alias, "ssb:");
	ADD(alias, "v", vendor != SSB_ANY_VENDOR, vendor);
	ADD(alias, "id", coreid != SSB_ANY_ID, coreid);
	ADD(alias, "rev", revision != SSB_ANY_REV, revision);
	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("ssb", ssb_device_id, do_ssb_entry);

/* Looks like: bcma:mNidNrevNclN. */
static int do_bcma_entry(const char *filename,
			 void *symval, char *alias)
{
	DEF_FIELD(symval, bcma_device_id, manuf);
	DEF_FIELD(symval, bcma_device_id, id);
	DEF_FIELD(symval, bcma_device_id, rev);
	DEF_FIELD(symval, bcma_device_id, class);

	strcpy(alias, "bcma:");
	ADD(alias, "m", manuf != BCMA_ANY_MANUF, manuf);
	ADD(alias, "id", id != BCMA_ANY_ID, id);
	ADD(alias, "rev", rev != BCMA_ANY_REV, rev);
	ADD(alias, "cl", class != BCMA_ANY_CLASS, class);
	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("bcma", bcma_device_id, do_bcma_entry);

/* Looks like: virtio:dNvN */
static int do_virtio_entry(const char *filename, void *symval,
			   char *alias)
{
	DEF_FIELD(symval, virtio_device_id, device);
	DEF_FIELD(symval, virtio_device_id, vendor);

	strcpy(alias, "virtio:");
	ADD(alias, "d", device != VIRTIO_DEV_ANY_ID, device);
	ADD(alias, "v", vendor != VIRTIO_DEV_ANY_ID, vendor);

	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("virtio", virtio_device_id, do_virtio_entry);

/*
 * Looks like: vmbus:guid
 * Each byte of the guid will be represented by two hex characters
 * in the name.
 */

static int do_vmbus_entry(const char *filename, void *symval,
			  char *alias)
{
	int i;
	DEF_FIELD_ADDR(symval, hv_vmbus_device_id, guid);
	char guid_name[(sizeof(*guid) + 1) * 2];

	for (i = 0; i < (sizeof(*guid) * 2); i += 2)
		sprintf(&guid_name[i], "%02x", TO_NATIVE((*guid)[i/2]));

	strcpy(alias, "vmbus:");
	strcat(alias, guid_name);

	return 1;
}
ADD_TO_DEVTABLE("vmbus", hv_vmbus_device_id, do_vmbus_entry);

/* Looks like: i2c:S */
static int do_i2c_entry(const char *filename, void *symval,
			char *alias)
{
	DEF_FIELD_ADDR(symval, i2c_device_id, name);
	sprintf(alias, I2C_MODULE_PREFIX "%s", *name);

	return 1;
}
ADD_TO_DEVTABLE("i2c", i2c_device_id, do_i2c_entry);

/* Looks like: spi:S */
static int do_spi_entry(const char *filename, void *symval,
			char *alias)
{
	DEF_FIELD_ADDR(symval, spi_device_id, name);
	sprintf(alias, SPI_MODULE_PREFIX "%s", *name);

	return 1;
}
ADD_TO_DEVTABLE("spi", spi_device_id, do_spi_entry);

static const struct dmifield {
	const char *prefix;
	int field;
} dmi_fields[] = {
	{ "bvn", DMI_BIOS_VENDOR },
	{ "bvr", DMI_BIOS_VERSION },
	{ "bd",  DMI_BIOS_DATE },
	{ "svn", DMI_SYS_VENDOR },
	{ "pn",  DMI_PRODUCT_NAME },
	{ "pvr", DMI_PRODUCT_VERSION },
	{ "rvn", DMI_BOARD_VENDOR },
	{ "rn",  DMI_BOARD_NAME },
	{ "rvr", DMI_BOARD_VERSION },
	{ "cvn", DMI_CHASSIS_VENDOR },
	{ "ct",  DMI_CHASSIS_TYPE },
	{ "cvr", DMI_CHASSIS_VERSION },
	{ NULL,  DMI_NONE }
};

static void dmi_ascii_filter(char *d, const char *s)
{
	/* Filter out characters we don't want to see in the modalias string */
	for (; *s; s++)
		if (*s > ' ' && *s < 127 && *s != ':')
			*(d++) = *s;

	*d = 0;
}


static int do_dmi_entry(const char *filename, void *symval,
			char *alias)
{
	int i, j;
	DEF_FIELD_ADDR(symval, dmi_system_id, matches);
	sprintf(alias, "dmi*");

	for (i = 0; i < ARRAY_SIZE(dmi_fields); i++) {
		for (j = 0; j < 4; j++) {
			if ((*matches)[j].slot &&
			    (*matches)[j].slot == dmi_fields[i].field) {
				sprintf(alias + strlen(alias), ":%s*",
					dmi_fields[i].prefix);
				dmi_ascii_filter(alias + strlen(alias),
						 (*matches)[j].substr);
				strcat(alias, "*");
			}
		}
	}

	strcat(alias, ":");
	return 1;
}
ADD_TO_DEVTABLE("dmi", dmi_system_id, do_dmi_entry);

static int do_platform_entry(const char *filename,
			     void *symval, char *alias)
{
	DEF_FIELD_ADDR(symval, platform_device_id, name);
	sprintf(alias, PLATFORM_MODULE_PREFIX "%s", *name);
	return 1;
}
ADD_TO_DEVTABLE("platform", platform_device_id, do_platform_entry);

static int do_mdio_entry(const char *filename,
			 void *symval, char *alias)
{
	int i;
	DEF_FIELD(symval, mdio_device_id, phy_id);
	DEF_FIELD(symval, mdio_device_id, phy_id_mask);

	alias += sprintf(alias, MDIO_MODULE_PREFIX);

	for (i = 0; i < 32; i++) {
		if (!((phy_id_mask >> (31-i)) & 1))
			*(alias++) = '?';
		else if ((phy_id >> (31-i)) & 1)
			*(alias++) = '1';
		else
			*(alias++) = '0';
	}

	/* Terminate the string */
	*alias = 0;

	return 1;
}
ADD_TO_DEVTABLE("mdio", mdio_device_id, do_mdio_entry);

/* Looks like: zorro:iN. */
static int do_zorro_entry(const char *filename, void *symval,
			  char *alias)
{
	DEF_FIELD(symval, zorro_device_id, id);
	strcpy(alias, "zorro:");
	ADD(alias, "i", id != ZORRO_WILDCARD, id);
	return 1;
}
ADD_TO_DEVTABLE("zorro", zorro_device_id, do_zorro_entry);

/* looks like: "pnp:dD" */
static int do_isapnp_entry(const char *filename,
			   void *symval, char *alias)
{
	DEF_FIELD(symval, isapnp_device_id, vendor);
	DEF_FIELD(symval, isapnp_device_id, function);
	sprintf(alias, "pnp:d%c%c%c%x%x%x%x*",
		'A' + ((vendor >> 2) & 0x3f) - 1,
		'A' + (((vendor & 3) << 3) | ((vendor >> 13) & 7)) - 1,
		'A' + ((vendor >> 8) & 0x1f) - 1,
		(function >> 4) & 0x0f, function & 0x0f,
		(function >> 12) & 0x0f, (function >> 8) & 0x0f);
	return 1;
}
ADD_TO_DEVTABLE("isapnp", isapnp_device_id, do_isapnp_entry);

/* Looks like: "ipack:fNvNdN". */
static int do_ipack_entry(const char *filename,
			  void *symval, char *alias)
{
	DEF_FIELD(symval, ipack_device_id, format);
	DEF_FIELD(symval, ipack_device_id, vendor);
	DEF_FIELD(symval, ipack_device_id, device);
	strcpy(alias, "ipack:");
	ADD(alias, "f", format != IPACK_ANY_FORMAT, format);
	ADD(alias, "v", vendor != IPACK_ANY_ID, vendor);
	ADD(alias, "d", device != IPACK_ANY_ID, device);
	add_wildcard(alias);
	return 1;
}
ADD_TO_DEVTABLE("ipack", ipack_device_id, do_ipack_entry);

/*
 * Append a match expression for a single masked hex digit.
 * outp points to a pointer to the character at which to append.
 *	*outp is updated on return to point just after the appended text,
 *	to facilitate further appending.
 */
static void append_nibble_mask(char **outp,
			       unsigned int nibble, unsigned int mask)
{
	char *p = *outp;
	unsigned int i;

	switch (mask) {
	case 0:
		*p++ = '?';
		break;

	case 0xf:
		p += sprintf(p, "%X",  nibble);
		break;

	default:
		/*
		 * Dumbly emit a match pattern for all possible matching
		 * digits.  This could be improved in some cases using ranges,
		 * but it has the advantage of being trivially correct, and is
		 * often optimal.
		 */
		*p++ = '[';
		for (i = 0; i < 0x10; i++)
			if ((i & mask) == nibble)
				p += sprintf(p, "%X", i);
		*p++ = ']';
	}

	/* Ensure that the string remains NUL-terminated: */
	*p = '\0';

	/* Advance the caller's end-of-string pointer: */
	*outp = p;
}

/*
 * looks like: "amba:dN"
 *
 * N is exactly 8 digits, where each is an upper-case hex digit, or
 *	a ? or [] pattern matching exactly one digit.
 */
static int do_amba_entry(const char *filename,
			 void *symval, char *alias)
{
	unsigned int digit;
	char *p = alias;
	DEF_FIELD(symval, amba_id, id);
	DEF_FIELD(symval, amba_id, mask);

	if ((id & mask) != id)
		fatal("%s: Masked-off bit(s) of AMBA device ID are non-zero: "
		      "id=0x%08X, mask=0x%08X.  Please fix this driver.\n",
		      filename, id, mask);

	p += sprintf(alias, "amba:d");
	for (digit = 0; digit < 8; digit++)
		append_nibble_mask(&p,
				   (id >> (4 * (7 - digit))) & 0xf,
				   (mask >> (4 * (7 - digit))) & 0xf);

	return 1;
}
ADD_TO_DEVTABLE("amba", amba_id, do_amba_entry);

/* LOOKS like x86cpu:vendor:VVVV:family:FFFF:model:MMMM:feature:*,FEAT,*
 * All fields are numbers. It would be nicer to use strings for vendor
 * and feature, but getting those out of the build system here is too
 * complicated.
 */

static int do_x86cpu_entry(const char *filename, void *symval,
			   char *alias)
{
	DEF_FIELD(symval, x86_cpu_id, feature);
	DEF_FIELD(symval, x86_cpu_id, family);
	DEF_FIELD(symval, x86_cpu_id, model);
	DEF_FIELD(symval, x86_cpu_id, vendor);

	strcpy(alias, "x86cpu:");
	ADD(alias, "vendor:",  vendor != X86_VENDOR_ANY, vendor);
	ADD(alias, ":family:", family != X86_FAMILY_ANY, family);
	ADD(alias, ":model:",  model  != X86_MODEL_ANY,  model);
	strcat(alias, ":feature:*");
	if (feature != X86_FEATURE_ANY)
		sprintf(alias + strlen(alias), "%04X*", feature);
	return 1;
}
ADD_TO_DEVTABLE("x86cpu", x86_cpu_id, do_x86cpu_entry);

/* Looks like: mei:S */
static int do_mei_entry(const char *filename, void *symval,
			char *alias)
{
	DEF_FIELD_ADDR(symval, mei_cl_device_id, name);

	sprintf(alias, MEI_CL_MODULE_PREFIX "%s", *name);

	return 1;
}
ADD_TO_DEVTABLE("mei", mei_cl_device_id, do_mei_entry);

/* Does namelen bytes of name exactly match the symbol? */
static bool sym_is(const char *name, unsigned namelen, const char *symbol)
{
	if (namelen != strlen(symbol))
		return false;

	return memcmp(name, symbol, namelen) == 0;
}

static void do_table(void *symval, unsigned long size,
		     unsigned long id_size,
		     const char *device_id,
		     void *function,
		     struct module *mod)
{
	unsigned int i;
	char alias[500];
	int (*do_entry)(const char *, void *entry, char *alias) = function;

	device_id_check(mod->name, device_id, size, id_size, symval);
	/* Leave last one: it's the terminator. */
	size -= id_size;

	for (i = 0; i < size; i += id_size) {
		if (do_entry(mod->name, symval+i, alias)) {
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
	char *zeros = NULL;
	const char *name;
	unsigned int namelen;

	/* We're looking for a section relative symbol */
	if (!sym->st_shndx || get_secindex(info, sym) >= info->num_sections)
		return;

	/* We're looking for an object */
	if (ELF_ST_TYPE(sym->st_info) != STT_OBJECT)
		return;

	/* All our symbols are of form <prefix>__mod_XXX_device_table. */
	name = strstr(symname, "__mod_");
	if (!name)
		return;
	name += strlen("__mod_");
	namelen = strlen(name);
	if (namelen < strlen("_device_table"))
		return;
	if (strcmp(name + namelen - strlen("_device_table"), "_device_table"))
		return;
	namelen -= strlen("_device_table");

	/* Handle all-NULL symbols allocated into .bss */
	if (info->sechdrs[get_secindex(info, sym)].sh_type & SHT_NOBITS) {
		zeros = calloc(1, sym->st_size);
		symval = zeros;
	} else {
		symval = (void *)info->hdr
			+ info->sechdrs[get_secindex(info, sym)].sh_offset
			+ sym->st_value;
	}

	/* First handle the "special" cases */
	if (sym_is(name, namelen, "usb"))
		do_usb_table(symval, sym->st_size, mod);
	else if (sym_is(name, namelen, "pnp"))
		do_pnp_device_entry(symval, sym->st_size, mod);
	else if (sym_is(name, namelen, "pnp_card"))
		do_pnp_card_entries(symval, sym->st_size, mod);
	else {
		struct devtable **p;
		INIT_SECTION(__devtable);

		for (p = __start___devtable; p < __stop___devtable; p++) {
			if (sym_is(name, namelen, (*p)->device_id)) {
				do_table(symval, sym->st_size, (*p)->id_size,
					 (*p)->device_id, (*p)->function, mod);
				break;
			}
		}
	}
	free(zeros);
}

/* Now add out buffered information to the generated C source */
void add_moddevtable(struct buffer *buf, struct module *mod)
{
	buf_printf(buf, "\n");
	buf_write(buf, mod->dev_table_buf.p, mod->dev_table_buf.pos);
	free(mod->dev_table_buf.p);
}
