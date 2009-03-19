#ifndef __ACPI_VIDEO_H
#define __ACPI_VIDEO_H

#if (defined CONFIG_ACPI_VIDEO || defined CONFIG_ACPI_VIDEO_MODULE)
extern int acpi_video_register(void);
#else
static inline int acpi_video_register(void) { return 0; }
#endif

#endif

