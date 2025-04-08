// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the generic kernel FIFO implementation.
 *
 * Copyright (C) 2024 Diego Vieira <diego.daniel.professional@gmail.com>
 */
#include <kunit/test.h>

#include <linux/kfifo.h>

#define KFIFO_SIZE 32
#define N_ELEMENTS 5

static void kfifo_test_reset_should_clear_the_fifo(struct kunit *test)
{
	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	kfifo_put(&my_fifo, 1);
	kfifo_put(&my_fifo, 2);
	kfifo_put(&my_fifo, 3);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 3);

	kfifo_reset(&my_fifo);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 0);
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
}

static void kfifo_test_define_should_define_an_empty_fifo(struct kunit *test)
{
	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	KUNIT_EXPECT_TRUE(test, kfifo_initialized(&my_fifo));
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 0);
}

static void kfifo_test_len_should_ret_n_of_stored_elements(struct kunit *test)
{
	u8 buffer1[N_ELEMENTS];

	for (int i = 0; i < N_ELEMENTS; i++)
		buffer1[i] = i + 1;

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 0);

	kfifo_in(&my_fifo, buffer1, N_ELEMENTS);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), N_ELEMENTS);

	kfifo_in(&my_fifo, buffer1, N_ELEMENTS);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), N_ELEMENTS * 2);

	kfifo_reset(&my_fifo);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 0);
}

static void kfifo_test_put_should_insert_and_get_should_pop(struct kunit *test)
{
	u8 out_data = 0;
	int processed_elements;
	u8 elements[] = { 3, 5, 11 };

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	// If the fifo is empty, get returns 0
	processed_elements = kfifo_get(&my_fifo, &out_data);
	KUNIT_EXPECT_EQ(test, processed_elements, 0);
	KUNIT_EXPECT_EQ(test, out_data, 0);

	for (int i = 0; i < 3; i++)
		kfifo_put(&my_fifo, elements[i]);

	for (int i = 0; i < 3; i++) {
		processed_elements = kfifo_get(&my_fifo, &out_data);
		KUNIT_EXPECT_EQ(test, processed_elements, 1);
		KUNIT_EXPECT_EQ(test, out_data, elements[i]);
	}
}

static void kfifo_test_in_should_insert_multiple_elements(struct kunit *test)
{
	u8 in_buffer[] = { 11, 25, 65 };
	u8 out_data;
	int processed_elements;

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	kfifo_in(&my_fifo, in_buffer, 3);

	for (int i = 0; i < 3; i++) {
		processed_elements = kfifo_get(&my_fifo, &out_data);
		KUNIT_EXPECT_EQ(test, processed_elements, 1);
		KUNIT_EXPECT_EQ(test, out_data, in_buffer[i]);
	}
}

static void kfifo_test_out_should_pop_multiple_elements(struct kunit *test)
{
	u8 in_buffer[] = { 11, 25, 65 };
	u8 out_buffer[3];
	int copied_elements;

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	for (int i = 0; i < 3; i++)
		kfifo_put(&my_fifo, in_buffer[i]);

	copied_elements = kfifo_out(&my_fifo, out_buffer, 3);
	KUNIT_EXPECT_EQ(test, copied_elements, 3);

	for (int i = 0; i < 3; i++)
		KUNIT_EXPECT_EQ(test, out_buffer[i], in_buffer[i]);
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
}

static void kfifo_test_dec_init_should_define_an_empty_fifo(struct kunit *test)
{
	DECLARE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	INIT_KFIFO(my_fifo);

	// my_fifo is a struct with an inplace buffer
	KUNIT_EXPECT_FALSE(test, __is_kfifo_ptr(&my_fifo));

	KUNIT_EXPECT_TRUE(test, kfifo_initialized(&my_fifo));
}

