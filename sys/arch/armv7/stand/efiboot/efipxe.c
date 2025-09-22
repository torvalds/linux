/*	$OpenBSD: efipxe.c,v 1.9 2021/12/11 20:11:17 naddy Exp $	*/
/*
 * Copyright (c) 2017 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <libsa.h>
#include <lib/libsa/tftp.h>
#include <lib/libsa/net.h>
#include <lib/libsa/netif.h>

#include <efi.h>
#include <efiapi.h>
#include "efiboot.h"
#include "disk.h"

extern EFI_BOOT_SERVICES	*BS;
extern EFI_DEVICE_PATH		*efi_bootdp;

extern char			*bootmac;
static UINT8			 boothw[16];
struct in_addr			 bootip, servip;
extern struct in_addr		 gateip;
static EFI_GUID			 devp_guid = DEVICE_PATH_PROTOCOL;
static EFI_GUID			 net_guid = EFI_SIMPLE_NETWORK_PROTOCOL;
static EFI_GUID			 pxe_guid = EFI_PXE_BASE_CODE_PROTOCOL;
static EFI_SIMPLE_NETWORK	*NET = NULL;
static EFI_PXE_BASE_CODE	*PXE = NULL;
static EFI_PHYSICAL_ADDRESS	 txbuf;
static int			 use_mtftp = 0;

extern int	 efi_device_path_depth(EFI_DEVICE_PATH *dp, int);
extern int	 efi_device_path_ncmp(EFI_DEVICE_PATH *, EFI_DEVICE_PATH *, int);

int		 efinet_probe(struct netif *, void *);
int		 efinet_match(struct netif *, void *);
void		 efinet_init(struct iodesc *, void *);
int		 efinet_get(struct iodesc *, void *, size_t, time_t);
int		 efinet_put(struct iodesc *, void *, size_t);
void		 efinet_end(struct netif *);

/*
 * TFTP initial probe.  This function discovers PXE handles and tries
 * to figure out if there has already been a successful PXE handshake.
 * If so, set the PXE variable.
 */
void
efi_pxeprobe(void)
{
	EFI_SIMPLE_NETWORK *net;
	EFI_PXE_BASE_CODE *pxe;
	EFI_DEVICE_PATH *dp0;
	EFI_HANDLE *handles;
	EFI_STATUS status;
	UINTN nhandles;
	int i, depth;

	if (efi_bootdp == NULL)
		return;

	status = BS->LocateHandleBuffer(ByProtocol, &pxe_guid, NULL, &nhandles,
	    &handles);
	if (status != EFI_SUCCESS)
		return;

	for (i = 0; i < nhandles; i++) {
		EFI_PXE_BASE_CODE_DHCPV4_PACKET *dhcp;

		status = BS->HandleProtocol(handles[i], &devp_guid,
		    (void **)&dp0);
		if (status != EFI_SUCCESS)
			continue;

		depth = efi_device_path_depth(efi_bootdp, MESSAGING_DEVICE_PATH);
		if (depth == -1 || efi_device_path_ncmp(efi_bootdp, dp0, depth))
			continue;

		status = BS->HandleProtocol(handles[i], &net_guid,
		    (void **)&net);
		if (status != EFI_SUCCESS)
			continue;

		status = BS->HandleProtocol(handles[i], &pxe_guid,
		    (void **)&pxe);
		if (status != EFI_SUCCESS)
			continue;

		if (pxe->Mode == NULL)
			continue;

		if (pxe->Mtftp != NULL) {
			status = pxe->Mtftp(NULL, 0, NULL, FALSE, NULL, NULL,
			    NULL, NULL, NULL, FALSE);
			if (status != EFI_UNSUPPORTED)
				use_mtftp = 1;
		}

		dhcp = (EFI_PXE_BASE_CODE_DHCPV4_PACKET *)&pxe->Mode->DhcpAck;
		memcpy(&bootip, dhcp->BootpYiAddr, sizeof(bootip));
		memcpy(&servip, dhcp->BootpSiAddr, sizeof(servip));
		memcpy(&gateip, dhcp->BootpSiAddr, sizeof(gateip));
		memcpy(boothw, dhcp->BootpHwAddr, sizeof(boothw));
		bootmac = boothw;
		NET = net;
		PXE = pxe;
		break;
	}
}

/*
 * TFTP filesystem layer implementation.
 */
struct mtftp_handle {
	unsigned char	*inbuf;	/* input buffer */
	size_t		 inbufsize;
	off_t		 inbufoff;
};

