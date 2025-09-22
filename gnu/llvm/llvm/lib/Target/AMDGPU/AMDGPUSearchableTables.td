//===-- AMDGPUSearchableTables.td - ------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Resource intrinsics table.
//===----------------------------------------------------------------------===//

class RsrcIntrinsic<AMDGPURsrcIntrinsic intr> {
  Intrinsic Intr = !cast<Intrinsic>(intr);
  bits<8> RsrcArg = intr.RsrcArg;
  bit IsImage = intr.IsImage;
}

def RsrcIntrinsics : GenericTable {
  let FilterClass = "RsrcIntrinsic";
  let Fields = ["Intr", "RsrcArg", "IsImage"];

  let PrimaryKey = ["Intr"];
  let PrimaryKeyName = "lookupRsrcIntrinsic";
}

foreach intr = !listconcat(AMDGPUBufferIntrinsics,
                           AMDGPUImageDimIntrinsics,
                           AMDGPUImageDimAtomicIntrinsics) in {
  def : RsrcIntrinsic<!cast<AMDGPURsrcIntrinsic>(intr)>;
}

class GcnBufferFormatBase<bits<8> f, bits<8> bpc, bits<8> numc, bits<8> nfmt, bits<8> dfmt> {
  bits<8> Format = f;
  bits<8> BitsPerComp = bpc;
  bits<8> NumComponents = numc;
  bits<8> NumFormat = nfmt;
  bits<8> DataFormat = dfmt;
}

class Gfx9BufferFormat<bits<8> f, bits<8> bpc, bits<8> numc, bits<8> nfmt, bits<8> dfmt> : GcnBufferFormatBase<f, bpc, numc, nfmt, dfmt>;
class Gfx10BufferFormat<bits<8> f, bits<8> bpc, bits<8> numc, bits<8> nfmt, bits<8> dfmt> : GcnBufferFormatBase<f, bpc, numc, nfmt, dfmt>;
class Gfx11PlusBufferFormat<bits<8> f, bits<8> bpc, bits<8> numc, bits<8> nfmt, bits<8> dfmt> : GcnBufferFormatBase<f, bpc, numc, nfmt, dfmt>;

class GcnBufferFormatTable  : GenericTable {
  let CppTypeName = "GcnBufferFormatInfo";
  let Fields = ["Format", "BitsPerComp", "NumComponents", "NumFormat", "DataFormat"];
  let PrimaryKey = ["BitsPerComp", "NumComponents", "NumFormat"];
}

def Gfx9BufferFormat : GcnBufferFormatTable {
  let FilterClass = "Gfx9BufferFormat";
  let PrimaryKeyName = "getGfx9BufferFormatInfo";
}
def Gfx10BufferFormat : GcnBufferFormatTable {
  let FilterClass = "Gfx10BufferFormat";
  let PrimaryKeyName = "getGfx10BufferFormatInfo";
}
def Gfx11PlusBufferFormat : GcnBufferFormatTable {
  let FilterClass = "Gfx11PlusBufferFormat";
  let PrimaryKeyName = "getGfx11PlusBufferFormatInfo";
}

def getGfx9BufferFormatInfo : SearchIndex {
  let Table = Gfx9BufferFormat;
  let Key = ["Format"];
}
def getGfx10BufferFormatInfo : SearchIndex {
  let Table = Gfx10BufferFormat;
  let Key = ["Format"];
}
def getGfx11PlusBufferFormatInfo : SearchIndex {
  let Table = Gfx11PlusBufferFormat;
  let Key = ["Format"];
}

