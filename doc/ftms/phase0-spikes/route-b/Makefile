# Spike build: strict C99.  `make test` / `make replay`
CC ?= cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -O2
# FLYWHEEL_DIR: the Flywheel checkout (no default; set it before make test / make replay).
FLYWHEEL_DIR ?=
VECTORS ?= $(FLYWHEEL_DIR)/shared/fixtures/ftms-vectors.json
RIDES ?= $(FLYWHEEL_DIR)/shared/fixtures/rides
B = build

all: $(B)/test_ftms_parse $(B)/replay_raw
$(B)/test_ftms_parse: test/test_ftms_parse.c src/ftms_parse.c src/ftms_parse.h test/tinyjson.h
	$(CC) $(CFLAGS) -o $@ test/test_ftms_parse.c src/ftms_parse.c -lm
$(B)/replay_raw: test/replay_raw.c src/ftms_parse.c src/ftms_parse.h test/tinyjson.h
	$(CC) $(CFLAGS) -o $@ test/replay_raw.c src/ftms_parse.c -lm
test: $(B)/test_ftms_parse
	$(B)/test_ftms_parse $(VECTORS)
replay: $(B)/replay_raw
	$(B)/replay_raw $(RIDES)/q200b-run1.raw.jsonl 10
clean:
	rm -f $(B)/*
.PHONY: all test replay clean
