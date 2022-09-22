CC=gcc
CFLAGS=-O2 -lpcap -Werror
SRC_DIR=./src
BIN_DIR=./bin
TARGET=client

SRCS=$(shell find $(SRC_DIR) -name '*.c')
$(BIN_DIR)/$(TARGET):$(SRCS)
	mkdir -p $(BIN_DIR)
	$(CC) $(SRCS) -o $@ $(CFLAGS)

clean:
	rm -r $(BIN_DIR)