int
mtftp_open(char *path, struct open_file *f)
{
	struct mtftp_handle *tftpfile;
	EFI_PHYSICAL_ADDRESS addr;
	EFI_IP_ADDRESS dstip;
	EFI_STATUS status;
	UINT64 size;

	if (strcmp("tftp", f->f_dev->dv_name) != 0)
		return ENXIO;

	if (PXE == NULL)
		return ENXIO;

	if (!use_mtftp)
		return ENXIO;

	tftpfile = alloc(sizeof(*tftpfile));
	if (tftpfile == NULL)
		return ENOMEM;
	memset(tftpfile, 0, sizeof(*tftpfile));

	memcpy(&dstip, &servip, sizeof(servip));
	status = PXE->Mtftp(PXE, EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE, NULL,
	    FALSE, &size, NULL, &dstip, path, NULL, FALSE);
	if (status != EFI_SUCCESS) {
		free(tftpfile, sizeof(*tftpfile));
		return ENOENT;
	}
	tftpfile->inbufsize = size;

	if (tftpfile->inbufsize == 0)
		goto out;

	status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(tftpfile->inbufsize), &addr);
	if (status != EFI_SUCCESS) {
		free(tftpfile, sizeof(*tftpfile));
		return ENOMEM;
	}
	tftpfile->inbuf = (unsigned char *)((paddr_t)addr);

	status = PXE->Mtftp(PXE, EFI_PXE_BASE_CODE_TFTP_READ_FILE,
	    tftpfile->inbuf, FALSE, &size, NULL, &dstip, path, NULL, FALSE);
	if (status != EFI_SUCCESS) {
		free(tftpfile, sizeof(*tftpfile));
		return ENXIO;
	}
out:
	f->f_fsdata = tftpfile;
	return 0;
}

int
mtftp_close(struct open_file *f)
{
	struct mtftp_handle *tftpfile = f->f_fsdata;

	if (tftpfile->inbuf != NULL)
		BS->FreePages((paddr_t)tftpfile->inbuf,
		    EFI_SIZE_TO_PAGES(tftpfile->inbufsize));
	free(tftpfile, sizeof(*tftpfile));
	return 0;
}

int
mtftp_read(struct open_file *f, void *addr, size_t size, size_t *resid)
{
	struct mtftp_handle *tftpfile = f->f_fsdata;
	size_t toread;

	if (size > tftpfile->inbufsize - tftpfile->inbufoff)
		toread = tftpfile->inbufsize - tftpfile->inbufoff;
	else
		toread = size;

	if (toread != 0) {
		memcpy(addr, tftpfile->inbuf + tftpfile->inbufoff, toread);
		tftpfile->inbufoff += toread;
	}

	if (resid != NULL)
		*resid = size - toread;
	return 0;
}

int
mtftp_write(struct open_file *f, void *start, size_t size, size_t *resid)
{
	return EROFS;
}

off_t
mtftp_seek(struct open_file *f, off_t offset, int where)
{
	struct mtftp_handle *tftpfile = f->f_fsdata;

	switch(where) {
	case SEEK_CUR:
		if (tftpfile->inbufoff + offset < 0 ||
		    tftpfile->inbufoff + offset > tftpfile->inbufsize) {
			errno = EOFFSET;
			break;
		}
		tftpfile->inbufoff += offset;
		return (tftpfile->inbufoff);
	case SEEK_SET:
		if (offset < 0 || offset > tftpfile->inbufsize) {
			errno = EOFFSET;
			break;
		}
		tftpfile->inbufoff = offset;
		return (tftpfile->inbufoff);
	case SEEK_END:
		tftpfile->inbufoff = tftpfile->inbufsize;
		return (tftpfile->inbufoff);
	default:
		errno = EINVAL;
	}
	return((off_t)-1);
}

int
mtftp_stat(struct open_file *f, struct stat *sb)
{
	struct mtftp_handle *tftpfile = f->f_fsdata;

	sb->st_mode = 0444;
	sb->st_nlink = 1;
	sb->st_uid = 0;
	sb->st_gid = 0;
	sb->st_size = tftpfile->inbufsize;

	return 0;
}

int
mtftp_readdir(struct open_file *f, char *name)
{
	return EOPNOTSUPP;
}

/*
 * Overload generic TFTP implementation to check that
 * we actually have a driver.
 */
int
efitftp_open(char *path, struct open_file *f)
{
	if (strcmp("tftp", f->f_dev->dv_name) != 0)
		return ENXIO;

	if (NET == NULL || PXE == NULL)
		return ENXIO;

	if (use_mtftp)
		return ENXIO;

	return tftp_open(path, f);
}

/*
 * Dummy network device.
 */
int tftpdev_sock = -1;

