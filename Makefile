CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror

SRC := src/img2tikz.c
BIN := img2tikz
TEST_SRC := tests/test_img2tikz.c
TEST_BIN := tests/test_img2tikz
TEST_CC ?= gcc
TEST_CFLAGS ?= -O0 -g -std=c11 -Wall -Wextra -Wpedantic -Werror --coverage

.PHONY: all clean test tests coverage

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -lm -o $(BIN)

$(TEST_BIN): $(TEST_SRC)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_SRC) -lm -o $(TEST_BIN)

test: tests

tests: $(TEST_BIN)
	./$(TEST_BIN)

coverage: clean $(TEST_BIN)
	rm -f src/*.gcda tests/*.gcda *.gcov
	./$(TEST_BIN)
	gcov -b tests/test_img2tikz.c

clean:
	rm -f $(BIN) $(TEST_BIN) *.gcov src/*.gcda src/*.gcno tests/*.gcda tests/*.gcno