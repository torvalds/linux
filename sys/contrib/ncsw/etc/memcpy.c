/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */



#include "std_ext.h"
#include "xx_ext.h"
#include "memcpy_ext.h"

void * MemCpy8(void* pDst, void* pSrc, uint32_t size)
{
    uint32_t i;

    for(i = 0; i < size; ++i)
        *(((uint8_t*)(pDst)) + i) = *(((uint8_t*)(pSrc)) + i);

    return pDst;
}

void * MemSet8(void* pDst, int c, uint32_t size)
{
    uint32_t i;

    for(i = 0; i < size; ++i)
        *(((uint8_t*)(pDst)) + i) = (uint8_t)(c);

    return pDst;
}

void * MemCpy32(void* pDst,void* pSrc, uint32_t size)
{
    uint32_t leftAlign;
    uint32_t rightAlign;
    uint32_t lastWord;
    uint32_t currWord;
    uint32_t *p_Src32;
    uint32_t *p_Dst32;
    uint8_t  *p_Src8;
    uint8_t  *p_Dst8;

    p_Src8 = (uint8_t*)(pSrc);
    p_Dst8 = (uint8_t*)(pDst);
    /* first copy byte by byte till the source first alignment
     * this step is necessary to ensure we do not even try to access
     * data which is before the source buffer, hence it is not ours.
     */
    while((PTR_TO_UINT(p_Src8) & 3) && size) /* (pSrc mod 4) > 0 and size > 0 */
    {
        *p_Dst8++ = *p_Src8++;
        size--;
    }

    /* align destination (possibly disaligning source)*/
    while((PTR_TO_UINT(p_Dst8) & 3) && size) /* (pDst mod 4) > 0 and size > 0 */
    {
        *p_Dst8++ = *p_Src8++;
        size--;
    }

    /* dest is aligned and source is not necessarily aligned */
    leftAlign = (uint32_t)((PTR_TO_UINT(p_Src8) & 3) << 3); /* leftAlign = (pSrc mod 4)*8 */
    rightAlign = 32 - leftAlign;


    if (leftAlign == 0)
    {
        /* source is also aligned */
        p_Src32 = (uint32_t*)(p_Src8);
        p_Dst32 = (uint32_t*)(p_Dst8);
        while (size >> 2) /* size >= 4 */
        {
            *p_Dst32++ = *p_Src32++;
            size -= 4;
        }
        p_Src8 = (uint8_t*)(p_Src32);
        p_Dst8 = (uint8_t*)(p_Dst32);
    }
    else
    {
        /* source is not aligned (destination is aligned)*/
        p_Src32 = (uint32_t*)(p_Src8 - (leftAlign >> 3));
        p_Dst32 = (uint32_t*)(p_Dst8);
        lastWord = *p_Src32++;
        while(size >> 3) /* size >= 8 */
        {
            currWord = *p_Src32;
            *p_Dst32 = (lastWord << leftAlign) | (currWord >> rightAlign);
            lastWord = currWord;
            p_Src32++;
            p_Dst32++;
            size -= 4;
        }
        p_Dst8 = (uint8_t*)(p_Dst32);
        p_Src8 = (uint8_t*)(p_Src32) - 4 + (leftAlign >> 3);
    }

    /* complete the left overs */
    while (size--)
        *p_Dst8++ = *p_Src8++;

    return pDst;
}

