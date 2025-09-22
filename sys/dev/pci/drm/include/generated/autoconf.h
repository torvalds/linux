/* Public domain. */

#include <sys/param.h>

#define CONFIG_DRM_KMS_HELPER			1
#define CONFIG_BACKLIGHT_CLASS_DEVICE		1
#define CONFIG_DRM_FBDEV_EMULATION		1
#define CONFIG_DRM_CLIENT_SETUP			1

#ifdef notyet
/* causes Intel GuC init to fail with large fbs */
#define CONFIG_FRAMEBUFFER_CONSOLE		1
#endif

#define CONFIG_DRM_PANEL			1
#define CONFIG_DRM_I915_CAPTURE_ERROR		1
#define CONFIG_DRM_AMD_DC			1
#if defined(__amd64__) || defined(__i386__)
#define CONFIG_DRM_AMD_DC_DCN			1
#define CONFIG_DRM_AMD_DC_FP			1
#endif
#if 0
#define CONFIG_DRM_AMDGPU_SI			1
#define CONFIG_DRM_AMD_DC_SI			1
#define CONFIG_DRM_AMDGPU_CIK			1
#endif

#define CONFIG_DRM_FBDEV_OVERALLOC		100

#define CONFIG_DRM_I915_PREEMPT_TIMEOUT		640	/* ms */
#define CONFIG_DRM_I915_TIMESLICE_DURATION	1	/* ms */
#define CONFIG_DRM_I915_HEARTBEAT_INTERVAL	2500	/* ms */
#define CONFIG_DRM_I915_MAX_REQUEST_BUSYWAIT	8000	/* ns */
#define CONFIG_DRM_I915_REQUEST_TIMEOUT		20000	/* ms */
#define CONFIG_DRM_I915_STOP_TIMEOUT		100	/* ms */
#define CONFIG_DRM_I915_FENCE_TIMEOUT		10000	/* ms */
#define CONFIG_DRM_I915_USERFAULT_AUTOSUSPEND	250	/* ms */
#define CONFIG_DRM_I915_PREEMPT_TIMEOUT_COMPUTE	7500	/* ms */
#define CONFIG_DRM_I915_FORCE_PROBE		""

#ifdef __HAVE_ACPI
#include "acpi.h"
#if NACPI > 0
#define CONFIG_ACPI				1
#define CONFIG_ACPI_SLEEP			1
#define CONFIG_AMD_PMC				1
#endif
#endif

#include "pci.h"
#if NPCI > 0
#define CONFIG_PCI				1
#define CONFIG_PCIEASPM				1
#endif

#include "agp.h"
#if NAGP > 0
#define CONFIG_AGP				1
#endif

#if defined(__amd64__) || defined(__i386__)
#define CONFIG_DMI				1
#endif

#ifdef __amd64__
#define CONFIG_X86				1
#define CONFIG_X86_64				1
#define CONFIG_X86_PAT				1
#endif

#ifdef __i386__
#define CONFIG_X86				1
#define CONFIG_X86_32				1
#define CONFIG_X86_PAT				1
#endif

#ifdef __arm__
#define CONFIG_ARM				1
#endif

#ifdef __arm64__
#define CONFIG_ARM64				1
#endif

#ifdef __macppc__
#define CONFIG_PPC				1
#define CONFIG_PPC_PMAC				1
#endif

#ifdef __powerpc64__
#define CONFIG_PPC64				1
#endif

#ifdef __loongson__
#define CONFIG_MIPS				1
#define CONFIG_CPU_LOONGSON64			1
#endif

#ifdef __LP64__
#define CONFIG_64BIT				1
#endif

#if defined(SUSPEND) || defined(HIBERNATE)
#define CONFIG_SUSPEND				1
#define CONFIG_PM_SLEEP				1
#endif
