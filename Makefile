CC = gcc
CFLAGS = -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -pthread -Iinclude -Isrc
LDFLAGS = -pthread -lm
SDL = /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0

SRC = src/main.c src/np_serial.c src/np_knight.c src/np_ring.c src/np_dsp.c src/np_font.c src/np_learn.c
BIN = np-exg

.PHONY: all clean cli

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(SDL) $(LDFLAGS)

clean:
	rm -f $(BIN)

# Headless smoke: open the first serial port for a few seconds
cli: $(BIN)
	./$(BIN) --cli --seconds 4
