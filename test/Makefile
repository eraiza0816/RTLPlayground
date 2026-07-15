CFLAGS = -g -O0 -Wall -Wno-unused-function -Wno-unknown-pragmas

TEST_TARGETS = test_cmd_parser test_cmd_help test_xmodem_crc test_noweb

.PHONY: all clean build $(TEST_TARGETS)

all: build
	@failed=0; total=0; \
	for t in $(TEST_TARGETS); do \
		echo "--- $$t ---"; \
		./$$t; \
		status=$$?; \
		total=$$((total + 1)); \
		if [ $$status -ne 0 ]; then failed=$$((failed + 1)); fi; \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
		echo "All $$total test(s) passed."; \
	else \
		echo "$$failed / $$total test(s) failed."; \
		exit 1; \
	fi

build: $(TEST_TARGETS)

test_cmd_parser: test_cmd_parser.c
	gcc $(CFLAGS) -o $@ $^

test_cmd_help: test_cmd_help.c
	gcc $(CFLAGS) -o $@ $^

test_xmodem_crc: test_xmodem_crc.c
	gcc $(CFLAGS) -o $@ $^

test_noweb: test_noweb.c
	gcc $(CFLAGS) -o $@ $^

clean:
	rm -f $(TEST_TARGETS)
