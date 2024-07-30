/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _IIO_BACKEND_H_
#define _IIO_BACKEND_H_

#include <linux/types.h>
#include <linux/iio/iio.h>

struct iio_chan_spec;
struct fwnode_handle;
struct iio_backend;
struct device;
struct iio_dev;

enum iio_backend_data_type {
	IIO_BACKEND_TWOS_COMPLEMENT,
	IIO_BACKEND_OFFSET_BINARY,
	IIO_BACKEND_DATA_TYPE_MAX
};

enum iio_backend_data_source {
	IIO_BACKEND_INTERNAL_CONTINUOUS_WAVE,
	IIO_BACKEND_EXTERNAL,
	IIO_BACKEND_DATA_SOURCE_MAX
};

#define iio_backend_debugfs_ptr(ptr)	PTR_IF(IS_ENABLED(CONFIG_DEBUG_FS), ptr)

/**
 * IIO_BACKEND_EX_INFO - Helper for an IIO extended channel attribute
 * @_name: Attribute name
 * @_shared: Whether the attribute is shared between all channels
 * @_what: Data private to the driver
 */
#define IIO_BACKEND_EX_INFO(_name, _shared, _what) {	\
	.name = (_name),				\
	.shared = (_shared),				\
	.read =  iio_backend_ext_info_get,		\
	.write = iio_backend_ext_info_set,		\
	.private = (_what),				\
}

/**
 * struct iio_backend_data_fmt - Backend data format
 * @type: Data type.
 * @sign_extend: Bool to tell if the data is sign extended.
 * @enable: Enable/Disable the data format module. If disabled,
 *	    not formatting will happen.
 */
struct iio_backend_data_fmt {
	enum iio_backend_data_type type;
	bool sign_extend;
	bool enable;
};

/* vendor specific from 32 */
enum iio_backend_test_pattern {
	IIO_BACKEND_NO_TEST_PATTERN,
	/* modified prbs9 */
	IIO_BACKEND_ADI_PRBS_9A = 32,
	/* modified prbs23 */
	IIO_BACKEND_ADI_PRBS_23A,
	IIO_BACKEND_TEST_PATTERN_MAX
};

enum iio_backend_sample_trigger {
	IIO_BACKEND_SAMPLE_TRIGGER_EDGE_FALLING,
	IIO_BACKEND_SAMPLE_TRIGGER_EDGE_RISING,
	IIO_BACKEND_SAMPLE_TRIGGER_MAX
};

/**
 * struct iio_backend_ops - operations structure for an iio_backend
 * @enable: Enable backend.
 * @disable: Disable backend.
 * @chan_enable: Enable one channel.
 * @chan_disable: Disable one channel.
 * @data_format_set: Configure the data format for a specific channel.
 * @data_source_set: Configure the data source for a specific channel.
 * @set_sample_rate: Configure the sampling rate for a specific channel.
 * @test_pattern_set: Configure a test pattern.
 * @chan_status: Get the channel status.
 * @iodelay_set: Set digital I/O delay.
 * @data_sample_trigger: Control when to sample data.
 * @request_buffer: Request an IIO buffer.
 * @free_buffer: Free an IIO buffer.
 * @extend_chan_spec: Extend an IIO channel.
 * @ext_info_set: Extended info setter.
 * @ext_info_get: Extended info getter.
 * @read_raw: Read a channel attribute from a backend device
 * @debugfs_print_chan_status: Print channel status into a buffer.
 * @debugfs_reg_access: Read or write register value of backend.
 **/
