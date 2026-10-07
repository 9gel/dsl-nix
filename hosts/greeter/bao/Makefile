# Unit tests for the bao. From this directory: make test
CC     = cc
CFLAGS = -std=c11 -Wall -Wextra -Werror
PYTHON = python3
BUILD  = build

.PHONY: test test-wave test-3d test-2d test-py clean

test: test-wave test-3d test-2d test-py

$(BUILD)/bao-wave-test: bao-wave-test.c bao-gesture.c \
		bao-gesture.h bao-feed.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I. -o $@ \
		bao-wave-test.c bao-gesture.c -lm

$(BUILD)/bao-3d-test: bao-3d-test.c bao-3d.c bao-3d.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DDSL_BAO_HEADLESS -DDSL_BAO_NO_MAIN \
		-I. -o $@ bao-3d-test.c bao-3d.c -lm

$(BUILD)/bao-test: bao-test.c bao.c bao.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DDSL_BAO_HEADLESS -DDSL_BAO_NO_MAIN \
		-I. -o $@ bao-test.c bao.c -lm

test-wave: $(BUILD)/bao-wave-test
	$(BUILD)/bao-wave-test

test-3d: $(BUILD)/bao-3d-test
	$(BUILD)/bao-3d-test

test-2d: $(BUILD)/bao-test
	$(BUILD)/bao-test

test-py:
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) test_bao.py

clean:
	rm -rf $(BUILD)