void * IO2IOCpy32(void* pDst,void* pSrc, uint32_t size)
{
    uint32_t leftAlign;
    uint32_t rightAlign;
    uint32_t lastWord;
    uint32_t currWord;
    uint32_t *p_Src32;
    uint32_t *p_Dst32;
    uint8_t  *p_Src8;
    uint8_t  *p_Dst8;

    p_Src8 = (uint8_t*)(pSrc);
    p_Dst8 = (uint8_t*)(pDst);
    /* first copy byte by byte till the source first alignment
     * this step is necessary to ensure we do not even try to access
     * data which is before the source buffer, hence it is not ours.
     */
    while((PTR_TO_UINT(p_Src8) & 3) && size) /* (pSrc mod 4) > 0 and size > 0 */
    {
        WRITE_UINT8(*p_Dst8, GET_UINT8(*p_Src8));
        p_Dst8++;p_Src8++;
        size--;
    }

    /* align destination (possibly disaligning source)*/
    while((PTR_TO_UINT(p_Dst8) & 3) && size) /* (pDst mod 4) > 0 and size > 0 */
    {
        WRITE_UINT8(*p_Dst8, GET_UINT8(*p_Src8));
        p_Dst8++;p_Src8++;
        size--;
    }

    /* dest is aligned and source is not necessarily aligned */
    leftAlign = (uint32_t)((PTR_TO_UINT(p_Src8) & 3) << 3); /* leftAlign = (pSrc mod 4)*8 */
    rightAlign = 32 - leftAlign;

    if (leftAlign == 0)
    {
        /* source is also aligned */
        p_Src32 = (uint32_t*)(p_Src8);
        p_Dst32 = (uint32_t*)(p_Dst8);
        while (size >> 2) /* size >= 4 */
        {
            WRITE_UINT32(*p_Dst32, GET_UINT32(*p_Src32));
            p_Dst32++;p_Src32++;
            size -= 4;
        }
        p_Src8 = (uint8_t*)(p_Src32);
        p_Dst8 = (uint8_t*)(p_Dst32);
    }
    else
    {
        /* source is not aligned (destination is aligned)*/
        p_Src32 = (uint32_t*)(p_Src8 - (leftAlign >> 3));
        p_Dst32 = (uint32_t*)(p_Dst8);
        lastWord = GET_UINT32(*p_Src32);
        p_Src32++;
        while(size >> 3) /* size >= 8 */
        {
            currWord = GET_UINT32(*p_Src32);
            WRITE_UINT32(*p_Dst32, (lastWord << leftAlign) | (currWord >> rightAlign));
            lastWord = currWord;
            p_Src32++;p_Dst32++;
            size -= 4;
        }
        p_Dst8 = (uint8_t*)(p_Dst32);
        p_Src8 = (uint8_t*)(p_Src32) - 4 + (leftAlign >> 3);
    }

    /* complete the left overs */
    while (size--)
    {
        WRITE_UINT8(*p_Dst8, GET_UINT8(*p_Src8));
        p_Dst8++;p_Src8++;
    }

    return pDst;
}