// Buffer formats with equal component sizes (GFX9 and earlier)
def : Gfx9BufferFormat< /*FORMAT_8_UNORM*/              0x01,  8, 1, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_8*/            1>;
def : Gfx9BufferFormat< /*FORMAT_8_SNORM*/              0x11,  8, 1, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_8*/            1>;
def : Gfx9BufferFormat< /*FORMAT_8_USCALED*/            0x21,  8, 1, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_8*/            1>;
def : Gfx9BufferFormat< /*FORMAT_8_SSCALED*/            0x31,  8, 1, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_8*/            1>;
def : Gfx9BufferFormat< /*FORMAT_8_UINT*/               0x41,  8, 1, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_8*/            1>;
def : Gfx9BufferFormat< /*FORMAT_8_SINT*/               0x51,  8, 1, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_8*/            1>;
def : Gfx9BufferFormat< /*FORMAT_16_UNORM*/             0x02, 16, 1, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_16*/           2>;
def : Gfx9BufferFormat< /*FORMAT_16_SNORM*/             0x12, 16, 1, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_16*/           2>;
def : Gfx9BufferFormat< /*FORMAT_16_USCALED*/           0x22, 16, 1, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_16*/           2>;
def : Gfx9BufferFormat< /*FORMAT_16_SSCALED*/           0x32, 16, 1, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_16*/           2>;
def : Gfx9BufferFormat< /*FORMAT_16_UINT*/              0x42, 16, 1, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_16*/           2>;
def : Gfx9BufferFormat< /*FORMAT_16_SINT*/              0x52, 16, 1, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_16*/           2>;
def : Gfx9BufferFormat< /*FORMAT_16_FLOAT*/             0x72, 16, 1, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_16*/           2>;
def : Gfx9BufferFormat< /*FORMAT_8_8_UNORM*/            0x03,  8, 2, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_8_8*/          3>;
def : Gfx9BufferFormat< /*FORMAT_8_8_SNORM*/            0x13,  8, 2, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_8_8*/          3>;
def : Gfx9BufferFormat< /*FORMAT_8_8_USCALED*/          0x23,  8, 2, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_8_8*/          3>;
def : Gfx9BufferFormat< /*FORMAT_8_8_SSCALED*/          0x33,  8, 2, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_8_8*/          3>;
def : Gfx9BufferFormat< /*FORMAT_8_8_UINT*/             0x43,  8, 2, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_8_8*/          3>;
def : Gfx9BufferFormat< /*FORMAT_8_8_SINT*/             0x53,  8, 2, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_8_8*/          3>;
def : Gfx9BufferFormat< /*FORMAT_32_UINT*/              0x44, 32, 1, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32*/           4>;
def : Gfx9BufferFormat< /*FORMAT_32_SINT*/              0x54, 32, 1, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32*/           4>;
def : Gfx9BufferFormat< /*FORMAT_32_FLOAT*/             0x74, 32, 1, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32*/           4>;
def : Gfx9BufferFormat< /*FORMAT_16_16_UNORM*/          0x05, 16, 2, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_16_16*/        5>;
def : Gfx9BufferFormat< /*FORMAT_16_16_SNORM*/          0x15, 16, 2, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_16_16*/        5>;
def : Gfx9BufferFormat< /*FORMAT_16_16_USCALED*/        0x25, 16, 2, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_16_16*/        5>;
def : Gfx9BufferFormat< /*FORMAT_16_16_SSCALED*/        0x35, 16, 2, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_16_16*/        5>;
def : Gfx9BufferFormat< /*FORMAT_16_16_UINT*/           0x45, 16, 2, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_16_16*/        5>;
def : Gfx9BufferFormat< /*FORMAT_16_16_SINT*/           0x55, 16, 2, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_16_16*/        5>;
def : Gfx9BufferFormat< /*FORMAT_16_16_FLOAT*/          0x75, 16, 2, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_16_16*/        5>;
def : Gfx9BufferFormat< /*FORMAT_8_8_8_8_UNORM*/        0x0A,  8, 4, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx9BufferFormat< /*FORMAT_8_8_8_8_SNORM*/        0x1A,  8, 4, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx9BufferFormat< /*FORMAT_8_8_8_8_USCALED*/      0x2A,  8, 4, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx9BufferFormat< /*FORMAT_8_8_8_8_SSCALED*/      0x3A,  8, 4, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx9BufferFormat< /*FORMAT_8_8_8_8_UINT*/         0x4A,  8, 4, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx9BufferFormat< /*FORMAT_8_8_8_8_SINT*/         0x5A,  8, 4, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx9BufferFormat< /*FORMAT_32_32_UINT*/           0x4B, 32, 2, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32_32*/       11>;
def : Gfx9BufferFormat< /*FORMAT_32_32_SINT*/           0x5B, 32, 2, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32_32*/       11>;
def : Gfx9BufferFormat< /*FORMAT_32_32_FLOAT*/          0x7B, 32, 2, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32_32*/       11>;
def : Gfx9BufferFormat< /*FORMAT_16_16_16_16_UNORM*/    0x0C, 16, 4, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx9BufferFormat< /*FORMAT_16_16_16_16_SNORM*/    0x1C, 16, 4, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx9BufferFormat< /*FORMAT_16_16_16_16_USCALED*/  0x2C, 16, 4, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx9BufferFormat< /*FORMAT_16_16_16_16_SSCALED*/  0x3C, 16, 4, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx9BufferFormat< /*FORMAT_16_16_16_16_UINT*/     0x4C, 16, 4, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx9BufferFormat< /*FORMAT_16_16_16_16_SINT*/     0x5C, 16, 4, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx9BufferFormat< /*FORMAT_16_16_16_16_FLOAT*/    0x7C, 16, 4, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx9BufferFormat< /*FORMAT_32_32_32_UINT*/        0x4D, 32, 3, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32_32_32*/    13>;
def : Gfx9BufferFormat< /*FORMAT_32_32_32_SINT*/        0x5D, 32, 3, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32_32_32*/    13>;
def : Gfx9BufferFormat< /*FORMAT_32_32_32_FLOAT*/       0x7D, 32, 3, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32_32_32*/    13>;
def : Gfx9BufferFormat< /*FORMAT_32_32_32_32_UINT*/     0x4E, 32, 4, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32_32_32_32*/ 14>;
def : Gfx9BufferFormat< /*FORMAT_32_32_32_32_SINT*/     0x5E, 32, 4, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32_32_32_32*/ 14>;
def : Gfx9BufferFormat< /*FORMAT_32_32_32_32_FLOAT*/    0x7E, 32, 4, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32_32_32_32*/ 14>;