struct iio_backend_ops {
	int (*enable)(struct iio_backend *back);
	void (*disable)(struct iio_backend *back);
	int (*chan_enable)(struct iio_backend *back, unsigned int chan);
	int (*chan_disable)(struct iio_backend *back, unsigned int chan);
	int (*data_format_set)(struct iio_backend *back, unsigned int chan,
			       const struct iio_backend_data_fmt *data);
	int (*data_source_set)(struct iio_backend *back, unsigned int chan,
			       enum iio_backend_data_source data);
	int (*set_sample_rate)(struct iio_backend *back, unsigned int chan,
			       u64 sample_rate_hz);
	int (*test_pattern_set)(struct iio_backend *back,
				unsigned int chan,
				enum iio_backend_test_pattern pattern);
	int (*chan_status)(struct iio_backend *back, unsigned int chan,
			   bool *error);
	int (*iodelay_set)(struct iio_backend *back, unsigned int chan,
			   unsigned int taps);
	int (*data_sample_trigger)(struct iio_backend *back,
				   enum iio_backend_sample_trigger trigger);
	struct iio_buffer *(*request_buffer)(struct iio_backend *back,
					     struct iio_dev *indio_dev);
	void (*free_buffer)(struct iio_backend *back,
			    struct iio_buffer *buffer);
	int (*extend_chan_spec)(struct iio_backend *back,
				struct iio_chan_spec *chan);
	int (*ext_info_set)(struct iio_backend *back, uintptr_t private,
			    const struct iio_chan_spec *chan,
			    const char *buf, size_t len);
	int (*ext_info_get)(struct iio_backend *back, uintptr_t private,
			    const struct iio_chan_spec *chan, char *buf);
	int (*read_raw)(struct iio_backend *back,
			struct iio_chan_spec const *chan, int *val, int *val2,
			long mask);
	int (*debugfs_print_chan_status)(struct iio_backend *back,
					 unsigned int chan, char *buf,
					 size_t len);
	int (*debugfs_reg_access)(struct iio_backend *back, unsigned int reg,
				  unsigned int writeval, unsigned int *readval);
};

/**
 * struct iio_backend_info - info structure for an iio_backend
 * @name: Backend name.
 * @ops: Backend operations.
 */
struct iio_backend_info {
	const char *name;
	const struct iio_backend_ops *ops;
};

int iio_backend_chan_enable(struct iio_backend *back, unsigned int chan);
int iio_backend_chan_disable(struct iio_backend *back, unsigned int chan);
int devm_iio_backend_enable(struct device *dev, struct iio_backend *back);
int iio_backend_enable(struct iio_backend *back);
void iio_backend_disable(struct iio_backend *back);
int iio_backend_data_format_set(struct iio_backend *back, unsigned int chan,
				const struct iio_backend_data_fmt *data);
int iio_backend_data_source_set(struct iio_backend *back, unsigned int chan,
				enum iio_backend_data_source data);
int iio_backend_set_sampling_freq(struct iio_backend *back, unsigned int chan,
				  u64 sample_rate_hz);
int iio_backend_test_pattern_set(struct iio_backend *back,
				 unsigned int chan,
				 enum iio_backend_test_pattern pattern);
int iio_backend_chan_status(struct iio_backend *back, unsigned int chan,
			    bool *error);
int iio_backend_iodelay_set(struct iio_backend *back, unsigned int lane,
			    unsigned int taps);
int iio_backend_data_sample_trigger(struct iio_backend *back,
				    enum iio_backend_sample_trigger trigger);
int devm_iio_backend_request_buffer(struct device *dev,
				    struct iio_backend *back,
				    struct iio_dev *indio_dev);
ssize_t iio_backend_ext_info_set(struct iio_dev *indio_dev, uintptr_t private,
				 const struct iio_chan_spec *chan,
				 const char *buf, size_t len);
ssize_t iio_backend_ext_info_get(struct iio_dev *indio_dev, uintptr_t private,
				 const struct iio_chan_spec *chan, char *buf);
int iio_backend_read_raw(struct iio_backend *back,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask);
int iio_backend_extend_chan_spec(struct iio_backend *back,
				 struct iio_chan_spec *chan);
void *iio_backend_get_priv(const struct iio_backend *conv);
struct iio_backend *devm_iio_backend_get(struct device *dev, const char *name);
struct iio_backend *devm_iio_backend_fwnode_get(struct device *dev,
						const char *name,
						struct fwnode_handle *fwnode);
struct iio_backend *
__devm_iio_backend_get_from_fwnode_lookup(struct device *dev,
					  struct fwnode_handle *fwnode);

int devm_iio_backend_register(struct device *dev,
			      const struct iio_backend_info *info, void *priv);

static inline int iio_backend_read_scale(struct iio_backend *back,
					 struct iio_chan_spec const *chan,
					 int *val, int *val2)
{
	return iio_backend_read_raw(back, chan, val, val2, IIO_CHAN_INFO_SCALE);
}

static inline int iio_backend_read_offset(struct iio_backend *back,
					  struct iio_chan_spec const *chan,
					  int *val, int *val2)
{
	return iio_backend_read_raw(back, chan, val, val2,
				    IIO_CHAN_INFO_OFFSET);
}

ssize_t iio_backend_debugfs_print_chan_status(struct iio_backend *back,
					      unsigned int chan, char *buf,
					      size_t len);
void iio_backend_debugfs_add(struct iio_backend *back,
			     struct iio_dev *indio_dev);
#endif
