/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42) (by Poul-Henning Kamp):
 * <joerg@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.        Joerg Wunsch
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD$
 */

/*
 * Simple demo program to illustrate the handling of FreeBSD's
 * libusb20.
 *
 * Issues a bulk output, and then requests a bulk input.
 */

/*
 * Examples:
 * Just list all VID:PID pairs
 * ./bulk
 *
 * Say "hello" to an Atmel JTAGICEmkII.
 * ./bulk -o 2 -i 0x82 -v 0x03eb -p 0x2103 0x1b 0 0 1 0 0 0 0x0e 1 0xf3 0x97
 *
 * Return the INQUIRY data of an USB mass storage device.
 * (It's best to have the umass(4) driver unloaded while doing such
 * experiments, and perform a "usbconfig reset" for the device if it
 * gets stuck.)
 * ./bulk -v 0x5e3 -p 0x723 -i 0x81 -o 2 0x55 0x53 0x42 0x43 1 2 3 4 31 12 0x80 0x24 0 0 0 0x12 0 0 0 36 0 0 0 0 0 0 0 0 0 0
 */


#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <libusb20.h>
#include <libusb20_desc.h>

#include "util.h"

/*
 * If you want to see the details of the internal datastructures
 * in the debugger, unifdef the following.
 */
#ifdef DEBUG
#  include <sys/queue.h>
#  include "/usr/src/lib/libusb/libusb20_int.h"
#endif

#define BUFLEN 64

#define TIMEOUT 5000 		/* 5 s */

int in_ep, out_ep;		/* endpoints */
uint8_t out_buf[BUFLEN];
uint16_t out_len;

static void
doit(struct libusb20_device *dev)
{
  int rv;

  /*
   * Open the device, allocating memory for two possible (bulk or
   * interrupt) transfers.
   *
   * If only control transfers are intended (via
   * libusb20_dev_request_sync()), transfer_max can be given as 0.
   */
  if ((rv = libusb20_dev_open(dev, 2)) != 0)
    {
      fprintf(stderr, "libusb20_dev_open: %s\n", libusb20_strerror(rv));
      return;
    }

  /*
   * If the device has more than one configuration, select the desired
   * one here.
   */
  if ((rv = libusb20_dev_set_config_index(dev, 0)) != 0)
    {
      fprintf(stderr, "libusb20_dev_set_config_index: %s\n", libusb20_strerror(rv));
      return;
    }

  /*
   * Two transfers have been requested in libusb20_dev_open() above;
   * obtain the corresponding transfer struct pointers.
   */
  struct libusb20_transfer *xfr_out = libusb20_tr_get_pointer(dev, 0);
  struct libusb20_transfer *xfr_in = libusb20_tr_get_pointer(dev, 1);

  if (xfr_in == NULL || xfr_out == NULL)
    {
      fprintf(stderr, "libusb20_tr_get_pointer: %s\n", libusb20_strerror(rv));
      return;
    }

  /*
   * Open both transfers, the "out" one for the write endpoint, the
   * "in" one for the read endpoint (ep | 0x80).
   */
  if ((rv = libusb20_tr_open(xfr_out, 0, 1, out_ep)) != 0)
    {
      fprintf(stderr, "libusb20_tr_open: %s\n", libusb20_strerror(rv));
      return;
    }
  if ((rv = libusb20_tr_open(xfr_in, 0, 1, in_ep)) != 0)
    {
      fprintf(stderr, "libusb20_tr_open: %s\n", libusb20_strerror(rv));
      return;
    }

  uint8_t in_buf[BUFLEN];
  uint32_t rlen;

  if (out_len > 0)
    {
      if ((rv = libusb20_tr_bulk_intr_sync(xfr_out, out_buf, out_len, &rlen, TIMEOUT))
	  != 0)
	{
	  fprintf(stderr, "libusb20_tr_bulk_intr_sync (OUT): %s\n", libusb20_strerror(rv));
	}
      printf("sent %d bytes\n", rlen);
    }

  if ((rv = libusb20_tr_bulk_intr_sync(xfr_in, in_buf, BUFLEN, &rlen, TIMEOUT))
      != 0)
    {
      fprintf(stderr, "libusb20_tr_bulk_intr_sync: %s\n", libusb20_strerror(rv));
    }
      printf("received %d bytes\n", rlen);
      if (rlen > 0)
	print_formatted(in_buf, rlen);

  libusb20_tr_close(xfr_out);
  libusb20_tr_close(xfr_in);

  libusb20_dev_close(dev);
}

static void
usage(void)
{
  fprintf(stderr,
	  "Usage ./usb -i <IN_EP> -o <OUT_EP> -v <VID> -p <PID> [<outdata> ...\n]");
  exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
  unsigned int vid = UINT_MAX, pid = UINT_MAX; /* impossible VID:PID */
  int c;

  while ((c = getopt(argc, argv, "i:o:p:v:")) != -1)
    switch (c)
      {
      case 'i':
	in_ep = strtol(optarg, NULL, 0);
	break;

      case 'o':
	out_ep = strtol(optarg, NULL, 0);
	break;

      case 'p':
	pid = strtol(optarg, NULL, 0);
	break;

      case 'v':
	vid = strtol(optarg, NULL, 0);
	break;

      default:
	usage();
	break;
      }
  argc -= optind;
  argv += optind;

  if (vid != UINT_MAX || pid != UINT_MAX)
    {
      if (in_ep == 0 || out_ep == 0)
	{
	  usage();
	}
      if ((in_ep & 0x80) == 0)
	{
	  fprintf(stderr, "IN_EP must have bit 7 set\n");
	  return (EX_USAGE);
	}

      if (argc > 0)
	{
	  for (out_len = 0; argc > 0 && out_len < BUFLEN; out_len++, argc--)
	    {
	      unsigned n = strtoul(argv[out_len], 0, 0);
	      if (n > 255)
		fprintf(stderr,
			"Warning: data #%d 0x%0x > 0xff, truncating\n",
			out_len, n);
	      out_buf[out_len] = (uint8_t)n;
	    }
	  out_len++;
	  if (argc > 0)
	    fprintf(stderr,
		    "Data count exceeds maximum of %d, ignoring %d elements\n",
		    BUFLEN, optind);
	}
    }

  struct libusb20_backend *be;
  struct libusb20_device *dev;

  if ((be = libusb20_be_alloc_default()) == NULL)
    {
      perror("libusb20_be_alloc()");
      return 1;
    }

  dev = NULL;
  while ((dev = libusb20_be_device_foreach(be, dev)) != NULL)
    {
      struct LIBUSB20_DEVICE_DESC_DECODED *ddp =
      libusb20_dev_get_device_desc(dev);

      printf("Found device %s (VID:PID = 0x%04x:0x%04x)\n",
	     libusb20_dev_get_desc(dev),
	     ddp->idVendor, ddp->idProduct);

      if (ddp->idVendor == vid && ddp->idProduct == pid)
	doit(dev);
    }

  libusb20_be_free(be);
  return 0;
}