// Buffer formats with equal component sizes (GFX10 and later)
multiclass Gfx10PlusBufferFormat<bits<8> f, bits<8> bpc, bits<8> numc, bits<8> nfmt, bits<8> dfmt> {
  def : Gfx10BufferFormat<f, bpc, numc, nfmt, dfmt>;
  def : Gfx11PlusBufferFormat<f, bpc, numc, nfmt, dfmt>;
}
defm : Gfx10PlusBufferFormat< /*FORMAT_8_UNORM*/              0x01,  8, 1, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_8*/            1>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_SNORM*/              0x02,  8, 1, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_8*/            1>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_USCALED*/            0x03,  8, 1, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_8*/            1>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_SSCALED*/            0x04,  8, 1, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_8*/            1>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_UINT*/               0x05,  8, 1, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_8*/            1>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_SINT*/               0x06,  8, 1, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_8*/            1>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_UNORM*/             0x07, 16, 1, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_16*/           2>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_SNORM*/             0x08, 16, 1, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_16*/           2>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_USCALED*/           0x09, 16, 1, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_16*/           2>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_SSCALED*/           0x0A, 16, 1, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_16*/           2>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_UINT*/              0x0B, 16, 1, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_16*/           2>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_SINT*/              0x0C, 16, 1, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_16*/           2>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_FLOAT*/             0x0D, 16, 1, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_16*/           2>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_8_UNORM*/            0x0E,  8, 2, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_8_8*/          3>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_8_SNORM*/            0x0F,  8, 2, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_8_8*/          3>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_8_USCALED*/          0x10,  8, 2, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_8_8*/          3>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_8_SSCALED*/          0x11,  8, 2, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_8_8*/          3>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_8_UINT*/             0x12,  8, 2, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_8_8*/          3>;
defm : Gfx10PlusBufferFormat< /*FORMAT_8_8_SINT*/             0x13,  8, 2, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_8_8*/          3>;
defm : Gfx10PlusBufferFormat< /*FORMAT_32_UINT*/              0x14, 32, 1, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32*/           4>;
defm : Gfx10PlusBufferFormat< /*FORMAT_32_SINT*/              0x15, 32, 1, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32*/           4>;
defm : Gfx10PlusBufferFormat< /*FORMAT_32_FLOAT*/             0x16, 32, 1, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32*/           4>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_16_UNORM*/          0x17, 16, 2, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_16_16*/        5>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_16_SNORM*/          0x18, 16, 2, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_16_16*/        5>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_16_USCALED*/        0x19, 16, 2, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_16_16*/        5>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_16_SSCALED*/        0x1A, 16, 2, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_16_16*/        5>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_16_UINT*/           0x1B, 16, 2, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_16_16*/        5>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_16_SINT*/           0x1C, 16, 2, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_16_16*/        5>;
defm : Gfx10PlusBufferFormat< /*FORMAT_16_16_FLOAT*/          0x1D, 16, 2, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_16_16*/        5>;

