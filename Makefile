CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -pthread
LDFLAGS ?= -pthread

BUILD := build

.PHONY: all clean test sweep tsan

all: $(BUILD)/bench $(BUILD)/test_zcring

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/zcring.o: src/zcring.c src/zcring.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/bench: bench/bench.c $(BUILD)/zcring.o | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/test_zcring: tests/test_zcring.c $(BUILD)/zcring.o | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Thread-sanitised build. Memory-ordering bugs in a lock-free ring are the
# top risk in the plan; catch them in week 1, not week 4.
tsan: tests/test_zcring.c src/zcring.c | $(BUILD)
	$(CC) -O1 -g -std=c11 -fsanitize=thread -pthread $^ -o $(BUILD)/test_tsan
	$(BUILD)/test_tsan

test: $(BUILD)/test_zcring
	$(BUILD)/test_zcring

sweep: $(BUILD)/bench
	@./scripts/sweep.sh

clean:
	rm -rf $(BUILD) results
