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

#include <stdarg.h>
#include <stdio.h>

#include "list.h"
#include "xalloc.h"

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

/**
 * module_alias_printf - add auto-generated MODULE_ALIAS()
 *
 * @mod: module
 * @append_wildcard: append '*' for future extension if not exist yet
 * @fmt: printf(3)-like format
 */
static void __attribute__((format (printf, 3, 4)))
module_alias_printf(struct module *mod, bool append_wildcard,
		    const char *fmt, ...)
{
	struct module_alias *new, *als;
	size_t len;
	int n;
	va_list ap;

	/* Determine required size. */
	va_start(ap, fmt);
	n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	if (n < 0) {
		error("vsnprintf failed\n");
		return;
	}

	len = n + 1;	/* extra byte for '\0' */

	if (append_wildcard)
		len++;	/* extra byte for '*' */

	new = xmalloc(sizeof(*new) + len);

	/* Now, really print it to the allocated buffer */
	va_start(ap, fmt);
	n = vsnprintf(new->str, len, fmt, ap);
	va_end(ap);

	if (n < 0) {
		error("vsnprintf failed\n");
		free(new);
		return;
	}

	if (append_wildcard && (n == 0 || new->str[n - 1] != '*')) {
		new->str[n] = '*';
		new->str[n + 1] = '\0';
	}

	/* avoid duplication */
	list_for_each_entry(als, &mod->aliases, node) {
		if (!strcmp(als->str, new->str)) {
			free(new);
			return;
		}
	}

	new->builtin_modname = NULL;
	list_add_tail(&new->node, &mod->aliases);
}

typedef uint32_t	__u32;
typedef uint16_t	__u16;
typedef unsigned char	__u8;

/* UUID types for backward compatibility, don't use in new code */
typedef struct {
	__u8 b[16];
} guid_t;

typedef struct {
	__u8 b[16];
} uuid_t;

#define	UUID_STRING_LEN		36

/* MEI UUID type, don't use anywhere else */
typedef struct {
	__u8 b[16];
} uuid_le;

/* Big exception to the "don't include kernel headers into userspace, which
 * even potentially has different endianness and word sizes, since
 * we handle those differences explicitly below */
#include "../../include/linux/mod_devicetable.h"

struct devtable {
	const char *device_id;
	unsigned long id_size;
	void (*do_entry)(struct module *mod, void *symval);
};

/* Define a variable f that holds the value of field f of struct devid
 * based at address m.
 */
#define DEF_FIELD(m, devid, f) \
	typeof(((struct devid *)0)->f) f = \
		get_unaligned_native((typeof(f) *)((m) + OFF_##devid##_##f))

/* Define a variable f that holds the address of field f of struct devid
 * based at address m.  Due to the way typeof works, for a field of type
 * T[N] the variable has type T(*)[N], _not_ T*.
 */
#define DEF_FIELD_ADDR(m, devid, f) \
	typeof(((struct devid *)0)->f) *f = ((m) + OFF_##devid##_##f)

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

static inline void add_uuid(char *str, uuid_le uuid)
{
	int len = strlen(str);

	sprintf(str + len, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid.b[3], uuid.b[2], uuid.b[1], uuid.b[0],
		uuid.b[5], uuid.b[4], uuid.b[7], uuid.b[6],
		uuid.b[8], uuid.b[9], uuid.b[10], uuid.b[11],
		uuid.b[12], uuid.b[13], uuid.b[14], uuid.b[15]);
}

static inline void add_guid(char *str, guid_t guid)
{
	int len = strlen(str);

	sprintf(str + len, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		guid.b[3], guid.b[2], guid.b[1], guid.b[0],
		guid.b[5], guid.b[4], guid.b[7], guid.b[6],
		guid.b[8], guid.b[9], guid.b[10], guid.b[11],
		guid.b[12], guid.b[13], guid.b[14], guid.b[15]);
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

	module_alias_printf(mod, true, "%s", alias);
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

static void do_usb_entry_multi(struct module *mod, void *symval)
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

static void do_of_entry(struct module *mod, void *symval)
{
	char alias[500];
	int len;
	char *tmp;

	DEF_FIELD_ADDR(symval, of_device_id, name);
	DEF_FIELD_ADDR(symval, of_device_id, type);
	DEF_FIELD_ADDR(symval, of_device_id, compatible);

	len = sprintf(alias, "of:N%sT%s", (*name)[0] ? *name : "*",
		      (*type)[0] ? *type : "*");

	if ((*compatible)[0])
		sprintf(&alias[len], "%sC%s", (*type)[0] ? "*" : "",
			*compatible);

	/* Replace all whitespace with underscores */
	for (tmp = alias; tmp && *tmp; tmp++)
		if (isspace(*tmp))
			*tmp = '_';

	module_alias_printf(mod, false, "%s", alias);
	module_alias_printf(mod, false, "%sC*", alias);
}

/* Looks like: hid:bNvNpN */
static void do_hid_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, hid_device_id, bus);
	DEF_FIELD(symval, hid_device_id, group);
	DEF_FIELD(symval, hid_device_id, vendor);
	DEF_FIELD(symval, hid_device_id, product);

	ADD(alias, "b", bus != HID_BUS_ANY, bus);
	ADD(alias, "g", group != HID_GROUP_ANY, group);
	ADD(alias, "v", vendor != HID_ANY_ID, vendor);
	ADD(alias, "p", product != HID_ANY_ID, product);

	module_alias_printf(mod, false, "hid:%s", alias);
}

/* Looks like: ieee1394:venNmoNspNverN */
static void do_ieee1394_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, ieee1394_device_id, match_flags);
	DEF_FIELD(symval, ieee1394_device_id, vendor_id);
	DEF_FIELD(symval, ieee1394_device_id, model_id);
	DEF_FIELD(symval, ieee1394_device_id, specifier_id);
	DEF_FIELD(symval, ieee1394_device_id, version);

	ADD(alias, "ven", match_flags & IEEE1394_MATCH_VENDOR_ID,
	    vendor_id);
	ADD(alias, "mo", match_flags & IEEE1394_MATCH_MODEL_ID,
	    model_id);
	ADD(alias, "sp", match_flags & IEEE1394_MATCH_SPECIFIER_ID,
	    specifier_id);
	ADD(alias, "ver", match_flags & IEEE1394_MATCH_VERSION,
	    version);

	module_alias_printf(mod, true, "ieee1394:%s", alias);
}

