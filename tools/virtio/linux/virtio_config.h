#define VIRTIO_TRANSPORT_F_START	28
#define VIRTIO_TRANSPORT_F_END		32

#define virtio_has_feature(dev, feature) \
	(__virtio_test_bit((dev), feature))

