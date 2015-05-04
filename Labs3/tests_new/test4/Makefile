CC = gcc
CFLAGS = -DNDEBUG -O3 -Wall -D_GNU_SOURCE

.PHONY: all clean

all: preem

preem: preem.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f preem
