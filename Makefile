TARGET ?= linux64

ifeq ($(TARGET),windows)
	LIBGBD_STATIC := build/libgbd.lib
	LIBGBD_SHARED := build/libgbd.dll
	TARGET_BINARY := build/gbd.exe
	CC := x86_64-w64-mingw32-gcc
	DEFS := -D WINDOWS
	ICONV_PREFIX := $(CURDIR)/libiconv/windows
	ICONV_CFG := build_libtool_need_lc=no archive_cmds_need_lc=no
else
	LIBGBD_STATIC := build/libgbd.a
	LIBGBD_SHARED := build/libgbd.so
	TARGET_BINARY := build/gbd
	CC := gcc
	DEFS := -D __LINUX__
	ICONV_PREFIX := $(CURDIR)/libiconv/linux
	ICONV_CFG :=
endif

export

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

.PHONY: all clean libiconv
.DEFAULT_GOAL := all

all: $(LIBGBD_STATIC) $(LIBGBD_SHARED) $(TARGET_BINARY)

clean:
	$(RM) -rf build

# libgbd for statically linking
$(LIBGBD_STATIC): $(O_FILES_LIBGBD)
	$(AR) rcs $@ $^

# libgbd for dynamically linking
$(LIBGBD_SHARED): $(O_FILES_LIBGBD) $(ICONV) $(LIBGFXD)
	$(CC) -shared $^ -o $@

# gbd front-end
$(TARGET_BINARY): $(O_FILES_GBD) $(ICONV) $(LIBGFXD) $(LIBGBD_STATIC)
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

libiconv:
ifeq (,$(wildcard $(ICONV)))
	mkdir -p build/libiconv/build
	mkdir -p $(ICONV_PREFIX)
 ifeq (,$(wildcard build/libiconv/build/Makefile))
	cd build/libiconv && wget https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.16.tar.gz && tar -xvzf libiconv-1.16.tar.gz
	cd build/libiconv/build && ../libiconv-1.16/configure --enable-static --prefix=$(ICONV_PREFIX) CC=$(CC) $(ICONV_CFG)
 endif
	$(MAKE) -C build/libiconv/build install-lib
	$(RM) -rf build/libiconv
else
	$(info libiconv.a is already present)
endif
