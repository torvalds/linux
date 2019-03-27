/*-
 * Copyright (c) 2001 Doug Rabson
 * Copyright (c) 2002, 2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>

#include <stand.h>
#include <net.h>
#include <netif.h>

#include <efi.h>
#include <efilib.h>

static EFI_GUID sn_guid = EFI_SIMPLE_NETWORK_PROTOCOL;

static void efinet_end(struct netif *);
static ssize_t efinet_get(struct iodesc *, void **, time_t);
static void efinet_init(struct iodesc *, void *);
static int efinet_match(struct netif *, void *);
static int efinet_probe(struct netif *, void *);
static ssize_t efinet_put(struct iodesc *, void *, size_t);

struct netif_driver efinetif = {   
	.netif_bname = "efinet",
	.netif_match = efinet_match,
	.netif_probe = efinet_probe,
	.netif_init = efinet_init,
	.netif_get = efinet_get,
	.netif_put = efinet_put,
	.netif_end = efinet_end,
	.netif_ifs = NULL,
	.netif_nifs = 0
};

#ifdef EFINET_DEBUG
static void
dump_mode(EFI_SIMPLE_NETWORK_MODE *mode)
{
	int i;

	printf("State                 = %x\n", mode->State);
	printf("HwAddressSize         = %u\n", mode->HwAddressSize);
	printf("MediaHeaderSize       = %u\n", mode->MediaHeaderSize);
	printf("MaxPacketSize         = %u\n", mode->MaxPacketSize);
	printf("NvRamSize             = %u\n", mode->NvRamSize);
	printf("NvRamAccessSize       = %u\n", mode->NvRamAccessSize);
	printf("ReceiveFilterMask     = %x\n", mode->ReceiveFilterMask);
	printf("ReceiveFilterSetting  = %u\n", mode->ReceiveFilterSetting);
	printf("MaxMCastFilterCount   = %u\n", mode->MaxMCastFilterCount);
	printf("MCastFilterCount      = %u\n", mode->MCastFilterCount);
	printf("MCastFilter           = {");
	for (i = 0; i < mode->MCastFilterCount; i++)
		printf(" %s", ether_sprintf(mode->MCastFilter[i].Addr));
	printf(" }\n");
	printf("CurrentAddress        = %s\n",
	    ether_sprintf(mode->CurrentAddress.Addr));
	printf("BroadcastAddress      = %s\n",
	    ether_sprintf(mode->BroadcastAddress.Addr));
	printf("PermanentAddress      = %s\n",
	    ether_sprintf(mode->PermanentAddress.Addr));
	printf("IfType                = %u\n", mode->IfType);
	printf("MacAddressChangeable  = %d\n", mode->MacAddressChangeable);
	printf("MultipleTxSupported   = %d\n", mode->MultipleTxSupported);
	printf("MediaPresentSupported = %d\n", mode->MediaPresentSupported);
	printf("MediaPresent          = %d\n", mode->MediaPresent);
}
#endif

static int
efinet_match(struct netif *nif, void *machdep_hint)
{
	struct devdesc *dev = machdep_hint;

	if (dev->d_unit == nif->nif_unit)
		return (1);
	return(0);
}

static int
efinet_probe(struct netif *nif, void *machdep_hint)
{

	return (0);
}

static ssize_t
efinet_put(struct iodesc *desc, void *pkt, size_t len)
{
	struct netif *nif = desc->io_netif;
	EFI_SIMPLE_NETWORK *net;
	EFI_STATUS status;
	void *buf;

	net = nif->nif_devdata;
	if (net == NULL)
		return (-1);

	status = net->Transmit(net, 0, len, pkt, NULL, NULL, NULL);
	if (status != EFI_SUCCESS)
		return (-1);

	/* Wait for the buffer to be transmitted */
	do {
		buf = NULL;	/* XXX Is this needed? */
		status = net->GetStatus(net, NULL, &buf);
		/*
		 * XXX EFI1.1 and the E1000 card returns a different 
		 * address than we gave.  Sigh.
		 */
	} while (status == EFI_SUCCESS && buf == NULL);

	/* XXX How do we deal with status != EFI_SUCCESS now? */
	return ((status == EFI_SUCCESS) ? len : -1);
}

