/******************************************************************************
 * hvm/hvm_xs_strings.h
 *
 * HVM xenstore strings used in HVMLOADER.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2013, Citrix Systems
 */

#ifndef __XEN_PUBLIC_HVM_HVM_XS_STRINGS_H__
#define __XEN_PUBLIC_HVM_HVM_XS_STRINGS_H__

#define HVM_XS_HVMLOADER               "hvmloader"
#define HVM_XS_BIOS                    "hvmloader/bios"
#define HVM_XS_GENERATION_ID_ADDRESS   "hvmloader/generation-id-address"
#define HVM_XS_ALLOW_MEMORY_RELOCATE   "hvmloader/allow-memory-relocate"

/* The following values allow additional ACPI tables to be added to the
 * virtual ACPI BIOS that hvmloader constructs. The values specify the guest
 * physical address and length of a block of ACPI tables to add. The format of
 * the block is simply concatenated raw tables (which specify their own length
 * in the ACPI header).
 */
#define HVM_XS_ACPI_PT_ADDRESS         "hvmloader/acpi/address"
#define HVM_XS_ACPI_PT_LENGTH          "hvmloader/acpi/length"

/* Any number of SMBIOS types can be passed through to an HVM guest using
 * the following xenstore values. The values specify the guest physical
 * address and length of a block of SMBIOS structures for hvmloader to use.
 * The block is formatted in the following way:
 *
 * <length><struct><length><struct>...
 *
 * Each length separator is a 32b integer indicating the length of the next
 * SMBIOS structure. For DMTF defined types (0 - 121), the passed in struct
 * will replace the default structure in hvmloader. In addition, any
 * OEM/vendortypes (128 - 255) will all be added.
 */
#define HVM_XS_SMBIOS_PT_ADDRESS       "hvmloader/smbios/address"
#define HVM_XS_SMBIOS_PT_LENGTH        "hvmloader/smbios/length"

/* Set to 1 to enable SMBIOS default portable battery (type 22) values. */
#define HVM_XS_SMBIOS_DEFAULT_BATTERY  "hvmloader/smbios/default_battery"

/* The following xenstore values are used to override some of the default
 * string values in the SMBIOS table constructed in hvmloader.
 */
#define HVM_XS_BIOS_STRINGS            "bios-strings"
#define HVM_XS_BIOS_VENDOR             "bios-strings/bios-vendor"
#define HVM_XS_BIOS_VERSION            "bios-strings/bios-version"
#define HVM_XS_SYSTEM_MANUFACTURER     "bios-strings/system-manufacturer"
#define HVM_XS_SYSTEM_PRODUCT_NAME     "bios-strings/system-product-name"
#define HVM_XS_SYSTEM_VERSION          "bios-strings/system-version"
#define HVM_XS_SYSTEM_SERIAL_NUMBER    "bios-strings/system-serial-number"
#define HVM_XS_ENCLOSURE_MANUFACTURER  "bios-strings/enclosure-manufacturer"
#define HVM_XS_ENCLOSURE_SERIAL_NUMBER "bios-strings/enclosure-serial-number"
#define HVM_XS_BATTERY_MANUFACTURER    "bios-strings/battery-manufacturer"
#define HVM_XS_BATTERY_DEVICE_NAME     "bios-strings/battery-device-name"

/* 1 to 99 OEM strings can be set in xenstore using values of the form
 * below. These strings will be loaded into the SMBIOS type 11 structure.
 */
#define HVM_XS_OEM_STRINGS             "bios-strings/oem-%d"

#endif /* __XEN_PUBLIC_HVM_HVM_XS_STRINGS_H__ */