// Buffer formats with equal component sizes (GFX10 only)
def : Gfx10BufferFormat< /*FORMAT_8_8_8_8_UNORM*/        0x38,  8, 4, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx10BufferFormat< /*FORMAT_8_8_8_8_SNORM*/        0x39,  8, 4, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx10BufferFormat< /*FORMAT_8_8_8_8_USCALED*/      0x3A,  8, 4, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx10BufferFormat< /*FORMAT_8_8_8_8_SSCALED*/      0x3B,  8, 4, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx10BufferFormat< /*FORMAT_8_8_8_8_UINT*/         0x3C,  8, 4, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx10BufferFormat< /*FORMAT_8_8_8_8_SINT*/         0x3D,  8, 4, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx10BufferFormat< /*FORMAT_32_32_UINT*/           0x3E, 32, 2, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32_32*/       11>;
def : Gfx10BufferFormat< /*FORMAT_32_32_SINT*/           0x3F, 32, 2, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32_32*/       11>;
def : Gfx10BufferFormat< /*FORMAT_32_32_FLOAT*/          0x40, 32, 2, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32_32*/       11>;
def : Gfx10BufferFormat< /*FORMAT_16_16_16_16_UNORM*/    0x41, 16, 4, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx10BufferFormat< /*FORMAT_16_16_16_16_SNORM*/    0x42, 16, 4, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx10BufferFormat< /*FORMAT_16_16_16_16_USCALED*/  0x43, 16, 4, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx10BufferFormat< /*FORMAT_16_16_16_16_SSCALED*/  0x44, 16, 4, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx10BufferFormat< /*FORMAT_16_16_16_16_UINT*/     0x45, 16, 4, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx10BufferFormat< /*FORMAT_16_16_16_16_SINT*/     0x46, 16, 4, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx10BufferFormat< /*FORMAT_16_16_16_16_FLOAT*/    0x47, 16, 4, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx10BufferFormat< /*FORMAT_32_32_32_UINT*/        0x48, 32, 3, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32_32_32*/    13>;
def : Gfx10BufferFormat< /*FORMAT_32_32_32_SINT*/        0x49, 32, 3, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32_32_32*/    13>;
def : Gfx10BufferFormat< /*FORMAT_32_32_32_FLOAT*/       0x4A, 32, 3, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32_32_32*/    13>;
def : Gfx10BufferFormat< /*FORMAT_32_32_32_32_UINT*/     0x4B, 32, 4, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32_32_32_32*/ 14>;
def : Gfx10BufferFormat< /*FORMAT_32_32_32_32_SINT*/     0x4C, 32, 4, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32_32_32_32*/ 14>;
def : Gfx10BufferFormat< /*FORMAT_32_32_32_32_FLOAT*/    0x4D, 32, 4, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32_32_32_32*/ 14>;

