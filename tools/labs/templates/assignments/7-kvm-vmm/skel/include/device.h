#ifndef DEVICE_H_
#define DEVICE_H_

#include "queue.h"

#define MAGIC_VALUE 0x74726976

#define DEVICE_RESET 0x0
#define DEVICE_CONFIG 0x2
#define DEVICE_READY 0x4

#define DRIVER_ACK 0x0
#define DRIVER 0x2
#define DRIVER_OK 0x4
#define DRIVER_RESET 0x8000

typedef struct device {
    uint32_t magic;
    uint8_t device_status;
    uint8_t driver_status;
    uint8_t max_queue_len;
} device_t;

typedef struct device_table {
    uint16_t count;
    uint64_t device_addresses[10];
 } device_table_t;
#endif