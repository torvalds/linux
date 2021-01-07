/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acuuid.h - ACPI-related UUID/GUID definitions
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACUUID_H__
#define __ACUUID_H__

/*
 * Note1: UUIDs and GUIDs are defined to be identical in ACPI.
 *
 * Note2: This file is standalone and should remain that way.
 */

/* Controllers */

#define UUID_GPIO_CONTROLLER            "4f248f40-d5e2-499f-834c-27758ea1cd3f"
#define UUID_USB_CONTROLLER             "ce2ee385-00e6-48cb-9f05-2edb927c4899"
#define UUID_SATA_CONTROLLER            "e4db149b-fcfe-425b-a6d8-92357d78fc7f"

/* Devices */

#define UUID_PCI_HOST_BRIDGE            "33db4d5b-1ff7-401c-9657-7441c03dd766"
#define UUID_I2C_DEVICE                 "3cdff6f7-4267-4555-ad05-b30a3d8938de"
#define UUID_POWER_BUTTON               "dfbcf3c5-e7a5-44e6-9c1f-29c76f6e059c"
#define UUID_MEMORY_DEVICE              "03b19910-f473-11dd-87af-0800200c9a66"
#define UUID_GENERIC_BUTTONS_DEVICE     "fa6bd625-9ce8-470d-a2c7-b3ca36c4282e"
#define UUID_NVDIMM_ROOT_DEVICE         "2f10e7a4-9e91-11e4-89d3-123b93f75cba"
#define UUID_CONTROL_METHOD_BATTERY     "f18fc78b-0f15-4978-b793-53f833a1d35b"

/* Interfaces */

#define UUID_DEVICE_LABELING            "e5c937d0-3553-4d7a-9117-ea4d19c3434d"
#define UUID_PHYSICAL_PRESENCE          "3dddfaa6-361b-4eb4-a424-8d10089d1653"

/* NVDIMM - NFIT table */

#define UUID_NFIT_DIMM                  "4309ac30-0d11-11e4-9191-0800200c9a66"
#define UUID_VOLATILE_MEMORY            "7305944f-fdda-44e3-b16c-3f22d252e5d0"
#define UUID_PERSISTENT_MEMORY          "66f0d379-b4f3-4074-ac43-0d3318b78cdb"
#define UUID_CONTROL_REGION             "92f701f6-13b4-405d-910b-299367e8234c"
#define UUID_DATA_REGION                "91af0530-5d86-470e-a6b0-0a2db9408249"
#define UUID_VOLATILE_VIRTUAL_DISK      "77ab535a-45fc-624b-5560-f7b281d1f96e"
#define UUID_VOLATILE_VIRTUAL_CD        "3d5abd30-4175-87ce-6d64-d2ade523c4bb"
#define UUID_PERSISTENT_VIRTUAL_DISK    "5cea02c9-4d07-69d3-269f-4496fbe096f9"
#define UUID_PERSISTENT_VIRTUAL_CD      "08018188-42cd-bb48-100f-5387d53ded3d"
#define UUID_NFIT_DIMM_N_MSFT           "1ee68b36-d4bd-4a1a-9a16-4f8e53d46e05"
#define UUID_NFIT_DIMM_N_HPE1           "9002c334-acf3-4c0e-9642-a235f0d53bc6"
#define UUID_NFIT_DIMM_N_HPE2           "5008664b-b758-41a0-a03c-27c2f2d04f7e"
#define UUID_NFIT_DIMM_N_HYPERV         "5746c5f2-a9a2-4264-ad0e-e4ddc9e09e80"

/* Processor Properties (ACPI 6.2) */

#define UUID_CACHE_PROPERTIES           "6DC63E77-257E-4E78-A973-A21F2796898D"
#define UUID_PHYSICAL_PROPERTY          "DDE4D59A-AA42-4349-B407-EA40F57D9FB7"

/* Miscellaneous */

#define UUID_PLATFORM_CAPABILITIES      "0811b06e-4a27-44f9-8d60-3cbbc22e7b48"
#define UUID_DYNAMIC_ENUMERATION        "d8c1a3a6-be9b-4c9b-91bf-c3cb81fc5daf"
#define UUID_BATTERY_THERMAL_LIMIT      "4c2067e3-887d-475c-9720-4af1d3ed602e"
#define UUID_THERMAL_EXTENSIONS         "14d399cd-7a27-4b18-8fb4-7cb7b9f4e500"
#define UUID_DEVICE_PROPERTIES          "daffd814-6eba-4d8c-8a91-bc9bbf4aa301"
#define UUID_DEVICE_GRAPHS              "ab02a46b-74c7-45a2-bd68-f7d344ef2153"
#define UUID_HIERARCHICAL_DATA_EXTENSION "dbb8e3e6-5886-4ba6-8795-1319f52a966b"
#define UUID_CORESIGHT_GRAPH            "3ecbc8b6-1d0e-4fb3-8107-e627f805c6cd"

#endif				/* __ACUUID_H__ */