static void kfifo_test_define_should_equal_declare_init(struct kunit *test)
{
	// declare a variable my_fifo of type struct kfifo of u8
	DECLARE_KFIFO(my_fifo1, u8, KFIFO_SIZE);
	// initialize the my_fifo variable
	INIT_KFIFO(my_fifo1);

	// DEFINE_KFIFO declares the variable with the initial value
	// essentially the same as calling DECLARE_KFIFO and INIT_KFIFO
	DEFINE_KFIFO(my_fifo2, u8, KFIFO_SIZE);

	// my_fifo1 and my_fifo2 have the same size
	KUNIT_EXPECT_EQ(test, sizeof(my_fifo1), sizeof(my_fifo2));
	KUNIT_EXPECT_EQ(test, kfifo_initialized(&my_fifo1),
			kfifo_initialized(&my_fifo2));
	KUNIT_EXPECT_EQ(test, kfifo_is_empty(&my_fifo1),
			kfifo_is_empty(&my_fifo2));
}

static void kfifo_test_alloc_should_initiliaze_a_ptr_fifo(struct kunit *test)
{
	int ret;
	DECLARE_KFIFO_PTR(my_fifo, u8);

	INIT_KFIFO(my_fifo);

	// kfifo_initialized returns false signaling the buffer pointer is NULL
	KUNIT_EXPECT_FALSE(test, kfifo_initialized(&my_fifo));

	// kfifo_alloc allocates the buffer
	ret = kfifo_alloc(&my_fifo, KFIFO_SIZE, GFP_KERNEL);
	KUNIT_EXPECT_EQ_MSG(test, ret, 0, "Memory allocation should succeed");
	KUNIT_EXPECT_TRUE(test, kfifo_initialized(&my_fifo));

	// kfifo_free frees the buffer
	kfifo_free(&my_fifo);
}

static void kfifo_test_peek_should_not_remove_elements(struct kunit *test)
{
	u8 out_data;
	int processed_elements;

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	// If the fifo is empty, peek returns 0
	processed_elements = kfifo_peek(&my_fifo, &out_data);
	KUNIT_EXPECT_EQ(test, processed_elements, 0);

	kfifo_put(&my_fifo, 3);
	kfifo_put(&my_fifo, 5);
	kfifo_put(&my_fifo, 11);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 3);

	processed_elements = kfifo_peek(&my_fifo, &out_data);
	KUNIT_EXPECT_EQ(test, processed_elements, 1);
	KUNIT_EXPECT_EQ(test, out_data, 3);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 3);

	// Using peek doesn't remove the element
	// so the read element and the fifo length
	// remains the same
	processed_elements = kfifo_peek(&my_fifo, &out_data);
	KUNIT_EXPECT_EQ(test, processed_elements, 1);
	KUNIT_EXPECT_EQ(test, out_data, 3);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 3);
}

static struct kunit_case kfifo_test_cases[] = {
	KUNIT_CASE(kfifo_test_reset_should_clear_the_fifo),
	KUNIT_CASE(kfifo_test_define_should_define_an_empty_fifo),
	KUNIT_CASE(kfifo_test_len_should_ret_n_of_stored_elements),
	KUNIT_CASE(kfifo_test_put_should_insert_and_get_should_pop),
	KUNIT_CASE(kfifo_test_in_should_insert_multiple_elements),
	KUNIT_CASE(kfifo_test_out_should_pop_multiple_elements),
	KUNIT_CASE(kfifo_test_dec_init_should_define_an_empty_fifo),
	KUNIT_CASE(kfifo_test_define_should_equal_declare_init),
	KUNIT_CASE(kfifo_test_alloc_should_initiliaze_a_ptr_fifo),
	KUNIT_CASE(kfifo_test_peek_should_not_remove_elements),
	{},
};

static struct kunit_suite kfifo_test_module = {
	.name = "kfifo",
	.test_cases = kfifo_test_cases,
};

kunit_test_suites(&kfifo_test_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Diego Vieira <diego.daniel.professional@gmail.com>");
MODULE_DESCRIPTION("KUnit test for the kernel FIFO");
