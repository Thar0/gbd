TARGET ?= linux64

ICONV_VERSION := libiconv-1.16

ifeq ($(TARGET),windows)
	LIBGBD_STATIC := build/libgbd.lib
	LIBGBD_SHARED := build/libgbd.dll
	TARGET_BINARY := build/gbd.exe
	CC := x86_64-w64-mingw32-gcc
	DEFS := -D WINDOWS
	ICONV_PREFIX := libiconv/windows
	ICONV_CFG := build_libtool_need_lc=no archive_cmds_need_lc=no
else
	LIBGBD_STATIC := build/libgbd.a
	LIBGBD_SHARED := build/libgbd.so
	TARGET_BINARY := build/gbd
	CC := gcc
	DEFS := -D __LINUX__
	ICONV_PREFIX := libiconv/linux
	ICONV_CFG :=
endif

export

CLANG_FORMAT := clang-format-14
FORMAT_ARGS := -i -style=file

ICONV := $(ICONV_PREFIX)/lib/libiconv.a
LIBGFXD := build/libgfxd.a
CFLAGS := -Og -g3 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -ffunction-sections -fdata-sections

# libgbd (the implementation for gbd)
SRC_DIRS_LIBGBD := $(shell find src/libgbd -type d)
C_FILES_LIBGBD := $(foreach dir,$(SRC_DIRS_LIBGBD),$(wildcard $(dir)/*.c))
O_FILES_LIBGBD := $(C_FILES_LIBGBD:%.c=build/%.o)

# gbd front-end (command line utility)
SRC_DIRS_GBD := $(shell find src/gbd -type d)
C_FILES_GBD := $(foreach dir,$(SRC_DIRS_GBD),$(wildcard $(dir)/*.c))
O_FILES_GBD := $(C_FILES_GBD:%.c=build/%.o)

DEP_FILES := $(O_FILES_LIBGBD:.o=.d) $(O_FILES_GBD:.o=.d)

$(shell mkdir -p $(SRC_DIRS_LIBGBD:%=build/%) $(SRC_DIRS_GBD:%=build/%))

.PHONY: all clean format libiconv
.DEFAULT_GOAL := all

all: $(LIBGBD_STATIC) $(LIBGBD_SHARED) $(TARGET_BINARY)

clean:
	$(RM) -rf build

distclean: clean
	$(RM) -r $(ICONV_PREFIX)

format:
	$(CLANG_FORMAT) $(FORMAT_ARGS) $(shell find src -type f -name "*.[ch]")

$(O_FILES_LIBGBD): $(ICONV)

# libgbd for statically linking
$(LIBGBD_STATIC): $(O_FILES_LIBGBD)
	$(AR) rcs $@ $^

# libgbd for dynamically linking
$(LIBGBD_SHARED): $(O_FILES_LIBGBD) $(LIBGFXD) $(ICONV)
	$(CC) -shared $^ -o $@

# gbd front-end
$(TARGET_BINARY): $(O_FILES_GBD) $(LIBGBD_STATIC) $(LIBGFXD) $(ICONV)
	$(CC) $^ -o $@

# -fPIC is required to make a shared library,
# and doesn't matter for statically linking.
# It seems we get lucky about libiconv also being compiled this way?

$(LIBGFXD): $(shell find libgfxd -type f -name "*.[ch]")
	$(MAKE) -C libgfxd CFLAGS='-O2 -fPIC'
	mv libgfxd/libgfxd.a $@

build/src/libgbd/%.o: CFLAGS += -fPIC

build/src/%.o: src/%.c
	$(CC) $(DEFS) $(CFLAGS) -MMD -I. -Iinclude -c $< -o $@

-include $(DEP_FILES)

libiconv: $(ICONV)
$(ICONV):
  ifeq (,$(wildcard libiconv/get/$(ICONV_VERSION)/configure))
    # Fetch iconv release if not present
	mkdir -p libiconv/get
	cd libiconv/get && wget https://ftp.gnu.org/pub/gnu/libiconv/$(ICONV_VERSION).tar.gz && tar -xvzf $(ICONV_VERSION).tar.gz
  endif
  ifeq (,$(wildcard $(ICONV_PREFIX)/build/Makefile))
    # Configure iconv if not already configured (platform-dependent)
	mkdir -p $(ICONV_PREFIX)/build
	cd $(ICONV_PREFIX)/build && ../../get/$(ICONV_VERSION)/configure --enable-static --prefix=$(CURDIR)/$(ICONV_PREFIX) CC=$(CC) $(ICONV_CFG)
  endif
  # Build & install to $(ICONV_PREFIX)/lib and $(ICONV_PREFIX)/include
	$(MAKE) -C $(ICONV_PREFIX)/build install-lib
