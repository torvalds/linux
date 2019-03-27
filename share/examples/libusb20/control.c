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
 */

/*
 * Examples:
 * Just list all VID:PID pairs
 * ./control
 *
 * Standard device request GET_STATUS, report two bytes of status
 * (bit 0 in the first byte returned is the "self powered" bit)
 * ./control -v 0x3eb -p 0x2103 in std dev get_status 0 0 2
 *
 * Request input reports through the interrupt pipe from a mouse
 * device (move the mouse around after issuing the command):
 * ./control -v 0x093a -p 0x2516 -i 0x81
 *
 */


#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <string.h>

#include <libusb20.h>
#include <libusb20_desc.h>

#include <sys/queue.h>

#include "util.h"

/*
 * If you want to see the details of the internal datastructures
 * in the debugger, unifdef the following.
 */
#ifdef DEBUG
#  include "/usr/src/lib/libusb/libusb20_int.h"
#endif

#define BUFLEN 64

#define TIMEOUT 5000 		/* 5 s */

int intr_ep;		/* endpoints */
struct LIBUSB20_CONTROL_SETUP_DECODED setup;

uint8_t out_buf[BUFLEN];
uint16_t out_len;

bool do_request;

static void
doit(struct libusb20_device *dev)
{
  int rv;

  if (do_request)
    printf("doit(): bmRequestType 0x%02x, bRequest 0x%02x, wValue 0x%04x, wIndex 0x%04x, wLength 0x%04x\n",
	   setup.bmRequestType,
	   setup.bRequest,
	   setup.wValue,
	   setup.wIndex,
	   setup.wLength);

  /*
   * Open the device, allocating memory for two possible (bulk or
   * interrupt) transfers.
   *
   * If only control transfers are intended (via
   * libusb20_dev_request_sync()), transfer_max can be given as 0.
   */
  if ((rv = libusb20_dev_open(dev, 1)) != 0)
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

  uint8_t *data = 0;
  uint16_t actlen;

  if ((setup.bmRequestType & 0x80) != 0)
    {
      /* this is an IN request, allocate a buffer */
      data = malloc(setup.wLength);
      if (data == 0)
	{
	  fprintf(stderr,
		  "Out of memory allocating %u bytes of reply buffer\n",
		  setup.wLength);
	  return;
	}
    }
  else
    data = out_buf;

  if (do_request)
    {
      if ((rv = libusb20_dev_request_sync(dev, &setup, data,
					  &actlen,
					  TIMEOUT,
					  0 /* flags */)) != 0)
	{
	  fprintf(stderr,
		  "libusb20_dev_request_sync: %s\n", libusb20_strerror(rv));
	}
      printf("sent %d bytes\n", actlen);
      if ((setup.bmRequestType & 0x80) != 0)
	{
	  print_formatted(data, (uint32_t)setup.wLength);
	  free(data);
	}
    }

  if (intr_ep != 0)
    {
      /*
       * One transfer has been requested in libusb20_dev_open() above;
       * obtain the corresponding transfer struct pointer.
       */
      struct libusb20_transfer *xfr_intr = libusb20_tr_get_pointer(dev, 0);

      if (xfr_intr == NULL)
	{
	  fprintf(stderr, "libusb20_tr_get_pointer: %s\n", libusb20_strerror(rv));
	  return;
	}

      /*
       * Open the interrupt transfer.
       */
      if ((rv = libusb20_tr_open(xfr_intr, 0, 1, intr_ep)) != 0)
	{
	  fprintf(stderr, "libusb20_tr_open: %s\n", libusb20_strerror(rv));
	  return;
	}

      uint8_t in_buf[BUFLEN];
      uint32_t rlen;

      if ((rv = libusb20_tr_bulk_intr_sync(xfr_intr, in_buf, BUFLEN, &rlen, TIMEOUT))
	  != 0)
	{
	  fprintf(stderr, "libusb20_tr_bulk_intr_sync: %s\n", libusb20_strerror(rv));
	}
      printf("received %d bytes\n", rlen);
      if (rlen > 0)
	print_formatted(in_buf, rlen);

      libusb20_tr_close(xfr_intr);
    }

  libusb20_dev_close(dev);
}

static void
usage(void)
{
  fprintf(stderr,
	  "Usage ./usb [-i <INTR_EP>] -v <VID> -p <PID> [dir type rcpt req wValue wIndex wLength [<outdata> ...]]\n");
  exit(EX_USAGE);
}

