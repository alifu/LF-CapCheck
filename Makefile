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

# Coverage build: no sanitizers, instrumented for llvm-cov.
COV_CFLAGS  = $(WARN) $(PLATFORM) $(HARDEN) $(INCLUDE) -O0 -g -fprofile-instr-generate \
              -fcoverage-mapping -I tests $(DEPFLAGS)
COV_LDFLAGS = -fprofile-instr-generate
COVERAGE_MIN ?= 80

# Fuzzer: same sanitizer build as the tests. `make fuzz FUZZ_ITERATIONS=100000 FUZZ_SEED=7`
FUZZ_ITERATIONS ?= 5000
FUZZ_SEED ?= 0x5EEDC0DE

# Static analysis (clang --analyze). The Annex-K "use fprintf_s" check is disabled: it flags
# every fprintf and is not applicable to this platform.
ANALYZE_FLAGS := --analyze -Xclang -analyzer-werror -Xclang -analyzer-checker=security \
                 -Xclang -analyzer-disable-checker=security.insecureAPI.DeprecatedOrUnsafeBufferHandling \
                 -Xclang -analyzer-checker=nullability -Xclang -analyzer-checker=optin.portability

SRCS      := $(shell find $(SRC_DIR) -name '*.c')
LIB_SRCS  := $(filter-out $(SRC_DIR)/main.c,$(SRCS))
TEST_SRCS := $(wildcard tests/test_*.c)
FUZZ_SRCS := $(wildcard tests/fuzz_*.c)

REL_OBJS      := $(SRCS:%.c=$(BUILD)/rel/%.o)
TEST_LIB_OBJS := $(LIB_SRCS:%.c=$(BUILD)/test/%.o)
TEST_OBJS     := $(TEST_SRCS:%.c=$(BUILD)/test/%.o)
TEST_BINS     := $(TEST_SRCS:%.c=$(BUILD)/test/%)
FUZZ_OBJS     := $(FUZZ_SRCS:%.c=$(BUILD)/test/%.o)
FUZZ_BINS     := $(FUZZ_SRCS:%.c=$(BUILD)/test/%)
COV_DIR       := $(BUILD)/cov
COV_LIB_OBJS  := $(LIB_SRCS:%.c=$(COV_DIR)/%.o)
COV_TEST_OBJS := $(TEST_SRCS:%.c=$(COV_DIR)/%.o)
COV_BINS      := $(TEST_SRCS:%.c=$(COV_DIR)/%)

# Vendored third-party code is compiled without project warning flags.
VENDOR_OBJS := $(filter $(BUILD)/rel/$(SRC_DIR)/vendor/%,$(REL_OBJS)) \
               $(filter $(BUILD)/test/$(SRC_DIR)/vendor/%,$(TEST_LIB_OBJS)) \
               $(filter $(COV_DIR)/$(SRC_DIR)/vendor/%,$(COV_LIB_OBJS))
$(VENDOR_OBJS): WARN := -std=c17 -w

BIN := $(BUILD)/$(NAME)

.PHONY: all test fuzz analyze coverage clean install
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

fuzz: $(FUZZ_BINS)
	@set -e; for f in $(FUZZ_BINS); do $$f $(FUZZ_ITERATIONS) $(FUZZ_SEED); done

analyze:
	@set -e; for f in $(filter-out $(SRC_DIR)/vendor/%,$(SRCS)); do \
	  $(CC) $(ANALYZE_FLAGS) -std=c17 $(PLATFORM) $(INCLUDE) -o /dev/null $$f; done; \
	echo "static analysis: no findings in $(words $(filter-out $(SRC_DIR)/vendor/%,$(SRCS))) files"

# Line coverage of project code (vendored code and tests excluded); fails below COVERAGE_MIN percent.
$(COV_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(COV_CFLAGS) -c $< -o $@

$(COV_DIR)/tests/%: $(COV_DIR)/tests/%.o $(COV_LIB_OBJS)
	$(CC) $(COV_LDFLAGS) $^ -o $@

coverage: $(COV_BINS)
	@rm -f $(COV_DIR)/*.profraw $(COV_DIR)/merged.profdata
	@set -e; for t in $(COV_BINS); do \
	  LLVM_PROFILE_FILE="$(COV_DIR)/%p.profraw" $$t >/dev/null 2>&1 || { echo "test failed: $$t"; exit 1; }; done
	@xcrun llvm-profdata merge -sparse $(COV_DIR)/*.profraw -o $(COV_DIR)/merged.profdata
	@xcrun llvm-cov report $(firstword $(COV_BINS)) $(addprefix -object ,$(wordlist 2,$(words $(COV_BINS)),$(COV_BINS))) \
	  -instr-profile=$(COV_DIR)/merged.profdata -ignore-filename-regex='vendor/|tests/' | tee $(COV_DIR)/report.txt
	@awk -v min=$(COVERAGE_MIN) '/^TOTAL/ { pct = $$(NF-3); sub(/%/, "", pct); \
	  if (pct + 0 < min) { printf "line coverage %s%% is below %s%%\n", pct, min; exit 1 } \
	  else { printf "line coverage %s%% (minimum %s%%)\n", pct, min } }' $(COV_DIR)/report.txt

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(NAME)

clean:
	rm -rf $(BUILD)

-include $(REL_OBJS:.o=.d) $(TEST_LIB_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(FUZZ_OBJS:.o=.d) \
         $(COV_LIB_OBJS:.o=.d) $(COV_TEST_OBJS:.o=.d)