void * Mem2IOCpy32(void* pDst,void* pSrc, uint32_t size)
{
    uint32_t leftAlign;
    uint32_t rightAlign;
    uint32_t lastWord;
    uint32_t currWord;
    uint32_t *p_Src32;
    uint32_t *p_Dst32;
    uint8_t  *p_Src8;
    uint8_t  *p_Dst8;

    p_Src8 = (uint8_t*)(pSrc);
    p_Dst8 = (uint8_t*)(pDst);
    /* first copy byte by byte till the source first alignment
     * this step is necessary to ensure we do not even try to access
     * data which is before the source buffer, hence it is not ours.
     */
    while((PTR_TO_UINT(p_Src8) & 3) && size) /* (pSrc mod 4) > 0 and size > 0 */
    {
        WRITE_UINT8(*p_Dst8, *p_Src8);
        p_Dst8++;p_Src8++;
        size--;
    }

    /* align destination (possibly disaligning source)*/
    while((PTR_TO_UINT(p_Dst8) & 3) && size) /* (pDst mod 4) > 0 and size > 0 */
    {
        WRITE_UINT8(*p_Dst8, *p_Src8);
        p_Dst8++;p_Src8++;
        size--;
    }

    /* dest is aligned and source is not necessarily aligned */
    leftAlign = (uint32_t)((PTR_TO_UINT(p_Src8) & 3) << 3); /* leftAlign = (pSrc mod 4)*8 */
    rightAlign = 32 - leftAlign;

    if (leftAlign == 0)
    {
        /* source is also aligned */
        p_Src32 = (uint32_t*)(p_Src8);
        p_Dst32 = (uint32_t*)(p_Dst8);
        while (size >> 2) /* size >= 4 */
        {
            WRITE_UINT32(*p_Dst32, *p_Src32);
            p_Dst32++;p_Src32++;
            size -= 4;
        }
        p_Src8 = (uint8_t*)(p_Src32);
        p_Dst8 = (uint8_t*)(p_Dst32);
    }
    else
    {
        /* source is not aligned (destination is aligned)*/
        p_Src32 = (uint32_t*)(p_Src8 - (leftAlign >> 3));
        p_Dst32 = (uint32_t*)(p_Dst8);
        lastWord = *p_Src32++;
        while(size >> 3) /* size >= 8 */
        {
            currWord = *p_Src32;
            WRITE_UINT32(*p_Dst32, (lastWord << leftAlign) | (currWord >> rightAlign));
            lastWord = currWord;
            p_Src32++;p_Dst32++;
            size -= 4;
        }
        p_Dst8 = (uint8_t*)(p_Dst32);
        p_Src8 = (uint8_t*)(p_Src32) - 4 + (leftAlign >> 3);
    }

    /* complete the left overs */
    while (size--)
    {
        WRITE_UINT8(*p_Dst8, *p_Src8);
        p_Dst8++;p_Src8++;
    }

    return pDst;
}

void * IO2MemCpy32(void* pDst,void* pSrc, uint32_t size)
{
    uint32_t leftAlign;
    uint32_t rightAlign;
    uint32_t lastWord;
    uint32_t currWord;
    uint32_t *p_Src32;
    uint32_t *p_Dst32;
    uint8_t  *p_Src8;
    uint8_t  *p_Dst8;

    p_Src8 = (uint8_t*)(pSrc);
    p_Dst8 = (uint8_t*)(pDst);
    /* first copy byte by byte till the source first alignment
     * this step is necessary to ensure we do not even try to access
     * data which is before the source buffer, hence it is not ours.
     */
    while((PTR_TO_UINT(p_Src8) & 3) && size) /* (pSrc mod 4) > 0 and size > 0 */
    {
        *p_Dst8 = GET_UINT8(*p_Src8);
        p_Dst8++;p_Src8++;
        size--;
    }

    /* align destination (possibly disaligning source)*/
    while((PTR_TO_UINT(p_Dst8) & 3) && size) /* (pDst mod 4) > 0 and size > 0 */
    {
        *p_Dst8 = GET_UINT8(*p_Src8);
        p_Dst8++;p_Src8++;
        size--;
    }

    /* dest is aligned and source is not necessarily aligned */
    leftAlign = (uint32_t)((PTR_TO_UINT(p_Src8) & 3) << 3); /* leftAlign = (pSrc mod 4)*8 */
    rightAlign = 32 - leftAlign;

    if (leftAlign == 0)
    {
        /* source is also aligned */
        p_Src32 = (uint32_t*)(p_Src8);
        p_Dst32 = (uint32_t*)(p_Dst8);
        while (size >> 2) /* size >= 4 */
        {
            *p_Dst32 = GET_UINT32(*p_Src32);
            p_Dst32++;p_Src32++;
            size -= 4;
        }
        p_Src8 = (uint8_t*)(p_Src32);
        p_Dst8 = (uint8_t*)(p_Dst32);
    }
    else
    {
        /* source is not aligned (destination is aligned)*/
        p_Src32 = (uint32_t*)(p_Src8 - (leftAlign >> 3));
        p_Dst32 = (uint32_t*)(p_Dst8);
        lastWord = GET_UINT32(*p_Src32);
        p_Src32++;
        while(size >> 3) /* size >= 8 */
        {
            currWord = GET_UINT32(*p_Src32);
            *p_Dst32 = (lastWord << leftAlign) | (currWord >> rightAlign);
            lastWord = currWord;
            p_Src32++;p_Dst32++;
            size -= 4;
        }
        p_Dst8 = (uint8_t*)(p_Dst32);
        p_Src8 = (uint8_t*)(p_Src32) - 4 + (leftAlign >> 3);
    }

    /* complete the left overs */
    while (size--)
    {
        *p_Dst8 = GET_UINT8(*p_Src8);
        p_Dst8++;p_Src8++;
    }

    return pDst;
}

