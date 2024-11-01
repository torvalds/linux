/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>


 Array initializer for PCI card IDs

(C) Copyright AudioScience Inc. 1998-2003
*******************************************************************************/

/*NOTE: when adding new lines to this header file
  they MUST be grouped by HPI entry point.
*/

{
HPI_PCI_VENDOR_ID_TI, HPI_PCI_DEV_ID_DSP6205,
		HPI_PCI_VENDOR_ID_AUDIOSCIENCE, PCI_ANY_ID, 0, 0,
		(kernel_ulong_t) HPI_6205}
, {
HPI_PCI_VENDOR_ID_TI, HPI_PCI_DEV_ID_PCI2040,
		HPI_PCI_VENDOR_ID_AUDIOSCIENCE, PCI_ANY_ID, 0, 0,
		(kernel_ulong_t) HPI_6000}
, {
0}
