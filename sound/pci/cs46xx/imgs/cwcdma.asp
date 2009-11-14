// 
//  Copyright(c) by Benny Sjostrand (benny@hostmobility.com)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
//


//
// This code runs inside the DSP (cs4610, cs4612, cs4624, or cs4630),
// to compile it you need a tool named SPASM 3.0 and DSP code owned by 
// Cirrus Logic(R). The SPASM program will generate a object file (cwcdma.osp),
// the "ospparser"  tool will genereate the cwcdma.h file it's included from
// the cs46xx_lib.c file.
//
//
// The purpose of this code is very simple: make it possible to tranfser
// the samples 'as they are' with no alteration from a PCMreader
// SCB (DMA from host) to any other SCB. This is useful for AC3 through SPDIF.
// SRC (source rate converters) task always alters the samples in somehow,
// however it's from 48khz -> 48khz.
// The alterations are not audible, but AC3 wont work. 
//
//        ...
//         |
// +---------------+
// | AsynchFGTxSCB |
// +---------------+
//        |
//    subListPtr
//        |
// +--------------+
// |   DMAReader  |
// +--------------+
//        |
//    subListPtr
//        |
// +-------------+
// | PCMReader   |
// +-------------+
// (DMA from host)
//

struct dmaSCB
  {
    long  dma_reserved1[3];

    short dma_reserved2:dma_outBufPtr;

    short dma_unused1:dma_unused2;

    long  dma_reserved3[4];

    short dma_subListPtr:dma_nextSCB;
    short dma_SPBptr:dma_entryPoint;

    long  dma_strmRsConfig;
    long  dma_strmBufPtr;

    long  dma_reserved4;

    VolumeControl s2m_volume;
  };

#export DMAReader
void DMAReader()
{
  execChild();
  r2 = r0->dma_subListPtr;
  r1 = r0->nextSCB;
	
  rsConfig01 = r2->strmRsConfig;
  // Load rsConfig for input buffer

  rsDMA01 = r2->basicReq.daw,       ,                   tb = Z(0 - rf);
  // Load rsDMA in case input buffer is a DMA buffer    Test to see if there is any data to transfer

  if (tb) goto execSibling_2ind1 after {
      r5 = rf + (-1);
      r6 = r1->dma_entryPoint;           // r6 = entry point of sibling task
      r1 = r1->dma_SPBptr,               // r1 = pointer to sibling task's SPB
          ,   ind = r6;                  // Load entry point of sibling task
  }

  rsConfig23 = r0->dma_strmRsConfig;
  // Load rsConfig for output buffer (never a DMA buffer)

  r4 = r0->dma_outBufPtr;

  rsa0 = r2->strmBufPtr;
  // rsa0 = input buffer pointer                        

  for (i = r5; i >= 0; --i)
    after {
      rsa2 = r4;
      // rsa2 = output buffer pointer

      nop;
      nop;
    }
  //*****************************
  // TODO: cycles to this point *
  //*****************************
    {
      acc0 =  (rsd0 = *rsa0++1);
      // get sample

      nop;  // Those "nop"'s are really uggly, but there's
      nop;  // something with DSP's pipelines which I don't
      nop;  // understand, resulting this code to fail without
            // having those "nop"'s (Benny)

      rsa0?reqDMA = r2;
      // Trigger DMA transfer on input stream, 
      // if needed to replenish input buffer

      nop;
      // Yet another magic "nop" to make stuff work

      ,,r98 = acc0 $+>> 0;
      // store sample in ALU

      nop;
      // latency on load register.
      // (this one is understandable)

      *rsa2++1 = r98;
      // store sample in output buffer

      nop; // The same story
      nop; // as above again ...
      nop;
    }
  // TODO: cycles per loop iteration

  r2->strmBufPtr = rsa0,,   ;
  // Update the modified buffer pointers

  r4 = rsa2;
  // Load output pointer position into r4

  r2 = r0->nextSCB;
  // Sibling task

  goto execSibling_2ind1 // takes 6 cycles
    after {
      r98 = r2->thisSPB:entryPoint;
      // Load child routine entry and data address 

      r1 = r9;
      // r9 is r2->thisSPB

      r0->dma_outBufPtr = r4,,
      // Store updated output buffer pointer

      ind = r8;
      // r8 is r2->entryPoint
    }
}
