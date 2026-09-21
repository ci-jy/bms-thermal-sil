# Build and test the BMS thermal controller.
#   make test      Unity unit tests + gcov branch coverage report
#   make app       FreeRTOS POSIX-port application (build/bms_app)
#   make tools     DBC codec command-line tool used by the SIL suite
CC      ?= gcc
CSTD    := -std=c11
WARN    := -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion
BUILD   := build
SRC     := $(wildcard src/*.c)
UNITY   := third_party/unity
TESTS   := $(patsubst tests/%.c,$(BUILD)/tests/%,$(wildcard tests/test_*.c))
COV_MIN ?= 85

FREERTOS := third_party/FreeRTOS-Kernel
RTOS_SRC := $(FREERTOS)/tasks.c $(FREERTOS)/queue.c $(FREERTOS)/list.c \
            $(FREERTOS)/portable/ThirdParty/GCC/Posix/port.c \
            $(FREERTOS)/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
RTOS_INC := -I$(FREERTOS)/include -I$(FREERTOS)/portable/ThirdParty/GCC/Posix \
            -I$(FREERTOS)/portable/ThirdParty/GCC/Posix/utils -Iapp

.PHONY: all test app tools coverage clean
all: app tools

# ---- unit tests with coverage -------------------------------------------
COV_OBJ := $(patsubst src/%.c,$(BUILD)/cov/%.o,$(SRC))

$(BUILD)/cov/%.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(WARN) -O0 -g --coverage -Isrc -c $< -o $@

$(BUILD)/unity.o: $(UNITY)/unity.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) -O1 -I$(UNITY) -c $< -o $@

$(BUILD)/tests/%: tests/%.c $(COV_OBJ) $(BUILD)/unity.o
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) -Wall -Wextra -O0 -g --coverage -Isrc -I$(UNITY) $< $(COV_OBJ) $(BUILD)/unity.o -lm -o $@

test: $(TESTS)
	@rm -f $(BUILD)/cov/*.gcda
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t; done
	@$(MAKE) --no-print-directory coverage

coverage:
	@python3 tools/coverage_report.py --objdir $(BUILD)/cov --min $(COV_MIN) $(SRC)

# ---- FreeRTOS application -------------------------------------------------
app: $(BUILD)/bms_app

$(BUILD)/bms_app: app/*.c app/*.h $(SRC) src/*.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -O2 -g -Wall -Wextra -Isrc $(RTOS_INC) app/main.c app/transport.c $(SRC) $(RTOS_SRC) -lpthread -lm -o $@

# ---- tools -------------------------------------------------------------------
tools: $(BUILD)/dbc_codec_cli

$(BUILD)/dbc_codec_cli: tools/dbc_codec_cli.c $(SRC) src/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CSTD) $(WARN) -O2 -Isrc tools/dbc_codec_cli.c $(SRC) -lm -o $@

clean:
	rm -rf $(BUILD)
