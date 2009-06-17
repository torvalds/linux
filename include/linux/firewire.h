#ifndef _LINUX_FIREWIRE_H
#define _LINUX_FIREWIRE_H

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <asm/atomic.h>
#include <asm/byteorder.h>

#define fw_notify(s, args...) printk(KERN_NOTICE KBUILD_MODNAME ": " s, ## args)
#define fw_error(s, args...) printk(KERN_ERR KBUILD_MODNAME ": " s, ## args)

static inline void fw_memcpy_from_be32(void *_dst, void *_src, size_t size)
{
	u32    *dst = _dst;
	__be32 *src = _src;
	int i;

	for (i = 0; i < size / 4; i++)
		dst[i] = be32_to_cpu(src[i]);
}

static inline void fw_memcpy_to_be32(void *_dst, void *_src, size_t size)
{
	fw_memcpy_from_be32(_dst, _src, size);
}
#define CSR_REGISTER_BASE		0xfffff0000000ULL

/* register offsets are relative to CSR_REGISTER_BASE */
#define CSR_STATE_CLEAR			0x0
#define CSR_STATE_SET			0x4
#define CSR_NODE_IDS			0x8
#define CSR_RESET_START			0xc
#define CSR_SPLIT_TIMEOUT_HI		0x18
#define CSR_SPLIT_TIMEOUT_LO		0x1c
#define CSR_CYCLE_TIME			0x200
#define CSR_BUS_TIME			0x204
#define CSR_BUSY_TIMEOUT		0x210
#define CSR_BUS_MANAGER_ID		0x21c
#define CSR_BANDWIDTH_AVAILABLE		0x220
#define CSR_CHANNELS_AVAILABLE		0x224
#define CSR_CHANNELS_AVAILABLE_HI	0x224
#define CSR_CHANNELS_AVAILABLE_LO	0x228
#define CSR_BROADCAST_CHANNEL		0x234
#define CSR_CONFIG_ROM			0x400
#define CSR_CONFIG_ROM_END		0x800
#define CSR_FCP_COMMAND			0xB00
#define CSR_FCP_RESPONSE		0xD00
#define CSR_FCP_END			0xF00
#define CSR_TOPOLOGY_MAP		0x1000
#define CSR_TOPOLOGY_MAP_END		0x1400
#define CSR_SPEED_MAP			0x2000
#define CSR_SPEED_MAP_END		0x3000

#define CSR_OFFSET		0x40
#define CSR_LEAF		0x80
#define CSR_DIRECTORY		0xc0

#define CSR_DESCRIPTOR		0x01
#define CSR_VENDOR		0x03
#define CSR_HARDWARE_VERSION	0x04
#define CSR_NODE_CAPABILITIES	0x0c
#define CSR_UNIT		0x11
#define CSR_SPECIFIER_ID	0x12
#define CSR_VERSION		0x13
#define CSR_DEPENDENT_INFO	0x14
#define CSR_MODEL		0x17
#define CSR_INSTANCE		0x18
#define CSR_DIRECTORY_ID	0x20

struct fw_csr_iterator {
	u32 *p;
	u32 *end;
};

void fw_csr_iterator_init(struct fw_csr_iterator *ci, u32 *p);
int fw_csr_iterator_next(struct fw_csr_iterator *ci, int *key, int *value);

extern struct bus_type fw_bus_type;

struct fw_card_driver;
struct fw_node;

struct fw_card {
	const struct fw_card_driver *driver;
	struct device *device;
	struct kref kref;
	struct completion done;

	int node_id;
	int generation;
	int current_tlabel;
	u64 tlabel_mask;
	struct list_head transaction_list;
	struct timer_list flush_timer;
	unsigned long reset_jiffies;

	unsigned long long guid;
	unsigned max_receive;
	int link_speed;
	int config_rom_generation;

	spinlock_t lock; /* Take this lock when handling the lists in
			  * this struct. */
	struct fw_node *local_node;
	struct fw_node *root_node;
	struct fw_node *irm_node;
	u8 color; /* must be u8 to match the definition in struct fw_node */
	int gap_count;
	bool beta_repeaters_present;

	int index;

	struct list_head link;

	/* Work struct for BM duties. */
	struct delayed_work work;
	int bm_retries;
	int bm_generation;

	bool broadcast_channel_allocated;
	u32 broadcast_channel;
	u32 topology_map[(CSR_TOPOLOGY_MAP_END - CSR_TOPOLOGY_MAP) / 4];
};

static inline struct fw_card *fw_card_get(struct fw_card *card)
{
	kref_get(&card->kref);

	return card;
}

void fw_card_release(struct kref *kref);

static inline void fw_card_put(struct fw_card *card)
{
	kref_put(&card->kref, fw_card_release);
}

struct fw_attribute_group {
	struct attribute_group *groups[2];
	struct attribute_group group;
	struct attribute *attrs[12];
};

enum fw_device_state {
	FW_DEVICE_INITIALIZING,
	FW_DEVICE_RUNNING,
	FW_DEVICE_GONE,
	FW_DEVICE_SHUTDOWN,
};

