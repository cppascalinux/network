CC=gcc
CFLAGS=-O2 -lpcap -Werror -Wall -fsanitize=address
SRC_DIR=./src
BIN_DIR=./bin
TARGET=client

SRCS=$(shell find $(SRC_DIR) -name '*.c')
HDRS=$(shell find $(SRC_DIR) -name '*.h')
$(BIN_DIR)/$(TARGET):$(SRCS) $(HDRS)
	mkdir -p $(BIN_DIR)
	$(CC) $(SRCS) -o $@ $(CFLAGS)

clean:
	rm -r $(BIN_DIR)