/* Looks like: pci:vNdNsvNsdNbcNscNiN or <prefix>_pci:vNdNsvNsdNbcNscNiN. */
static void do_pci_entry(struct module *mod, void *symval)
{
	char alias[256];
	/* Class field can be divided into these three. */
	unsigned char baseclass, subclass, interface,
		baseclass_mask, subclass_mask, interface_mask;

	DEF_FIELD(symval, pci_device_id, vendor);
	DEF_FIELD(symval, pci_device_id, device);
	DEF_FIELD(symval, pci_device_id, subvendor);
	DEF_FIELD(symval, pci_device_id, subdevice);
	DEF_FIELD(symval, pci_device_id, class);
	DEF_FIELD(symval, pci_device_id, class_mask);
	DEF_FIELD(symval, pci_device_id, override_only);

	switch (override_only) {
	case 0:
		strcpy(alias, "pci:");
		break;
	case PCI_ID_F_VFIO_DRIVER_OVERRIDE:
		strcpy(alias, "vfio_pci:");
		break;
	default:
		warn("Unknown PCI driver_override alias %08X\n",
		     override_only);
	}

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
		     mod->name, class_mask);
		return;
	}

	ADD(alias, "bc", baseclass_mask == 0xFF, baseclass);
	ADD(alias, "sc", subclass_mask == 0xFF, subclass);
	ADD(alias, "i", interface_mask == 0xFF, interface);

	module_alias_printf(mod, true, "%s", alias);
}

/* looks like: "ccw:tNmNdtNdmN" */
static void do_ccw_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, ccw_device_id, match_flags);
	DEF_FIELD(symval, ccw_device_id, cu_type);
	DEF_FIELD(symval, ccw_device_id, cu_model);
	DEF_FIELD(symval, ccw_device_id, dev_type);
	DEF_FIELD(symval, ccw_device_id, dev_model);

	ADD(alias, "t", match_flags&CCW_DEVICE_ID_MATCH_CU_TYPE,
	    cu_type);
	ADD(alias, "m", match_flags&CCW_DEVICE_ID_MATCH_CU_MODEL,
	    cu_model);
	ADD(alias, "dt", match_flags&CCW_DEVICE_ID_MATCH_DEVICE_TYPE,
	    dev_type);
	ADD(alias, "dm", match_flags&CCW_DEVICE_ID_MATCH_DEVICE_MODEL,
	    dev_model);

	module_alias_printf(mod, true, "ccw:%s", alias);
}

/* looks like: "ap:tN" */
static void do_ap_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, ap_device_id, dev_type);

	module_alias_printf(mod, false, "ap:t%02X*", dev_type);
}

/* looks like: "css:tN" */
static void do_css_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, css_device_id, type);

	module_alias_printf(mod, false, "css:t%01X", type);
}

/* Looks like: "serio:tyNprNidNexN" */
static void do_serio_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, serio_device_id, type);
	DEF_FIELD(symval, serio_device_id, proto);
	DEF_FIELD(symval, serio_device_id, id);
	DEF_FIELD(symval, serio_device_id, extra);

	ADD(alias, "ty", type != SERIO_ANY, type);
	ADD(alias, "pr", proto != SERIO_ANY, proto);
	ADD(alias, "id", id != SERIO_ANY, id);
	ADD(alias, "ex", extra != SERIO_ANY, extra);

	module_alias_printf(mod, true, "serio:%s", alias);
}

/* looks like: "acpi:ACPI0003" or "acpi:PNP0C0B" or "acpi:LNXVIDEO" or
 *             "acpi:bbsspp" (bb=base-class, ss=sub-class, pp=prog-if)
 *
 * NOTE: Each driver should use one of the following : _HID, _CIDs
 *       or _CLS. Also, bb, ss, and pp can be substituted with ??
 *       as don't care byte.
 */
