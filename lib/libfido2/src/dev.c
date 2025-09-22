/*
 * Copyright (c) 2018-2022 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "fido.h"

#ifndef TLS
#define TLS
#endif

static TLS bool disable_u2f_fallback;

#ifdef FIDO_FUZZ
static void
set_random_report_len(fido_dev_t *dev)
{
	dev->rx_len = CTAP_MIN_REPORT_LEN +
	    uniform_random(CTAP_MAX_REPORT_LEN - CTAP_MIN_REPORT_LEN + 1);
	dev->tx_len = CTAP_MIN_REPORT_LEN +
	    uniform_random(CTAP_MAX_REPORT_LEN - CTAP_MIN_REPORT_LEN + 1);
}
#endif

static void
fido_dev_set_extension_flags(fido_dev_t *dev, const fido_cbor_info_t *info)
{
	char * const	*ptr = fido_cbor_info_extensions_ptr(info);
	size_t		 len = fido_cbor_info_extensions_len(info);

	for (size_t i = 0; i < len; i++)
		if (strcmp(ptr[i], "credProtect") == 0)
			dev->flags |= FIDO_DEV_CRED_PROT;
}

static void
fido_dev_set_option_flags(fido_dev_t *dev, const fido_cbor_info_t *info)
{
	char * const	*ptr = fido_cbor_info_options_name_ptr(info);
	const bool	*val = fido_cbor_info_options_value_ptr(info);
	size_t		 len = fido_cbor_info_options_len(info);

	for (size_t i = 0; i < len; i++)
		if (strcmp(ptr[i], "clientPin") == 0) {
			dev->flags |= val[i] ?
			    FIDO_DEV_PIN_SET : FIDO_DEV_PIN_UNSET;
		} else if (strcmp(ptr[i], "credMgmt") == 0 ||
			   strcmp(ptr[i], "credentialMgmtPreview") == 0) {
			if (val[i])
				dev->flags |= FIDO_DEV_CREDMAN;
		} else if (strcmp(ptr[i], "uv") == 0) {
			dev->flags |= val[i] ?
			    FIDO_DEV_UV_SET : FIDO_DEV_UV_UNSET;
		} else if (strcmp(ptr[i], "pinUvAuthToken") == 0) {
			if (val[i])
				dev->flags |= FIDO_DEV_TOKEN_PERMS;
		}
}

static void
fido_dev_set_protocol_flags(fido_dev_t *dev, const fido_cbor_info_t *info)
{
	const uint8_t	*ptr = fido_cbor_info_protocols_ptr(info);
	size_t		 len = fido_cbor_info_protocols_len(info);

	for (size_t i = 0; i < len; i++)
		switch (ptr[i]) {
		case CTAP_PIN_PROTOCOL1:
			dev->flags |= FIDO_DEV_PIN_PROTOCOL1;
			break;
		case CTAP_PIN_PROTOCOL2:
			dev->flags |= FIDO_DEV_PIN_PROTOCOL2;
			break;
		default:
			fido_log_debug("%s: unknown protocol %u", __func__,
			    ptr[i]);
			break;
		}
}

static void
fido_dev_set_flags(fido_dev_t *dev, const fido_cbor_info_t *info)
{
	fido_dev_set_extension_flags(dev, info);
	fido_dev_set_option_flags(dev, info);
	fido_dev_set_protocol_flags(dev, info);
}

static int
fido_dev_open_tx(fido_dev_t *dev, const char *path, int *ms)
{
	int r;

	if (dev->io_handle != NULL) {
		fido_log_debug("%s: handle=%p", __func__, dev->io_handle);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	if (dev->io.open == NULL || dev->io.close == NULL) {
		fido_log_debug("%s: NULL open/close", __func__);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	if (dev->cid != CTAP_CID_BROADCAST) {
		fido_log_debug("%s: cid=0x%x", __func__, dev->cid);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	if (fido_get_random(&dev->nonce, sizeof(dev->nonce)) < 0) {
		fido_log_debug("%s: fido_get_random", __func__);
		return (FIDO_ERR_INTERNAL);
	}

	if ((dev->io_handle = dev->io.open(path)) == NULL) {
		fido_log_debug("%s: dev->io.open", __func__);
		return (FIDO_ERR_INTERNAL);
	}

	if (dev->io_own) {
		dev->rx_len = CTAP_MAX_REPORT_LEN;
		dev->tx_len = CTAP_MAX_REPORT_LEN;
	} else {
		dev->rx_len = fido_hid_report_in_len(dev->io_handle);
		dev->tx_len = fido_hid_report_out_len(dev->io_handle);
	}

#ifdef FIDO_FUZZ
	set_random_report_len(dev);
#endif

	if (dev->rx_len < CTAP_MIN_REPORT_LEN ||
	    dev->rx_len > CTAP_MAX_REPORT_LEN) {
		fido_log_debug("%s: invalid rx_len %zu", __func__, dev->rx_len);
		r = FIDO_ERR_RX;
		goto fail;
	}

	if (dev->tx_len < CTAP_MIN_REPORT_LEN ||
	    dev->tx_len > CTAP_MAX_REPORT_LEN) {
		fido_log_debug("%s: invalid tx_len %zu", __func__, dev->tx_len);
		r = FIDO_ERR_TX;
		goto fail;
	}

	if (fido_tx(dev, CTAP_CMD_INIT, &dev->nonce, sizeof(dev->nonce),
	    ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	return (FIDO_OK);
fail:
	dev->io.close(dev->io_handle);
	dev->io_handle = NULL;

	return (r);
}

static int
fido_dev_open_rx(fido_dev_t *dev, int *ms)
{
	fido_cbor_info_t	*info = NULL;
	int			 reply_len;
	int			 r;

	if ((reply_len = fido_rx(dev, CTAP_CMD_INIT, &dev->attr,
	    sizeof(dev->attr), ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		r = FIDO_ERR_RX;
		goto fail;
	}

#ifdef FIDO_FUZZ
	dev->attr.nonce = dev->nonce;
#endif

	if ((size_t)reply_len != sizeof(dev->attr) ||
	    dev->attr.nonce != dev->nonce) {
		fido_log_debug("%s: invalid nonce", __func__);
		r = FIDO_ERR_RX;
		goto fail;
	}

	dev->flags = 0;
	dev->cid = dev->attr.cid;

	if (fido_dev_is_fido2(dev)) {
		if ((info = fido_cbor_info_new()) == NULL) {
			fido_log_debug("%s: fido_cbor_info_new", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}
		if ((r = fido_dev_get_cbor_info_wait(dev, info,
		    ms)) != FIDO_OK) {
			fido_log_debug("%s: fido_dev_cbor_info_wait: %d",
			    __func__, r);
			if (disable_u2f_fallback)
				goto fail;
			fido_log_debug("%s: falling back to u2f", __func__);
			fido_dev_force_u2f(dev);
		} else {
			fido_dev_set_flags(dev, info);
		}
	}

	if (fido_dev_is_fido2(dev) && info != NULL) {
		dev->maxmsgsize = fido_cbor_info_maxmsgsiz(info);
		fido_log_debug("%s: FIDO_MAXMSG=%d, maxmsgsiz=%lu", __func__,
		    FIDO_MAXMSG, (unsigned long)dev->maxmsgsize);
	}

	r = FIDO_OK;
fail:
	fido_cbor_info_free(&info);

	if (r != FIDO_OK) {
		dev->io.close(dev->io_handle);
		dev->io_handle = NULL;
	}

	return (r);
}

static int
fido_dev_open_wait(fido_dev_t *dev, const char *path, int *ms)
{
	int r;

#ifdef USE_WINHELLO
	if (strcmp(path, FIDO_WINHELLO_PATH) == 0)
		return (fido_winhello_open(dev));
#endif
	if ((r = fido_dev_open_tx(dev, path, ms)) != FIDO_OK ||
	    (r = fido_dev_open_rx(dev, ms)) != FIDO_OK)
		return (r);

	return (FIDO_OK);
}

static void
run_manifest(fido_dev_info_t *devlist, size_t ilen, size_t *olen,
    const char *type, int (*manifest)(fido_dev_info_t *, size_t, size_t *))
{
	size_t ndevs = 0;
	int r;

	if (*olen >= ilen) {
		fido_log_debug("%s: skipping %s", __func__, type);
		return;
	}
	if ((r = manifest(devlist + *olen, ilen - *olen, &ndevs)) != FIDO_OK)
		fido_log_debug("%s: %s: 0x%x", __func__, type, r);
	fido_log_debug("%s: found %zu %s device%s", __func__, ndevs, type,
	    ndevs == 1 ? "" : "s");
	*olen += ndevs;
}

int
fido_dev_info_manifest(fido_dev_info_t *devlist, size_t ilen, size_t *olen)
{
	*olen = 0;

	run_manifest(devlist, ilen, olen, "hid", fido_hid_manifest);
#ifdef USE_NFC
	run_manifest(devlist, ilen, olen, "nfc", fido_nfc_manifest);
#endif
#ifdef USE_PCSC
	run_manifest(devlist, ilen, olen, "pcsc", fido_pcsc_manifest);
#endif
#ifdef USE_WINHELLO
	run_manifest(devlist, ilen, olen, "winhello", fido_winhello_manifest);
#endif

	return (FIDO_OK);
}

int
fido_dev_open_with_info(fido_dev_t *dev)
{
	int ms = dev->timeout_ms;

	if (dev->path == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (fido_dev_open_wait(dev, dev->path, &ms));
}

int
fido_dev_open(fido_dev_t *dev, const char *path)
{
	int ms = dev->timeout_ms;

#ifdef USE_NFC
	if (fido_is_nfc(path) && fido_dev_set_nfc(dev) < 0) {
		fido_log_debug("%s: fido_dev_set_nfc", __func__);
		return FIDO_ERR_INTERNAL;
	}
#endif
#ifdef USE_PCSC
	if (fido_is_pcsc(path) && fido_dev_set_pcsc(dev) < 0) {
		fido_log_debug("%s: fido_dev_set_pcsc", __func__);
		return FIDO_ERR_INTERNAL;
	}
#endif

	return (fido_dev_open_wait(dev, path, &ms));
}

int
fido_dev_close(fido_dev_t *dev)
{
#ifdef USE_WINHELLO
	if (dev->flags & FIDO_DEV_WINHELLO)
		return (fido_winhello_close(dev));
#endif
	if (dev->io_handle == NULL || dev->io.close == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	dev->io.close(dev->io_handle);
	dev->io_handle = NULL;
	dev->cid = CTAP_CID_BROADCAST;

	return (FIDO_OK);
}

int
fido_dev_set_sigmask(fido_dev_t *dev, const fido_sigset_t *sigmask)
{
	if (dev->io_handle == NULL || sigmask == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

#ifdef USE_NFC
	if (dev->transport.rx == fido_nfc_rx && dev->io.read == fido_nfc_read)
		return (fido_nfc_set_sigmask(dev->io_handle, sigmask));
#endif
	if (dev->transport.rx == NULL && dev->io.read == fido_hid_read)
		return (fido_hid_set_sigmask(dev->io_handle, sigmask));

	return (FIDO_ERR_INVALID_ARGUMENT);
}

int
fido_dev_cancel(fido_dev_t *dev)
{
	int ms = dev->timeout_ms;

#ifdef USE_WINHELLO
	if (dev->flags & FIDO_DEV_WINHELLO)
		return (fido_winhello_cancel(dev));
#endif
	if (fido_dev_is_fido2(dev) == false)
		return (FIDO_ERR_INVALID_ARGUMENT);
	if (fido_tx(dev, CTAP_CMD_CANCEL, NULL, 0, &ms) < 0)
		return (FIDO_ERR_TX);

	return (FIDO_OK);
}

int
fido_dev_set_io_functions(fido_dev_t *dev, const fido_dev_io_t *io)
{
	if (dev->io_handle != NULL) {
		fido_log_debug("%s: non-NULL handle", __func__);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	if (io == NULL || io->open == NULL || io->close == NULL ||
	    io->read == NULL || io->write == NULL) {
		fido_log_debug("%s: NULL function", __func__);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	dev->io = *io;
	dev->io_own = true;

	return (FIDO_OK);
}

int
fido_dev_set_transport_functions(fido_dev_t *dev, const fido_dev_transport_t *t)
{
	if (dev->io_handle != NULL) {
		fido_log_debug("%s: non-NULL handle", __func__);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	dev->transport = *t;
	dev->io_own = true;

	return (FIDO_OK);
}

void *
fido_dev_io_handle(const fido_dev_t *dev)
{

	return (dev->io_handle);
}

void
fido_init(int flags)
{
	if (flags & FIDO_DEBUG || getenv("FIDO_DEBUG") != NULL)
		fido_log_init();

	disable_u2f_fallback = (flags & FIDO_DISABLE_U2F_FALLBACK);
}

fido_dev_t *
fido_dev_new(void)
{
	fido_dev_t *dev;

	if ((dev = calloc(1, sizeof(*dev))) == NULL)
		return (NULL);

	dev->cid = CTAP_CID_BROADCAST;
	dev->timeout_ms = -1;
	dev->io = (fido_dev_io_t) {
		&fido_hid_open,
		&fido_hid_close,
		&fido_hid_read,
		&fido_hid_write,
	};

	return (dev);
}

fido_dev_t *
fido_dev_new_with_info(const fido_dev_info_t *di)
{
	fido_dev_t *dev;

	if ((dev = calloc(1, sizeof(*dev))) == NULL)
		return (NULL);

#if 0
	if (di->io.open == NULL || di->io.close == NULL ||
	    di->io.read == NULL || di->io.write == NULL) {
		fido_log_debug("%s: NULL function", __func__);
		fido_dev_free(&dev);
		return (NULL);
	}
#endif

	dev->io = di->io;
	dev->io_own = di->transport.tx != NULL || di->transport.rx != NULL;
	dev->transport = di->transport;
	dev->cid = CTAP_CID_BROADCAST;
	dev->timeout_ms = -1;

	if ((dev->path = strdup(di->path)) == NULL) {
		fido_log_debug("%s: strdup", __func__);
		fido_dev_free(&dev);
		return (NULL);
	}

	return (dev);
}

void
fido_dev_free(fido_dev_t **dev_p)
{
	fido_dev_t *dev;

	if (dev_p == NULL || (dev = *dev_p) == NULL)
		return;

	free(dev->path);
	free(dev);

	*dev_p = NULL;
}

uint8_t
fido_dev_protocol(const fido_dev_t *dev)
{
	return (dev->attr.protocol);
}

uint8_t
fido_dev_major(const fido_dev_t *dev)
{
	return (dev->attr.major);
}

uint8_t
fido_dev_minor(const fido_dev_t *dev)
{
	return (dev->attr.minor);
}

uint8_t
fido_dev_build(const fido_dev_t *dev)
{
	return (dev->attr.build);
}

uint8_t
fido_dev_flags(const fido_dev_t *dev)
{
	return (dev->attr.flags);
}

bool
fido_dev_is_fido2(const fido_dev_t *dev)
{
	return (dev->attr.flags & FIDO_CAP_CBOR);
}

bool
fido_dev_is_winhello(const fido_dev_t *dev)
{
	return (dev->flags & FIDO_DEV_WINHELLO);
}

bool
fido_dev_supports_pin(const fido_dev_t *dev)
{
	return (dev->flags & (FIDO_DEV_PIN_SET|FIDO_DEV_PIN_UNSET));
}

bool
fido_dev_has_pin(const fido_dev_t *dev)
{
	return (dev->flags & FIDO_DEV_PIN_SET);
}

bool
fido_dev_supports_cred_prot(const fido_dev_t *dev)
{
	return (dev->flags & FIDO_DEV_CRED_PROT);
}

bool
fido_dev_supports_credman(const fido_dev_t *dev)
{
	return (dev->flags & FIDO_DEV_CREDMAN);
}

bool
fido_dev_supports_uv(const fido_dev_t *dev)
{
	return (dev->flags & (FIDO_DEV_UV_SET|FIDO_DEV_UV_UNSET));
}

bool
fido_dev_has_uv(const fido_dev_t *dev)
{
	return (dev->flags & FIDO_DEV_UV_SET);
}

bool
fido_dev_supports_permissions(const fido_dev_t *dev)
{
	return (dev->flags & FIDO_DEV_TOKEN_PERMS);
}

void
fido_dev_force_u2f(fido_dev_t *dev)
{
	dev->attr.flags &= (uint8_t)~FIDO_CAP_CBOR;
	dev->flags = 0;
}

void
fido_dev_force_fido2(fido_dev_t *dev)
{
	dev->attr.flags |= FIDO_CAP_CBOR;
}

uint8_t
fido_dev_get_pin_protocol(const fido_dev_t *dev)
{
	if (dev->flags & FIDO_DEV_PIN_PROTOCOL2)
		return (CTAP_PIN_PROTOCOL2);
	else if (dev->flags & FIDO_DEV_PIN_PROTOCOL1)
		return (CTAP_PIN_PROTOCOL1);

	return (0);
}

uint64_t
fido_dev_maxmsgsize(const fido_dev_t *dev)
{
	return (dev->maxmsgsize);
}

int
fido_dev_set_timeout(fido_dev_t *dev, int ms)
{
	if (ms < -1)
		return (FIDO_ERR_INVALID_ARGUMENT);

	dev->timeout_ms = ms;

	return (FIDO_OK);
}
