/*******************************************************************************
**
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE

********************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>

#include <dev/pms/RefTisa/discovery/dm/dmdefs.h>
#include <dev/pms/RefTisa/discovery/dm/dmtypes.h>
#include <dev/pms/RefTisa/discovery/dm/dmproto.h>

osGLOBAL void 
*dm_memset(void *s, int c, bit32 n)
{
  bit32   i;

  char *dst = (char *)s;
  for (i=0; i < n; i++)
  {
    dst[i] = (char) c;
  }
  return (void *)(&dst[i-n]);
}

osGLOBAL void 
*dm_memcpy(void *dst, void *src, bit32 count)
{
  bit32 x;
  unsigned char *dst1 = (unsigned char *)dst;
  unsigned char *src1 = (unsigned char *)src;

  for (x=0; x < count; x++)
    dst1[x] = src1[x];

  return dst;

}

/** hexidecimal dump */
osGLOBAL void 
dmhexdump(const char *ptitle, bit8 *pbuf, int len)
{
  int i;
  DM_DBG1(("%s - dmhexdump(len=%d):\n", ptitle, (int)len));
  if (!pbuf)
  {
    DM_DBG1(("pbuf is NULL\n"));
    return;
  }
  for (i = 0; i < len; )
  {
    if (len - i > 4)
    {
      DM_DBG1((" 0x%02x, 0x%02x, 0x%02x, 0x%02x,\n", pbuf[i], pbuf[i+1], pbuf[i+2], pbuf[i+3]));
      i += 4;
    }
    else
    {
      DM_DBG1((" 0x%02x,", pbuf[i]));
      i++;
    }
  }
  DM_DBG1(("\n"));
}
#endif /* FDS_ DM */