static ssize_t
efinet_get(struct iodesc *desc, void **pkt, time_t timeout)
{
	struct netif *nif = desc->io_netif;
	EFI_SIMPLE_NETWORK *net;
	EFI_STATUS status;
	UINTN bufsz;
	time_t t;
	char *buf, *ptr;
	ssize_t ret = -1;

	net = nif->nif_devdata;
	if (net == NULL)
		return (ret);

	bufsz = net->Mode->MaxPacketSize + ETHER_HDR_LEN + ETHER_CRC_LEN;
	buf = malloc(bufsz + ETHER_ALIGN);
	if (buf == NULL)
		return (ret);
	ptr = buf + ETHER_ALIGN;

	t = getsecs();
	while ((getsecs() - t) < timeout) {
		status = net->Receive(net, NULL, &bufsz, ptr, NULL, NULL, NULL);
		if (status == EFI_SUCCESS) {
			*pkt = buf;
			ret = (ssize_t)bufsz;
			break;
		}
		if (status != EFI_NOT_READY)
			break;
	}

	if (ret == -1)
		free(buf);
	return (ret);
}

static void
efinet_init(struct iodesc *desc, void *machdep_hint)
{
	struct netif *nif = desc->io_netif;
	EFI_SIMPLE_NETWORK *net;
	EFI_HANDLE h;
	EFI_STATUS status;
	UINT32 mask;

	if (nif->nif_driver->netif_ifs[nif->nif_unit].dif_unit < 0) {
		printf("Invalid network interface %d\n", nif->nif_unit);
		return;
	}

	h = nif->nif_driver->netif_ifs[nif->nif_unit].dif_private;
	status = BS->HandleProtocol(h, &sn_guid, (VOID **)&nif->nif_devdata);
	if (status != EFI_SUCCESS) {
		printf("net%d: cannot fetch interface data (status=%lu)\n",
		    nif->nif_unit, EFI_ERROR_CODE(status));
		return;
	}

	net = nif->nif_devdata;
	if (net->Mode->State == EfiSimpleNetworkStopped) {
		status = net->Start(net);
		if (status != EFI_SUCCESS) {
			printf("net%d: cannot start interface (status=%lu)\n",
			    nif->nif_unit, EFI_ERROR_CODE(status));
			return;
		}
	}

	if (net->Mode->State != EfiSimpleNetworkInitialized) {
		status = net->Initialize(net, 0, 0);
		if (status != EFI_SUCCESS) {
			printf("net%d: cannot init. interface (status=%lu)\n",
			    nif->nif_unit, EFI_ERROR_CODE(status));
			return;
		}
	}

	mask = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
	    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST;

	status = net->ReceiveFilters(net, mask, 0, FALSE, 0, NULL);
	if (status != EFI_SUCCESS)
		printf("net%d: cannot set rx. filters (status=%lu)\n",
		    nif->nif_unit, EFI_ERROR_CODE(status));

#ifdef EFINET_DEBUG
	dump_mode(net->Mode);
#endif

	bcopy(net->Mode->CurrentAddress.Addr, desc->myea, 6);
	desc->xid = 1;
}

static void
efinet_end(struct netif *nif)
{
	EFI_SIMPLE_NETWORK *net = nif->nif_devdata; 

	if (net == NULL)
		return;

	net->Shutdown(net);
}

static int efinet_dev_init(void);
static int efinet_dev_print(int);

struct devsw efinet_dev = {
	.dv_name = "net",
	.dv_type = DEVT_NET,
	.dv_init = efinet_dev_init,
	.dv_strategy = NULL,		/* Will be set in efinet_dev_init */
	.dv_open = NULL,		/* Will be set in efinet_dev_init */
	.dv_close = NULL,		/* Will be set in efinet_dev_init */
	.dv_ioctl = noioctl,
	.dv_print = efinet_dev_print,
	.dv_cleanup = NULL
};

