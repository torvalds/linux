#ifndef _RUN_TEST_H_
#define _RUN_TEST_H_

/* functions exported by the framework */
void basic_test(int condition);

/* function exported by the test */
void init_world(void);
void cleanup_world(void);
size_t get_num_tests(void);

/* test function prototype */
typedef void (test_f)(void);

struct run_test_t {
	test_f *function;		/* test/evaluation function */
	char *description;		/* test description */
	size_t points;			/* points for each test */
};

/* Use test_index to pass through test_array. */
extern struct run_test_t test_array[];
extern size_t max_points;

#endif /* _RUN_TEST_H_ */
