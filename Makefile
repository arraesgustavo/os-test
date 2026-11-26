CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -D_GNU_SOURCE
LDFLAGS = -pthread

ASAN_CFLAGS = $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -g
ASAN_LDFLAGS = $(LDFLAGS) -fsanitize=address,undefined

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
TARGET = $(BUILD_DIR)/os_test
TARGET_ASAN = $(BUILD_DIR)/os_test_asan
TARGET_ANDROID = $(BUILD_DIR)/os_test_android
TARGET_ANDROID_STATIC = $(BUILD_DIR)/os_test_android_static

SRCS = src/main.c src/test_scheduler.c src/test_ipc.c src/test_memory.c src/test_linux_sec.c src/test_android_sec.c
HEADERS = include/os_test.h
OBJS = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Android cross-compilation configuration (arm64-v8a / aarch64)
ANDROID_API ?= 28
ANDROID_HOST_TAG ?= linux-x86_64
ANDROID_TOOLCHAIN = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/$(ANDROID_HOST_TAG)/bin
ANDROID_CC ?= $(ANDROID_TOOLCHAIN)/aarch64-linux-android$(ANDROID_API)-clang
ANDROID_CFLAGS = $(CFLAGS) -fPIE
ANDROID_LDFLAGS = -pie

# Lightweight static ARM64 cross-compiler alternative (no NDK required)
# Example override: make android-static CROSS_CC="zig cc -target aarch64-linux-musl"
CROSS_CC ?= aarch64-linux-gnu-gcc
ANDROID_ARGS ?=

.PHONY: all linux asan android android-static test-android clean

all: linux

linux: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: src/%.c $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

asan: $(TARGET_ASAN)

$(TARGET_ASAN): $(SRCS) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(ASAN_CFLAGS) $(SRCS) -o $@ $(ASAN_LDFLAGS)

android: $(TARGET_ANDROID)

$(TARGET_ANDROID): $(SRCS) $(HEADERS) | $(BUILD_DIR)
	@if [ -z "$(ANDROID_NDK_HOME)" ] && [ ! -x "$(ANDROID_CC)" ] && ! command -v "$(ANDROID_CC)" >/dev/null 2>&1; then \
		echo "Error: ANDROID_NDK_HOME is not set." >&2; \
		echo "Set ANDROID_NDK_HOME to your Android NDK path, e.g.:" >&2; \
		echo "  export ANDROID_NDK_HOME=/path/to/android-ndk-r27" >&2; \
		echo "  make android ANDROID_API=$(ANDROID_API)" >&2; \
		echo "Or build without the NDK using the static ARM64 target:" >&2; \
		echo "  make android-static" >&2; \
		exit 1; \
	fi
	@if [ ! -x "$(ANDROID_CC)" ] && ! command -v "$(ANDROID_CC)" >/dev/null 2>&1; then \
		echo "Error: Android NDK compiler not found: $(ANDROID_CC)" >&2; \
		echo "Check ANDROID_NDK_HOME='$(ANDROID_NDK_HOME)' and ANDROID_API='$(ANDROID_API)'," >&2; \
		echo "or use the alternative target: make android-static" >&2; \
		exit 1; \
	fi
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(SRCS) -o $@ $(ANDROID_LDFLAGS)

android-static: $(TARGET_ANDROID_STATIC)

$(TARGET_ANDROID_STATIC): $(SRCS) $(HEADERS) | $(BUILD_DIR)
	@CROSS_BIN="$(firstword $(CROSS_CC))"; \
	if ! command -v "$$CROSS_BIN" >/dev/null 2>&1; then \
		echo "Error: ARM64 cross-compiler '$$CROSS_BIN' not found in PATH." >&2; \
		echo "Install aarch64-linux-gnu-gcc or override CROSS_CC, e.g.:" >&2; \
		echo "  make android-static CROSS_CC='zig cc -target aarch64-linux-musl'" >&2; \
		exit 1; \
	fi
	$(CROSS_CC) $(CFLAGS) -static $(SRCS) -o $@ $(LDFLAGS)

test-android:
	./scripts/run_android.sh $(ANDROID_ARGS)

$(BUILD_DIR) $(OBJ_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