void * MemCpy64(void* pDst,void* pSrc, uint32_t size)
{
    uint32_t leftAlign;
    uint32_t rightAlign;
    uint64_t lastWord;
    uint64_t currWord;
    uint64_t *pSrc64;
    uint64_t *pDst64;
    uint8_t  *p_Src8;
    uint8_t  *p_Dst8;

    p_Src8 = (uint8_t*)(pSrc);
    p_Dst8 = (uint8_t*)(pDst);
    /* first copy byte by byte till the source first alignment
     * this step is necessarily to ensure we do not even try to access
     * data which is before the source buffer, hence it is not ours.
     */
    while((PTR_TO_UINT(p_Src8) & 7) && size) /* (pSrc mod 8) > 0 and size > 0 */
    {
        *p_Dst8++ = *p_Src8++;
        size--;
    }

    /* align destination (possibly disaligning source)*/
    while((PTR_TO_UINT(p_Dst8) & 7) && size) /* (pDst mod 8) > 0 and size > 0 */
    {
        *p_Dst8++ = *p_Src8++;
        size--;
    }

    /* dest is aligned and source is not necessarily aligned */
    leftAlign = (uint32_t)((PTR_TO_UINT(p_Src8) & 7) << 3); /* leftAlign = (pSrc mod 8)*8 */
    rightAlign = 64 - leftAlign;


    if (leftAlign == 0)
    {
        /* source is also aligned */
        pSrc64 = (uint64_t*)(p_Src8);
        pDst64 = (uint64_t*)(p_Dst8);
        while (size >> 3) /* size >= 8 */
        {
            *pDst64++ = *pSrc64++;
            size -= 8;
        }
        p_Src8 = (uint8_t*)(pSrc64);
        p_Dst8 = (uint8_t*)(pDst64);
    }
    else
    {
        /* source is not aligned (destination is aligned)*/
        pSrc64 = (uint64_t*)(p_Src8 - (leftAlign >> 3));
        pDst64 = (uint64_t*)(p_Dst8);
        lastWord = *pSrc64++;
        while(size >> 4) /* size >= 16 */
        {
            currWord = *pSrc64;
            *pDst64 = (lastWord << leftAlign) | (currWord >> rightAlign);
            lastWord = currWord;
            pSrc64++;
            pDst64++;
            size -= 8;
        }
        p_Dst8 = (uint8_t*)(pDst64);
        p_Src8 = (uint8_t*)(pSrc64) - 8 + (leftAlign >> 3);
    }

    /* complete the left overs */
    while (size--)
        *p_Dst8++ = *p_Src8++;

    return pDst;
}

void * MemSet32(void* pDst, uint8_t val, uint32_t size)
{
    uint32_t val32;
    uint32_t *p_Dst32;
    uint8_t  *p_Dst8;

    p_Dst8 = (uint8_t*)(pDst);

    /* generate four 8-bit val's in 32-bit container */
    val32  = (uint32_t) val;
    val32 |= (val32 <<  8);
    val32 |= (val32 << 16);

    /* align destination to 32 */
    while((PTR_TO_UINT(p_Dst8) & 3) && size) /* (pDst mod 4) > 0 and size > 0 */
    {
        *p_Dst8++ = val;
        size--;
    }

    /* 32-bit chunks */
    p_Dst32 = (uint32_t*)(p_Dst8);
    while (size >> 2) /* size >= 4 */
    {
        *p_Dst32++ = val32;
        size -= 4;
    }

    /* complete the leftovers */
    p_Dst8 = (uint8_t*)(p_Dst32);
    while (size--)
        *p_Dst8++ = val;

    return pDst;
}

