/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42) (by Poul-Henning Kamp):
 * <joerg@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.        Joerg Wunsch
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD$
 */

/*
 * Helper functions common to all examples
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <libusb20.h>
#include <libusb20_desc.h>

#include "util.h"

/*
 * Print "len" bytes from "buf" in hex, followed by an ASCII
 * representation (somewhat resembling the output of hd(1)).
 */
void
print_formatted(uint8_t *buf, uint32_t len)
{
  int i, j;

  for (j = 0; j < len; j += 16)
    {
      printf("%02x: ", j);

      for (i = 0; i < 16 && i + j < len; i++)
	printf("%02x ", buf[i + j]);
      printf("  ");
      for (i = 0; i < 16 && i + j < len; i++)
	{
	  uint8_t c = buf[i + j];
	  if(c >= ' ' && c <= '~')
	    printf("%c", (char)c);
	  else
	    putchar('.');
	}
      putchar('\n');
    }
}
