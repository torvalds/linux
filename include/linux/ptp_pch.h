/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PTP PCH
 *
 * Copyright 2019 Linaro Ltd.
 *
 * Author Lee Jones <lee.jones@linaro.org>
 */

#ifndef _PTP_PCH_H_
#define _PTP_PCH_H_

void pch_ch_control_write(struct pci_dev *pdev, u32 val);
u32  pch_ch_event_read(struct pci_dev *pdev);
void pch_ch_event_write(struct pci_dev *pdev, u32 val);
u32  pch_src_uuid_lo_read(struct pci_dev *pdev);
u32  pch_src_uuid_hi_read(struct pci_dev *pdev);
u64  pch_rx_snap_read(struct pci_dev *pdev);
u64  pch_tx_snap_read(struct pci_dev *pdev);
int  pch_set_station_address(u8 *addr, struct pci_dev *pdev);

#endif /* _PTP_PCH_H_ */
