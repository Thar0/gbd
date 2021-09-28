TARGET ?= linux64

ifeq ($(TARGET),windows)
	TARGET_BINARY := build/gbd.exe
	CC := x86_64-w64-mingw32-gcc
	DEFS := -D WINDOWS
	ICONV_PREFIX := $(CURDIR)/libiconv-win
	ICONV_CFG := build_libtool_need_lc=no archive_cmds_need_lc=no
	ICONV := $(ICONV_PREFIX)/lib/libiconv.a
else
	TARGET_BINARY := build/gbd
	CC := gcc
	DEFS := -D __LINUX__
	ICONV_PREFIX := /usr/local
	ICONV_CFG :=
	ICONV := $(ICONV_PREFIX)/lib/libiconv.a
endif

export

OPTFLAGS := -O2 -ffunction-sections -fdata-sections

SRC_DIRS := $(shell find src -type d)
C_FILES := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
O_FILES := $(foreach f,$(C_FILES:.c=.o),build/$f)

$(shell mkdir -p $(foreach dir,$(SRC_DIRS),build/$(dir)))

.PHONY: all clean
.DEFAULT_GOAL := all

all: $(TARGET_BINARY)

clean:
	$(RM) -rf build

$(TARGET_BINARY): $(O_FILES) build/libgfxd.a
	$(CC) $(DEFS) $^ $(ICONV) -o $@

build/libgfxd.a:
	$(MAKE) -C libgfxd && mv libgfxd/libgfxd.a build

build/src/%.o: src/%.c
	$(CC) $(DEFS) $(OPTFLAGS) -I. -Isrc -c $< -o $@

libiconv:
ifeq (,$(wildcard $(ICONV)))
	mkdir -p build/libiconv/build
ifeq ($(TARGET),windows)
	mkdir -p $(ICONV_PREFIX)
endif
ifeq (,$(wildcard build/libiconv/build/Makefile))
	cd build/libiconv && wget https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.16.tar.gz
	cd build/libiconv && tar -xvzf libiconv-1.16.tar.gz
	cd build/libiconv/build && ../libiconv-1.16/configure --enable-static --prefix=$(ICONV_PREFIX) CC=$(CC) $(ICONV_CFG)
endif
	$(MAKE) -C build/libiconv/build install-lib
else
	$(info libiconv.a is already present)
endif
