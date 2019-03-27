# Zstandard Seekable Format

### Notices

Copyright (c) 2017-present Facebook, Inc.

Permission is granted to copy and distribute this document
for any purpose and without charge,
including translations into other languages
and incorporation into compilations,
provided that the copyright notice and this notice are preserved,
and that any substantive changes or deletions from the original
are clearly marked.
Distribution of this document is unlimited.

### Version
0.1.0 (11/04/17)

## Introduction
This document defines a format for compressed data to be stored so that subranges of the data can be efficiently decompressed without requiring the entire document to be decompressed.
This is done by splitting up the input data into frames,
each of which are compressed independently,
and so can be decompressed independently.
Decompression then takes advantage of a provided 'seek table', which allows the decompressor to immediately jump to the desired data.  This is done in a way that is compatible with the original Zstandard format by placing the seek table in a Zstandard skippable frame.

### Overall conventions
In this document:
- square brackets i.e. `[` and `]` are used to indicate optional fields or parameters.
- the naming convention for identifiers is `Mixed_Case_With_Underscores`
- All numeric fields are little-endian unless specified otherwise

## Format

The format consists of a number of frames (Zstandard compressed frames and skippable frames), followed by a final skippable frame at the end containing the seek table.

### Seek Table Format
The structure of the seek table frame is as follows:

|`Skippable_Magic_Number`|`Frame_Size`|`[Seek_Table_Entries]`|`Seek_Table_Footer`|
|------------------------|------------|----------------------|-------------------|
| 4 bytes                | 4 bytes    | 8-12 bytes each      | 9 bytes           |

__`Skippable_Magic_Number`__

Value : 0x184D2A5E.
This is for compatibility with [Zstandard skippable frames].
Since it is legal for other Zstandard skippable frames to use the same
magic number, it is not recommended for a decoder to recognize frames
solely on this.

__`Frame_Size`__

The total size of the skippable frame, not including the `Skippable_Magic_Number` or `Frame_Size`.
This is for compatibility with [Zstandard skippable frames].

[Zstandard skippable frames]: https://github.com/facebook/zstd/blob/master/doc/zstd_compression_format.md#skippable-frames

#### `Seek_Table_Footer`
The seek table footer format is as follows:

|`Number_Of_Frames`|`Seek_Table_Descriptor`|`Seekable_Magic_Number`|
|------------------|-----------------------|-----------------------|
| 4 bytes          | 1 byte                | 4 bytes               |

__`Seekable_Magic_Number`__

Value : 0x8F92EAB1.
This value must be the last bytes present in the compressed file so that decoders
can efficiently find it and determine if there is an actual seek table present.

__`Number_Of_Frames`__

The number of stored frames in the data.

__`Seek_Table_Descriptor`__

A bitfield describing the format of the seek table.

| Bit number | Field name                |
| ---------- | ----------                |
| 7          | `Checksum_Flag`           |
| 6-2        | `Reserved_Bits`           |
| 1-0        | `Unused_Bits`             |

While only `Checksum_Flag` currently exists, there are 7 other bits in this field that can be used for future changes to the format,
for example the addition of inline dictionaries.

__`Checksum_Flag`__

If the checksum flag is set, each of the seek table entries contains a 4 byte checksum of the uncompressed data contained in its frame.

`Reserved_Bits` are not currently used but may be used in the future for breaking changes, so a compliant decoder should ensure they are set to 0.  `Unused_Bits` may be used in the future for non-breaking changes, so a compliant decoder should not interpret these bits.

#### __`Seek_Table_Entries`__

`Seek_Table_Entries` consists of `Number_Of_Frames` (one for each frame in the data, not including the seek table frame) entries of the following form, in sequence:

|`Compressed_Size`|`Decompressed_Size`|`[Checksum]`|
|-----------------|-------------------|------------|
| 4 bytes         | 4 bytes           | 4 bytes    |

__`Compressed_Size`__

The compressed size of the frame.
The cumulative sum of the `Compressed_Size` fields of frames `0` to `i` gives the offset in the compressed file of frame `i+1`.

__`Decompressed_Size`__

The size of the decompressed data contained in the frame.  For skippable or otherwise empty frames, this value is 0.

__`Checksum`__

Only present if `Checksum_Flag` is set in the `Seek_Table_Descriptor`.  Value : the least significant 32 bits of the XXH64 digest of the uncompressed data, stored in little-endian format.

## Version Changes
- 0.1.0: initial version
