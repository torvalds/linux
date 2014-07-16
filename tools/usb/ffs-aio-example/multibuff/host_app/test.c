#include <libusb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define VENDOR	0x1d6b
#define PRODUCT	0x0105

/* endpoints indexes */

#define EP_BULK_IN	(1 | LIBUSB_ENDPOINT_IN)
#define EP_BULK_OUT	(2 | LIBUSB_ENDPOINT_OUT)

#define BUF_LEN		8192

/*
 * struct test_state - describes test program state
 * @list: list of devices returned by libusb_get_device_list function
 * @found: pointer to struct describing tested device
 * @ctx: context, set to NULL
 * @handle: handle of tested device
 * @attached: indicates that device was attached to kernel, and has to be
 *            reattached at the end of test program
 */

struct test_state {
	libusb_device *found;
	libusb_context *ctx;
	libusb_device_handle *handle;
	int attached;
};

/*
 * test_init - initialize test program
 */

int test_init(struct test_state *state)
{
	int i, ret;
	ssize_t cnt;
	libusb_device **list;

	state->found = NULL;
	state->ctx = NULL;
	state->handle = NULL;
	state->attached = 0;

	ret = libusb_init(&state->ctx);
	if (ret) {
		printf("cannot init libusb: %s\n", libusb_error_name(ret));
		return 1;
	}

	cnt = libusb_get_device_list(state->ctx, &list);
	if (cnt <= 0) {
		printf("no devices found\n");
		goto error1;
	}

	for (i = 0; i < cnt; ++i) {
		libusb_device *dev = list[i];
		struct libusb_device_descriptor desc;
		ret = libusb_get_device_descriptor(dev, &desc);
		if (ret) {
			printf("unable to get device descriptor: %s\n",
			       libusb_error_name(ret));
			goto error2;
		}
		if (desc.idVendor == VENDOR && desc.idProduct == PRODUCT) {
			state->found = dev;
			break;
		}
	}

	if (!state->found) {
		printf("no devices found\n");
		goto error2;
	}

	ret = libusb_open(state->found, &state->handle);
	if (ret) {
		printf("cannot open device: %s\n", libusb_error_name(ret));
		goto error2;
	}

	if (libusb_claim_interface(state->handle, 0)) {
		ret = libusb_detach_kernel_driver(state->handle, 0);
		if (ret) {
			printf("unable to detach kernel driver: %s\n",
			       libusb_error_name(ret));
			goto error3;
		}
		state->attached = 1;
		ret = libusb_claim_interface(state->handle, 0);
		if (ret) {
			printf("cannot claim interface: %s\n",
			       libusb_error_name(ret));
			goto error4;
		}
	}

	return 0;

error4:
	if (state->attached == 1)
		libusb_attach_kernel_driver(state->handle, 0);

error3:
	libusb_close(state->handle);

error2:
	libusb_free_device_list(list, 1);

error1:
	libusb_exit(state->ctx);
	return 1;
}

/*
 * test_exit - cleanup test program
 */

void test_exit(struct test_state *state)
{
	libusb_release_interface(state->handle, 0);
	if (state->attached == 1)
		libusb_attach_kernel_driver(state->handle, 0);
	libusb_close(state->handle);
	libusb_exit(state->ctx);
}

int main(void)
{
	struct test_state state;

	if (test_init(&state))
		return 1;

	while (1) {
		static unsigned char buffer[BUF_LEN];
		int bytes;
		libusb_bulk_transfer(state.handle, EP_BULK_IN, buffer, BUF_LEN,
				     &bytes, 500);
	}
	test_exit(&state);
}
