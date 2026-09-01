CC = gcc
CFLAGS = -std=c11 -D_DEFAULT_SOURCE -DNPL_POSIX -O2 -Wall -Wextra -pthread \
	-Iinclude -Isrc -Inplearn/include
LDFLAGS = -pthread -lm
SDL = /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0
AR = ar

HOST = src/main.c src/np_serial.c src/np_knight.c src/np_ring.c src/np_dsp.c src/np_font.c \
	src/np_smx.c src/np_algo.c
NPL = nplearn/src/nplearn.c nplearn/src/nplearn_filt.c nplearn/src/nplearn_posix.c
LIB = libnplearn.a
BIN = np-exg

TEST_CORE = tests/test_core
LIVE = tests/live_collect
TEST_SRC = src/np_serial.c src/np_knight.c src/np_ring.c src/np_dsp.c src/np_smx.c src/np_algo.c
TEST_NPL = nplearn/src/nplearn.c nplearn/src/nplearn_filt.c nplearn/src/nplearn_posix.c

.PHONY: all lib clean cli test test-live deliver

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
	rm -f $(BIN) $(LIB) nplearn/src/*.o $(TEST_CORE) $(LIVE)

cli: $(BIN)
	./$(BIN) --cli --seconds 4

$(TEST_CORE): tests/test_core.c $(TEST_SRC) $(LIB)
	$(CC) $(CFLAGS) -o $@ tests/test_core.c $(TEST_SRC) $(LIB) $(LDFLAGS)

$(LIVE): tests/live_collect.c $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ tests/live_collect.c $(TEST_SRC) $(LDFLAGS)

test: $(TEST_CORE)
	./$(TEST_CORE)

test-live: $(LIVE)
	@if [ -r /dev/ttyUSB1 ]; then \
		./$(LIVE) /dev/ttyUSB1 tests/fixtures/live-table.csv; \
	else \
		sg dialout -c './$(LIVE) /dev/ttyUSB1 tests/fixtures/live-table.csv'; \
	fi

# Loop mock + live until the harness delivers or tries run out.
deliver: $(TEST_CORE) $(LIVE)
	@ok=0; \
	for i in 1 2 3 4 5; do \
		echo "==== deliver try $$i/5 mock ===="; \
		./$(TEST_CORE) || continue; \
		echo "==== deliver try $$i/5 live ===="; \
		if [ -r /dev/ttyUSB1 ]; then \
			./$(LIVE) /dev/ttyUSB1 tests/fixtures/live-table.csv && ok=1 && break; \
		else \
			sg dialout -c './$(LIVE) /dev/ttyUSB1 tests/fixtures/live-table.csv' && ok=1 && break; \
		fi; \
		echo "retry after 3s"; sleep 3; \
	done; \
	if [ "$$ok" != 1 ]; then echo DELIVER FAIL; exit 1; fi; \
	echo DELIVER OK
