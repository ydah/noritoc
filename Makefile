CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
LDFLAGS ?=

SRC := \
	src/common.c \
	src/kana.c \
	src/ki.c \
	src/kotowari.c \
	src/fumikumi.c \
	src/sakahi.c \
	src/kazohi.c \
	src/utsushi.c \
	src/main.c
OBJ := $(SRC:.c=.o)

.PHONY: all test clean

all: noritoc

noritoc: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	sh test/run-tests.sh

clean:
	rm -f noritoc $(OBJ)
