/* generated from cwcdma.osp DO NOT MODIFY */

#ifndef __HEADER_cwcdma_H__
#define __HEADER_cwcdma_H__

static struct dsp_symbol_entry cwcdma_symbols[] = {
  { 0x8000, "EXECCHILD",0x03 },
  { 0x8001, "EXECCHILD_98",0x03 },
  { 0x8003, "EXECCHILD_PUSH1IND",0x03 },
  { 0x8008, "EXECSIBLING",0x03 },
  { 0x800a, "EXECSIBLING_298",0x03 },
  { 0x800b, "EXECSIBLING_2IND1",0x03 },
  { 0x8010, "TIMINGMASTER",0x03 },
  { 0x804f, "S16_CODECINPUTTASK",0x03 },
  { 0x805e, "PCMSERIALINPUTTASK",0x03 },
  { 0x806d, "S16_MIX_TO_OSTREAM",0x03 },
  { 0x809a, "S16_MIX",0x03 },
  { 0x80bb, "S16_UPSRC",0x03 },
  { 0x813b, "MIX3_EXP",0x03 },
  { 0x8164, "DECIMATEBYPOW2",0x03 },
  { 0x8197, "VARIDECIMATE",0x03 },
  { 0x81f2, "_3DINPUTTASK",0x03 },
  { 0x820a, "_3DPRLGCINPTASK",0x03 },
  { 0x8227, "_3DSTEREOINPUTTASK",0x03 },
  { 0x8242, "_3DOUTPUTTASK",0x03 },
  { 0x82c4, "HRTF_MORPH_TASK",0x03 },
  { 0x82c6, "WAIT4DATA",0x03 },
  { 0x82fa, "PROLOGIC",0x03 },
  { 0x8496, "DECORRELATOR",0x03 },
  { 0x84a4, "STEREO2MONO",0x03 },
  { 0x0000, "OVERLAYBEGINADDRESS",0x00 },
  { 0x0000, "DMAREADER",0x03 },
  { 0x0018, "#CODE_END",0x00 },
}; /* cwcdma symbols */

static u32 cwcdma_code[] = {
/* OVERLAYBEGINADDRESS */
/* 0000 */ 0x00002731,0x00001400,0x0004c108,0x000e5044,
/* 0002 */ 0x0005f608,0x00000000,0x000007ae,0x000be300,
/* 0004 */ 0x00058630,0x00001400,0x0007afb0,0x000e9584,
/* 0006 */ 0x00007301,0x000a9840,0x0005e708,0x000cd104,
/* 0008 */ 0x00067008,0x00000000,0x000902a0,0x00001000,
/* 000A */ 0x00012a01,0x000c0000,0x00000000,0x00000000,
/* 000C */ 0x00021843,0x000c0000,0x00000000,0x000c0000,
/* 000E */ 0x0000e101,0x000c0000,0x00000cac,0x00000000,
/* 0010 */ 0x00080000,0x000e5ca1,0x00000000,0x000c0000,
/* 0012 */ 0x00000000,0x00000000,0x00000000,0x00092c00,
/* 0014 */ 0x000122c1,0x000e5084,0x00058730,0x00001400,
/* 0016 */ 0x000d7488,0x000e4782,0x00007401,0x0001c100
};

/* #CODE_END */

static struct dsp_segment_desc cwcdma_segments[] = {
  { SEGTYPE_SP_PROGRAM, 0x00000000, 0x00000030, cwcdma_code },
};

static struct dsp_module_desc cwcdma_module = {
  "cwcdma",
  {
    27,
    cwcdma_symbols
  },
  1,
  cwcdma_segments,
};

#endif /* __HEADER_cwcdma_H__ */
