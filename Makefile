# LF-CapCheck build. This Makefile is the source of truth for flags (Homebrew builds with it);
# the Xcode project mirrors them for editing/debugging.

NAME    := lf-capcheck
SRC_DIR := LF-CapCheck
BUILD   := build
PREFIX  ?= /usr/local
CC      ?= clang

WARN    := -std=c17 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow \
           -Wstrict-prototypes -Wmissing-prototypes -Wformat=2
# Strict -std=c17 hides POSIX/BSD APIs (mkdtemp, fchmod, strlcpy...) on macOS.
PLATFORM := -D_DARWIN_C_SOURCE
HARDEN  := -fstack-protector-strong -fPIE
INCLUDE := -I$(SRC_DIR)
DEPFLAGS = -MMD -MP

# _FORTIFY_SOURCE is release-only: the sanitizer build defines it as 0 itself.
REL_CFLAGS  = $(WARN) $(PLATFORM) $(HARDEN) -D_FORTIFY_SOURCE=2 $(INCLUDE) -O2 $(DEPFLAGS)
TEST_CFLAGS = $(WARN) $(PLATFORM) $(HARDEN) $(INCLUDE) -O1 -g -fno-omit-frame-pointer \
              -fsanitize=address,undefined -fno-sanitize-recover=undefined \
              -I tests $(DEPFLAGS)
TEST_LDFLAGS = -fsanitize=address,undefined

SRCS      := $(shell find $(SRC_DIR) -name '*.c')
LIB_SRCS  := $(filter-out $(SRC_DIR)/main.c,$(SRCS))
TEST_SRCS := $(wildcard tests/test_*.c)

REL_OBJS      := $(SRCS:%.c=$(BUILD)/rel/%.o)
TEST_LIB_OBJS := $(LIB_SRCS:%.c=$(BUILD)/test/%.o)
TEST_OBJS     := $(TEST_SRCS:%.c=$(BUILD)/test/%.o)
TEST_BINS     := $(TEST_SRCS:%.c=$(BUILD)/test/%)

# Vendored third-party code is compiled without project warning flags.
VENDOR_OBJS := $(filter $(BUILD)/rel/$(SRC_DIR)/vendor/%,$(REL_OBJS)) \
               $(filter $(BUILD)/test/$(SRC_DIR)/vendor/%,$(TEST_LIB_OBJS))
$(VENDOR_OBJS): WARN := -std=c17 -w

BIN := $(BUILD)/$(NAME)

.PHONY: all test clean install
.SECONDARY:
all: $(BIN)

$(BIN): $(REL_OBJS)
	$(CC) $(REL_OBJS) -o $@
	@if [ "$$(uname)" = "Darwin" ]; then codesign --force --options runtime -s - $@; fi

$(BUILD)/rel/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(REL_CFLAGS) -c $< -o $@

$(BUILD)/test/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -c $< -o $@

$(BUILD)/test/tests/%: $(BUILD)/test/tests/%.o $(TEST_LIB_OBJS)
	$(CC) $(TEST_LDFLAGS) $^ -o $@

test: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t"; $$t; done; echo "all test binaries passed"

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(NAME)

clean:
	rm -rf $(BUILD)

-include $(REL_OBJS:.o=.d) $(TEST_LIB_OBJS:.o=.d) $(TEST_OBJS:.o=.d)
