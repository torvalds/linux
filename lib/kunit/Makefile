obj-$(CONFIG_KUNIT) +=			kunit.o

kunit-objs +=				test.o \
					resource.o \
					user_alloc.o \
					static_stub.o \
					string-stream.o \
					assert.o \
					try-catch.o \
					executor.o \
					attributes.o \
					device.o

ifeq ($(CONFIG_KUNIT_DEBUGFS),y)
kunit-objs +=				debugfs.o
endif

# KUnit 'hooks' are built-in even when KUnit is built as a module.
obj-y +=				hooks.o

obj-$(CONFIG_KUNIT_TEST) +=		kunit-test.o

# string-stream-test compiles built-in only.
ifeq ($(CONFIG_KUNIT_TEST),y)
obj-$(CONFIG_KUNIT_TEST) +=		string-stream-test.o
obj-$(CONFIG_KUNIT_TEST) +=		assert_test.o
endif

obj-$(CONFIG_KUNIT_EXAMPLE_TEST) +=	kunit-example-test.o
