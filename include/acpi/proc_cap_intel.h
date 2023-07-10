/* SPDX-License-Identifier: GPL-2.0 */

/* Vendor specific processor capabilities bit definition
 * for Intel processors. Those bits are used to convey OSPM
 * power management capabilities to the platform.
 */

#ifndef __PROC_CAP_INTEL_H__
#define __PROC_CAP_INTEL_H__

#define ACPI_PROC_CAP_P_FFH			(0x0001)
#define ACPI_PROC_CAP_C_C1_HALT			(0x0002)
#define ACPI_PROC_CAP_T_FFH			(0x0004)
#define ACPI_PROC_CAP_SMP_C1PT			(0x0008)
#define ACPI_PROC_CAP_SMP_C2C3			(0x0010)
#define ACPI_PROC_CAP_SMP_P_SWCOORD		(0x0020)
#define ACPI_PROC_CAP_SMP_C_SWCOORD		(0x0040)
#define ACPI_PROC_CAP_SMP_T_SWCOORD		(0x0080)
#define ACPI_PROC_CAP_C_C1_FFH			(0x0100)
#define ACPI_PROC_CAP_C_C2C3_FFH		(0x0200)
#define ACPI_PROC_CAP_SMP_P_HWCOORD		(0x0800)

#define ACPI_PROC_CAP_EST_CAPABILITY_SMP	(ACPI_PROC_CAP_SMP_C1PT | \
						 ACPI_PROC_CAP_C_C1_HALT | \
						 ACPI_PROC_CAP_P_FFH)

#define ACPI_PROC_CAP_EST_CAPABILITY_SWSMP	(ACPI_PROC_CAP_SMP_C1PT | \
						 ACPI_PROC_CAP_C_C1_HALT | \
						 ACPI_PROC_CAP_SMP_P_SWCOORD | \
						 ACPI_PROC_CAP_SMP_P_HWCOORD | \
						 ACPI_PROC_CAP_P_FFH)

#define ACPI_PROC_CAP_C_CAPABILITY_SMP		(ACPI_PROC_CAP_SMP_C2C3  | \
						 ACPI_PROC_CAP_SMP_C1PT  | \
						 ACPI_PROC_CAP_C_C1_HALT | \
						 ACPI_PROC_CAP_C_C1_FFH  | \
						 ACPI_PROC_CAP_C_C2C3_FFH)

#endif /* __PROC_CAP_INTEL_H__ */
