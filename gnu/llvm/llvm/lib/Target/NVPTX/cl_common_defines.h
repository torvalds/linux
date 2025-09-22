#ifndef CL_COMMON_DEFINES_H
#define CL_COMMON_DEFINES_H
// This file includes defines that are common to both kernel code and
// the NVPTX back-end.

//
// Common defines for Image intrinsics
// Channel order
enum {
  CLK_R = 0x10B0,
  CLK_A = 0x10B1,
  CLK_RG = 0x10B2,
  CLK_RA = 0x10B3,
  CLK_RGB = 0x10B4,
  CLK_RGBA = 0x10B5,
  CLK_BGRA = 0x10B6,
  CLK_ARGB = 0x10B7,

#if (__NV_CL_C_VERSION == __NV_CL_C_VERSION_1_0)
  CLK_xRGB = 0x10B7,
#endif

  CLK_INTENSITY = 0x10B8,
  CLK_LUMINANCE = 0x10B9

#if (__NV_CL_C_VERSION >= __NV_CL_C_VERSION_1_1)
      ,
  CLK_Rx = 0x10BA,
  CLK_RGx = 0x10BB,
  CLK_RGBx = 0x10BC
#endif
};

typedef enum clk_channel_type {
  // valid formats for float return types
  CLK_SNORM_INT8 = 0x10D0,  // four channel RGBA unorm8
  CLK_SNORM_INT16 = 0x10D1, // four channel RGBA unorm16
  CLK_UNORM_INT8 = 0x10D2,  // four channel RGBA unorm8
  CLK_UNORM_INT16 = 0x10D3, // four channel RGBA unorm16
  CLK_HALF_FLOAT = 0x10DD,  // four channel RGBA half
  CLK_FLOAT = 0x10DE,       // four channel RGBA float

#if (__NV_CL_C_VERSION >= __NV_CL_C_VERSION_1_1)
  CLK_UNORM_SHORT_565 = 0x10D4,
  CLK_UNORM_SHORT_555 = 0x10D5,
  CLK_UNORM_INT_101010 = 0x10D6,
#endif

  // valid only for integer return types
  CLK_SIGNED_INT8 = 0x10D7,
  CLK_SIGNED_INT16 = 0x10D8,
  CLK_SIGNED_INT32 = 0x10D9,
  CLK_UNSIGNED_INT8 = 0x10DA,
  CLK_UNSIGNED_INT16 = 0x10DB,
  CLK_UNSIGNED_INT32 = 0x10DC,

  // CI SPI for CPU
  __CLK_UNORM_INT8888,  // four channel ARGB unorm8
  __CLK_UNORM_INT8888R, // four channel BGRA unorm8

  __CLK_VALID_IMAGE_TYPE_COUNT,
  __CLK_INVALID_IMAGE_TYPE = __CLK_VALID_IMAGE_TYPE_COUNT,
  __CLK_VALID_IMAGE_TYPE_MASK_BITS = 4, // number of bits required to
                                        // represent any image type
  __CLK_VALID_IMAGE_TYPE_MASK = (1 << __CLK_VALID_IMAGE_TYPE_MASK_BITS) - 1
} clk_channel_type;

typedef enum clk_sampler_type {
  __CLK_ADDRESS_BASE = 0,
  CLK_ADDRESS_NONE = 0 << __CLK_ADDRESS_BASE,
  CLK_ADDRESS_CLAMP = 1 << __CLK_ADDRESS_BASE,
  CLK_ADDRESS_CLAMP_TO_EDGE = 2 << __CLK_ADDRESS_BASE,
  CLK_ADDRESS_REPEAT = 3 << __CLK_ADDRESS_BASE,
  CLK_ADDRESS_MIRROR = 4 << __CLK_ADDRESS_BASE,

#if (__NV_CL_C_VERSION >= __NV_CL_C_VERSION_1_1)
  CLK_ADDRESS_MIRRORED_REPEAT = CLK_ADDRESS_MIRROR,
#endif
  __CLK_ADDRESS_MASK =
      CLK_ADDRESS_NONE | CLK_ADDRESS_CLAMP | CLK_ADDRESS_CLAMP_TO_EDGE |
      CLK_ADDRESS_REPEAT | CLK_ADDRESS_MIRROR,
  __CLK_ADDRESS_BITS = 3, // number of bits required to
                          // represent address info

  __CLK_NORMALIZED_BASE = __CLK_ADDRESS_BITS,
  CLK_NORMALIZED_COORDS_FALSE = 0,
  CLK_NORMALIZED_COORDS_TRUE = 1 << __CLK_NORMALIZED_BASE,
  __CLK_NORMALIZED_MASK =
      CLK_NORMALIZED_COORDS_FALSE | CLK_NORMALIZED_COORDS_TRUE,
  __CLK_NORMALIZED_BITS = 1, // number of bits required to
                             // represent normalization

  __CLK_FILTER_BASE = __CLK_NORMALIZED_BASE + __CLK_NORMALIZED_BITS,
  CLK_FILTER_NEAREST = 0 << __CLK_FILTER_BASE,
  CLK_FILTER_LINEAR = 1 << __CLK_FILTER_BASE,
  CLK_FILTER_ANISOTROPIC = 2 << __CLK_FILTER_BASE,
  __CLK_FILTER_MASK =
      CLK_FILTER_NEAREST | CLK_FILTER_LINEAR | CLK_FILTER_ANISOTROPIC,
  __CLK_FILTER_BITS = 2, // number of bits required to
                         // represent address info

  __CLK_MIP_BASE = __CLK_FILTER_BASE + __CLK_FILTER_BITS,
  CLK_MIP_NEAREST = 0 << __CLK_MIP_BASE,
  CLK_MIP_LINEAR = 1 << __CLK_MIP_BASE,
  CLK_MIP_ANISOTROPIC = 2 << __CLK_MIP_BASE,
  __CLK_MIP_MASK = CLK_MIP_NEAREST | CLK_MIP_LINEAR | CLK_MIP_ANISOTROPIC,
  __CLK_MIP_BITS = 2,

  __CLK_SAMPLER_BITS = __CLK_MIP_BASE + __CLK_MIP_BITS,
  __CLK_SAMPLER_MASK = __CLK_MIP_MASK | __CLK_FILTER_MASK |
                       __CLK_NORMALIZED_MASK | __CLK_ADDRESS_MASK,

  __CLK_ANISOTROPIC_RATIO_BITS = 5,
  __CLK_ANISOTROPIC_RATIO_MASK =
      (int) 0x80000000 >> (__CLK_ANISOTROPIC_RATIO_BITS - 1)
} clk_sampler_type;

// Memory synchronization
#define CLK_LOCAL_MEM_FENCE (1 << 0)
#define CLK_GLOBAL_MEM_FENCE (1 << 1)

#endif // CL_COMMON_DEFINES_H
