# Tests and the two kiosk binaries. From this directory:
#   make test    no display, no camera, no extra libraries
#   make all     dsl-bao-3d and dsl-bao-wave
# Nix passes the asset paths. `make` alone runs the tests.
CC          = cc
CFLAGS      = -std=c11 -Wall -Wextra -Werror
PROD_CFLAGS = -O2 -std=c11 -Wall -Wextra
PYTHON      = python3
BUILD       = build
PREFIX     ?= /usr/local

SCREEN_TURN ?= 1
MODELS      ?=
FONT        ?=
QR          ?=
SOUNDS      ?=
PALM        ?=
ORT_INC     ?=

.PHONY: test test-wave test-3d test-2d test-py \
	all bao wave install clean

test: test-wave test-3d test-2d test-py

all: bao wave

bao: $(BUILD)/dsl-bao-3d
wave: $(BUILD)/dsl-bao-wave

$(BUILD)/dsl-bao-3d: bao-3d.c bao-3d.h bao-feed.h
	mkdir -p $(BUILD)
	test -n "$(MODELS)" -a -n "$(FONT)" \
		-a -n "$(QR)" -a -n "$(SOUNDS)"
	$(CC) $(PROD_CFLAGS) -I. -o $@ bao-3d.c \
		-DDSL_SCREEN_TURN=$(SCREEN_TURN) \
		-DDSL_BAO_MODELS='"$(MODELS)"' \
		-DDSL_BAO_FONT='"$(FONT)"' \
		-DDSL_BAO_QR='"$(QR)"' \
		-DDSL_BAO_SOUNDS='"$(SOUNDS)"' \
		$$(pkg-config --cflags --libs raylib) \
		-lasound -lm

$(BUILD)/dsl-bao-wave: bao-wave.c bao-gesture.c \
		bao-gesture.h bao-feed.h
	mkdir -p $(BUILD)
	test -n "$(PALM)" -a -n "$(ORT_INC)"
	$(CC) $(PROD_CFLAGS) -I. -I$(ORT_INC) -o $@ \
		bao-gesture.c bao-wave.c \
		-DDSL_BAO_PALM='"$(PALM)"' \
		$$(pkg-config --cflags --libs libjpeg) \
		-lonnxruntime -lm

install: all
	mkdir -p $(PREFIX)/bin
	install -m 755 $(BUILD)/dsl-bao-3d \
		$(PREFIX)/bin/dsl-bao-3d-bin
	install -m 755 $(BUILD)/dsl-bao-wave \
		$(PREFIX)/bin/dsl-bao-wave

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
