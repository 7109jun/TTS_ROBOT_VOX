CC      ?= gcc
CFLAGS  ?= -std=c17 -O2 -Wall -Wextra -Wpedantic
SRC     := $(filter-out src/main.c,$(wildcard src/*.c))
BIN     := robotvox
ifeq ($(OS),Windows_NT)
  BIN := robotvox.exe
  LDLIBS := -lshell32 -lwinmm
else
  LDLIBS := -lm -ldl
endif

all: $(BIN)

$(BIN): $(SRC) src/main.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -o $@ $(SRC) src/main.c $(LDLIBS)

test: $(BIN)
	$(CC) $(CFLAGS) -o tests/dsp_test tests/dsp_test.c $(SRC) $(LDLIBS) && ./tests/dsp_test

clean:
	rm -f $(BIN) tests/dsp_test tests/dump_ph tests/synth_test

.PHONY: all test clean