// Buffer formats with equal component sizes (GFX11 and later)
def : Gfx11PlusBufferFormat< /*FORMAT_8_8_8_8_UNORM*/        0x2A,  8, 4, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx11PlusBufferFormat< /*FORMAT_8_8_8_8_SNORM*/        0x2B,  8, 4, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx11PlusBufferFormat< /*FORMAT_8_8_8_8_USCALED*/      0x2C,  8, 4, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx11PlusBufferFormat< /*FORMAT_8_8_8_8_SSCALED*/      0x2D,  8, 4, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx11PlusBufferFormat< /*FORMAT_8_8_8_8_UINT*/         0x2E,  8, 4, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx11PlusBufferFormat< /*FORMAT_8_8_8_8_SINT*/         0x2F,  8, 4, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_8_8_8_8*/     10>;
def : Gfx11PlusBufferFormat< /*FORMAT_32_32_UINT*/           0x30, 32, 2, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32_32*/       11>;
def : Gfx11PlusBufferFormat< /*FORMAT_32_32_SINT*/           0x31, 32, 2, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32_32*/       11>;
def : Gfx11PlusBufferFormat< /*FORMAT_32_32_FLOAT*/          0x32, 32, 2, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32_32*/       11>;
def : Gfx11PlusBufferFormat< /*FORMAT_16_16_16_16_UNORM*/    0x33, 16, 4, /*NUM_FORMAT_UNORM*/   0, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx11PlusBufferFormat< /*FORMAT_16_16_16_16_SNORM*/    0x34, 16, 4, /*NUM_FORMAT_SNORM*/   1, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx11PlusBufferFormat< /*FORMAT_16_16_16_16_USCALED*/  0x35, 16, 4, /*NUM_FORMAT_USCALED*/ 2, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx11PlusBufferFormat< /*FORMAT_16_16_16_16_SSCALED*/  0x36, 16, 4, /*NUM_FORMAT_SSCALED*/ 3, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx11PlusBufferFormat< /*FORMAT_16_16_16_16_UINT*/     0x37, 16, 4, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx11PlusBufferFormat< /*FORMAT_16_16_16_16_SINT*/     0x38, 16, 4, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx11PlusBufferFormat< /*FORMAT_16_16_16_16_FLOAT*/    0x39, 16, 4, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_16_16_16_16*/ 12>;
def : Gfx11PlusBufferFormat< /*FORMAT_32_32_32_UINT*/        0x3A, 32, 3, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32_32_32*/    13>;
def : Gfx11PlusBufferFormat< /*FORMAT_32_32_32_SINT*/        0x3B, 32, 3, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32_32_32*/    13>;
def : Gfx11PlusBufferFormat< /*FORMAT_32_32_32_FLOAT*/       0x3C, 32, 3, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32_32_32*/    13>;
def : Gfx11PlusBufferFormat< /*FORMAT_32_32_32_32_UINT*/     0x3D, 32, 4, /*NUM_FORMAT_UINT*/    4, /*DATA_FORMAT_32_32_32_32*/ 14>;
def : Gfx11PlusBufferFormat< /*FORMAT_32_32_32_32_SINT*/     0x3E, 32, 4, /*NUM_FORMAT_SINT*/    5, /*DATA_FORMAT_32_32_32_32*/ 14>;
def : Gfx11PlusBufferFormat< /*FORMAT_32_32_32_32_FLOAT*/    0x3F, 32, 4, /*NUM_FORMAT_FLOAT*/   7, /*DATA_FORMAT_32_32_32_32*/ 14>;

class SourceOfDivergence<Intrinsic intr> {
  Intrinsic Intr = intr;
}

def SourcesOfDivergence : GenericTable {
  let FilterClass = "SourceOfDivergence";
  let Fields = ["Intr"];

  let PrimaryKey = ["Intr"];
  let PrimaryKeyName = "lookupSourceOfDivergence";
}

