# Adjusted Makefile to include new sources and shsrv integration
PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
	include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
	$(error PS5_PAYLOAD_SDK is undefined)
endif

CFLAGS += -Wall -Werror -Iinclude
SRCS = src/sshsvr.c src/session.c src/builtins.c src/base64.c shsrv/elfldr.c shsrv/pt.c
OBJS = $(SRCS:.c=.o)
TARGET = ps5-ssh-srvr.elf
KILL_SRC = tools/kill_sshsvr.c
KILL_OBJ = $(KILL_SRC:.c=.o)
KILL_TARGET = kill_ps5-ssh-srvr.elf

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(KILL_TARGET): $(KILL_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(KILL_OBJ) $(LIBS)

clean:
	rm -f $(OBJS) $(TARGET) $(KILL_OBJ) $(KILL_TARGET)

deploy: $(TARGET)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

deploy-kill: $(KILL_TARGET)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^