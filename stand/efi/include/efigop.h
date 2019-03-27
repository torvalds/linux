/* $FreeBSD$ */
/*++

Copyright (c)  1999 - 2002 Intel Corporation. All rights reserved
This software and associated documentation (if any) is furnished
under a license and may only be used or copied in accordance
with the terms of the license. Except as permitted by such
license, no part of this software or documentation may be
reproduced, stored in a retrieval system, or transmitted in any
form or by any means without the express written consent of
Intel Corporation.

Module Name:

    efigop.h

Abstract:
    Info about framebuffers




Revision History

--*/

#ifndef _EFIGOP_H
#define _EFIGOP_H

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a} }

INTERFACE_DECL(_EFI_GRAPHICS_OUTPUT);

typedef struct {
	UINT32	RedMask;
	UINT32	GreenMask;
	UINT32	BlueMask;
	UINT32	ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
	PixelRedGreenBlueReserved8BitPerColor,
	PixelBlueGreenRedReserved8BitPerColor,
	PixelBitMask,
	PixelBltOnly,
	PixelFormatMax,
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
	UINT32				Version;
	UINT32				HorizontalResolution;
	UINT32				VerticalResolution;
	EFI_GRAPHICS_PIXEL_FORMAT	PixelFormat;
	EFI_PIXEL_BITMASK		PixelInformation;
	UINT32				PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
	UINT32					MaxMode;
	UINT32					Mode;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION	*Info;
	UINTN					SizeOfInfo;
	EFI_PHYSICAL_ADDRESS			FrameBufferBase;
	UINTN					FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef
EFI_STATUS
(EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE) (
    IN  struct _EFI_GRAPHICS_OUTPUT		*This,
    IN  UINT32					ModeNumber,
    OUT UINTN					*SizeOfInfo,
    OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION	**Info
    );

typedef
EFI_STATUS
(EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE) (
    IN  struct _EFI_GRAPHICS_OUTPUT	*This,
    IN  UINT32				ModeNumber
    );

typedef struct {
	UINT8	Blue;
	UINT8	Green;
	UINT8	Red;
	UINT8	Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

typedef enum {
	EfiBltVideoFill,
	EfiBltVideoToBltBuffer,
	EfiBltBufferToVideo,
	EfiBltVideoToVideo,
	EfiGraphcisOutputBltOperationMax,
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

typedef
EFI_STATUS
(EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT) (
    IN struct _EFI_GRAPHICS_OUTPUT		*This,
    IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL	*BltBuffer,
    IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION	BltOperation,
    IN UINTN					SourceX,
    IN UINTN					SourceY,
    IN UINTN					DestinationX,
    IN UINTN					DestinationY,
    IN UINTN					Width,
    IN UINTN					Height,
    IN UINTN					Delta
    );

typedef struct _EFI_GRAPHICS_OUTPUT {
	EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE	QueryMode;
	EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE	SetMode;
	EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT	Blt;
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE	*Mode;
} EFI_GRAPHICS_OUTPUT;

#endif /* _EFIGOP_H */
