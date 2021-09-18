
CC := gcc
OPTFLAGS := -O2 -ffunction-sections -fdata-sections

SRC_DIRS := $(shell find src -type d)
C_FILES := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
O_FILES := $(foreach f,$(C_FILES:.c=.o),build/$f)

$(shell mkdir -p $(foreach dir,$(SRC_DIRS),build/$(dir)))

.PHONY: all clean
.DEFAULT_GOAL := all

all: build/gbidbg

clean:
	$(RM) -rf build

build/gbidbg: $(O_FILES) build/libgfxd.a
	$(CC) $^ /usr/local/lib/libiconv.a -o $@

build/libgfxd.a:
	$(MAKE) -C libgfxd && mv libgfxd/libgfxd.a build

build/src/%.o: src/%.c
	$(CC) $(OPTFLAGS) -I. -Isrc -Iinclude -c $< -o $@

libiconv:
ifeq (,$(wildcard /usr/local/lib/libiconv.a))
	mkdir -p build/libiconv/build
ifeq (,$(wildcard build/libiconv/build/Makefile))
	cd build/libiconv && wget https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.16.tar.gz
	cd build/libiconv && tar -xvzf libiconv-1.16.tar.gz
	cd build/libiconv/build && ../libiconv-1.16/configure --enable-static --prefix=/usr/local/
endif
	$(MAKE) -C build/libiconv/build install-lib
else
	$(info libiconv.a is already installed)
endif
