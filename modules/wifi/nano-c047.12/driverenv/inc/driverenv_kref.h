#ifndef __DRIVERENV_KREF_H__
#define __DRIVERENV_KREF_H__

#include <linux/kref.h>

#define de_kref kref

#define DriverEnvironment_kref_init kref_init
#define DriverEnvironment_kref_get kref_get
#define DriverEnvironment_kref_put kref_put

#endif /* __DRIVERENV_KREF_H__ */