static int
efinet_dev_init()
{
	struct netif_dif *dif;
	struct netif_stats *stats;
	EFI_DEVICE_PATH *devpath, *node;
	EFI_SIMPLE_NETWORK *net;
	EFI_HANDLE *handles, *handles2;
	EFI_STATUS status;
	UINTN sz;
	int err, i, nifs;
	extern struct devsw netdev;

	sz = 0;
	handles = NULL;
	status = BS->LocateHandle(ByProtocol, &sn_guid, NULL, &sz, NULL);
	if (status == EFI_BUFFER_TOO_SMALL) {
		handles = (EFI_HANDLE *)malloc(sz);
		status = BS->LocateHandle(ByProtocol, &sn_guid, NULL, &sz,
		    handles);
		if (EFI_ERROR(status))
			free(handles);
	}
	if (EFI_ERROR(status))
		return (efi_status_to_errno(status));
	handles2 = (EFI_HANDLE *)malloc(sz);
	if (handles2 == NULL) {
		free(handles);
		return (ENOMEM);
	}
	nifs = 0;
	for (i = 0; i < sz / sizeof(EFI_HANDLE); i++) {
		devpath = efi_lookup_devpath(handles[i]);
		if (devpath == NULL)
			continue;
		if ((node = efi_devpath_last_node(devpath)) == NULL)
			continue;

		if (DevicePathType(node) != MESSAGING_DEVICE_PATH ||
		    DevicePathSubType(node) != MSG_MAC_ADDR_DP)
			continue;

		/*
		 * Open the network device in exclusive mode. Without this
		 * we will be racing with the UEFI network stack. It will
		 * pull packets off the network leading to lost packets.
		 */
		status = BS->OpenProtocol(handles[i], &sn_guid, (void **)&net,
		    IH, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE);
		if (status != EFI_SUCCESS) {
			printf("Unable to open network interface %d for "
			    "exclusive access: %lu\n", i,
			    EFI_ERROR_CODE(status));
		}

		handles2[nifs] = handles[i];
		nifs++;
	}
	free(handles);
	if (nifs == 0) {
		err = ENOENT;
		goto done;
	}

	err = efi_register_handles(&efinet_dev, handles2, NULL, nifs);
	if (err != 0)
		goto done;

	efinetif.netif_ifs = calloc(nifs, sizeof(struct netif_dif));
	stats = calloc(nifs, sizeof(struct netif_stats));
	if (efinetif.netif_ifs == NULL || stats == NULL) {
		free(efinetif.netif_ifs);
		free(stats);
		efinetif.netif_ifs = NULL;
		err = ENOMEM;
		goto done;
	}
	efinetif.netif_nifs = nifs;

	for (i = 0; i < nifs; i++) {

		dif = &efinetif.netif_ifs[i];
		dif->dif_unit = i;
		dif->dif_nsel = 1;
		dif->dif_stats = &stats[i];
		dif->dif_private = handles2[i];
	}

	efinet_dev.dv_open = netdev.dv_open;
	efinet_dev.dv_close = netdev.dv_close;
	efinet_dev.dv_strategy = netdev.dv_strategy;

done:
	free(handles2);
	return (err);
}

static int
efinet_dev_print(int verbose)
{
	CHAR16 *text;
	EFI_HANDLE h;
	int unit, ret = 0;

	printf("%s devices:", efinet_dev.dv_name);
	if ((ret = pager_output("\n")) != 0)
		return (ret);

	for (unit = 0, h = efi_find_handle(&efinet_dev, 0);
	    h != NULL; h = efi_find_handle(&efinet_dev, ++unit)) {
		printf("    %s%d:", efinet_dev.dv_name, unit);
		if (verbose) {
			text = efi_devpath_name(efi_lookup_devpath(h));
			if (text != NULL) {
				printf("    %S", text);
				efi_free_devpath_name(text);
			}
		}
		if ((ret = pager_output("\n")) != 0)
			break;
	}
	return (ret);
}
