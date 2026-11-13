# Makefile para nbping - cross-compile NetModule NB2700 (PowerPC)
#
# Toolchain: netbox-ppc (GCC 4.4.5 + uClibc 0.9.31)
# Ubicacion: sdk/toolchain-ppc-4.4.5-x86_64/ dentro del proyecto

SDK_DIR ?= $(CURDIR)/sdk/toolchain-ppc-4.4.5-x86_64
CROSS   ?= $(SDK_DIR)/bin/powerpc-openwrt-linux
CC       = $(CROSS)-gcc
STRIP    = $(CROSS)-strip

CFLAGS   = -static -Os -Wall -Wextra
TARGET   = nbping

all: $(TARGET)

$(TARGET): nbping.c
	$(CC) $(CFLAGS) -o $@ nbping.c
	$(STRIP) $@
	@echo "Compilado: $$(file $@)"

clean:
	rm -f $(TARGET)

.PHONY: all clean