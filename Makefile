# -----------------------------------------------------------------
# Compilers and Compilation Flags Configuration
# -----------------------------------------------------------------
CC         ?= gcc
CFLAGS     ?= -O2 -Wall -Wextra
LDFLAGS    ?= 

# Fetch linking flags and includes automatically using pkg-config
PKG_CFLAGS  := $(shell pkg-config --cflags xft fontconfig xcb xcb-util xcb-keysyms x11 x11-xcb xcb-imdkit rime)
PKG_LIBS    := $(shell pkg-config --libs xft fontconfig xcb xcb-util xcb-keysyms x11 x11-xcb xcb-imdkit rime)

# -----------------------------------------------------------------
# Installation Paths Configuration (Crucial for MacPorts DestDir)
# -----------------------------------------------------------------
# PREFIX is usually set to /opt/local by MacPorts
PREFIX     ?= /usr/local
BINDIR     := $(PREFIX)/bin

# DESTDIR is injected by MacPorts staging phase sandbox
DESTDIR    ?=

UTHASH_CFLAGS := -I$(PREFIX)/include/uthash -I$(PREFIX)/include

TOTAL_CFLAGS := $(CFLAGS) $(PKG_CFLAGS) $(UTHASH_CFLAGS) -Wno-unused-parameter
TOTAL_LIBS   := $(LDFLAGS) $(PKG_LIBS)

TARGET     := xim_rime
SRC        := $(wildcard *.c)
OBJ        := $(SRC:.c=.o)

# -----------------------------------------------------------------
# Build Targets Rules Layout
# -----------------------------------------------------------------
.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(TOTAL_LIBS)

%.o: %.c
	$(CC) $(TOTAL_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

# -----------------------------------------------------------------
# 🌟 The Missing Install Target Block (Fully compatible with MacPorts)
# -----------------------------------------------------------------
install: all
	@echo "Installing $(TARGET) into staging sandbox..."
	# Use standard BSD/GNU install tool to create binary directories cleanly
	install -d $(DESTDIR)$(BINDIR)
	# Copy target executable binary file and enforce standard 755 execution permissions
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "Installation stage successfully completed."

uninstall:
	@echo "Removing $(TARGET) from system paths..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "Uninstall completed."

