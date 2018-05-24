/*
    msp3400.h - definition for msp3400 inputs and outputs

    Copyright (C) 2006 Hans Verkuil (hverkuil@xs4all.nl)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _MSP3400_H_
#define _MSP3400_H_

/* msp3400 routing
   ===============

   The msp3400 has a complicated routing scheme with many possible
   combinations. The details are all in the datasheets but I will try
   to give a short description here.

   Inputs
   ======

   There are 1) tuner inputs, 2) I2S inputs, 3) SCART inputs. You will have
   to select which tuner input to use and which SCART input to use. The
   selected tuner input, the selected SCART input and all I2S inputs go to
   the DSP (the tuner input first goes through the demodulator).

   The DSP handles things like volume, bass/treble, balance, and some chips
   have support for surround sound. It has several outputs: MAIN, AUX, I2S
   and SCART1/2. Each output can select which DSP input to use. So the MAIN
   output can select the tuner input while at the same time the SCART1 output
   uses the I2S input.

   Outputs
   =======

   Most DSP outputs are also the outputs of the msp3400. However, the SCART
   outputs of the msp3400 can select which input to use: either the SCART1 or
   SCART2 output from the DSP, or the msp3400 SCART inputs, thus completely
   bypassing the DSP.

   Summary
   =======

   So to specify a complete routing scheme for the msp3400 you will have to
   specify in the 'input' arg of the s_routing function:

   1) which tuner input to use
   2) which SCART input to use
   3) which DSP input to use for each DSP output

   And in the 'output' arg of the s_routing function you specify:

   1) which SCART input to use for each SCART output

   Depending on how the msp is wired to the other components you can
   ignore or mute certain inputs or outputs.

   Also, depending on the msp version only a subset of the inputs or
   outputs may be present. At the end of this header some tables are
   added containing a list of what is available for each msp version.
 */

/* Inputs to the DSP unit: two independent selections have to be made:
   1) the tuner (SIF) input
   2) the SCART input
   Bits 0-2 are used for the SCART input select, bit 3 is used for the tuner
   input, bits 4-7 are reserved.
 */

/* SCART input to DSP selection */
#define MSP_IN_SCART1		0  /* Pin SC1_IN */
#define MSP_IN_SCART2		1  /* Pin SC2_IN */
#define MSP_IN_SCART3		2  /* Pin SC3_IN */
#define MSP_IN_SCART4		3  /* Pin SC4_IN */
#define MSP_IN_MONO		6  /* Pin MONO_IN */
#define MSP_IN_MUTE		7  /* Mute DSP input */
#define MSP_SCART_TO_DSP(in)	(in)
/* Tuner input to demodulator and DSP selection */
#define MSP_IN_TUNER1		0  /* Analog Sound IF input pin ANA_IN1 */
#define MSP_IN_TUNER2		1  /* Analog Sound IF input pin ANA_IN2 */
#define MSP_TUNER_TO_DSP(in)	((in) << 3)

/* The msp has up to 5 DSP outputs, each output can independently select
   a DSP input.

   The DSP outputs are: loudspeaker output (aka MAIN), headphones output
   (aka AUX), SCART1 DA output, SCART2 DA output and an I2S output.
   There also is a quasi-peak detector output, but that is not used by
   this driver and is set to the same input as the loudspeaker output.
   Not all outputs are supported by all msp models. Setting the input
   of an unsupported output will be ignored by the driver.

   There are up to 16 DSP inputs to choose from, so each output is
   assigned 4 bits.

   Note: the 44x8G can mix two inputs and feed the result back to the
   DSP. This is currently not implemented. Also not implemented is the
   multi-channel capable I2S3 input of the 44x0G. If someone can demonstrate
   a need for one of those features then additional support can be added. */
#define MSP_DSP_IN_TUNER	0  /* Tuner DSP input */
#define MSP_DSP_IN_SCART	2  /* SCART DSP input */
#define MSP_DSP_IN_I2S1		5  /* I2S1 DSP input */
#define MSP_DSP_IN_I2S2		6  /* I2S2 DSP input */
#define MSP_DSP_IN_I2S3		7  /* I2S3 DSP input */
#define MSP_DSP_IN_MAIN_AVC	11 /* MAIN AVC processed DSP input */
#define MSP_DSP_IN_MAIN		12 /* MAIN DSP input */
#define MSP_DSP_IN_AUX		13 /* AUX DSP input */
#define MSP_DSP_TO_MAIN(in)	((in) << 4)
#define MSP_DSP_TO_AUX(in)	((in) << 8)
#define MSP_DSP_TO_SCART1(in)	((in) << 12)
#define MSP_DSP_TO_SCART2(in)	((in) << 16)
#define MSP_DSP_TO_I2S(in)	((in) << 20)

