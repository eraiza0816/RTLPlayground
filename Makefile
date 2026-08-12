VERSION=0.2.26

# WebUI option: WEB=1 (default) to enable, WEB=0 to disable
WEB ?= 1

# CLI variant: FULL=1 enables the legacy EOS-mode CLI (mode system,
# '?'/help, Tab completion). Default (FULL=0) is the Lite flat CLI.
FULL ?= 0
IMAGESIZE = 524288
DEFAULT_CONFIG_LOCATION = 454656
CONFIG_LOCATION = 458752
HTML_LOCATION = 262144

ifeq ($(origin CC),default)
CC = sdcc
endif
CC_FLAGS = -mmcs51 -DNO_CHACHA_HELPERS -DNO_AEAD_TEST -I. -Ihttpd -Iuip
ASM ?= sdas8051
AFLAGS= -plosgff

SUBDIRS := tools
SUBDIRSCLEAN=$(addsuffix clean,$(SUBDIRS))

ifeq ($(MACHINE),)
	MACHINE:= $(shell grep "^\s*#define MACHINE_" machine.h | sed "s/^\s*#define MACHINE_//")
else
	CC_FLAGS += -DMACHINE_$(MACHINE)
endif

BUILDDIR = output/$(MACHINE)
VERSION_HEADER := version.h

GIT_VERSION := $(shell git rev-parse --short HEAD 2>/dev/null)
ifeq ($(GIT_VERSION),)
	# GitHub Actions: git rev-parse can fail in the runner environment;
	# fall back to the checked-out commit SHA, then to a placeholder so
	# the version string never has an empty component.
	GIT_VERSION := $(shell printf '%s' $${GITHUB_SHA} | cut -c1-7)
endif
ifeq ($(GIT_VERSION),)
	GIT_VERSION := unknown
endif
ifeq ($(shell git status --porcelain --untracked-files=no 2>/dev/null),)
else
	GIT_VERSION := $(GIT_VERSION)-dirty
endif

VERSION_EXTENSION = v$(VERSION)-$(GIT_VERSION)
ifeq ($(FULL),1)
	VERSION_EXTENSION := $(VERSION_EXTENSION)-full
else
	VERSION_EXTENSION := $(VERSION_EXTENSION)-lite
endif
FILENAME_EXTENSION = $(VERSION_EXTENSION)-$(MACHINE)

all: create_build_dir $(VERSION_HEADER) $(SUBDIRS) $(BUILDDIR)/rtlplayground-$(FILENAME_EXTENSION).bin

create_build_dir:
	mkdir -p "$(BUILDDIR)"
	mkdir -p "$(BUILDDIR)/uip"
	mkdir -p "$(BUILDDIR)/httpd"
	mkdir -p "$(BUILDDIR)/telnetd"
	mkdir -p "$(BUILDDIR)/crypto"

SRCS = rtlplayground.c crc16.c rtl837x_flash.c rtl837x_leds.c rtl837x_phy.c rtl837x_port.c cmd_parser.c html_data.c rtl837x_igmp.c
SRCS += rtl837x_stp.c rtl837x_pins.c dhcp.c machine.c cmd_editor.c rtl837x_bandwidth.c rtl837x_init.c
SRCS += rtl837x_storm.c rtl837x_qos.c rtl837x_acl.c
SRCS += uip/timer.c uip/uip.c uip/uip_arp.c uip/uiplib.c uip/uip-fw.c uip/uip-neighbor.c uip/uip-split.c udp_apps.c
SRCS += httpd/httpd.c httpd/page_impl.c httpd/api_status.c
SRCS += sfp_bitbang.c
SRCS += telnetd/telnetd.c
SRCS += cmd_commit.c
SRCS += cmd_xmodem.c
SRCS += ping.c
SRCS += lldp.c
SRCS += crypto/chacha20.c crypto/poly1305.c crypto/aead.c

ifeq ($(WEB),0)
	CC_FLAGS += -DNO_WEBUI
	SRCS := $(filter-out html_data.c, $(SRCS))
endif

ifeq ($(FULL),1)
	CC_FLAGS += -DFULL_CLI
	SRCS += cmd_mode.c cmd_help.c
endif

OBJS = ${SRCS:%.c=$(BUILDDIR)/%.rel}
DEPS := ${SRCS:%.c=$(BUILDDIR)/%.d}
HTML := $(shell find html -name '*.js' -or -name '*.html' -or -name '*.svg')

# Minified copy of the web UI sources, used as the fileadder input.
# The raw html/ sources stay untouched for development; the minified
# copy is a build artifact under output/.
HTML_MIN := output/html_min
.PHONY: html_min
html_min: $(HTML)
	rm -rf $(HTML_MIN)
	mkdir -p $(HTML_MIN)
	@for f in $(HTML); do python3 tools/minify.py $$f $(HTML_MIN)/$$(basename $$f) || exit 1; done