void * IOMemSet32(void* pDst, uint8_t val, uint32_t size)
{
    uint32_t val32;
    uint32_t *p_Dst32;
    uint8_t  *p_Dst8;

    p_Dst8 = (uint8_t*)(pDst);

    /* generate four 8-bit val's in 32-bit container */
    val32  = (uint32_t) val;
    val32 |= (val32 <<  8);
    val32 |= (val32 << 16);

    /* align destination to 32 */
    while((PTR_TO_UINT(p_Dst8) & 3) && size) /* (pDst mod 4) > 0 and size > 0 */
    {
        WRITE_UINT8(*p_Dst8, val);
        p_Dst8++;
        size--;
    }

    /* 32-bit chunks */
    p_Dst32 = (uint32_t*)(p_Dst8);
    while (size >> 2) /* size >= 4 */
    {
        WRITE_UINT32(*p_Dst32, val32);
        p_Dst32++;
        size -= 4;
    }

    /* complete the leftovers */
    p_Dst8 = (uint8_t*)(p_Dst32);
    while (size--)
    {
        WRITE_UINT8(*p_Dst8, val);
        p_Dst8++;
    }

    return pDst;
}

void * MemSet64(void* pDst, uint8_t val, uint32_t size)
{
    uint64_t val64;
    uint64_t *pDst64;
    uint8_t  *p_Dst8;

    p_Dst8 = (uint8_t*)(pDst);

    /* generate four 8-bit val's in 32-bit container */
    val64  = (uint64_t) val;
    val64 |= (val64 <<  8);
    val64 |= (val64 << 16);
    val64 |= (val64 << 24);
    val64 |= (val64 << 32);

    /* align destination to 64 */
    while((PTR_TO_UINT(p_Dst8) & 7) && size) /* (pDst mod 8) > 0 and size > 0 */
    {
        *p_Dst8++ = val;
        size--;
    }

    /* 64-bit chunks */
    pDst64 = (uint64_t*)(p_Dst8);
    while (size >> 4) /* size >= 8 */
    {
        *pDst64++ = val64;
        size -= 8;
    }

    /* complete the leftovers */
    p_Dst8 = (uint8_t*)(pDst64);
    while (size--)
        *p_Dst8++ = val;

    return pDst;
}

void MemDisp(uint8_t *p, int size)
{
    uint32_t    space = (uint32_t)(PTR_TO_UINT(p) & 0x3);
    uint8_t     *p_Limit;

    if (space)
    {
        p_Limit = (p - space + 4);

        XX_Print("0x%08X: ", (p - space));

        while (space--)
        {
            XX_Print("--");
        }
        while (size  && (p < p_Limit))
        {
            XX_Print("%02x", *(uint8_t*)p);
            size--;
            p++;
        }

        XX_Print(" ");
        p_Limit += 12;

        while ((size > 3) && (p < p_Limit))
        {
            XX_Print("%08x ", *(uint32_t*)p);
            size -= 4;
            p += 4;
        }
        XX_Print("\r\n");
    }

    while (size > 15)
    {
        XX_Print("0x%08X: %08x %08x %08x %08x\r\n",
                 p, *(uint32_t *)p, *(uint32_t *)(p + 4),
                 *(uint32_t *)(p + 8), *(uint32_t *)(p + 12));
        size -= 16;
        p += 16;
    }

    if (size)
    {
        XX_Print("0x%08X: ", p);

        while (size > 3)
        {
            XX_Print("%08x ", *(uint32_t *)p);
            size -= 4;
            p += 4;
        }
        while (size)
        {
            XX_Print("%02x", *(uint8_t *)p);
            size--;
            p++;
        }

        XX_Print("\r\n");
    }
}
