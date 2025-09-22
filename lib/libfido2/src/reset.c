/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "fido.h"

static int
fido_dev_reset_tx(fido_dev_t *dev, int *ms)
{
	const unsigned char cbor[] = { CTAP_CBOR_RESET };

	if (fido_tx(dev, CTAP_CMD_CBOR, cbor, sizeof(cbor), ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		return (FIDO_ERR_TX);
	}

	return (FIDO_OK);
}

static int
fido_dev_reset_wait(fido_dev_t *dev, int *ms)
{
	int r;

	if ((r = fido_dev_reset_tx(dev, ms)) != FIDO_OK ||
	    (r = fido_rx_cbor_status(dev, ms)) != FIDO_OK)
		return (r);

	if (dev->flags & FIDO_DEV_PIN_SET) {
		dev->flags &= ~FIDO_DEV_PIN_SET;
		dev->flags |= FIDO_DEV_PIN_UNSET;
	}

	return (FIDO_OK);
}

int
fido_dev_reset(fido_dev_t *dev)
{
	int ms = dev->timeout_ms;

	return (fido_dev_reset_wait(dev, &ms));
}