/* Output SCART select: the SCART outputs can select which input
   to use. */
#define MSP_SC_IN_SCART1	0  /* SCART1 input, bypassing the DSP */
#define MSP_SC_IN_SCART2	1  /* SCART2 input, bypassing the DSP */
#define MSP_SC_IN_SCART3	2  /* SCART3 input, bypassing the DSP */
#define MSP_SC_IN_SCART4	3  /* SCART4 input, bypassing the DSP */
#define MSP_SC_IN_DSP_SCART1	4  /* DSP SCART1 input */
#define MSP_SC_IN_DSP_SCART2	5  /* DSP SCART2 input */
#define MSP_SC_IN_MONO		6  /* MONO input, bypassing the DSP */
#define MSP_SC_IN_MUTE		7  /* MUTE output */
#define MSP_SC_TO_SCART1(in)	(in)
#define MSP_SC_TO_SCART2(in)	((in) << 4)

/* Shortcut macros */
#define MSP_INPUT(sc, t, main_aux_src, sc_i2s_src) \
	(MSP_SCART_TO_DSP(sc) | \
	 MSP_TUNER_TO_DSP(t) | \
	 MSP_DSP_TO_MAIN(main_aux_src) | \
	 MSP_DSP_TO_AUX(main_aux_src) | \
	 MSP_DSP_TO_SCART1(sc_i2s_src) | \
	 MSP_DSP_TO_SCART2(sc_i2s_src) | \
	 MSP_DSP_TO_I2S(sc_i2s_src))
#define MSP_INPUT_DEFAULT MSP_INPUT(MSP_IN_SCART1, MSP_IN_TUNER1, \
				    MSP_DSP_IN_TUNER, MSP_DSP_IN_TUNER)
#define MSP_OUTPUT(sc) \
	(MSP_SC_TO_SCART1(sc) | \
	 MSP_SC_TO_SCART2(sc))
/* This equals the RESET position of the msp3400 ACB register */
#define MSP_OUTPUT_DEFAULT (MSP_SC_TO_SCART1(MSP_SC_IN_SCART3) | \
			    MSP_SC_TO_SCART2(MSP_SC_IN_DSP_SCART1))

/* Tuner inputs vs. msp version */
/* Chip      TUNER_1   TUNER_2
   -------------------------
   msp34x0b  y         y
   msp34x0c  y         y
   msp34x0d  y         y
   msp34x5d  y         n
   msp34x7d  y         n
   msp34x0g  y         y
   msp34x1g  y         y
   msp34x2g  y         y
   msp34x5g  y         n
   msp34x7g  y         n
   msp44x0g  y         y
   msp44x8g  y         y
 */

/* SCART inputs vs. msp version */
/* Chip      SC1 SC2 SC3 SC4
   -------------------------
   msp34x0b  y   y   y   n
   msp34x0c  y   y   y   n
   msp34x0d  y   y   y   y
   msp34x5d  y   y   n   n
   msp34x7d  y   n   n   n
   msp34x0g  y   y   y   y
   msp34x1g  y   y   y   y
   msp34x2g  y   y   y   y
   msp34x5g  y   y   n   n
   msp34x7g  y   n   n   n
   msp44x0g  y   y   y   y
   msp44x8g  y   y   y   y
 */

/* DSP inputs vs. msp version (tuner and SCART inputs are always available) */
/* Chip      I2S1 I2S2 I2S3 MAIN_AVC MAIN AUX
   ------------------------------------------
   msp34x0b  y    n    n    n        n    n
   msp34x0c  y    y    n    n        n    n
   msp34x0d  y    y    n    n        n    n
   msp34x5d  y    y    n    n        n    n
   msp34x7d  n    n    n    n        n    n
   msp34x0g  y    y    n    n        n    n
   msp34x1g  y    y    n    n        n    n
   msp34x2g  y    y    n    y        y    y
   msp34x5g  y    y    n    n        n    n
   msp34x7g  n    n    n    n        n    n
   msp44x0g  y    y    y    y        y    y
   msp44x8g  y    y    y    n        n    n
 */

/* DSP outputs vs. msp version */
/* Chip      MAIN AUX SCART1 SCART2 I2S
   ------------------------------------
   msp34x0b  y    y   y      n      y
   msp34x0c  y    y   y      n      y
   msp34x0d  y    y   y      y      y
   msp34x5d  y    n   y      n      y
   msp34x7d  y    n   y      n      n
   msp34x0g  y    y   y      y      y
   msp34x1g  y    y   y      y      y
   msp34x2g  y    y   y      y      y
   msp34x5g  y    n   y      n      y
   msp34x7g  y    n   y      n      n
   msp44x0g  y    y   y      y      y
   msp44x8g  y    y   y      y      y
 */

#endif /* MSP3400_H */