int
tftpopen(struct open_file *f, ...)
{
	EFI_STATUS status;
	u_int unit;
	va_list ap;

	va_start(ap, f);
	unit = va_arg(ap, u_int);
	va_end(ap);

	/* No PXE set -> no PXE available */
	if (PXE == NULL)
		return 1;

	if (unit != 0)
		return 1;

	if (!use_mtftp) {
		status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
		    EFI_SIZE_TO_PAGES(RECV_SIZE), &txbuf);
		if (status != EFI_SUCCESS)
			return ENOMEM;

		if ((tftpdev_sock = netif_open("efinet")) < 0) {
			BS->FreePages(txbuf, EFI_SIZE_TO_PAGES(RECV_SIZE));
			return ENXIO;
		}

		f->f_devdata = &tftpdev_sock;
	}

	return 0;
}

int
tftpclose(struct open_file *f)
{
	int ret = 0;

	if (!use_mtftp) {
		ret = netif_close(*(int *)f->f_devdata);
		BS->FreePages(txbuf, EFI_SIZE_TO_PAGES(RECV_SIZE));
		txbuf = 0;
	}

	return ret;
}

int
tftpioctl(struct open_file *f, u_long cmd, void *data)
{
	return EOPNOTSUPP;
}

int
tftpstrategy(void *devdata, int rw, daddr_t blk, size_t size, void *buf,
	size_t *rsize)
{
	return EOPNOTSUPP;
}

/*
 * Simple Network Protocol driver.
 */
struct netif_stats efinet_stats;
struct netif_dif efinet_ifs[] = {
	{ 0, 1, &efinet_stats, 0, 0, },
};

struct netif_driver efinet_driver = {
	"efinet",
	efinet_match,
	efinet_probe,
	efinet_init,
	efinet_get,
	efinet_put,
	efinet_end,
	efinet_ifs,
	nitems(efinet_ifs)
};

int
efinet_match(struct netif *nif, void *v)
{
	return 1;
}

int
efinet_probe(struct netif *nif, void *v)
{
	if (strncmp(v, efinet_driver.netif_bname, 3))
		return -1;

	return 0;
}

void
efinet_init(struct iodesc *desc, void *v)
{
	EFI_SIMPLE_NETWORK *net = NET;
	EFI_STATUS status;

	if (net == NULL)
		return;

	if (net->Mode->State == EfiSimpleNetworkStopped) {
		status = net->Start(net);
		if (status != EFI_SUCCESS)
			return;
	}

	if (net->Mode->State != EfiSimpleNetworkInitialized) {
		status = net->Initialize(net, 0, 0);
		if (status != EFI_SUCCESS)
			return;
	}

	net->ReceiveFilters(net, EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
	    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
	    0, FALSE, 0, NULL);

	memcpy(desc->myea, net->Mode->CurrentAddress.Addr, 6);
	memcpy(&desc->myip, &bootip, sizeof(bootip));
	desc->xid = 1;
}

int
efinet_get(struct iodesc *desc, void *pkt, size_t len, time_t tmo)
{
	EFI_SIMPLE_NETWORK *net = NET;
	EFI_STATUS status;
	UINTN bufsz, pktsz;
	time_t t;
	char *buf, *ptr;
	ssize_t ret = -1;

	if (net == NULL)
		return ret;

	bufsz = net->Mode->MaxPacketSize + ETHER_HDR_LEN + ETHER_CRC_LEN;
	buf = alloc(bufsz + ETHER_ALIGN);
	if (buf == NULL)
		return ret;
	ptr = buf + ETHER_ALIGN;

	t = getsecs();
	status = EFI_NOT_READY;
	while ((getsecs() - t) < tmo) {
		pktsz = bufsz;
		status = net->Receive(net, NULL, &pktsz, ptr, NULL, NULL, NULL);
		if (status == EFI_SUCCESS)
			break;
		if (status != EFI_NOT_READY)
			break;
	}

	if (status == EFI_SUCCESS) {
		memcpy(pkt, ptr, min((ssize_t)pktsz, len));
		ret = min((ssize_t)pktsz, len);
	}

	free(buf, bufsz + ETHER_ALIGN);
	return ret;
}

int
efinet_put(struct iodesc *desc, void *pkt, size_t len)
{
	EFI_SIMPLE_NETWORK *net = NET;
	EFI_STATUS status;
	void *buf = NULL;
	int ret = -1;

	if (net == NULL)
		goto out;

	if (len > RECV_SIZE)
		goto out;

	memcpy((void *)txbuf, pkt, len);
	status = net->Transmit(net, 0, len, (void *)txbuf, NULL, NULL, NULL);
	if (status != EFI_SUCCESS)
		goto out;

	buf = NULL;
	while (status == EFI_SUCCESS) {
		status = net->GetStatus(net, NULL, &buf);
		if (buf)
			break;
	}

	if (status == EFI_SUCCESS)
		ret = len;

out:
	return ret;
}

void
efinet_end(struct netif *nif)
{
	EFI_SIMPLE_NETWORK *net = NET;

	if (net == NULL)
		return;

	net->Shutdown(net);
}
