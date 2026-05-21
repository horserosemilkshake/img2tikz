CC := clang
CFLAGS := -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror

SRC := src/img2tikz.c
BIN := img2tikz

.PHONY: all clean

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -lm -o $(BIN)

clean:
	rm -f $(BIN)