static void do_acpi_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, acpi_device_id, id);
	DEF_FIELD(symval, acpi_device_id, cls);
	DEF_FIELD(symval, acpi_device_id, cls_msk);

	if ((*id)[0])
		module_alias_printf(mod, false, "acpi*:%s:*", *id);
	else {
		char alias[256];
		int i, byte_shift, cnt = 0;
		unsigned int msk;

		for (i = 1; i <= 3; i++) {
			byte_shift = 8 * (3-i);
			msk = (cls_msk >> byte_shift) & 0xFF;
			if (msk)
				sprintf(&alias[cnt], "%02x",
					(cls >> byte_shift) & 0xFF);
			else
				sprintf(&alias[cnt], "??");
			cnt += 2;
		}
		module_alias_printf(mod, false, "acpi*:%s:*", alias);
	}
}

/* looks like: "pnp:dD" */
static void do_pnp_device_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, pnp_device_id, id);
	char acpi_id[sizeof(*id)];

	/* fix broken pnp bus lowercasing */
	for (unsigned int i = 0; i < sizeof(acpi_id); i++)
		acpi_id[i] = toupper((*id)[i]);
	module_alias_printf(mod, false, "pnp:d%s*", *id);
	module_alias_printf(mod, false, "acpi*:%s:*", acpi_id);
}

/* looks like: "pnp:dD" for every device of the card */
static void do_pnp_card_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, pnp_card_device_id, devs);

	for (unsigned int i = 0; i < PNP_MAX_DEVICES; i++) {
		const char *id = (char *)(*devs)[i].id;
		char acpi_id[PNP_ID_LEN];

		if (!id[0])
			break;

		/* fix broken pnp bus lowercasing */
		for (unsigned int j = 0; j < sizeof(acpi_id); j++)
			acpi_id[j] = toupper(id[j]);

		/* add an individual alias for every device entry */
		module_alias_printf(mod, false, "pnp:d%s*", id);
		module_alias_printf(mod, false, "acpi*:%s:*", acpi_id);
	}
}

/* Looks like: pcmcia:mNcNfNfnNpfnNvaNvbNvcNvdN. */
static void do_pcmcia_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, pcmcia_device_id, match_flags);
	DEF_FIELD(symval, pcmcia_device_id, manf_id);
	DEF_FIELD(symval, pcmcia_device_id, card_id);
	DEF_FIELD(symval, pcmcia_device_id, func_id);
	DEF_FIELD(symval, pcmcia_device_id, function);
	DEF_FIELD(symval, pcmcia_device_id, device_no);
	DEF_FIELD_ADDR(symval, pcmcia_device_id, prod_id_hash);

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
	ADD(alias, "pa", match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID1,
	    get_unaligned_native(*prod_id_hash + 0));
	ADD(alias, "pb", match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID2,
	    get_unaligned_native(*prod_id_hash + 1));
	ADD(alias, "pc", match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID3,
	    get_unaligned_native(*prod_id_hash + 2));
	ADD(alias, "pd", match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID4,
	    get_unaligned_native(*prod_id_hash + 3));

	module_alias_printf(mod, true, "pcmcia:%s", alias);
}

static void do_vio_entry(struct module *mod, void *symval)
{
	char alias[256];
	char *tmp;
	DEF_FIELD_ADDR(symval, vio_device_id, type);
	DEF_FIELD_ADDR(symval, vio_device_id, compat);

	sprintf(alias, "vio:T%sS%s", (*type)[0] ? *type : "*",
			(*compat)[0] ? *compat : "*");

	/* Replace all whitespace with underscores */
	for (tmp = alias; tmp && *tmp; tmp++)
		if (isspace (*tmp))
			*tmp = '_';

	module_alias_printf(mod, true, "%s", alias);
}

static void do_input(char *alias,
		     kernel_ulong_t *arr, unsigned int min, unsigned int max)
{
	unsigned int i;

	for (i = min; i <= max; i++)
		if (get_unaligned_native(arr + i / BITS_PER_LONG) &
		    (1ULL << (i % BITS_PER_LONG)))
			sprintf(alias + strlen(alias), "%X,*", i);
}

/* input:b0v0p0e0-eXkXrXaXmXlXsXfXwX where X is comma-separated %02X. */
static void do_input_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

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

	module_alias_printf(mod, false, "input:%s", alias);
}

static void do_eisa_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, eisa_device_id, sig);
	module_alias_printf(mod, false, EISA_DEVICE_MODALIAS_FMT "*", *sig);
}

/* Looks like: parisc:tNhvNrevNsvN */
static void do_parisc_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, parisc_device_id, hw_type);
	DEF_FIELD(symval, parisc_device_id, hversion);
	DEF_FIELD(symval, parisc_device_id, hversion_rev);
	DEF_FIELD(symval, parisc_device_id, sversion);

	ADD(alias, "t", hw_type != PA_HWTYPE_ANY_ID, hw_type);
	ADD(alias, "hv", hversion != PA_HVERSION_ANY_ID, hversion);
	ADD(alias, "rev", hversion_rev != PA_HVERSION_REV_ANY_ID, hversion_rev);
	ADD(alias, "sv", sversion != PA_SVERSION_ANY_ID, sversion);

	module_alias_printf(mod, true, "parisc:%s", alias);
}

