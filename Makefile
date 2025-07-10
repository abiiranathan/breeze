# Detect operating system
ifeq ($(OS),Windows_NT)
    # Windows settings
    LIB_EXT = .dll
    LIB_PREFIX = 
    STATIC_EXT = .lib
    EXE_EXT = .exe
    RM = del /Q
    MKDIR = mkdir
    INSTALL = copy
    INSTALL_LIB_DIR = $(PREFIX)/lib
    INSTALL_BIN_DIR = $(PREFIX)/bin
else
    # Unix-like settings
    LIB_EXT = .so
    LIB_PREFIX = lib
    STATIC_EXT = .a
    EXE_EXT =
    RM = rm -f
    MKDIR = mkdir -p
    INSTALL = install
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        LIB_EXT = .dylib
    endif
    INSTALL_LIB_DIR = $(PREFIX)/lib
    INSTALL_BIN_DIR = $(PREFIX)/bin
endif

# Compiler and flags
CC = clang
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_GNU_SOURCE
LDFLAGS = -L. -lbreeze

# Source files
LIB_SRC = breeze.c
LIB_OBJ = $(LIB_SRC:.c=.o)
LIB_NAME = $(LIB_PREFIX)breeze

EXE_SRC = main.c
EXE_OBJ = $(EXE_SRC:.c=.o)
EXE_NAME = breeze$(EXE_EXT)

# Installation paths
PREFIX ?= /usr/local
INSTALL_INCLUDE_DIR = $(PREFIX)/include/breeze

.PHONY: all static shared clean install install-static install-shared uninstall

all: static $(EXE_NAME)

# Build static library
static: $(LIB_NAME)$(STATIC_EXT)

$(LIB_NAME)$(STATIC_EXT): $(LIB_OBJ)
ifeq ($(OS),Windows_NT)
	lib /out:$@ $^
else
	ar rcs $@ $^
endif

# Build shared library
shared: $(LIB_NAME)$(LIB_EXT)

$(LIB_NAME)$(LIB_EXT): $(LIB_OBJ)
ifeq ($(OS),Windows_NT)
	$(CC) -shared -o $@ $^
else
	$(CC) -shared -o $@ $^
endif

# Build executable
$(EXE_NAME): $(EXE_OBJ) $(LIB_NAME)$(STATIC_EXT)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Installation
install: install-static

install-static: static
	$(MKDIR) "$(INSTALL_LIB_DIR)"
	$(MKDIR) "$(INSTALL_INCLUDE_DIR)"
	$(MKDIR) "$(INSTALL_BIN_DIR)"
ifeq ($(OS),Windows_NT)
	$(INSTALL) $(LIB_NAME)$(STATIC_EXT) "$(INSTALL_LIB_DIR)"
	$(INSTALL) breeze.h "$(INSTALL_INCLUDE_DIR)"
	$(INSTALL) $(EXE_NAME) "$(INSTALL_BIN_DIR)"
else
	$(INSTALL) -m 644 $(LIB_NAME)$(STATIC_EXT) "$(INSTALL_LIB_DIR)"
	$(INSTALL) -m 644 breeze.h "$(INSTALL_INCLUDE_DIR)"
	$(INSTALL) -m 755 $(EXE_NAME) "$(INSTALL_BIN_DIR)"
endif

install-shared: shared
	$(MKDIR) "$(INSTALL_LIB_DIR)"
ifeq ($(OS),Windows_NT)
	$(INSTALL) $(LIB_NAME)$(LIB_EXT) "$(INSTALL_LIB_DIR)"
else
	$(INSTALL) -m 755 $(LIB_NAME)$(LIB_EXT) "$(INSTALL_LIB_DIR)"
endif

# Uninstall
uninstall:
	$(RM) "$(INSTALL_LIB_DIR)/$(LIB_NAME).*"
	$(RM) "$(INSTALL_BIN_DIR)/$(EXE_NAME)"
ifeq ($(OS),Windows_NT)
	$(RM) /S /Q "$(INSTALL_INCLUDE_DIR)"
else
	rm -rf "$(INSTALL_INCLUDE_DIR)"
endif

clean:
	$(RM) $(LIB_OBJ) $(EXE_OBJ) $(LIB_NAME).* $(EXE_NAME)
