TARGET ?= linux64

ifeq ($(TARGET),windows)
	TARGET_BINARY := build/gbd.exe
	CC := x86_64-w64-mingw32-gcc
	DEFS := -D WINDOWS
	ICONV_PREFIX := $(CURDIR)/libiconv/windows
	ICONV_CFG := build_libtool_need_lc=no archive_cmds_need_lc=no
else
	TARGET_BINARY := build/gbd
	CC := gcc
	DEFS := -D __LINUX__
	ICONV_PREFIX := $(CURDIR)/libiconv/linux
	ICONV_CFG :=
endif

export

ICONV := $(ICONV_PREFIX)/lib/libiconv.a
CFLAGS := -Og -g3 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -ffunction-sections -fdata-sections

SRC_DIRS := $(shell find src -type d)
C_FILES := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
O_FILES := $(foreach f,$(C_FILES:.c=.o),build/$f)
DEP_FILES := $(O_FILES:.o=.d)

$(shell mkdir -p $(foreach dir,$(SRC_DIRS),build/$(dir)))

.PHONY: all clean libiconv
.DEFAULT_GOAL := all

all: $(TARGET_BINARY)

clean:
	$(RM) -rf build

$(TARGET_BINARY): $(O_FILES) build/libgfxd.a
	$(CC) $(DEFS) $^ $(ICONV) -o $@

build/libgfxd.a: $(shell find libgfxd -type f -name "*.[ch]")
	$(MAKE) -C libgfxd && mv libgfxd/libgfxd.a build

build/src/%.o: src/%.c
	$(CC) $(DEFS) $(CFLAGS) -MMD -I. -Isrc -c $< -o $@

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
