#ifndef __ACPI_VIDEO_H
#define __ACPI_VIDEO_H

#if (defined CONFIG_ACPI_VIDEO || defined CONFIG_ACPI_VIDEO_MODULE)
extern int acpi_video_register(void);
extern int acpi_video_exit(void);
#else
static inline int acpi_video_register(void) { return 0; }
static inline void acpi_video_exit(void) { return; }
#endif

#endif

