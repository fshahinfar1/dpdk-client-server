PKGCONF = pkg-config
CC = gcc
COptFlag = -O3 -g
CWarningFlags = -Werror -Wall -Wextra -Wcast-align -Wno-return-type
CFLAGS += \
	  -iquote ./include -D_GNU_SOURCE \
	  $(COptFlag) \
	  $(CWarningFlags) \
	  $(shell $(PKGCONF) --cflags libdpdk)

LDFLAGS += $(shell $(PKGCONF) --libs libdpdk)

# $(info CFLAGS:)
# $(info  $(CFLAGS))
# $(info ------------------------------)
# $(info LDFLAGS:)
# $(info $(LDFLAGS))
# $(info ------------------------------)

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(dir $(MKFILE_PATH))
OUTPUT_DIR  := ./build/

# Extra libraries we need
LIBS = -lpcap -lnuma \
       -lpthread -ldl \
       -lm

# binary name
APP = $(OUTPUT_DIR)/app

# all source are stored in SRCS-y
SRCS := main.c
SRCS += server.c
SRCS += client.c
SRCS += utils/exp.c
SRCS += utils/percentile.c
SRCS += utils/flow_rules.c
SRCS += utils/arp.c
SRCS += utils/zipf.c
SRCS += utils/exponential.c

OBJS = $(patsubst %.c, %.o, $(SRCS))
_BUILT_OBJECTS = $(patsubst %.o, $(OUTPUT_DIR)/%.o, $(OBJS))

.PHONY: default clean

default: $(OUTPUT_DIR) $(APP)

# How to generate an object file
%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

# Stating what objects we need (fall backs to %.o rule)
$(_BUILT_OBJECTS): $(SRCS)

# Actually building the app
$(APP): $(_BUILT_OBJECTS) Makefile
	$(CC) -o $@ $(_BUILT_OBJECTS) $(LDFLAGS) $(LIBS)

# Make sure that output directory exists
$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)
	mkdir -p $(OUTPUT_DIR)/utils/

clean:
	rm -rf build/