def : SourceOfDivergence<int_amdgcn_workitem_id_x>;
def : SourceOfDivergence<int_amdgcn_workitem_id_y>;
def : SourceOfDivergence<int_amdgcn_workitem_id_z>;
def : SourceOfDivergence<int_amdgcn_interp_mov>;
def : SourceOfDivergence<int_amdgcn_interp_p1>;
def : SourceOfDivergence<int_amdgcn_interp_p2>;
def : SourceOfDivergence<int_amdgcn_interp_p1_f16>;
def : SourceOfDivergence<int_amdgcn_interp_p2_f16>;
def : SourceOfDivergence<int_amdgcn_lds_direct_load>;
def : SourceOfDivergence<int_amdgcn_lds_param_load>;
def : SourceOfDivergence<int_amdgcn_mbcnt_hi>;
def : SourceOfDivergence<int_amdgcn_mbcnt_lo>;
def : SourceOfDivergence<int_r600_read_tidig_x>;
def : SourceOfDivergence<int_r600_read_tidig_y>;
def : SourceOfDivergence<int_r600_read_tidig_z>;
def : SourceOfDivergence<int_amdgcn_atomic_cond_sub_u32>;
def : SourceOfDivergence<int_amdgcn_global_atomic_csub>;
def : SourceOfDivergence<int_amdgcn_global_atomic_fadd>;
def : SourceOfDivergence<int_amdgcn_global_atomic_fmin>;
def : SourceOfDivergence<int_amdgcn_global_atomic_fmax>;
def : SourceOfDivergence<int_amdgcn_global_atomic_fmin_num>;
def : SourceOfDivergence<int_amdgcn_global_atomic_fmax_num>;
def : SourceOfDivergence<int_amdgcn_global_atomic_ordered_add_b64>;
def : SourceOfDivergence<int_amdgcn_flat_atomic_fadd>;
def : SourceOfDivergence<int_amdgcn_flat_atomic_fmin>;
def : SourceOfDivergence<int_amdgcn_flat_atomic_fmax>;
def : SourceOfDivergence<int_amdgcn_flat_atomic_fmin_num>;
def : SourceOfDivergence<int_amdgcn_flat_atomic_fmax_num>;
def : SourceOfDivergence<int_amdgcn_global_atomic_fadd_v2bf16>;
def : SourceOfDivergence<int_amdgcn_flat_atomic_fadd_v2bf16>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_swap>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_add>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_sub>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_smin>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_umin>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_smax>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_umax>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_and>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_or>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_xor>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_inc>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_dec>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_fadd>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_fmin>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_fmax>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_cmpswap>;
def : SourceOfDivergence<int_amdgcn_raw_buffer_atomic_cond_sub_u32>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_swap>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_add>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_sub>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_smin>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_umin>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_smax>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_umax>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_and>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_or>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_xor>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_inc>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_dec>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_fadd>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_fmin>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_fmax>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_cmpswap>;
def : SourceOfDivergence<int_amdgcn_raw_ptr_buffer_atomic_cond_sub_u32>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_swap>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_add>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_sub>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_smin>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_umin>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_smax>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_umax>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_and>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_or>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_xor>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_inc>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_dec>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_fadd>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_fmin>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_fmax>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_cmpswap>;
def : SourceOfDivergence<int_amdgcn_struct_buffer_atomic_cond_sub_u32>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_swap>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_add>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_sub>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_smin>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_umin>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_smax>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_umax>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_and>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_or>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_xor>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_inc>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_dec>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_fadd>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_fmin>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_fmax>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_cmpswap>;
def : SourceOfDivergence<int_amdgcn_struct_ptr_buffer_atomic_cond_sub_u32>;
def : SourceOfDivergence<int_amdgcn_ps_live>;
def : SourceOfDivergence<int_amdgcn_live_mask>;
def : SourceOfDivergence<int_amdgcn_ds_swizzle>;
def : SourceOfDivergence<int_amdgcn_ds_ordered_add>;
def : SourceOfDivergence<int_amdgcn_ds_ordered_swap>;
def : SourceOfDivergence<int_amdgcn_permlane16>;
def : SourceOfDivergence<int_amdgcn_permlanex16>;
def : SourceOfDivergence<int_amdgcn_permlane16_var>;
def : SourceOfDivergence<int_amdgcn_permlanex16_var>;
def : SourceOfDivergence<int_amdgcn_mov_dpp>;
def : SourceOfDivergence<int_amdgcn_mov_dpp8>;
def : SourceOfDivergence<int_amdgcn_update_dpp>;
def : SourceOfDivergence<int_amdgcn_writelane>;

foreach intr = AMDGPUMFMAIntrinsics908 in
def : SourceOfDivergence<intr>;
foreach intr = AMDGPUMFMAIntrinsics90A in
def : SourceOfDivergence<intr>;
foreach intr = AMDGPUMFMAIntrinsics940 in
def : SourceOfDivergence<intr>;
foreach intr = AMDGPUWMMAIntrinsicsGFX11 in
def : SourceOfDivergence<intr>;
foreach intr = AMDGPUWMMAIntrinsicsGFX12 in
def : SourceOfDivergence<intr>;

def : SourceOfDivergence<int_amdgcn_global_load_tr_b64>;
def : SourceOfDivergence<int_amdgcn_global_load_tr_b128>;

// The dummy boolean output is divergent from the IR's perspective,
// but the mask results are uniform. These produce a divergent and
// uniform result, so the returned struct is collectively divergent.
// isAlwaysUniform can override the extract of the uniform component.
def : SourceOfDivergence<int_amdgcn_if>;
def : SourceOfDivergence<int_amdgcn_else>;
def : SourceOfDivergence<int_amdgcn_loop>;
def : SourceOfDivergence<int_amdgcn_inverse_ballot>;

foreach intr = AMDGPUImageDimAtomicIntrinsics in
def : SourceOfDivergence<intr>;

class AlwaysUniform<Intrinsic intr> {
  Intrinsic Intr = intr;
}

def UniformIntrinsics : GenericTable {
  let FilterClass = "AlwaysUniform";
  let Fields = ["Intr"];

  let PrimaryKey = ["Intr"];
  let PrimaryKeyName = "lookupAlwaysUniform";
}

def : AlwaysUniform<int_amdgcn_readfirstlane>;
def : AlwaysUniform<int_amdgcn_readlane>;
def : AlwaysUniform<int_amdgcn_icmp>;
def : AlwaysUniform<int_amdgcn_fcmp>;
def : AlwaysUniform<int_amdgcn_ballot>;
def : AlwaysUniform<int_amdgcn_if_break>;
