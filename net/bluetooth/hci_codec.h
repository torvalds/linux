/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (C) 2014 Intel Corporation */

void hci_read_supported_codecs(struct hci_dev *hdev);
void hci_read_supported_codecs_v2(struct hci_dev *hdev);
void hci_codec_list_clear(struct list_head *codec_list);
