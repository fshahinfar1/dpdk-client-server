PKGCONF = pkg-config
CC = gcc
COptFlag = -O3 -g
CWarningFlags = -Wall -Wextra -Wcast-align -Wno-return-type
CFLAGS += \
					-iquote ./include -D_GNU_SOURCE \
					$(COptFlag) \
					$(CWarningFlags) \
					$(shell $(PKGCONF) --cflags libdpdk)


LDFLAGS += $(shell $(PKGCONF) --static --libs libdpdk)
# Check if we need MLX5 driver or not
HAS_MLX = $(shell lspci -v | grep mlx5)
ifneq ($(HAS_MLX),)
LDFLAGS += -lmlx5
endif

# $(info CFLAGS:)
# $(info  $(CFLAGS))
# $(info ------------------------------)
# $(info LDFLAGS:)
# $(info $(LDFLAGS))
# $(info ------------------------------)

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(dir $(MKFILE_PATH))
OUTPUT_DIR  := ./build

# Extra libraries we need
LIBS = -lpcap -lnuma \
       -lpthread -ldl \
       -lm

# binary name
BIN =  $(OUTPUT_DIR)/app
BIN += $(OUTPUT_DIR)/client_udp_number
BIN += $(OUTPUT_DIR)/client_tcp_timestamp
BIN += $(OUTPUT_DIR)/client_tcp_number

# all source are stored in SRCS-y
SRCS := main.c
SRCS += config.c
SRCS += server.c
SRCS += client.c
SRCS += latency_client.c
SRCS += utils/exp.c
SRCS += utils/percentile.c
SRCS += utils/flow_rules.c
SRCS += utils/arp.c
SRCS += utils/zipf.c
SRCS += utils/exponential.c
HEADER_FILES = $(shell find . -iname "*.h")

OBJS = $(patsubst %.c, %.o, $(SRCS))
_BUILT_OBJECTS = $(patsubst %.o, $(OUTPUT_DIR)/%.o, $(OBJS))

.PHONY: default clean

default: $(OUTPUT_DIR) $(BIN)

# Make sure that output directory exists
$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)
	mkdir -p $(OUTPUT_DIR)/utils/

clean:
	rm -rf build/

$(OUTPUT_DIR)/app: $(SRCS) $(HEADER_FILES) Makefile
	@echo Compiling $@
	@$(CC) -o $@ $(CFLAGS) $(SRCS) $(LDFLAGS) $(LIBS)

$(OUTPUT_DIR)/client_udp_number: $(SRCS) $(HEADER_FILES) Makefile
	@echo Compiling $@
	@$(CC) -o $@ $(CFLAGS) -D_PAYLOAD_NUMBER $(SRCS) $(LDFLAGS) $(LIBS)

$(OUTPUT_DIR)/client_tcp_timestamp: $(SRCS) $(HEADER_FILES) Makefile
	@echo Compiling $@
	@$(CC) -o $@ $(CFLAGS) -D_USE_TCP $(SRCS) $(LDFLAGS) $(LIBS)

$(OUTPUT_DIR)/client_tcp_number: $(SRCS) $(HEADER_FILES) Makefile
	@echo Compiling $@
	@$(CC) -o $@ $(CFLAGS) -D_USE_TCP -D_PAYLOAD_NUMBER $(SRCS) $(LDFLAGS) $(LIBS)
