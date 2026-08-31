CC = gcc
CFLAGS = -std=c11 -D_DEFAULT_SOURCE -DNPL_POSIX -O2 -Wall -Wextra -pthread \
	-Iinclude -Isrc -Inplearn/include
LDFLAGS = -pthread -lm
SDL = /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0
AR = ar

HOST = src/main.c src/np_serial.c src/np_knight.c src/np_ring.c src/np_dsp.c src/np_font.c
NPL = nplearn/src/nplearn.c nplearn/src/nplearn_filt.c nplearn/src/nplearn_posix.c
LIB = libnplearn.a
BIN = np-exg

.PHONY: all lib clean cli

all: $(BIN)

lib: $(LIB)

$(LIB): $(NPL) nplearn/include/nplearn.h
	$(CC) $(CFLAGS) -c nplearn/src/nplearn.c -o nplearn/src/nplearn.o
	$(CC) $(CFLAGS) -c nplearn/src/nplearn_filt.c -o nplearn/src/nplearn_filt.o
	$(CC) $(CFLAGS) -c nplearn/src/nplearn_posix.c -o nplearn/src/nplearn_posix.o
	$(AR) rcs $@ nplearn/src/nplearn.o nplearn/src/nplearn_filt.o nplearn/src/nplearn_posix.o

$(BIN): $(HOST) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(HOST) $(LIB) $(SDL) $(LDFLAGS)

clean:
	rm -f $(BIN) $(LIB) nplearn/src/*.o

cli: $(BIN)
	./$(BIN) --cli --seconds 4
