obj-$(CONFIG_KUNIT) +=			kunit.o

kunit-objs +=				test.o \
					string-stream.o \
					assert.o \
					try-catch.o

obj-$(CONFIG_KUNIT_TEST) +=		kunit-test.o

# string-stream-test compiles built-in only.
ifeq ($(CONFIG_KUNIT_TEST),y)
obj-$(CONFIG_KUNIT_TEST) +=		string-stream-test.o
endif

obj-$(CONFIG_KUNIT_EXAMPLE_TEST) +=	kunit-example-test.o
