#include <linux/bug.h>
#include <linux/string.h>
#include <linux/virtio_features.h>

#ifndef VIRTIO_FEATURES_BITS
#define VIRTIO_FEATURES_BITS 128
#endif
#ifndef VIRTIO_U64
#define VIRTIO_U64(b)           ((b) >> 6)
#endif