/* Looks like: sdio:cNvNdN. */
static void do_sdio_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, sdio_device_id, class);
	DEF_FIELD(symval, sdio_device_id, vendor);
	DEF_FIELD(symval, sdio_device_id, device);

	ADD(alias, "c", class != (__u8)SDIO_ANY_ID, class);
	ADD(alias, "v", vendor != (__u16)SDIO_ANY_ID, vendor);
	ADD(alias, "d", device != (__u16)SDIO_ANY_ID, device);

	module_alias_printf(mod, true, "sdio:%s", alias);
}

/* Looks like: ssb:vNidNrevN. */
static void do_ssb_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, ssb_device_id, vendor);
	DEF_FIELD(symval, ssb_device_id, coreid);
	DEF_FIELD(symval, ssb_device_id, revision);

	ADD(alias, "v", vendor != SSB_ANY_VENDOR, vendor);
	ADD(alias, "id", coreid != SSB_ANY_ID, coreid);
	ADD(alias, "rev", revision != SSB_ANY_REV, revision);

	module_alias_printf(mod, true, "ssb:%s", alias);
}

/* Looks like: bcma:mNidNrevNclN. */
static void do_bcma_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, bcma_device_id, manuf);
	DEF_FIELD(symval, bcma_device_id, id);
	DEF_FIELD(symval, bcma_device_id, rev);
	DEF_FIELD(symval, bcma_device_id, class);

	ADD(alias, "m", manuf != BCMA_ANY_MANUF, manuf);
	ADD(alias, "id", id != BCMA_ANY_ID, id);
	ADD(alias, "rev", rev != BCMA_ANY_REV, rev);
	ADD(alias, "cl", class != BCMA_ANY_CLASS, class);

	module_alias_printf(mod, true, "bcma:%s", alias);
}

/* Looks like: virtio:dNvN */
static void do_virtio_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, virtio_device_id, device);
	DEF_FIELD(symval, virtio_device_id, vendor);

	ADD(alias, "d", device != VIRTIO_DEV_ANY_ID, device);
	ADD(alias, "v", vendor != VIRTIO_DEV_ANY_ID, vendor);

	module_alias_printf(mod, true, "virtio:%s", alias);
}

/*
 * Looks like: vmbus:guid
 * Each byte of the guid will be represented by two hex characters
 * in the name.
 */
static void do_vmbus_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, hv_vmbus_device_id, guid);
	char guid_name[sizeof(*guid) * 2 + 1];

	for (int i = 0; i < sizeof(*guid); i++)
		sprintf(&guid_name[i * 2], "%02x", guid->b[i]);

	module_alias_printf(mod, false, "vmbus:%s", guid_name);
}

/* Looks like: rpmsg:S */
static void do_rpmsg_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, rpmsg_device_id, name);

	module_alias_printf(mod, false, RPMSG_DEVICE_MODALIAS_FMT, *name);
}

/* Looks like: i2c:S */
static void do_i2c_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, i2c_device_id, name);

	module_alias_printf(mod, false, I2C_MODULE_PREFIX "%s", *name);
}

static void do_i3c_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, i3c_device_id, match_flags);
	DEF_FIELD(symval, i3c_device_id, dcr);
	DEF_FIELD(symval, i3c_device_id, manuf_id);
	DEF_FIELD(symval, i3c_device_id, part_id);
	DEF_FIELD(symval, i3c_device_id, extra_info);

	ADD(alias, "dcr", match_flags & I3C_MATCH_DCR, dcr);
	ADD(alias, "manuf", match_flags & I3C_MATCH_MANUF, manuf_id);
	ADD(alias, "part", match_flags & I3C_MATCH_PART, part_id);
	ADD(alias, "ext", match_flags & I3C_MATCH_EXTRA_INFO, extra_info);

	module_alias_printf(mod, false, "i3c:%s", alias);
}

static void do_slim_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, slim_device_id, manf_id);
	DEF_FIELD(symval, slim_device_id, prod_code);

	module_alias_printf(mod, false, "slim:%x:%x:*", manf_id, prod_code);
}

/* Looks like: spi:S */
static void do_spi_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, spi_device_id, name);

	module_alias_printf(mod, false, SPI_MODULE_PREFIX "%s", *name);
}

static const struct dmifield {
	const char *prefix;
	int field;
} dmi_fields[] = {
	{ "bvn", DMI_BIOS_VENDOR },
	{ "bvr", DMI_BIOS_VERSION },
	{ "bd",  DMI_BIOS_DATE },
	{ "br",  DMI_BIOS_RELEASE },
	{ "efr", DMI_EC_FIRMWARE_RELEASE },
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


static void do_dmi_entry(struct module *mod, void *symval)
{
	char alias[256] = {};
	int i, j;
	DEF_FIELD_ADDR(symval, dmi_system_id, matches);

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

	module_alias_printf(mod, false, "dmi*%s:", alias);
}

static void do_platform_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, platform_device_id, name);

	module_alias_printf(mod, false, PLATFORM_MODULE_PREFIX "%s", *name);
}

static void do_mdio_entry(struct module *mod, void *symval)
{
	char id[33];
	int i;
	DEF_FIELD(symval, mdio_device_id, phy_id);
	DEF_FIELD(symval, mdio_device_id, phy_id_mask);

	for (i = 0; i < 32; i++) {
		if (!((phy_id_mask >> (31-i)) & 1))
			id[i] = '?';
		else if ((phy_id >> (31-i)) & 1)
			id[i] = '1';
		else
			id[i] = '0';
	}

	/* Terminate the string */
	id[32] = '\0';

	module_alias_printf(mod, false, MDIO_MODULE_PREFIX "%s", id);
}