ifeq ($(WEB),1)
html_data.c html_data.h: $(HTML) tools/output/fileadder html_min
	tools/output/fileadder -a $(HTML_LOCATION) -s $(IMAGESIZE) -b BANK1 -z -d $(HTML_MIN) -p html_data
# httpd.c includes html_data.h; declare it explicitly so a parallel
# build (-j) does not compile httpd before the header is generated.
$(BUILDDIR)/httpd/httpd.rel: html_data.h
endif

$(VERSION_HEADER):
	@echo "GIT_VERSION=$(GIT_VERSION)"
	@echo "#ifndef VERSION_H" > $(VERSION_HEADER)
	@echo "#define VERSION_H" >> $(VERSION_HEADER)
	@echo "#define VERSION_SW \"$(VERSION_EXTENSION)\"" >> $(VERSION_HEADER)
	@echo "#define BUILD_DATE \"$(shell date +"%Y-%m-%d %H:%M:%S")\"" >> $(VERSION_HEADER)
	@echo "#endif" >> $(VERSION_HEADER)

ifeq ($(WEB),1)
httpd: html_data.h
endif

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	-rm -f html_data.c html_data.h $(VERSION_HEADER)
	-rm -rf $(HTML_MIN)
	-if [ -d $(BUILDDIR) ]; then find $(BUILDDIR) -type f ! -name "*.bin" -delete; fi

distclean:
	-rm -f html_data.c html_data.h $(VERSION_HEADER)
	-rm -rf $(BUILDDIR)

TELNET_FLAGS = $(CC_FLAGS) --stack-auto

$(BUILDDIR)/telnetd/%.rel: telnetd/%.c
	$(CC) -MMD $(TELNET_FLAGS) -DMACHINE_$(MACHINE) -o $@ -c $<

$(BUILDDIR)/crypto/%.rel: crypto/%.c
	$(CC) -MMD $(CC_FLAGS) -o $@ -c $<

$(BUILDDIR)/%.rel: %.c
	$(CC) -MMD $(CC_FLAGS) -o $@ -c $<

$(BUILDDIR)/%.rel: %.asm
	${ASM} ${AFLAGS} -o $@ $<
#	mv -f $(addprefix $(basename $^), .lst .rel .sym) .

$(BUILDDIR)/rtlplayground.ihx: $(OBJS) $(BUILDDIR)/crtstart.rel $(BUILDDIR)/rtlplayground_mem.rel $(BUILDDIR)/rtlplayground_util.rel
	$(CC) $(CC_FLAGS) -Wl-bHOME=0x00000 -Wl-bBANK1=0x14000 -Wl-bBANK2=0x24000 -Wl-bBANK3=0x34000 -Wl-bBANK4=0x44000 -Wl-r -o $@ $^

$(BUILDDIR)/rtlplayground.img: $(BUILDDIR)/rtlplayground.ihx
	objcopy --input-target=ihex -O binary $< $@

$(BUILDDIR)/rtlplayground-$(FILENAME_EXTENSION).bin: $(BUILDDIR)/rtlplayground.img
	if [ -e $@ ]; then rm $@; fi
	tools/output/imagebuilder -i $^ $@
	tools/output/fileadder -a $(DEFAULT_CONFIG_LOCATION) -s $(IMAGESIZE) -d config.txt $@
	tools/output/fileadder -a $(CONFIG_LOCATION) -s $(IMAGESIZE) -d config.txt $@
ifeq ($(WEB),1)
	tools/output/fileadder -a $(HTML_LOCATION) -s $(IMAGESIZE) -z -d $(HTML_MIN) -p html_data -b BANK1 $@
endif
	tools/output/crc_calculator -u $@
	ln -sf $(MACHINE)/rtlplayground-$(FILENAME_EXTENSION).bin output/rtlplayground.bin

.PHONY: clean all $(SUBDIRS) $(VERSION_HEADER)

.PHONY:
# Compile machine.c for every machine. Written for /bin/sh (dash), which
# does not support `set -o pipefail` (the CI runs on debian-slim where
# /bin/sh is dash), so errors are handled with an explicit exit.
machine_check:
	@mkdir -p $(BUILDDIR)/tmp
	@for MACHINE in `grep -e ' MACHINE_' machine.c | sed -e 's%^.* MACHINE_%%' -e 's%[ ]*//.*$$%%' | sort -u`; \
	do \
	echo "Checking $${MACHINE}"; \
	$(CC) $(CC_FLAGS) -DMACHINE_$${MACHINE} -MMD -o $(BUILDDIR)/tmp/machine_check -c machine.c || exit 1; \
	done
	@rm -rf $(BUILDDIR)/tmp

-include $(DEPS)
