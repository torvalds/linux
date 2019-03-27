Zstandard Compression Format
============================

### Notices

Copyright (c) 2016-present Yann Collet, Facebook, Inc.

Permission is granted to copy and distribute this document
for any purpose and without charge,
including translations into other languages
and incorporation into compilations,
provided that the copyright notice and this notice are preserved,
and that any substantive changes or deletions from the original
are clearly marked.
Distribution of this document is unlimited.

### Version

0.3.1 (25/10/18)


Introduction
------------

The purpose of this document is to define a lossless compressed data format,
that is independent of CPU type, operating system,
file system and character set, suitable for
file compression, pipe and streaming compression,
using the [Zstandard algorithm](http://www.zstandard.org).
The text of the specification assumes a basic background in programming
at the level of bits and other primitive data representations.

The data can be produced or consumed,
even for an arbitrarily long sequentially presented input data stream,
using only an a priori bounded amount of intermediate storage,
and hence can be used in data communications.
The format uses the Zstandard compression method,
and optional [xxHash-64 checksum method](http://www.xxhash.org),
for detection of data corruption.

The data format defined by this specification
does not attempt to allow random access to compressed data.

Unless otherwise indicated below,
a compliant compressor must produce data sets
that conform to the specifications presented here.
It doesn’t need to support all options though.

A compliant decompressor must be able to decompress
at least one working set of parameters
that conforms to the specifications presented here.
It may also ignore informative fields, such as checksum.
Whenever it does not support a parameter defined in the compressed stream,
it must produce a non-ambiguous error code and associated error message
explaining which parameter is unsupported.

This specification is intended for use by implementers of software
to compress data into Zstandard format and/or decompress data from Zstandard format.
The Zstandard format is supported by an open source reference implementation,
written in portable C, and available at : https://github.com/facebook/zstd .


### Overall conventions
In this document:
- square brackets i.e. `[` and `]` are used to indicate optional fields or parameters.
- the naming convention for identifiers is `Mixed_Case_With_Underscores`

### Definitions
Content compressed by Zstandard is transformed into a Zstandard __frame__.
Multiple frames can be appended into a single file or stream.
A frame is completely independent, has a defined beginning and end,
and a set of parameters which tells the decoder how to decompress it.

A frame encapsulates one or multiple __blocks__.
Each block contains arbitrary content, which is described by its header,
and has a guaranteed maximum content size, which depends on frame parameters.
Unlike frames, each block depends on previous blocks for proper decoding.
However, each block can be decompressed without waiting for its successor,
allowing streaming operations.

Overview
---------
- [Frames](#frames)
  - [Zstandard frames](#zstandard-frames)
    - [Blocks](#blocks)
      - [Literals Section](#literals-section)
      - [Sequences Section](#sequences-section)
      - [Sequence Execution](#sequence-execution)
  - [Skippable frames](#skippable-frames)
- [Entropy Encoding](#entropy-encoding)
  - [FSE](#fse)
  - [Huffman Coding](#huffman-coding)
- [Dictionary Format](#dictionary-format)

Frames
------
Zstandard compressed data is made of one or more __frames__.
Each frame is independent and can be decompressed independently of other frames.
The decompressed content of multiple concatenated frames is the concatenation of
each frame decompressed content.

There are two frame formats defined by Zstandard:
  Zstandard frames and Skippable frames.
Zstandard frames contain compressed data, while
skippable frames contain custom user metadata.

## Zstandard frames
The structure of a single Zstandard frame is following:

| `Magic_Number` | `Frame_Header` |`Data_Block`| [More data blocks] | [`Content_Checksum`] |
|:--------------:|:--------------:|:----------:| ------------------ |:--------------------:|
|  4 bytes       |  2-14 bytes    |  n bytes   |                    |     0-4 bytes        |

__`Magic_Number`__

4 Bytes, __little-endian__ format.
Value : 0xFD2FB528
Note: This value was selected to be less probable to find at the beginning of some random file.
It avoids trivial patterns (0x00, 0xFF, repeated bytes, increasing bytes, etc.),
contains byte values outside of ASCII range,
and doesn't map into UTF8 space.
It reduces the chances that a text file represent this value by accident.

__`Frame_Header`__

2 to 14 Bytes, detailed in [`Frame_Header`](#frame_header).

__`Data_Block`__

Detailed in [`Blocks`](#blocks).
That’s where compressed data is stored.

__`Content_Checksum`__

An optional 32-bit checksum, only present if `Content_Checksum_flag` is set.
The content checksum is the result
of [xxh64() hash function](http://www.xxhash.org)
digesting the original (decoded) data as input, and a seed of zero.
The low 4 bytes of the checksum are stored in __little-endian__ format.

### `Frame_Header`

The `Frame_Header` has a variable size, with a minimum of 2 bytes,
and up to 14 bytes depending on optional parameters.
The structure of `Frame_Header` is following:

| `Frame_Header_Descriptor` | [`Window_Descriptor`] | [`Dictionary_ID`] | [`Frame_Content_Size`] |
| ------------------------- | --------------------- | ----------------- | ---------------------- |
| 1 byte                    | 0-1 byte              | 0-4 bytes         | 0-8 bytes              |

#### `Frame_Header_Descriptor`

The first header's byte is called the `Frame_Header_Descriptor`.
It describes which other fields are present.
Decoding this byte is enough to tell the size of `Frame_Header`.

| Bit number | Field name                |
| ---------- | ----------                |
| 7-6        | `Frame_Content_Size_flag` |
| 5          | `Single_Segment_flag`     |
| 4          | `Unused_bit`              |
| 3          | `Reserved_bit`            |
| 2          | `Content_Checksum_flag`   |
| 1-0        | `Dictionary_ID_flag`      |

In this table, bit 7 is the highest bit, while bit 0 is the lowest one.

__`Frame_Content_Size_flag`__

This is a 2-bits flag (`= Frame_Header_Descriptor >> 6`),
specifying if `Frame_Content_Size` (the decompressed data size)
is provided within the header.
`Flag_Value` provides `FCS_Field_Size`,
which is the number of bytes used by `Frame_Content_Size`
according to the following table:

|  `Flag_Value`  |    0   |  1  |  2  |  3  |
| -------------- | ------ | --- | --- | --- |
|`FCS_Field_Size`| 0 or 1 |  2  |  4  |  8  |

When `Flag_Value` is `0`, `FCS_Field_Size` depends on `Single_Segment_flag` :
if `Single_Segment_flag` is set, `FCS_Field_Size` is 1.
Otherwise, `FCS_Field_Size` is 0 : `Frame_Content_Size` is not provided.

__`Single_Segment_flag`__

If this flag is set,
data must be regenerated within a single continuous memory segment.

In this case, `Window_Descriptor` byte is skipped,
but `Frame_Content_Size` is necessarily present.
As a consequence, the decoder must allocate a memory segment
of size equal or larger than `Frame_Content_Size`.

In order to preserve the decoder from unreasonable memory requirements,
a decoder is allowed to reject a compressed frame
which requests a memory size beyond decoder's authorized range.

For broader compatibility, decoders are recommended to support
memory sizes of at least 8 MB.
This is only a recommendation,
each decoder is free to support higher or lower limits,
depending on local limitations.

__`Unused_bit`__

A decoder compliant with this specification version shall not interpret this bit.
It might be used in any future version,
to signal a property which is transparent to properly decode the frame.
An encoder compliant with this specification version must set this bit to zero.

__`Reserved_bit`__

This bit is reserved for some future feature.
Its value _must be zero_.
A decoder compliant with this specification version must ensure it is not set.
This bit may be used in a future revision,
to signal a feature that must be interpreted to decode the frame correctly.

__`Content_Checksum_flag`__

If this flag is set, a 32-bits `Content_Checksum` will be present at frame's end.
See `Content_Checksum` paragraph.

__`Dictionary_ID_flag`__

This is a 2-bits flag (`= FHD & 3`),
telling if a dictionary ID is provided within the header.
It also specifies the size of this field as `DID_Field_Size`.

|`Flag_Value`    |  0  |  1  |  2  |  3  |
| -------------- | --- | --- | --- | --- |
|`DID_Field_Size`|  0  |  1  |  2  |  4  |

#### `Window_Descriptor`

Provides guarantees on minimum memory buffer required to decompress a frame.
This information is important for decoders to allocate enough memory.

The `Window_Descriptor` byte is optional.
When `Single_Segment_flag` is set, `Window_Descriptor` is not present.
In this case, `Window_Size` is `Frame_Content_Size`,
which can be any value from 0 to 2^64-1 bytes (16 ExaBytes).

| Bit numbers |     7-3    |     2-0    |
| ----------- | ---------- | ---------- |
| Field name  | `Exponent` | `Mantissa` |

The minimum memory buffer size is called `Window_Size`.
It is described by the following formulas :
```
windowLog = 10 + Exponent;
windowBase = 1 << windowLog;
windowAdd = (windowBase / 8) * Mantissa;
Window_Size = windowBase + windowAdd;
```
The minimum `Window_Size` is 1 KB.
The maximum `Window_Size` is `(1<<41) + 7*(1<<38)` bytes, which is 3.75 TB.

In general, larger `Window_Size` tend to improve compression ratio,
but at the cost of memory usage.

To properly decode compressed data,
a decoder will need to allocate a buffer of at least `Window_Size` bytes.

In order to preserve decoder from unreasonable memory requirements,
a decoder is allowed to reject a compressed frame
which requests a memory size beyond decoder's authorized range.

For improved interoperability,
it's recommended for decoders to support `Window_Size` of up to 8 MB,
and it's recommended for encoders to not generate frame requiring `Window_Size` larger than 8 MB.
It's merely a recommendation though,
decoders are free to support larger or lower limits,
depending on local limitations.

#### `Dictionary_ID`

This is a variable size field, which contains
the ID of the dictionary required to properly decode the frame.
`Dictionary_ID` field is optional. When it's not present,
it's up to the decoder to know which dictionary to use.

`Dictionary_ID` field size is provided by `DID_Field_Size`.
`DID_Field_Size` is directly derived from value of `Dictionary_ID_flag`.
1 byte can represent an ID 0-255.
2 bytes can represent an ID 0-65535.
4 bytes can represent an ID 0-4294967295.
Format is __little-endian__.

It's allowed to represent a small ID (for example `13`)
with a large 4-bytes dictionary ID, even if it is less efficient.

_Reserved ranges :_
Within private environments, any `Dictionary_ID` can be used.

However, for frames and dictionaries distributed in public space,
`Dictionary_ID` must be attributed carefully.
Rules for public environment are not yet decided,
but the following ranges are reserved for some future registrar :
- low range  : `<= 32767`
- high range : `>= (1 << 31)`

Outside of these ranges, any value of `Dictionary_ID`
which is both `>= 32768` and `< (1<<31)` can be used freely,
even in public environment.



#### `Frame_Content_Size`

This is the original (uncompressed) size. This information is optional.
`Frame_Content_Size` uses a variable number of bytes, provided by `FCS_Field_Size`.
`FCS_Field_Size` is provided by the value of `Frame_Content_Size_flag`.
`FCS_Field_Size` can be equal to 0 (not present), 1, 2, 4 or 8 bytes.

| `FCS_Field_Size` |    Range   |
| ---------------- | ---------- |
|        0         |   unknown  |
|        1         |   0 - 255  |
|        2         | 256 - 65791|
|        4         | 0 - 2^32-1 |
|        8         | 0 - 2^64-1 |

`Frame_Content_Size` format is __little-endian__.
When `FCS_Field_Size` is 1, 4 or 8 bytes, the value is read directly.
When `FCS_Field_Size` is 2, _the offset of 256 is added_.
It's allowed to represent a small size (for example `18`) using any compatible variant.


Blocks
-------

After `Magic_Number` and `Frame_Header`, there are some number of blocks.
Each frame must have at least one block,
but there is no upper limit on the number of blocks per frame.

The structure of a block is as follows:

| `Block_Header` | `Block_Content` |
|:--------------:|:---------------:|
|    3 bytes     |     n bytes     |

`Block_Header` uses 3 bytes, written using __little-endian__ convention.
It contains 3 fields :

| `Last_Block` | `Block_Type` | `Block_Size` |
|:------------:|:------------:|:------------:|
|    bit 0     |  bits 1-2    |  bits 3-23   |

__`Last_Block`__

The lowest bit signals if this block is the last one.
The frame will end after this last block.
It may be followed by an optional `Content_Checksum`
(see [Zstandard Frames](#zstandard-frames)).

__`Block_Type`__

The next 2 bits represent the `Block_Type`.
There are 4 block types :

|    Value     |      0      |      1      |         2          |     3     |
| ------------ | ----------- | ----------- | ------------------ | --------- |
| `Block_Type` | `Raw_Block` | `RLE_Block` | `Compressed_Block` | `Reserved`|

- `Raw_Block` - this is an uncompressed block.
  `Block_Content` contains `Block_Size` bytes.

- `RLE_Block` - this is a single byte, repeated `Block_Size` times.
  `Block_Content` consists of a single byte.
  On the decompression side, this byte must be repeated `Block_Size` times.

- `Compressed_Block` - this is a [Zstandard compressed block](#compressed-blocks),
  explained later on.
  `Block_Size` is the length of `Block_Content`, the compressed data.
  The decompressed size is not known,
  but its maximum possible value is guaranteed (see below)

- `Reserved` - this is not a block.
  This value cannot be used with current version of this specification.
  If such a value is present, it is considered corrupted data.

__`Block_Size`__

The upper 21 bits of `Block_Header` represent the `Block_Size`.
`Block_Size` is the size of the block excluding the header.
A block can contain any number of bytes (even zero), up to
`Block_Maximum_Decompressed_Size`, which is the smallest of:
-  Window_Size
-  128 KB

A `Compressed_Block` has the extra restriction that `Block_Size` is always
strictly less than the decompressed size.
If this condition cannot be respected,
the block must be sent uncompressed instead (`Raw_Block`).


Compressed Blocks
-----------------
To decompress a compressed block, the compressed size must be provided
from `Block_Size` field within `Block_Header`.

A compressed block consists of 2 sections :
- [Literals Section](#literals-section)
- [Sequences Section](#sequences-section)

The results of the two sections are then combined to produce the decompressed
data in [Sequence Execution](#sequence-execution)

#### Prerequisites
To decode a compressed block, the following elements are necessary :
- Previous decoded data, up to a distance of `Window_Size`,
  or beginning of the Frame, whichever is smaller.
- List of "recent offsets" from previous `Compressed_Block`.
- The previous Huffman tree, required by `Treeless_Literals_Block` type
- Previous FSE decoding tables, required by `Repeat_Mode`
  for each symbol type (literals lengths, match lengths, offsets)

Note that decoding tables aren't always from the previous `Compressed_Block`.

- Every decoding table can come from a dictionary.
- The Huffman tree comes from the previous `Compressed_Literals_Block`.

Literals Section
----------------
All literals are regrouped in the first part of the block.
They can be decoded first, and then copied during [Sequence Execution],
or they can be decoded on the flow during [Sequence Execution].

Literals can be stored uncompressed or compressed using Huffman prefix codes.
When compressed, an optional tree description can be present,
followed by 1 or 4 streams.

| `Literals_Section_Header` | [`Huffman_Tree_Description`] | [jumpTable] | Stream1 | [Stream2] | [Stream3] | [Stream4] |
| ------------------------- | ---------------------------- | ----------- | ------- | --------- | --------- | --------- |


### `Literals_Section_Header`

Header is in charge of describing how literals are packed.
It's a byte-aligned variable-size bitfield, ranging from 1 to 5 bytes,
using __little-endian__ convention.

| `Literals_Block_Type` | `Size_Format` | `Regenerated_Size` | [`Compressed_Size`] |
| --------------------- | ------------- | ------------------ | ------------------- |
|       2 bits          |  1 - 2 bits   |    5 - 20 bits     |     0 - 18 bits     |

In this representation, bits on the left are the lowest bits.

__`Literals_Block_Type`__

This field uses 2 lowest bits of first byte, describing 4 different block types :

| `Literals_Block_Type`       | Value |
| --------------------------- | ----- |
| `Raw_Literals_Block`        |   0   |
| `RLE_Literals_Block`        |   1   |
| `Compressed_Literals_Block` |   2   |
| `Treeless_Literals_Block`   |   3   |

- `Raw_Literals_Block` - Literals are stored uncompressed.
- `RLE_Literals_Block` - Literals consist of a single byte value
        repeated `Regenerated_Size` times.
- `Compressed_Literals_Block` - This is a standard Huffman-compressed block,
        starting with a Huffman tree description.
        See details below.
- `Treeless_Literals_Block` - This is a Huffman-compressed block,
        using Huffman tree _from previous Huffman-compressed literals block_.
        `Huffman_Tree_Description` will be skipped.
        Note: If this mode is triggered without any previous Huffman-table in the frame
        (or [dictionary](#dictionary-format)), this should be treated as data corruption.

__`Size_Format`__

`Size_Format` is divided into 2 families :

- For `Raw_Literals_Block` and `RLE_Literals_Block`,
  it's only necessary to decode `Regenerated_Size`.
  There is no `Compressed_Size` field.
- For `Compressed_Block` and `Treeless_Literals_Block`,
  it's required to decode both `Compressed_Size`
  and `Regenerated_Size` (the decompressed size).
  It's also necessary to decode the number of streams (1 or 4).

For values spanning several bytes, convention is __little-endian__.

__`Size_Format` for `Raw_Literals_Block` and `RLE_Literals_Block`__ :

`Size_Format` uses 1 _or_ 2 bits.
Its value is : `Size_Format = (Literals_Section_Header[0]>>2) & 3`

- `Size_Format` == 00 or 10 : `Size_Format` uses 1 bit.
               `Regenerated_Size` uses 5 bits (0-31).
               `Literals_Section_Header` uses 1 byte.
               `Regenerated_Size = Literals_Section_Header[0]>>3`
- `Size_Format` == 01 : `Size_Format` uses 2 bits.
               `Regenerated_Size` uses 12 bits (0-4095).
               `Literals_Section_Header` uses 2 bytes.
               `Regenerated_Size = (Literals_Section_Header[0]>>4) + (Literals_Section_Header[1]<<4)`
- `Size_Format` == 11 : `Size_Format` uses 2 bits.
               `Regenerated_Size` uses 20 bits (0-1048575).
               `Literals_Section_Header` uses 3 bytes.
               `Regenerated_Size = (Literals_Section_Header[0]>>4) + (Literals_Section_Header[1]<<4) + (Literals_Section_Header[2]<<12)`

Only Stream1 is present for these cases.
Note : it's allowed to represent a short value (for example `13`)
using a long format, even if it's less efficient.

__`Size_Format` for `Compressed_Literals_Block` and `Treeless_Literals_Block`__ :

`Size_Format` always uses 2 bits.

- `Size_Format` == 00 : _A single stream_.
               Both `Regenerated_Size` and `Compressed_Size` use 10 bits (0-1023).
               `Literals_Section_Header` uses 3 bytes.
- `Size_Format` == 01 : 4 streams.
               Both `Regenerated_Size` and `Compressed_Size` use 10 bits (0-1023).
               `Literals_Section_Header` uses 3 bytes.
- `Size_Format` == 10 : 4 streams.
               Both `Regenerated_Size` and `Compressed_Size` use 14 bits (0-16383).
               `Literals_Section_Header` uses 4 bytes.
- `Size_Format` == 11 : 4 streams.
               Both `Regenerated_Size` and `Compressed_Size` use 18 bits (0-262143).
               `Literals_Section_Header` uses 5 bytes.

Both `Compressed_Size` and `Regenerated_Size` fields follow __little-endian__ convention.
Note: `Compressed_Size` __includes__ the size of the Huffman Tree description
_when_ it is present.

#### Raw Literals Block
The data in Stream1 is `Regenerated_Size` bytes long,
it contains the raw literals data to be used during [Sequence Execution].

#### RLE Literals Block
Stream1 consists of a single byte which should be repeated `Regenerated_Size` times
to generate the decoded literals.

#### Compressed Literals Block and Treeless Literals Block
Both of these modes contain Huffman encoded data.

For `Treeless_Literals_Block`,
the Huffman table comes from previously compressed literals block,
or from a dictionary.


### `Huffman_Tree_Description`
This section is only present when `Literals_Block_Type` type is `Compressed_Literals_Block` (`2`).
The format of the Huffman tree description can be found at [Huffman Tree description](#huffman-tree-description).
The size of `Huffman_Tree_Description` is determined during decoding process,
it must be used to determine where streams begin.
`Total_Streams_Size = Compressed_Size - Huffman_Tree_Description_Size`.


### Jump Table
The Jump Table is only present when there are 4 Huffman-coded streams.

Reminder : Huffman compressed data consists of either 1 or 4 Huffman-coded streams.

If only one stream is present, it is a single bitstream occupying the entire
remaining portion of the literals block, encoded as described within
[Huffman-Coded Streams](#huffman-coded-streams).

If there are four streams, `Literals_Section_Header` only provided
enough information to know the decompressed and compressed sizes
of all four streams _combined_.
The decompressed size of _each_ stream is equal to `(Regenerated_Size+3)/4`,
except for the last stream which may be up to 3 bytes smaller,
to reach a total decompressed size as specified in `Regenerated_Size`.

The compressed size of each stream is provided explicitly in the Jump Table.
Jump Table is 6 bytes long, and consist of three 2-byte __little-endian__ fields,
describing the compressed sizes of the first three streams.
`Stream4_Size` is computed from total `Total_Streams_Size` minus sizes of other streams.

`Stream4_Size = Total_Streams_Size - 6 - Stream1_Size - Stream2_Size - Stream3_Size`.

Note: if `Stream1_Size + Stream2_Size + Stream3_Size > Total_Streams_Size`,
data is considered corrupted.

Each of these 4 bitstreams is then decoded independently as a Huffman-Coded stream,
as described at [Huffman-Coded Streams](#huffman-coded-streams)


Sequences Section
-----------------
A compressed block is a succession of _sequences_ .
A sequence is a literal copy command, followed by a match copy command.
A literal copy command specifies a length.
It is the number of bytes to be copied (or extracted) from the Literals Section.
A match copy command specifies an offset and a length.

When all _sequences_ are decoded,
if there are literals left in the _literals section_,
these bytes are added at the end of the block.

This is described in more detail in [Sequence Execution](#sequence-execution).

The `Sequences_Section` regroup all symbols required to decode commands.
There are 3 symbol types : literals lengths, offsets and match lengths.
They are encoded together, interleaved, in a single _bitstream_.

The `Sequences_Section` starts by a header,
followed by optional probability tables for each symbol type,
followed by the bitstream.

| `Sequences_Section_Header` | [`Literals_Length_Table`] | [`Offset_Table`] | [`Match_Length_Table`] | bitStream |
| -------------------------- | ------------------------- | ---------------- | ---------------------- | --------- |

To decode the `Sequences_Section`, it's required to know its size.
Its size is deduced from the size of `Literals_Section`:
`Sequences_Section_Size = Block_Size - Literals_Section_Size`.


#### `Sequences_Section_Header`

Consists of 2 items:
- `Number_of_Sequences`
- Symbol compression modes

__`Number_of_Sequences`__

This is a variable size field using between 1 and 3 bytes.
Let's call its first byte `byte0`.
- `if (byte0 == 0)` : there are no sequences.
            The sequence section stops there.
            Decompressed content is defined entirely as Literals Section content.
            The FSE tables used in `Repeat_Mode` aren't updated.
- `if (byte0 < 128)` : `Number_of_Sequences = byte0` . Uses 1 byte.
- `if (byte0 < 255)` : `Number_of_Sequences = ((byte0-128) << 8) + byte1` . Uses 2 bytes.
- `if (byte0 == 255)`: `Number_of_Sequences = byte1 + (byte2<<8) + 0x7F00` . Uses 3 bytes.

__Symbol compression modes__

This is a single byte, defining the compression mode of each symbol type.

|Bit number|          7-6            |      5-4       |        3-2           |     1-0    |
| -------- | ----------------------- | -------------- | -------------------- | ---------- |
|Field name| `Literals_Lengths_Mode` | `Offsets_Mode` | `Match_Lengths_Mode` | `Reserved` |

The last field, `Reserved`, must be all-zeroes.

`Literals_Lengths_Mode`, `Offsets_Mode` and `Match_Lengths_Mode` define the `Compression_Mode` of
literals lengths, offsets, and match lengths symbols respectively.

They follow the same enumeration :

|        Value       |         0         |      1     |           2           |       3       |
| ------------------ | ----------------- | ---------- | --------------------- | ------------- |
| `Compression_Mode` | `Predefined_Mode` | `RLE_Mode` | `FSE_Compressed_Mode` | `Repeat_Mode` |

- `Predefined_Mode` : A predefined FSE distribution table is used, defined in
          [default distributions](#default-distributions).
          No distribution table will be present.
- `RLE_Mode` : The table description consists of a single byte, which contains the symbol's value.
          This symbol will be used for all sequences.
- `FSE_Compressed_Mode` : standard FSE compression.
          A distribution table will be present.
          The format of this distribution table is described in [FSE Table Description](#fse-table-description).
          Note that the maximum allowed accuracy log for literals length and match length tables is 9,
          and the maximum accuracy log for the offsets table is 8.
          `FSE_Compressed_Mode` must not be used when only one symbol is present,
          `RLE_Mode` should be used instead (although any other mode will work).
- `Repeat_Mode` : The table used in the previous `Compressed_Block` with `Number_of_Sequences > 0` will be used again,
          or if this is the first block, table in the dictionary will be used.
          Note that this includes `RLE_mode`, so if `Repeat_Mode` follows `RLE_Mode`, the same symbol will be repeated.
          It also includes `Predefined_Mode`, in which case `Repeat_Mode` will have same outcome as `Predefined_Mode`.
          No distribution table will be present.
          If this mode is used without any previous sequence table in the frame
          (nor [dictionary](#dictionary-format)) to repeat, this should be treated as corruption.

#### The codes for literals lengths, match lengths, and offsets.

Each symbol is a _code_ in its own context,
which specifies `Baseline` and `Number_of_Bits` to add.
_Codes_ are FSE compressed,
and interleaved with raw additional bits in the same bitstream.

##### Literals length codes

Literals length codes are values ranging from `0` to `35` included.
They define lengths from 0 to 131071 bytes.
The literals length is equal to the decoded `Baseline` plus
the result of reading `Number_of_Bits` bits from the bitstream,
as a __little-endian__ value.

| `Literals_Length_Code` |         0-15           |
| ---------------------- | ---------------------- |
| length                 | `Literals_Length_Code` |
| `Number_of_Bits`       |          0             |

| `Literals_Length_Code` |  16  |  17  |  18  |  19  |  20  |  21  |  22  |  23  |
| ---------------------- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- |
| `Baseline`             |  16  |  18  |  20  |  22  |  24  |  28  |  32  |  40  |
| `Number_of_Bits`       |   1  |   1  |   1  |   1  |   2  |   2  |   3  |   3  |

| `Literals_Length_Code` |  24  |  25  |  26  |  27  |  28  |  29  |  30  |  31  |
| ---------------------- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- |
| `Baseline`             |  48  |  64  |  128 |  256 |  512 | 1024 | 2048 | 4096 |
| `Number_of_Bits`       |   4  |   6  |   7  |   8  |   9  |  10  |  11  |  12  |

| `Literals_Length_Code` |  32  |  33  |  34  |  35  |
| ---------------------- | ---- | ---- | ---- | ---- |
| `Baseline`             | 8192 |16384 |32768 |65536 |
| `Number_of_Bits`       |  13  |  14  |  15  |  16  |


##### Match length codes

Match length codes are values ranging from `0` to `52` included.
They define lengths from 3 to 131074 bytes.
The match length is equal to the decoded `Baseline` plus
the result of reading `Number_of_Bits` bits from the bitstream,
as a __little-endian__ value.

| `Match_Length_Code` |         0-31            |
| ------------------- | ----------------------- |
| value               | `Match_Length_Code` + 3 |
| `Number_of_Bits`    |          0              |

| `Match_Length_Code` |  32  |  33  |  34  |  35  |  36  |  37  |  38  |  39  |
| ------------------- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- |
| `Baseline`          |  35  |  37  |  39  |  41  |  43  |  47  |  51  |  59  |
| `Number_of_Bits`    |   1  |   1  |   1  |   1  |   2  |   2  |   3  |   3  |

| `Match_Length_Code` |  40  |  41  |  42  |  43  |  44  |  45  |  46  |  47  |
| ------------------- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- |
| `Baseline`          |  67  |  83  |  99  |  131 |  259 |  515 | 1027 | 2051 |
| `Number_of_Bits`    |   4  |   4  |   5  |   7  |   8  |   9  |  10  |  11  |

| `Match_Length_Code` |  48  |  49  |  50  |  51  |  52  |
| ------------------- | ---- | ---- | ---- | ---- | ---- |
| `Baseline`          | 4099 | 8195 |16387 |32771 |65539 |
| `Number_of_Bits`    |  12  |  13  |  14  |  15  |  16  |

##### Offset codes

Offset codes are values ranging from `0` to `N`.

A decoder is free to limit its maximum `N` supported.
Recommendation is to support at least up to `22`.
For information, at the time of this writing.
the reference decoder supports a maximum `N` value of `31`.

An offset code is also the number of additional bits to read in __little-endian__ fashion,
and can be translated into an `Offset_Value` using the following formulas :

```
Offset_Value = (1 << offsetCode) + readNBits(offsetCode);
if (Offset_Value > 3) offset = Offset_Value - 3;
```
It means that maximum `Offset_Value` is `(2^(N+1))-1`
supporting back-reference distances up to `(2^(N+1))-4`,
but is limited by [maximum back-reference distance](#window_descriptor).

`Offset_Value` from 1 to 3 are special : they define "repeat codes".
This is described in more detail in [Repeat Offsets](#repeat-offsets).

#### Decoding Sequences
FSE bitstreams are read in reverse direction than written. In zstd,
the compressor writes bits forward into a block and the decompressor
must read the bitstream _backwards_.

To find the start of the bitstream it is therefore necessary to
know the offset of the last byte of the block which can be found
by counting `Block_Size` bytes after the block header.

After writing the last bit containing information, the compressor
writes a single `1`-bit and then fills the byte with 0-7 `0` bits of
padding. The last byte of the compressed bitstream cannot be `0` for
that reason.

When decompressing, the last byte containing the padding is the first
byte to read. The decompressor needs to skip 0-7 initial `0`-bits and
the first `1`-bit it occurs. Afterwards, the useful part of the bitstream
begins.

FSE decoding requires a 'state' to be carried from symbol to symbol.
For more explanation on FSE decoding, see the [FSE section](#fse).

For sequence decoding, a separate state keeps track of each
literal lengths, offsets, and match lengths symbols.
Some FSE primitives are also used.
For more details on the operation of these primitives, see the [FSE section](#fse).

##### Starting states
The bitstream starts with initial FSE state values,
each using the required number of bits in their respective _accuracy_,
decoded previously from their normalized distribution.

It starts by `Literals_Length_State`,
followed by `Offset_State`,
and finally `Match_Length_State`.

Reminder : always keep in mind that all values are read _backward_,
so the 'start' of the bitstream is at the highest position in memory,
immediately before the last `1`-bit for padding.

After decoding the starting states, a single sequence is decoded
`Number_Of_Sequences` times.
These sequences are decoded in order from first to last.
Since the compressor writes the bitstream in the forward direction,
this means the compressor must encode the sequences starting with the last
one and ending with the first.

##### Decoding a sequence
For each of the symbol types, the FSE state can be used to determine the appropriate code.
The code then defines the `Baseline` and `Number_of_Bits` to read for each type.
See the [description of the codes] for how to determine these values.

[description of the codes]: #the-codes-for-literals-lengths-match-lengths-and-offsets

Decoding starts by reading the `Number_of_Bits` required to decode `Offset`.
It then does the same for `Match_Length`, and then for `Literals_Length`.
This sequence is then used for [sequence execution](#sequence-execution).

If it is not the last sequence in the block,
the next operation is to update states.
Using the rules pre-calculated in the decoding tables,
`Literals_Length_State` is updated,
followed by `Match_Length_State`,
and then `Offset_State`.
See the [FSE section](#fse) for details on how to update states from the bitstream.

This operation will be repeated `Number_of_Sequences` times.
At the end, the bitstream shall be entirely consumed,
otherwise the bitstream is considered corrupted.

#### Default Distributions
If `Predefined_Mode` is selected for a symbol type,
its FSE decoding table is generated from a predefined distribution table defined here.
For details on how to convert this distribution into a decoding table, see the [FSE section].

[FSE section]: #from-normalized-distribution-to-decoding-tables

##### Literals Length
The decoding table uses an accuracy log of 6 bits (64 states).
```
short literalsLength_defaultDistribution[36] =
        { 4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
          2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1,
         -1,-1,-1,-1 };
```

##### Match Length
The decoding table uses an accuracy log of 6 bits (64 states).
```
short matchLengths_defaultDistribution[53] =
        { 1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
          1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
          1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,-1,-1,
         -1,-1,-1,-1,-1 };
```

##### Offset Codes
The decoding table uses an accuracy log of 5 bits (32 states),
and supports a maximum `N` value of 28, allowing offset values up to 536,870,908 .

If any sequence in the compressed block requires a larger offset than this,
it's not possible to use the default distribution to represent it.
```
short offsetCodes_defaultDistribution[29] =
        { 1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
          1, 1, 1, 1, 1, 1, 1, 1,-1,-1,-1,-1,-1 };
```


Sequence Execution
------------------
Once literals and sequences have been decoded,
they are combined to produce the decoded content of a block.

Each sequence consists of a tuple of (`literals_length`, `offset_value`, `match_length`),
decoded as described in the [Sequences Section](#sequences-section).
To execute a sequence, first copy `literals_length` bytes
from the decoded literals to the output.

Then `match_length` bytes are copied from previous decoded data.
The offset to copy from is determined by `offset_value`:
if `offset_value > 3`, then the offset is `offset_value - 3`.
If `offset_value` is from 1-3, the offset is a special repeat offset value.
See the [repeat offset](#repeat-offsets) section for how the offset is determined
in this case.

The offset is defined as from the current position, so an offset of 6
and a match length of 3 means that 3 bytes should be copied from 6 bytes back.
Note that all offsets leading to previously decoded data
must be smaller than `Window_Size` defined in `Frame_Header_Descriptor`.

#### Repeat offsets
As seen in [Sequence Execution](#sequence-execution),
the first 3 values define a repeated offset and we will call them
`Repeated_Offset1`, `Repeated_Offset2`, and `Repeated_Offset3`.
They are sorted in recency order, with `Repeated_Offset1` meaning "most recent one".

If `offset_value == 1`, then the offset used is `Repeated_Offset1`, etc.

There is an exception though, when current sequence's `literals_length = 0`.
In this case, repeated offsets are shifted by one,
so an `offset_value` of 1 means `Repeated_Offset2`,
an `offset_value` of 2 means `Repeated_Offset3`,
and an `offset_value` of 3 means `Repeated_Offset1 - 1_byte`.

For the first block, the starting offset history is populated with following values :
`Repeated_Offset1`=1, `Repeated_Offset2`=4, `Repeated_Offset3`=8,
unless a dictionary is used, in which case they come from the dictionary.

Then each block gets its starting offset history from the ending values of the most recent `Compressed_Block`.
Note that blocks which are not `Compressed_Block` are skipped, they do not contribute to offset history.

[Offset Codes]: #offset-codes

###### Offset updates rules

The newest offset takes the lead in offset history,
shifting others back by one rank,
up to the previous rank of the new offset _if it was present in history_.

__Examples__ :

In the common case, when new offset is not part of history :
`Repeated_Offset3` = `Repeated_Offset2`
`Repeated_Offset2` = `Repeated_Offset1`
`Repeated_Offset1` = `NewOffset`

When the new offset _is_ part of history, there may be specific adjustments.

When `NewOffset` == `Repeated_Offset1`, offset history remains actually unmodified.

When `NewOffset` == `Repeated_Offset2`,
`Repeated_Offset1` and `Repeated_Offset2` ranks are swapped.
`Repeated_Offset3` is unmodified.

When `NewOffset` == `Repeated_Offset3`,
there is actually no difference with the common case :
all offsets are shifted by one rank,
`NewOffset` (== `Repeated_Offset3`) becomes the new `Repeated_Offset1`.

Also worth mentioning, the specific corner case when `offset_value` == 3,
and the literal length of the current sequence is zero.
In which case , `NewOffset` = `Repeated_Offset1` - 1_byte.
Here also, from an offset history update perspective, it's just a common case :
`Repeated_Offset3` = `Repeated_Offset2`
`Repeated_Offset2` = `Repeated_Offset1`
`Repeated_Offset1` = `NewOffset` ( == `Repeated_Offset1` - 1_byte )



Skippable Frames
----------------

| `Magic_Number` | `Frame_Size` | `User_Data` |
|:--------------:|:------------:|:-----------:|
|   4 bytes      |  4 bytes     |   n bytes   |

Skippable frames allow the insertion of user-defined metadata
into a flow of concatenated frames.

Skippable frames defined in this specification are compatible with [LZ4] ones.

[LZ4]:http://www.lz4.org

From a compliant decoder perspective, skippable frames need just be skipped,
and their content ignored, resuming decoding after the skippable frame.

It can be noted that a skippable frame
can be used to watermark a stream of concatenated frames
embedding any kind of tracking information (even just an UUID).
Users wary of such possibility should scan the stream of concatenated frames
in an attempt to detect such frame for analysis or removal.

__`Magic_Number`__

4 Bytes, __little-endian__ format.
Value : 0x184D2A5?, which means any value from 0x184D2A50 to 0x184D2A5F.
All 16 values are valid to identify a skippable frame.
This specification doesn't detail any specific tagging for skippable frames.

__`Frame_Size`__

This is the size, in bytes, of the following `User_Data`
(without including the magic number nor the size field itself).
This field is represented using 4 Bytes, __little-endian__ format, unsigned 32-bits.
This means `User_Data` can’t be bigger than (2^32-1) bytes.

__`User_Data`__

The `User_Data` can be anything. Data will just be skipped by the decoder.



Entropy Encoding
----------------
Two types of entropy encoding are used by the Zstandard format:
FSE, and Huffman coding.
Huffman is used to compress literals,
while FSE is used for all other symbols
(`Literals_Length_Code`, `Match_Length_Code`, offset codes)
and to compress Huffman headers.


FSE
---
FSE, short for Finite State Entropy, is an entropy codec based on [ANS].
FSE encoding/decoding involves a state that is carried over between symbols,
so decoding must be done in the opposite direction as encoding.
Therefore, all FSE bitstreams are read from end to beginning.
Note that the order of the bits in the stream is not reversed,
we just read the elements in the reverse order they are written.

For additional details on FSE, see [Finite State Entropy].

[Finite State Entropy]:https://github.com/Cyan4973/FiniteStateEntropy/

FSE decoding involves a decoding table which has a power of 2 size, and contain three elements:
`Symbol`, `Num_Bits`, and `Baseline`.
The `log2` of the table size is its `Accuracy_Log`.
An FSE state value represents an index in this table.

To obtain the initial state value, consume `Accuracy_Log` bits from the stream as a __little-endian__ value.
The next symbol in the stream is the `Symbol` indicated in the table for that state.
To obtain the next state value,
the decoder should consume `Num_Bits` bits from the stream as a __little-endian__ value and add it to `Baseline`.

[ANS]: https://en.wikipedia.org/wiki/Asymmetric_Numeral_Systems

### FSE Table Description
To decode FSE streams, it is necessary to construct the decoding table.
The Zstandard format encodes FSE table descriptions as follows:

An FSE distribution table describes the probabilities of all symbols
from `0` to the last present one (included)
on a normalized scale of `1 << Accuracy_Log` .
Note that there must be two or more symbols with nonzero probability.

It's a bitstream which is read forward, in __little-endian__ fashion.
It's not necessary to know bitstream exact size,
it will be discovered and reported by the decoding process.

The bitstream starts by reporting on which scale it operates.
Let's `low4Bits` designate the lowest 4 bits of the first byte :
`Accuracy_Log = low4bits + 5`.

Then follows each symbol value, from `0` to last present one.
The number of bits used by each field is variable.
It depends on :

- Remaining probabilities + 1 :
  __example__ :
  Presuming an `Accuracy_Log` of 8,
  and presuming 100 probabilities points have already been distributed,
  the decoder may read any value from `0` to `256 - 100 + 1 == 157` (inclusive).
  Therefore, it must read `log2sup(157) == 8` bits.

- Value decoded : small values use 1 less bit :
  __example__ :
  Presuming values from 0 to 157 (inclusive) are possible,
  255-157 = 98 values are remaining in an 8-bits field.
  They are used this way :
  first 98 values (hence from 0 to 97) use only 7 bits,
  values from 98 to 157 use 8 bits.
  This is achieved through this scheme :

  | Value read | Value decoded | Number of bits used |
  | ---------- | ------------- | ------------------- |
  |   0 -  97  |   0 -  97     |  7                  |
  |  98 - 127  |  98 - 127     |  8                  |
  | 128 - 225  |   0 -  97     |  7                  |
  | 226 - 255  | 128 - 157     |  8                  |

Symbols probabilities are read one by one, in order.

Probability is obtained from Value decoded by following formula :
`Proba = value - 1`

It means value `0` becomes negative probability `-1`.
`-1` is a special probability, which means "less than 1".
Its effect on distribution table is described in the [next section].
For the purpose of calculating total allocated probability points, it counts as one.

[next section]:#from-normalized-distribution-to-decoding-tables

When a symbol has a __probability__ of `zero`,
it is followed by a 2-bits repeat flag.
This repeat flag tells how many probabilities of zeroes follow the current one.
It provides a number ranging from 0 to 3.
If it is a 3, another 2-bits repeat flag follows, and so on.

When last symbol reaches cumulated total of `1 << Accuracy_Log`,
decoding is complete.
If the last symbol makes cumulated total go above `1 << Accuracy_Log`,
distribution is considered corrupted.

Then the decoder can tell how many bytes were used in this process,
and how many symbols are present.
The bitstream consumes a round number of bytes.
Any remaining bit within the last byte is just unused.

#### From normalized distribution to decoding tables

The distribution of normalized probabilities is enough
to create a unique decoding table.

It follows the following build rule :

The table has a size of `Table_Size = 1 << Accuracy_Log`.
Each cell describes the symbol decoded,
and instructions to get the next state.

Symbols are scanned in their natural order for "less than 1" probabilities.
Symbols with this probability are being attributed a single cell,
starting from the end of the table and retreating.
These symbols define a full state reset, reading `Accuracy_Log` bits.

All remaining symbols are allocated in their natural order.
Starting from symbol `0` and table position `0`,
each symbol gets allocated as many cells as its probability.
Cell allocation is spreaded, not linear :
each successor position follow this rule :

```
position += (tableSize>>1) + (tableSize>>3) + 3;
position &= tableSize-1;
```

A position is skipped if already occupied by a "less than 1" probability symbol.
`position` does not reset between symbols, it simply iterates through
each position in the table, switching to the next symbol when enough
states have been allocated to the current one.

The result is a list of state values.
Each state will decode the current symbol.

To get the `Number_of_Bits` and `Baseline` required for next state,
it's first necessary to sort all states in their natural order.
The lower states will need 1 more bit than higher ones.
The process is repeated for each symbol.

__Example__ :
Presuming a symbol has a probability of 5.
It receives 5 state values. States are sorted in natural order.

Next power of 2 is 8.
Space of probabilities is divided into 8 equal parts.
Presuming the `Accuracy_Log` is 7, it defines 128 states.
Divided by 8, each share is 16 large.

In order to reach 8, 8-5=3 lowest states will count "double",
doubling the number of shares (32 in width),
requiring one more bit in the process.

Baseline is assigned starting from the higher states using fewer bits,
and proceeding naturally, then resuming at the first state,
each takes its allocated width from Baseline.

| state order      |   0   |   1   |    2   |   3  |   4   |
| ---------------- | ----- | ----- | ------ | ---- | ----- |
| width            |  32   |  32   |   32   |  16  |  16   |
| `Number_of_Bits` |   5   |   5   |    5   |   4  |   4   |
| range number     |   2   |   4   |    6   |   0  |   1   |
| `Baseline`       |  32   |  64   |   96   |   0  |  16   |
| range            | 32-63 | 64-95 | 96-127 | 0-15 | 16-31 |

The next state is determined from current state
by reading the required `Number_of_Bits`, and adding the specified `Baseline`.

See [Appendix A] for the results of this process applied to the default distributions.

[Appendix A]: #appendix-a---decoding-tables-for-predefined-codes


Huffman Coding
--------------
Zstandard Huffman-coded streams are read backwards,
similar to the FSE bitstreams.
Therefore, to find the start of the bitstream, it is therefore to
know the offset of the last byte of the Huffman-coded stream.

After writing the last bit containing information, the compressor
writes a single `1`-bit and then fills the byte with 0-7 `0` bits of
padding. The last byte of the compressed bitstream cannot be `0` for
that reason.

When decompressing, the last byte containing the padding is the first
byte to read. The decompressor needs to skip 0-7 initial `0`-bits and
the first `1`-bit it occurs. Afterwards, the useful part of the bitstream
begins.

The bitstream contains Huffman-coded symbols in __little-endian__ order,
with the codes defined by the method below.

### Huffman Tree Description

Prefix coding represents symbols from an a priori known alphabet
by bit sequences (codewords), one codeword for each symbol,
in a manner such that different symbols may be represented
by bit sequences of different lengths,
but a parser can always parse an encoded string
unambiguously symbol-by-symbol.

Given an alphabet with known symbol frequencies,
the Huffman algorithm allows the construction of an optimal prefix code
using the fewest bits of any possible prefix codes for that alphabet.

Prefix code must not exceed a maximum code length.
More bits improve accuracy but cost more header size,
and require more memory or more complex decoding operations.
This specification limits maximum code length to 11 bits.

#### Representation

All literal values from zero (included) to last present one (excluded)
are represented by `Weight` with values from `0` to `Max_Number_of_Bits`.
Transformation from `Weight` to `Number_of_Bits` follows this formula :
```
Number_of_Bits = Weight ? (Max_Number_of_Bits + 1 - Weight) : 0
```
The last symbol's `Weight` is deduced from previously decoded ones,
by completing to the nearest power of 2.
This power of 2 gives `Max_Number_of_Bits`, the depth of the current tree.
`Max_Number_of_Bits` must be <= 11,
otherwise the representation is considered corrupted.

__Example__ :
Let's presume the following Huffman tree must be described :

|  literal value   |  0  |  1  |  2  |  3  |  4  |  5  |
| ---------------- | --- | --- | --- | --- | --- | --- |
| `Number_of_Bits` |  1  |  2  |  3  |  0  |  4  |  4  |

The tree depth is 4, since its longest elements uses 4 bits
(longest elements are the one with smallest frequency).
Value `5` will not be listed, as it can be determined from values for 0-4,
nor will values above `5` as they are all 0.
Values from `0` to `4` will be listed using `Weight` instead of `Number_of_Bits`.
Weight formula is :
```
Weight = Number_of_Bits ? (Max_Number_of_Bits + 1 - Number_of_Bits) : 0
```
It gives the following series of weights :

| literal value |  0  |  1  |  2  |  3  |  4  |
| ------------- | --- | --- | --- | --- | --- |
|   `Weight`    |  4  |  3  |  2  |  0  |  1  |

The decoder will do the inverse operation :
having collected weights of literal symbols from `0` to `4`,
it knows the last literal, `5`, is present with a non-zero `Weight`.
The `Weight` of `5` can be determined by advancing to the next power of 2.
The sum of `2^(Weight-1)` (excluding 0's) is :
`8 + 4 + 2 + 0 + 1 = 15`.
Nearest larger power of 2 value is 16.
Therefore, `Max_Number_of_Bits = 4` and `Weight[5] = 16-15 = 1`.

#### Huffman Tree header

This is a single byte value (0-255),
which describes how the series of weights is encoded.

- if `headerByte` < 128 :
  the series of weights is compressed using FSE (see below).
  The length of the FSE-compressed series is equal to `headerByte` (0-127).

- if `headerByte` >= 128 :
  + the series of weights uses a direct representation,
    where each `Weight` is encoded directly as a 4 bits field (0-15).
  + They are encoded forward, 2 weights to a byte,
    first weight taking the top four bits and second one taking the bottom four.
    * e.g. the following operations could be used to read the weights:
      `Weight[0] = (Byte[0] >> 4), Weight[1] = (Byte[0] & 0xf)`, etc.
  + The full representation occupies `Ceiling(Number_of_Weights/2)` bytes,
    meaning it uses only full bytes even if `Number_of_Weights` is odd.
  + `Number_of_Weights = headerByte - 127`.
    * Note that maximum `Number_of_Weights` is 255-127 = 128,
      therefore, only up to 128 `Weight` can be encoded using direct representation.
    * Since the last non-zero `Weight` is _not_ encoded,
      this scheme is compatible with alphabet sizes of up to 129 symbols,
      hence including literal symbol 128.
    * If any literal symbol > 128 has a non-zero `Weight`,
      direct representation is not possible.
      In such case, it's necessary to use FSE compression.


#### Finite State Entropy (FSE) compression of Huffman weights

In this case, the series of Huffman weights is compressed using FSE compression.
It's a single bitstream with 2 interleaved states,
sharing a single distribution table.

To decode an FSE bitstream, it is necessary to know its compressed size.
Compressed size is provided by `headerByte`.
It's also necessary to know its _maximum possible_ decompressed size,
which is `255`, since literal values span from `0` to `255`,
and last symbol's `Weight` is not represented.

An FSE bitstream starts by a header, describing probabilities distribution.
It will create a Decoding Table.
For a list of Huffman weights, the maximum accuracy log is 6 bits.
For more description see the [FSE header description](#fse-table-description)

The Huffman header compression uses 2 states,
which share the same FSE distribution table.
The first state (`State1`) encodes the even indexed symbols,
and the second (`State2`) encodes the odd indexed symbols.
`State1` is initialized first, and then `State2`, and they take turns
decoding a single symbol and updating their state.
For more details on these FSE operations, see the [FSE section](#fse).

The number of symbols to decode is determined
by tracking bitStream overflow condition:
If updating state after decoding a symbol would require more bits than
remain in the stream, it is assumed that extra bits are 0.  Then,
symbols for each of the final states are decoded and the process is complete.

#### Conversion from weights to Huffman prefix codes

All present symbols shall now have a `Weight` value.
It is possible to transform weights into `Number_of_Bits`, using this formula:
```
Number_of_Bits = (Weight>0) ? Max_Number_of_Bits + 1 - Weight : 0
```
Symbols are sorted by `Weight`.
Within same `Weight`, symbols keep natural sequential order.
Symbols with a `Weight` of zero are removed.
Then, starting from lowest `Weight`, prefix codes are distributed in sequential order.

__Example__ :
Let's presume the following list of weights has been decoded :

| Literal  |  0  |  1  |  2  |  3  |  4  |  5  |
| -------- | --- | --- | --- | --- | --- | --- |
| `Weight` |  4  |  3  |  2  |  0  |  1  |  1  |

Sorted by weight and then natural sequential order,
it gives the following distribution :

| Literal          |  3  |  4  |  5  |  2  |  1  |   0  |
| ---------------- | --- | --- | --- | --- | --- | ---- |
| `Weight`         |  0  |  1  |  1  |  2  |  3  |   4  |
| `Number_of_Bits` |  0  |  4  |  4  |  3  |  2  |   1  |
| prefix codes     | N/A | 0000| 0001| 001 | 01  |   1  |

### Huffman-coded Streams

Given a Huffman decoding table,
it's possible to decode a Huffman-coded stream.

Each bitstream must be read _backward_,
that is starting from the end down to the beginning.
Therefore it's necessary to know the size of each bitstream.

It's also necessary to know exactly which _bit_ is the last one.
This is detected by a final bit flag :
the highest bit of latest byte is a final-bit-flag.
Consequently, a last byte of `0` is not possible.
And the final-bit-flag itself is not part of the useful bitstream.
Hence, the last byte contains between 0 and 7 useful bits.

Starting from the end,
it's possible to read the bitstream in a __little-endian__ fashion,
keeping track of already used bits. Since the bitstream is encoded in reverse
order, starting from the end read symbols in forward order.

For example, if the literal sequence "0145" was encoded using above prefix code,
it would be encoded (in reverse order) as:

|Symbol  |   5  |   4  |  1 | 0 | Padding |
|--------|------|------|----|---|---------|
|Encoding|`0000`|`0001`|`01`|`1`| `00001` |

Resulting in following 2-bytes bitstream :
```
00010000 00001101
```

Here is an alternative representation with the symbol codes separated by underscore:
```
0001_0000 00001_1_01
```

Reading highest `Max_Number_of_Bits` bits,
it's possible to compare extracted value to decoding table,
determining the symbol to decode and number of bits to discard.

The process continues up to reading the required number of symbols per stream.
If a bitstream is not entirely and exactly consumed,
hence reaching exactly its beginning position with _all_ bits consumed,
the decoding process is considered faulty.


Dictionary Format
-----------------

Zstandard is compatible with "raw content" dictionaries,
free of any format restriction, except that they must be at least 8 bytes.
These dictionaries function as if they were just the `Content` part
of a formatted dictionary.

But dictionaries created by `zstd --train` follow a format, described here.

__Pre-requisites__ : a dictionary has a size,
                     defined either by a buffer limit, or a file size.

| `Magic_Number` | `Dictionary_ID` | `Entropy_Tables` | `Content` |
| -------------- | --------------- | ---------------- | --------- |

__`Magic_Number`__ : 4 bytes ID, value 0xEC30A437, __little-endian__ format

__`Dictionary_ID`__ : 4 bytes, stored in __little-endian__ format.
              `Dictionary_ID` can be any value, except 0 (which means no `Dictionary_ID`).
              It's used by decoders to check if they use the correct dictionary.

_Reserved ranges :_
              If the frame is going to be distributed in a private environment,
              any `Dictionary_ID` can be used.
              However, for public distribution of compressed frames,
              the following ranges are reserved and shall not be used :

              - low range  : <= 32767
              - high range : >= (2^31)

__`Entropy_Tables`__ : follow the same format as tables in [compressed blocks].
              See the relevant [FSE](#fse-table-description)
              and [Huffman](#huffman-tree-description) sections for how to decode these tables.
              They are stored in following order :
              Huffman tables for literals, FSE table for offsets,
              FSE table for match lengths, and FSE table for literals lengths.
              These tables populate the Repeat Stats literals mode and
              Repeat distribution mode for sequence decoding.
              It's finally followed by 3 offset values, populating recent offsets (instead of using `{1,4,8}`),
              stored in order, 4-bytes __little-endian__ each, for a total of 12 bytes.
              Each recent offset must have a value < dictionary size.

__`Content`__ : The rest of the dictionary is its content.
              The content act as a "past" in front of data to compress or decompress,
              so it can be referenced in sequence commands.
              As long as the amount of data decoded from this frame is less than or
              equal to `Window_Size`, sequence commands may specify offsets longer
              than the total length of decoded output so far to reference back to the
              dictionary, even parts of the dictionary with offsets larger than `Window_Size`.  
              After the total output has surpassed `Window_Size` however,
              this is no longer allowed and the dictionary is no longer accessible.

[compressed blocks]: #the-format-of-compressed_block

If a dictionary is provided by an external source,
it should be loaded with great care, its content considered untrusted.



Appendix A - Decoding tables for predefined codes
-------------------------------------------------

This appendix contains FSE decoding tables
for the predefined literal length, match length, and offset codes.
The tables have been constructed using the algorithm as given above in chapter
"from normalized distribution to decoding tables".
The tables here can be used as examples
to crosscheck that an implementation build its decoding tables correctly.

#### Literal Length Code:

| State | Symbol | Number_Of_Bits | Base |
| ----- | ------ | -------------- | ---- |
|     0 |      0 |              4 |    0 |
|     1 |      0 |              4 |   16 |
|     2 |      1 |              5 |   32 |
|     3 |      3 |              5 |    0 |
|     4 |      4 |              5 |    0 |
|     5 |      6 |              5 |    0 |
|     6 |      7 |              5 |    0 |
|     7 |      9 |              5 |    0 |
|     8 |     10 |              5 |    0 |
|     9 |     12 |              5 |    0 |
|    10 |     14 |              6 |    0 |
|    11 |     16 |              5 |    0 |
|    12 |     18 |              5 |    0 |
|    13 |     19 |              5 |    0 |
|    14 |     21 |              5 |    0 |
|    15 |     22 |              5 |    0 |
|    16 |     24 |              5 |    0 |
|    17 |     25 |              5 |   32 |
|    18 |     26 |              5 |    0 |
|    19 |     27 |              6 |    0 |
|    20 |     29 |              6 |    0 |
|    21 |     31 |              6 |    0 |
|    22 |      0 |              4 |   32 |
|    23 |      1 |              4 |    0 |
|    24 |      2 |              5 |    0 |
|    25 |      4 |              5 |   32 |
|    26 |      5 |              5 |    0 |
|    27 |      7 |              5 |   32 |
|    28 |      8 |              5 |    0 |
|    29 |     10 |              5 |   32 |
|    30 |     11 |              5 |    0 |
|    31 |     13 |              6 |    0 |
|    32 |     16 |              5 |   32 |
|    33 |     17 |              5 |    0 |
|    34 |     19 |              5 |   32 |
|    35 |     20 |              5 |    0 |
|    36 |     22 |              5 |   32 |
|    37 |     23 |              5 |    0 |
|    38 |     25 |              4 |    0 |
|    39 |     25 |              4 |   16 |
|    40 |     26 |              5 |   32 |
|    41 |     28 |              6 |    0 |
|    42 |     30 |              6 |    0 |
|    43 |      0 |              4 |   48 |
|    44 |      1 |              4 |   16 |
|    45 |      2 |              5 |   32 |
|    46 |      3 |              5 |   32 |
|    47 |      5 |              5 |   32 |
|    48 |      6 |              5 |   32 |
|    49 |      8 |              5 |   32 |
|    50 |      9 |              5 |   32 |
|    51 |     11 |              5 |   32 |
|    52 |     12 |              5 |   32 |
|    53 |     15 |              6 |    0 |
|    54 |     17 |              5 |   32 |
|    55 |     18 |              5 |   32 |
|    56 |     20 |              5 |   32 |
|    57 |     21 |              5 |   32 |
|    58 |     23 |              5 |   32 |
|    59 |     24 |              5 |   32 |
|    60 |     35 |              6 |    0 |
|    61 |     34 |              6 |    0 |
|    62 |     33 |              6 |    0 |
|    63 |     32 |              6 |    0 |

#### Match Length Code:

| State | Symbol | Number_Of_Bits | Base |
| ----- | ------ | -------------- | ---- |
|     0 |      0 |              6 |    0 |
|     1 |      1 |              4 |    0 |
|     2 |      2 |              5 |   32 |
|     3 |      3 |              5 |    0 |
|     4 |      5 |              5 |    0 |
|     5 |      6 |              5 |    0 |
|     6 |      8 |              5 |    0 |
|     7 |     10 |              6 |    0 |
|     8 |     13 |              6 |    0 |
|     9 |     16 |              6 |    0 |
|    10 |     19 |              6 |    0 |
|    11 |     22 |              6 |    0 |
|    12 |     25 |              6 |    0 |
|    13 |     28 |              6 |    0 |
|    14 |     31 |              6 |    0 |
|    15 |     33 |              6 |    0 |
|    16 |     35 |              6 |    0 |
|    17 |     37 |              6 |    0 |
|    18 |     39 |              6 |    0 |
|    19 |     41 |              6 |    0 |
|    20 |     43 |              6 |    0 |
|    21 |     45 |              6 |    0 |
|    22 |      1 |              4 |   16 |
|    23 |      2 |              4 |    0 |
|    24 |      3 |              5 |   32 |
|    25 |      4 |              5 |    0 |
|    26 |      6 |              5 |   32 |
|    27 |      7 |              5 |    0 |
|    28 |      9 |              6 |    0 |
|    29 |     12 |              6 |    0 |
|    30 |     15 |              6 |    0 |
|    31 |     18 |              6 |    0 |
|    32 |     21 |              6 |    0 |
|    33 |     24 |              6 |    0 |
|    34 |     27 |              6 |    0 |
|    35 |     30 |              6 |    0 |
|    36 |     32 |              6 |    0 |
|    37 |     34 |              6 |    0 |
|    38 |     36 |              6 |    0 |
|    39 |     38 |              6 |    0 |
|    40 |     40 |              6 |    0 |
|    41 |     42 |              6 |    0 |
|    42 |     44 |              6 |    0 |
|    43 |      1 |              4 |   32 |
|    44 |      1 |              4 |   48 |
|    45 |      2 |              4 |   16 |
|    46 |      4 |              5 |   32 |
|    47 |      5 |              5 |   32 |
|    48 |      7 |              5 |   32 |
|    49 |      8 |              5 |   32 |
|    50 |     11 |              6 |    0 |
|    51 |     14 |              6 |    0 |
|    52 |     17 |              6 |    0 |
|    53 |     20 |              6 |    0 |
|    54 |     23 |              6 |    0 |
|    55 |     26 |              6 |    0 |
|    56 |     29 |              6 |    0 |
|    57 |     52 |              6 |    0 |
|    58 |     51 |              6 |    0 |
|    59 |     50 |              6 |    0 |
|    60 |     49 |              6 |    0 |
|    61 |     48 |              6 |    0 |
|    62 |     47 |              6 |    0 |
|    63 |     46 |              6 |    0 |

#### Offset Code:

| State | Symbol | Number_Of_Bits | Base |
| ----- | ------ | -------------- | ---- |
|     0 |      0 |              5 |    0 |
|     1 |      6 |              4 |    0 |
|     2 |      9 |              5 |    0 |
|     3 |     15 |              5 |    0 |
|     4 |     21 |              5 |    0 |
|     5 |      3 |              5 |    0 |
|     6 |      7 |              4 |    0 |
|     7 |     12 |              5 |    0 |
|     8 |     18 |              5 |    0 |
|     9 |     23 |              5 |    0 |
|    10 |      5 |              5 |    0 |
|    11 |      8 |              4 |    0 |
|    12 |     14 |              5 |    0 |
|    13 |     20 |              5 |    0 |
|    14 |      2 |              5 |    0 |
|    15 |      7 |              4 |   16 |
|    16 |     11 |              5 |    0 |
|    17 |     17 |              5 |    0 |
|    18 |     22 |              5 |    0 |
|    19 |      4 |              5 |    0 |
|    20 |      8 |              4 |   16 |
|    21 |     13 |              5 |    0 |
|    22 |     19 |              5 |    0 |
|    23 |      1 |              5 |    0 |
|    24 |      6 |              4 |   16 |
|    25 |     10 |              5 |    0 |
|    26 |     16 |              5 |    0 |
|    27 |     28 |              5 |    0 |
|    28 |     27 |              5 |    0 |
|    29 |     26 |              5 |    0 |
|    30 |     25 |              5 |    0 |
|    31 |     24 |              5 |    0 |



Appendix B - Resources for implementers
-------------------------------------------------

An open source reference implementation is available on :
https://github.com/facebook/zstd

The project contains a frame generator, called [decodeCorpus],
which can be used by any 3rd-party implementation
to verify that a tested decoder is compliant with the specification.

[decodeCorpus]: https://github.com/facebook/zstd/tree/v1.3.4/tests#decodecorpus---tool-to-generate-zstandard-frames-for-decoder-testing

`decodeCorpus` generates random valid frames.
A compliant decoder should be able to decode them all,
or at least provide a meaningful error code explaining for which reason it cannot
(memory limit restrictions for example).


Version changes
---------------
- 0.3.1 : minor clarification regarding offset history update rules
- 0.3.0 : minor edits to match RFC8478
- 0.2.9 : clarifications for huffman weights direct representation, by Ulrich Kunitz
- 0.2.8 : clarifications for IETF RFC discuss
- 0.2.7 : clarifications from IETF RFC review, by Vijay Gurbani and Nick Terrell
- 0.2.6 : fixed an error in huffman example, by Ulrich Kunitz
- 0.2.5 : minor typos and clarifications
- 0.2.4 : section restructuring, by Sean Purcell
- 0.2.3 : clarified several details, by Sean Purcell
- 0.2.2 : added predefined codes, by Johannes Rudolph
- 0.2.1 : clarify field names, by Przemyslaw Skibinski
- 0.2.0 : numerous format adjustments for zstd v0.8+
- 0.1.2 : limit Huffman tree depth to 11 bits
- 0.1.1 : reserved dictID ranges
- 0.1.0 : initial release