/* Looks like: zorro:iN. */
static void do_zorro_entry(struct module *mod, void *symval)
{
	char alias[256] = {};
	DEF_FIELD(symval, zorro_device_id, id);

	ADD(alias, "i", id != ZORRO_WILDCARD, id);

	module_alias_printf(mod, false, "zorro:%s", alias);
}

/* looks like: "pnp:dD" */
static void do_isapnp_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, isapnp_device_id, vendor);
	DEF_FIELD(symval, isapnp_device_id, function);
	module_alias_printf(mod, false, "pnp:d%c%c%c%x%x%x%x*",
		'A' + ((vendor >> 2) & 0x3f) - 1,
		'A' + (((vendor & 3) << 3) | ((vendor >> 13) & 7)) - 1,
		'A' + ((vendor >> 8) & 0x1f) - 1,
		(function >> 4) & 0x0f, function & 0x0f,
		(function >> 12) & 0x0f, (function >> 8) & 0x0f);
}

/* Looks like: "ipack:fNvNdN". */
static void do_ipack_entry(struct module *mod, void *symval)
{
	char alias[256] = {};
	DEF_FIELD(symval, ipack_device_id, format);
	DEF_FIELD(symval, ipack_device_id, vendor);
	DEF_FIELD(symval, ipack_device_id, device);

	ADD(alias, "f", format != IPACK_ANY_FORMAT, format);
	ADD(alias, "v", vendor != IPACK_ANY_ID, vendor);
	ADD(alias, "d", device != IPACK_ANY_ID, device);

	module_alias_printf(mod, true, "ipack:%s", alias);
}

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
static void do_amba_entry(struct module *mod, void *symval)
{
	char alias[256];
	unsigned int digit;
	char *p = alias;
	DEF_FIELD(symval, amba_id, id);
	DEF_FIELD(symval, amba_id, mask);

	if ((id & mask) != id)
		fatal("%s: Masked-off bit(s) of AMBA device ID are non-zero: id=0x%08X, mask=0x%08X.  Please fix this driver.\n",
		      mod->name, id, mask);

	for (digit = 0; digit < 8; digit++)
		append_nibble_mask(&p,
				   (id >> (4 * (7 - digit))) & 0xf,
				   (mask >> (4 * (7 - digit))) & 0xf);

	module_alias_printf(mod, false, "amba:d%s", alias);
}

/*
 * looks like: "mipscdmm:tN"
 *
 * N is exactly 2 digits, where each is an upper-case hex digit, or
 *	a ? or [] pattern matching exactly one digit.
 */
static void do_mips_cdmm_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, mips_cdmm_device_id, type);

	module_alias_printf(mod, false, "mipscdmm:t%02X*", type);
}

/* LOOKS like cpu:type:x86,venVVVVfamFFFFmodMMMM:feature:*,FEAT,*
 * All fields are numbers. It would be nicer to use strings for vendor
 * and feature, but getting those out of the build system here is too
 * complicated.
 */

static void do_x86cpu_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, x86_cpu_id, feature);
	DEF_FIELD(symval, x86_cpu_id, family);
	DEF_FIELD(symval, x86_cpu_id, model);
	DEF_FIELD(symval, x86_cpu_id, vendor);

	ADD(alias, "ven", vendor != X86_VENDOR_ANY, vendor);
	ADD(alias, "fam", family != X86_FAMILY_ANY, family);
	ADD(alias, "mod", model  != X86_MODEL_ANY,  model);
	strcat(alias, ":feature:*");
	if (feature != X86_FEATURE_ANY)
		sprintf(alias + strlen(alias), "%04X*", feature);

	module_alias_printf(mod, false, "cpu:type:x86,%s", alias);
}

/* LOOKS like cpu:type:*:feature:*FEAT* */
static void do_cpu_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, cpu_feature, feature);

	module_alias_printf(mod, false, "cpu:type:*:feature:*%04X*", feature);
}

/* Looks like: mei:S:uuid:N:* */
static void do_mei_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD_ADDR(symval, mei_cl_device_id, name);
	DEF_FIELD_ADDR(symval, mei_cl_device_id, uuid);
	DEF_FIELD(symval, mei_cl_device_id, version);

	add_uuid(alias, *uuid);
	ADD(alias, ":", version != MEI_CL_VERSION_ANY, version);

	module_alias_printf(mod, false, MEI_CL_MODULE_PREFIX "%s:%s:*",
			    (*name)[0] ? *name : "*", alias);
}

/* Looks like: rapidio:vNdNavNadN */
static void do_rio_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, rio_device_id, did);
	DEF_FIELD(symval, rio_device_id, vid);
	DEF_FIELD(symval, rio_device_id, asm_did);
	DEF_FIELD(symval, rio_device_id, asm_vid);

	ADD(alias, "v", vid != RIO_ANY_ID, vid);
	ADD(alias, "d", did != RIO_ANY_ID, did);
	ADD(alias, "av", asm_vid != RIO_ANY_ID, asm_vid);
	ADD(alias, "ad", asm_did != RIO_ANY_ID, asm_did);

	module_alias_printf(mod, true, "rapidio:%s", alias);
}