/*
 * Note, fw_device.generation always has to be read before fw_device.node_id.
 * Use SMP memory barriers to ensure this.  Otherwise requests will be sent
 * to an outdated node_id if the generation was updated in the meantime due
 * to a bus reset.
 *
 * Likewise, fw-core will take care to update .node_id before .generation so
 * that whenever fw_device.generation is current WRT the actual bus generation,
 * fw_device.node_id is guaranteed to be current too.
 *
 * The same applies to fw_device.card->node_id vs. fw_device.generation.
 *
 * fw_device.config_rom and fw_device.config_rom_length may be accessed during
 * the lifetime of any fw_unit belonging to the fw_device, before device_del()
 * was called on the last fw_unit.  Alternatively, they may be accessed while
 * holding fw_device_rwsem.
 */
struct fw_device {
	atomic_t state;
	struct fw_node *node;
	int node_id;
	int generation;
	unsigned max_speed;
	struct fw_card *card;
	struct device device;

	struct mutex client_list_mutex;
	struct list_head client_list;

	u32 *config_rom;
	size_t config_rom_length;
	int config_rom_retries;
	unsigned is_local:1;
	unsigned max_rec:4;
	unsigned cmc:1;
	unsigned irmc:1;
	unsigned bc_implemented:2;

	struct delayed_work work;
	struct fw_attribute_group attribute_group;
};

static inline struct fw_device *fw_device(struct device *dev)
{
	return container_of(dev, struct fw_device, device);
}

static inline int fw_device_is_shutdown(struct fw_device *device)
{
	return atomic_read(&device->state) == FW_DEVICE_SHUTDOWN;
}

static inline struct fw_device *fw_device_get(struct fw_device *device)
{
	get_device(&device->device);

	return device;
}

static inline void fw_device_put(struct fw_device *device)
{
	put_device(&device->device);
}

int fw_device_enable_phys_dma(struct fw_device *device);

/*
 * fw_unit.directory must not be accessed after device_del(&fw_unit.device).
 */
struct fw_unit {
	struct device device;
	u32 *directory;
	struct fw_attribute_group attribute_group;
};

static inline struct fw_unit *fw_unit(struct device *dev)
{
	return container_of(dev, struct fw_unit, device);
}

static inline struct fw_unit *fw_unit_get(struct fw_unit *unit)
{
	get_device(&unit->device);

	return unit;
}

static inline void fw_unit_put(struct fw_unit *unit)
{
	put_device(&unit->device);
}

static inline struct fw_device *fw_parent_device(struct fw_unit *unit)
{
	return fw_device(unit->device.parent);
}

struct ieee1394_device_id;

struct fw_driver {
	struct device_driver driver;
	/* Called when the parent device sits through a bus reset. */
	void (*update)(struct fw_unit *unit);
	const struct ieee1394_device_id *id_table;
};

struct fw_packet;
struct fw_request;

typedef void (*fw_packet_callback_t)(struct fw_packet *packet,
				     struct fw_card *card, int status);
typedef void (*fw_transaction_callback_t)(struct fw_card *card, int rcode,
					  void *data, size_t length,
					  void *callback_data);
/*
 * Important note:  The callback must guarantee that either fw_send_response()
 * or kfree() is called on the @request.
 */
typedef void (*fw_address_callback_t)(struct fw_card *card,
				      struct fw_request *request,
				      int tcode, int destination, int source,
				      int generation, int speed,
				      unsigned long long offset,
				      void *data, size_t length,
				      void *callback_data);

struct fw_packet {
	int speed;
	int generation;
	u32 header[4];
	size_t header_length;
	void *payload;
	size_t payload_length;
	dma_addr_t payload_bus;
	u32 timestamp;

	/*
	 * This callback is called when the packet transmission has
	 * completed; for successful transmission, the status code is
	 * the ack received from the destination, otherwise it's a
	 * negative errno: ENOMEM, ESTALE, ETIMEDOUT, ENODEV, EIO.
	 * The callback can be called from tasklet context and thus
	 * must never block.
	 */
	fw_packet_callback_t callback;
	int ack;
	struct list_head link;
	void *driver_data;
};

struct fw_transaction {
	int node_id; /* The generation is implied; it is always the current. */
	int tlabel;
	int timestamp;
	struct list_head link;

	struct fw_packet packet;

	/*
	 * The data passed to the callback is valid only during the
	 * callback.
	 */
	fw_transaction_callback_t callback;
	void *callback_data;
};

struct fw_address_handler {
	u64 offset;
	size_t length;
	fw_address_callback_t address_callback;
	void *callback_data;
	struct list_head link;
};

struct fw_address_region {
	u64 start;
	u64 end;
};

extern const struct fw_address_region fw_high_memory_region;

int fw_core_add_address_handler(struct fw_address_handler *handler,
				const struct fw_address_region *region);
void fw_core_remove_address_handler(struct fw_address_handler *handler);
void fw_send_response(struct fw_card *card,
		      struct fw_request *request, int rcode);
void fw_send_request(struct fw_card *card, struct fw_transaction *t,
		     int tcode, int destination_id, int generation, int speed,
		     unsigned long long offset, void *payload, size_t length,
		     fw_transaction_callback_t callback, void *callback_data);
int fw_cancel_transaction(struct fw_card *card,
			  struct fw_transaction *transaction);
int fw_run_transaction(struct fw_card *card, int tcode, int destination_id,
		       int generation, int speed, unsigned long long offset,
		       void *payload, size_t length);

#endif /* _LINUX_FIREWIRE_H */
