CFLAGS = -g -O0 -Wall -Wno-unused-function

.PHONY: all clean build

all: build
	./test_cmd_parser

build: test_cmd_parser

test_cmd_parser: test_cmd_parser.c
	gcc $(CFLAGS) -o $@ $^

clean:
	rm -f test_cmd_parser