/* Looks like: ulpi:vNpN */
static void do_ulpi_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, ulpi_device_id, vendor);
	DEF_FIELD(symval, ulpi_device_id, product);

	module_alias_printf(mod, false, "ulpi:v%04xp%04x", vendor, product);
}

/* Looks like: hdaudio:vNrNaN */
static void do_hda_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, hda_device_id, vendor_id);
	DEF_FIELD(symval, hda_device_id, rev_id);
	DEF_FIELD(symval, hda_device_id, api_version);

	ADD(alias, "v", vendor_id != 0, vendor_id);
	ADD(alias, "r", rev_id != 0, rev_id);
	ADD(alias, "a", api_version != 0, api_version);

	module_alias_printf(mod, true, "hdaudio:%s", alias);
}

/* Looks like: sdw:mNpNvNcN */
static void do_sdw_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, sdw_device_id, mfg_id);
	DEF_FIELD(symval, sdw_device_id, part_id);
	DEF_FIELD(symval, sdw_device_id, sdw_version);
	DEF_FIELD(symval, sdw_device_id, class_id);

	ADD(alias, "m", mfg_id != 0, mfg_id);
	ADD(alias, "p", part_id != 0, part_id);
	ADD(alias, "v", sdw_version != 0, sdw_version);
	ADD(alias, "c", class_id != 0, class_id);

	module_alias_printf(mod, true, "sdw:%s", alias);
}

/* Looks like: fsl-mc:vNdN */
static void do_fsl_mc_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, fsl_mc_device_id, vendor);
	DEF_FIELD_ADDR(symval, fsl_mc_device_id, obj_type);

	module_alias_printf(mod, false, "fsl-mc:v%08Xd%s", vendor, *obj_type);
}

/* Looks like: tbsvc:kSpNvNrN */
static void do_tbsvc_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, tb_service_id, match_flags);
	DEF_FIELD_ADDR(symval, tb_service_id, protocol_key);
	DEF_FIELD(symval, tb_service_id, protocol_id);
	DEF_FIELD(symval, tb_service_id, protocol_version);
	DEF_FIELD(symval, tb_service_id, protocol_revision);

	if (match_flags & TBSVC_MATCH_PROTOCOL_KEY)
		sprintf(alias + strlen(alias), "k%s", *protocol_key);
	else
		strcat(alias + strlen(alias), "k*");
	ADD(alias, "p", match_flags & TBSVC_MATCH_PROTOCOL_ID, protocol_id);
	ADD(alias, "v", match_flags & TBSVC_MATCH_PROTOCOL_VERSION,
	    protocol_version);
	ADD(alias, "r", match_flags & TBSVC_MATCH_PROTOCOL_REVISION,
	    protocol_revision);

	module_alias_printf(mod, true, "tbsvc:%s", alias);
}

/* Looks like: typec:idN */
static void do_typec_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, typec_device_id, svid);

	module_alias_printf(mod, false, "typec:id%04X", svid);
}

/* Looks like: tee:uuid */
static void do_tee_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, tee_client_device_id, uuid);

	module_alias_printf(mod, true,
			    "tee:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid->b[0], uuid->b[1], uuid->b[2], uuid->b[3], uuid->b[4],
		uuid->b[5], uuid->b[6], uuid->b[7], uuid->b[8], uuid->b[9],
		uuid->b[10], uuid->b[11], uuid->b[12], uuid->b[13], uuid->b[14],
		uuid->b[15]);
}

/* Looks like: wmi:guid */
static void do_wmi_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, wmi_device_id, guid_string);

	if (strlen(*guid_string) != UUID_STRING_LEN) {
		warn("Invalid WMI device id 'wmi:%s' in '%s'\n",
				*guid_string, mod->name);
		return;
	}

	module_alias_printf(mod, false, WMI_MODULE_PREFIX "%s", *guid_string);
}

/* Looks like: mhi:S */
static void do_mhi_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, mhi_device_id, chan);
	module_alias_printf(mod, false, MHI_DEVICE_MODALIAS_FMT, *chan);
}

/* Looks like: mhi_ep:S */
static void do_mhi_ep_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, mhi_device_id, chan);

	module_alias_printf(mod, false, MHI_EP_DEVICE_MODALIAS_FMT, *chan);
}

/* Looks like: ishtp:{guid} */
static void do_ishtp_entry(struct module *mod, void *symval)
{
	char alias[256] = {};
	DEF_FIELD_ADDR(symval, ishtp_device_id, guid);

	add_guid(alias, *guid);

	module_alias_printf(mod, false, ISHTP_MODULE_PREFIX "{%s}", alias);
}

static void do_auxiliary_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, auxiliary_device_id, name);

	module_alias_printf(mod, false, AUXILIARY_MODULE_PREFIX "%s", *name);
}

/*
 * Looks like: ssam:dNcNtNiNfN
 *
 * N is exactly 2 digits, where each is an upper-case hex digit.
 */