static const char *reqnames[] =
{
  "get_status",
  "clear_feature",
  "res1",
  "set_feature",
  "res2",
  "set_address",
  "get_descriptor",
  "set_descriptor",
  "get_configuration",
  "set_configuration",
  "get_interface",
  "set_interface",
  "synch_frame",
};

static int
get_req(const char *reqname)
{
  size_t i;
  size_t l = strlen(reqname);

  for (i = 0;
       i < sizeof reqnames / sizeof reqnames[0];
       i++)
    if (strncasecmp(reqname, reqnames[i], l) == 0)
      return i;

  return strtoul(reqname, 0, 0);
}


static int
parse_req(int argc, char **argv)
{
  int idx;
  uint8_t rt = 0;

  for (idx = 0; argc != 0 && idx <= 6; argc--, idx++)
    switch (idx)
      {
      case 0:
	/* dir[ection]: i[n] | o[ut] */
	if (*argv[idx] == 'i')
	  rt |= 0x80;
	else if (*argv[idx] == 'o')
	  /* nop */;
	else
	  {
	    fprintf(stderr, "request direction must be \"in\" or \"out\" (got %s)\n",
		    argv[idx]);
	    return -1;
	  }
	break;

      case 1:
	/* type: s[tandard] | c[lass] | v[endor] */
	if (*argv[idx] == 's')
	  /* nop */;
	else if (*argv[idx] == 'c')
	  rt |= 0x20;
	else if (*argv[idx] == 'v')
	  rt |= 0x40;
	else
	  {
	    fprintf(stderr,
		    "request type must be one of \"standard\", \"class\", or \"vendor\" (got %s)\n",
		    argv[idx]);
	    return -1;
	  }
	break;

      case 2:
	/* rcpt: d[evice], i[nterface], e[ndpoint], o[ther] */
	if (*argv[idx] == 'd')
	  /* nop */;
	else if (*argv[idx] == 'i')
	  rt |= 1;
	else if (*argv[idx] == 'e')
	  rt |= 2;
	else if (*argv[idx] == 'o')
	  rt |= 3;
	else
	  {
	    fprintf(stderr,
		    "recipient must be one of \"device\", \"interface\", \"endpoint\", or \"other\" (got %s)\n",
		    argv[idx]);
	    return -1;
	  }
	setup.bmRequestType = rt;
	break;

      case 3:
	setup.bRequest = get_req(argv[idx]);
	break;

      case 4:
	setup.wValue = strtoul(argv[idx], 0, 0);
	break;

      case 5:
	setup.wIndex = strtoul(argv[idx], 0, 0);
	break;

      case 6:
	setup.wLength = strtoul(argv[idx], 0, 0);
	break;
      }

  return argc;
}


int
main(int argc, char **argv)
{
  unsigned int vid = UINT_MAX, pid = UINT_MAX; /* impossible VID:PID */
  int c;

  /*
   * Initialize setup struct.  This step is required, and initializes
   * internal fields in the struct.
   *
   * All the "public" fields are named exactly the way as the USB
   * standard describes them, namely:
   *
   *	setup.bmRequestType: bitmask, bit 7 is direction
   *	                              bits 6/5 is request type
   *	                                       (standard, class, vendor)
   *	                              bits 4..0 is recipient
   *	                                       (device, interface, endpoint,
   *	                                        other)
   *	setup.bRequest:      the request itself (see get_req() for standard
   *	                                         requests, or specific value)
   *	setup.wValue:        a 16-bit value
   *	setup.wIndex:        another 16-bit value
   *	setup.wLength:       length of associated data transfer, direction
   *	                     depends on bit 7 of bmRequestType
   */
  LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &setup);

  while ((c = getopt(argc, argv, "i:p:v:")) != -1)
    switch (c)
      {
      case 'i':
	intr_ep = strtol(optarg, NULL, 0);
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
      if (intr_ep != 0 && (intr_ep & 0x80) == 0)
	{
	  fprintf(stderr, "Interrupt endpoint must be of type IN\n");
	  usage();
	}

      if (argc > 0)
	{
	  do_request = true;

	  int rv = parse_req(argc, argv);
	  if (rv < 0)
	    return EX_USAGE;
	  argc = rv;

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
