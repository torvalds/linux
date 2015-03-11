define RUN_TESTS
	@for TEST in $(TEST_PROGS); do \
		(./$$TEST && echo "selftests: $$TEST [PASS]") || echo "selftests: $$TEST [FAIL]"; \
	done;
endef

run_tests: all
	$(RUN_TESTS)

.PHONY: run_tests all clean