static void do_ssam_entry(struct module *mod, void *symval)
{
	char alias[256] = {};

	DEF_FIELD(symval, ssam_device_id, match_flags);
	DEF_FIELD(symval, ssam_device_id, domain);
	DEF_FIELD(symval, ssam_device_id, category);
	DEF_FIELD(symval, ssam_device_id, target);
	DEF_FIELD(symval, ssam_device_id, instance);
	DEF_FIELD(symval, ssam_device_id, function);

	ADD(alias, "t", match_flags & SSAM_MATCH_TARGET, target);
	ADD(alias, "i", match_flags & SSAM_MATCH_INSTANCE, instance);
	ADD(alias, "f", match_flags & SSAM_MATCH_FUNCTION, function);

	module_alias_printf(mod, false, "ssam:d%02Xc%02X%s",
			    domain, category, alias);
}

/* Looks like: dfl:tNfN */
static void do_dfl_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, dfl_device_id, type);
	DEF_FIELD(symval, dfl_device_id, feature_id);

	module_alias_printf(mod, true, "dfl:t%04Xf%04X", type, feature_id);
}

/* Looks like: cdx:vNdN */
static void do_cdx_entry(struct module *mod, void *symval)
{
	char alias[256];

	DEF_FIELD(symval, cdx_device_id, vendor);
	DEF_FIELD(symval, cdx_device_id, device);
	DEF_FIELD(symval, cdx_device_id, subvendor);
	DEF_FIELD(symval, cdx_device_id, subdevice);
	DEF_FIELD(symval, cdx_device_id, class);
	DEF_FIELD(symval, cdx_device_id, class_mask);
	DEF_FIELD(symval, cdx_device_id, override_only);

	switch (override_only) {
	case 0:
		strcpy(alias, "cdx:");
		break;
	case CDX_ID_F_VFIO_DRIVER_OVERRIDE:
		strcpy(alias, "vfio_cdx:");
		break;
	default:
		warn("Unknown CDX driver_override alias %08X\n",
		     override_only);
		return;
	}

	ADD(alias, "v", vendor != CDX_ANY_ID, vendor);
	ADD(alias, "d", device != CDX_ANY_ID, device);
	ADD(alias, "sv", subvendor != CDX_ANY_ID, subvendor);
	ADD(alias, "sd", subdevice != CDX_ANY_ID, subdevice);
	ADD(alias, "c", class_mask == 0xFFFFFF, class);

	module_alias_printf(mod, false, "%s", alias);
}

static void do_vchiq_entry(struct module *mod, void *symval)
{
	DEF_FIELD_ADDR(symval, vchiq_device_id, name);

	module_alias_printf(mod, false, "vchiq:%s", *name);
}

/* Looks like: coreboot:tN */
static void do_coreboot_entry(struct module *mod, void *symval)
{
	DEF_FIELD(symval, coreboot_device_id, tag);

	module_alias_printf(mod, false, "coreboot:t%08X", tag);
}

/* Does namelen bytes of name exactly match the symbol? */
static bool sym_is(const char *name, unsigned namelen, const char *symbol)
{
	if (namelen != strlen(symbol))
		return false;

	return memcmp(name, symbol, namelen) == 0;
}

static void do_table(const char *name, void *symval, unsigned long size,
		     unsigned long id_size,
		     const char *device_id,
		     void (*do_entry)(struct module *mod, void *symval),
		     struct module *mod)
{
	unsigned int i;

	if (size % id_size || size < id_size) {
		error("%s: type mismatch between %s[] and MODULE_DEVICE_TABLE(%s, ...)\n",
		      mod->name, name, device_id);
		return;
	}

	/* Verify the last entry is a terminator */
	for (i = size - id_size; i < size; i++) {
		if (*(uint8_t *)(symval + i)) {
			error("%s: %s[] is not terminated with a NULL entry\n",
			      mod->name, name);
			return;
		}
	}

	/* Leave last one: it's the terminator. */
	size -= id_size;

	for (i = 0; i < size; i += id_size)
		do_entry(mod, symval + i);
}

