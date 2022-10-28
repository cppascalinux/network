CC=gcc
CFLAGS=-O2 -lpcap -Werror -Wall -fsanitize=address -Wl,--wrap,socket -Wl,--wrap,bind -Wl,--wrap,listen -Wl,--wrap,connect -Wl,--wrap,accept -Wl,--wrap,read -Wl,--wrap,write -Wl,--wrap,close -Wl,--wrap,getaddrinfo -Wl,--wrap,freeaddrinfo -Wl,--wrap,setsockopt
SRC_DIR=./src
BIN_DIR=./bin
CKPT_DIR=./checkpoints
NAMES=echo_client echo_server perf_client perf_server client file_client file_server
TARGETS=$(foreach wrd,$(NAMES),$(BIN_DIR)/$(wrd))
SRCS=$(shell find $(SRC_DIR) -name '*.c')
HDRS=$(shell find $(SRC_DIR) -name '*.h')

all: $(TARGETS)

$(BIN_DIR)/% : $(CKPT_DIR)/%.c $(CKPT_DIR)/unp.c $(SRCS) $(HDRS)
	mkdir -p $(BIN_DIR)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -r $(BIN_DIR)