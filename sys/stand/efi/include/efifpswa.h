/* $FreeBSD: head/sys/boot/efi/include/efifpswa.h 96893 2002-05-19 03:17:22Z marcel $ */
#ifndef _EFI_FPSWA_H
#define _EFI_FPSWA_H

/*
 * EFI FP SWA Driver (Floating Point Software Assist)
 */

#define EFI_INTEL_FPSWA \
    { 0xc41b6531, 0x97b9, 0x11d3, { 0x9a, 0x29, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

INTERFACE_DECL(_FPSWA_INTERFACE);

typedef struct _FPSWA_RET {
    UINT64                          status;
    UINT64                          err1;
    UINT64                          err2;
    UINT64                          err3;
} FPSWA_RET;

typedef
FPSWA_RET
(EFIAPI *EFI_FPSWA) (
    IN UINTN                        TrapType,
    IN OUT VOID                     *Bundle,
    IN OUT UINT64                   *pipsr,
    IN OUT UINT64                   *pfsr,
    IN OUT UINT64                   *pisr,
    IN OUT UINT64                   *ppreds,
    IN OUT UINT64                   *pifs,
    IN OUT VOID                     *fp_state
    );

typedef struct _FPSWA_INTERFACE {
    UINT32                          Revision;
    UINT32                          Reserved;
    EFI_FPSWA                       Fpswa;
} FPSWA_INTERFACE;

#endif