static const struct devtable devtable[] = {
	{"hid", SIZE_hid_device_id, do_hid_entry},
	{"ieee1394", SIZE_ieee1394_device_id, do_ieee1394_entry},
	{"pci", SIZE_pci_device_id, do_pci_entry},
	{"ccw", SIZE_ccw_device_id, do_ccw_entry},
	{"ap", SIZE_ap_device_id, do_ap_entry},
	{"css", SIZE_css_device_id, do_css_entry},
	{"serio", SIZE_serio_device_id, do_serio_entry},
	{"acpi", SIZE_acpi_device_id, do_acpi_entry},
	{"pcmcia", SIZE_pcmcia_device_id, do_pcmcia_entry},
	{"vio", SIZE_vio_device_id, do_vio_entry},
	{"input", SIZE_input_device_id, do_input_entry},
	{"eisa", SIZE_eisa_device_id, do_eisa_entry},
	{"parisc", SIZE_parisc_device_id, do_parisc_entry},
	{"sdio", SIZE_sdio_device_id, do_sdio_entry},
	{"ssb", SIZE_ssb_device_id, do_ssb_entry},
	{"bcma", SIZE_bcma_device_id, do_bcma_entry},
	{"virtio", SIZE_virtio_device_id, do_virtio_entry},
	{"vmbus", SIZE_hv_vmbus_device_id, do_vmbus_entry},
	{"rpmsg", SIZE_rpmsg_device_id, do_rpmsg_entry},
	{"i2c", SIZE_i2c_device_id, do_i2c_entry},
	{"i3c", SIZE_i3c_device_id, do_i3c_entry},
	{"slim", SIZE_slim_device_id, do_slim_entry},
	{"spi", SIZE_spi_device_id, do_spi_entry},
	{"dmi", SIZE_dmi_system_id, do_dmi_entry},
	{"platform", SIZE_platform_device_id, do_platform_entry},
	{"mdio", SIZE_mdio_device_id, do_mdio_entry},
	{"zorro", SIZE_zorro_device_id, do_zorro_entry},
	{"isapnp", SIZE_isapnp_device_id, do_isapnp_entry},
	{"ipack", SIZE_ipack_device_id, do_ipack_entry},
	{"amba", SIZE_amba_id, do_amba_entry},
	{"mipscdmm", SIZE_mips_cdmm_device_id, do_mips_cdmm_entry},
	{"x86cpu", SIZE_x86_cpu_id, do_x86cpu_entry},
	{"cpu", SIZE_cpu_feature, do_cpu_entry},
	{"mei", SIZE_mei_cl_device_id, do_mei_entry},
	{"rapidio", SIZE_rio_device_id, do_rio_entry},
	{"ulpi", SIZE_ulpi_device_id, do_ulpi_entry},
	{"hdaudio", SIZE_hda_device_id, do_hda_entry},
	{"sdw", SIZE_sdw_device_id, do_sdw_entry},
	{"fslmc", SIZE_fsl_mc_device_id, do_fsl_mc_entry},
	{"tbsvc", SIZE_tb_service_id, do_tbsvc_entry},
	{"typec", SIZE_typec_device_id, do_typec_entry},
	{"tee", SIZE_tee_client_device_id, do_tee_entry},
	{"wmi", SIZE_wmi_device_id, do_wmi_entry},
	{"mhi", SIZE_mhi_device_id, do_mhi_entry},
	{"mhi_ep", SIZE_mhi_device_id, do_mhi_ep_entry},
	{"auxiliary", SIZE_auxiliary_device_id, do_auxiliary_entry},
	{"ssam", SIZE_ssam_device_id, do_ssam_entry},
	{"dfl", SIZE_dfl_device_id, do_dfl_entry},
	{"ishtp", SIZE_ishtp_device_id, do_ishtp_entry},
	{"cdx", SIZE_cdx_device_id, do_cdx_entry},
	{"vchiq", SIZE_vchiq_device_id, do_vchiq_entry},
	{"coreboot", SIZE_coreboot_device_id, do_coreboot_entry},
	{"of", SIZE_of_device_id, do_of_entry},
	{"usb", SIZE_usb_device_id, do_usb_entry_multi},
	{"pnp", SIZE_pnp_device_id, do_pnp_device_entry},
	{"pnp_card", SIZE_pnp_card_device_id, do_pnp_card_entry},
};

/* Create MODULE_ALIAS() statements.
 * At this time, we cannot write the actual output C source yet,
 * so we write into the mod->dev_table_buf buffer. */
void handle_moddevtable(struct module *mod, struct elf_info *info,
			Elf_Sym *sym, const char *symname)
{
	void *symval;
	char *zeros = NULL;
	const char *type, *name, *modname;
	size_t typelen, modnamelen;
	static const char *prefix = "__mod_device_table__";

	/* We're looking for a section relative symbol */
	if (!sym->st_shndx || get_secindex(info, sym) >= info->num_sections)
		return;

	/* We're looking for an object */
	if (ELF_ST_TYPE(sym->st_info) != STT_OBJECT)
		return;

	/* All our symbols are of form __mod_device_table__kmod_<modname>__<type>__<name>. */
	if (!strstarts(symname, prefix))
		return;

	modname = strstr(symname, "__kmod_");
	if (!modname)
		return;
	modname += strlen("__kmod_");

	type = strstr(modname, "__");
	if (!type)
		return;
	modnamelen = type - modname;
	type += strlen("__");

	name = strstr(type, "__");
	if (!name)
		return;
	typelen = name - type;
	name += strlen("__");

	/* Handle all-NULL symbols allocated into .bss */
	if (info->sechdrs[get_secindex(info, sym)].sh_type & SHT_NOBITS) {
		zeros = calloc(1, sym->st_size);
		symval = zeros;
	} else {
		symval = sym_get_data(info, sym);
	}

	for (int i = 0; i < ARRAY_SIZE(devtable); i++) {
		const struct devtable *p = &devtable[i];

		if (sym_is(type, typelen, p->device_id)) {
			do_table(name, symval, sym->st_size, p->id_size,
				 p->device_id, p->do_entry, mod);
			break;
		}
	}

	if (mod->is_vmlinux) {
		struct module_alias *alias;

		/*
		 * If this is vmlinux, record the name of the builtin module.
		 * Traverse the linked list in the reverse order, and set the
		 * builtin_modname unless it has already been set in the
		 * previous call.
		 */
		list_for_each_entry_reverse(alias, &mod->aliases, node) {
			if (alias->builtin_modname)
				break;
			alias->builtin_modname = xstrndup(modname, modnamelen);
		}
	}

	free(zeros);
}
