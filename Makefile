CFLAGS ?= -O2 -march=native -pipe
CFLAGS += -std=c99 -Wall -Wextra
PREFIX ?= $(HOME)/.local

xregion: xregion.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lX11

install: xregion
	install -Dm755 xregion $(PREFIX)/bin/xregion

clean:
	rm -f xregion
