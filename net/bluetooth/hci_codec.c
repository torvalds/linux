// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2021 Intel Corporation */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "hci_codec.h"

static int hci_codec_list_add(struct list_head *list,
			      struct hci_op_read_local_codec_caps *sent,
			      struct hci_rp_read_local_codec_caps *rp,
			      void *caps,
			      __u32 len)
{
	struct codec_list *entry;

	entry = kzalloc(sizeof(*entry) + len, GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->id = sent->id;
	if (sent->id == 0xFF) {
		entry->cid = __le16_to_cpu(sent->cid);
		entry->vid = __le16_to_cpu(sent->vid);
	}
	entry->transport = sent->transport;
	entry->len = len;
	entry->num_caps = rp->num_caps;
	if (rp->num_caps)
		memcpy(entry->caps, caps, len);
	list_add(&entry->list, list);

	return 0;
}

void hci_codec_list_clear(struct list_head *codec_list)
{
	struct codec_list *c, *n;

	list_for_each_entry_safe(c, n, codec_list, list) {
		list_del(&c->list);
		kfree(c);
	}
}

static void hci_read_codec_capabilities(struct hci_dev *hdev, __u8 transport,
					struct hci_op_read_local_codec_caps
					*cmd)
{
	__u8 i;

	for (i = 0; i < TRANSPORT_TYPE_MAX; i++) {
		if (transport & BIT(i)) {
			struct hci_rp_read_local_codec_caps *rp;
			struct hci_codec_caps *caps;
			struct sk_buff *skb;
			__u8 j;
			__u32 len;

			cmd->transport = i;
			skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_CODEC_CAPS,
					     sizeof(*cmd), cmd,
					     HCI_CMD_TIMEOUT);
			if (IS_ERR(skb)) {
				bt_dev_err(hdev, "Failed to read codec capabilities (%ld)",
					   PTR_ERR(skb));
				continue;
			}

			if (skb->len < sizeof(*rp))
				goto error;

			rp = (void *)skb->data;

			if (rp->status)
				goto error;

			if (!rp->num_caps) {
				len = 0;
				/* this codec doesn't have capabilities */
				goto skip_caps_parse;
			}

			skb_pull(skb, sizeof(*rp));

			for (j = 0, len = 0; j < rp->num_caps; j++) {
				caps = (void *)skb->data;
				if (skb->len < sizeof(*caps))
					goto error;
				if (skb->len < caps->len)
					goto error;
				len += sizeof(caps->len) + caps->len;
				skb_pull(skb,  sizeof(caps->len) + caps->len);
			}

skip_caps_parse:
			hci_dev_lock(hdev);
			hci_codec_list_add(&hdev->local_codecs, cmd, rp,
					   (__u8 *)rp + sizeof(*rp), len);
			hci_dev_unlock(hdev);
error:
			kfree_skb(skb);
		}
	}
}

void hci_read_supported_codecs(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct hci_rp_read_local_supported_codecs *rp;
	struct hci_std_codecs *std_codecs;
	struct hci_vnd_codecs *vnd_codecs;
	struct hci_op_read_local_codec_caps caps;
	__u8 i;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_CODECS, 0, NULL,
			     HCI_CMD_TIMEOUT);

	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Failed to read local supported codecs (%ld)",
			   PTR_ERR(skb));
		return;
	}

	if (skb->len < sizeof(*rp))
		goto error;

	rp = (void *)skb->data;

	if (rp->status)
		goto error;

	skb_pull(skb, sizeof(rp->status));

	std_codecs = (void *)skb->data;

	/* validate codecs length before accessing */
	if (skb->len < flex_array_size(std_codecs, codec, std_codecs->num)
	    + sizeof(std_codecs->num))
		goto error;

	/* enumerate codec capabilities of standard codecs */
	memset(&caps, 0, sizeof(caps));
	for (i = 0; i < std_codecs->num; i++) {
		caps.id = std_codecs->codec[i];
		caps.direction = 0x00;
		hci_read_codec_capabilities(hdev, LOCAL_CODEC_ACL_MASK, &caps);
	}

	skb_pull(skb, flex_array_size(std_codecs, codec, std_codecs->num)
		 + sizeof(std_codecs->num));

	vnd_codecs = (void *)skb->data;

	/* validate vendor codecs length before accessing */
	if (skb->len <
	    flex_array_size(vnd_codecs, codec, vnd_codecs->num)
	    + sizeof(vnd_codecs->num))
		goto error;

	/* enumerate vendor codec capabilities */
	for (i = 0; i < vnd_codecs->num; i++) {
		caps.id = 0xFF;
		caps.cid = vnd_codecs->codec[i].cid;
		caps.vid = vnd_codecs->codec[i].vid;
		caps.direction = 0x00;
		hci_read_codec_capabilities(hdev, LOCAL_CODEC_ACL_MASK, &caps);
	}

error:
	kfree_skb(skb);
}
