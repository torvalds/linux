/*
   Copyright (c) 2011,2012 Intel Corp.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 and
   only version 2 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#ifndef __AMP_H
#define __AMP_H

void amp_read_loc_info(struct hci_dev *hdev, struct amp_mgr *mgr);
void amp_read_loc_assoc_frag(struct hci_dev *hdev, u8 phy_handle);
void amp_read_loc_assoc(struct hci_dev *hdev, struct amp_mgr *mgr);

#endif /* __AMP_H */
