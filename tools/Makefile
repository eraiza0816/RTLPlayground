CC = gcc
CCFLAGS = -Wall -o
BUILDDIR = output

all: create_build_dir $(BUILDDIR)/injector $(BUILDDIR)/fileadder $(BUILDDIR)/httpd_sim\
	$(BUILDDIR)/crc_calculator $(BUILDDIR)/imagebuilder $(BUILDDIR)/cli_sim $(BUILDDIR)/aead_test

create_build_dir:
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

$(BUILDDIR)/injector: injector.c
	gcc $^ $(CCFLAGS) $@

$(BUILDDIR)/fileadder: fileadder.c
	gcc $^ $(CCFLAGS) $@ -lz

$(BUILDDIR)/crc_calculator: crc_calculator.c
	gcc $^ $(CCFLAGS) $@

$(BUILDDIR)/httpd_sim: httpd_sim.c httpd_sim.h ../crypto/aead.c ../crypto/chacha20.c ../crypto/poly1305.c rtl837x_common.h
	gcc -O2 -Wall -Wno-unknown-pragmas -I. -include rtl837x_common.h $< ../crypto/aead.c ../crypto/chacha20.c ../crypto/poly1305.c $(CCFLAGS) $@ -I/usr/include/json-c -ljson-c

$(BUILDDIR)/cli_sim: cli_sim.c httpd_sim.h
	gcc -O2 -Wall -I. $< $(CCFLAGS) $@

$(BUILDDIR)/aead_test: aead_test.c ../crypto/aead.c ../crypto/chacha20.c ../crypto/poly1305.c rtl837x_common.h
	gcc -O2 -Wall -Wno-unknown-pragmas -I. -include rtl837x_common.h $< ../crypto/aead.c ../crypto/chacha20.c ../crypto/poly1305.c $(CCFLAGS) $@

$(BUILDDIR)/imagebuilder: imagebuilder.c
	gcc $^ $(CCFLAGS) $@

$(BUILDDIR)/rtlplayground_exporter: rtlplayground_exporter/main.go rtlplayground_exporter/go.mod
	cd rtlplayground_exporter && go build -o ../$@ .
