CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -D_GNU_SOURCE
LDFLAGS = -pthread

ASAN_CFLAGS = $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -g
ASAN_LDFLAGS = $(LDFLAGS) -fsanitize=address,undefined

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
TARGET = $(BUILD_DIR)/os_test
TARGET_ASAN = $(BUILD_DIR)/os_test_asan

SRCS = src/main.c src/test_scheduler.c src/test_ipc.c src/test_memory.c src/test_linux_sec.c src/test_android_sec.c
HEADERS = include/os_test.h
OBJS = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Fase 4: adicionar variaveis de cross-compilacao Android NDK
# (ex.: ANDROID_CC, ANDROID_API, TARGET_ANDROID e TARGET_ANDROID_STATIC).

.PHONY: all linux asan clean

all: linux

linux: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: src/%.c $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

asan: $(TARGET_ASAN)

$(TARGET_ASAN): $(SRCS) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(ASAN_CFLAGS) $(SRCS) -o $@ $(ASAN_LDFLAGS)

$(BUILD_DIR) $(OBJ_DIR):
	mkdir -p $@

# Fase 4: adicionar target 'android' para compilar binario dinamico contra Bionic.
# Fase 4: adicionar target 'android-static' para compilar binario estatico para Android.
# Fase 4: adicionar target 'test-android' para envio e execucao via adb no dispositivo.

clean:
	rm -rf $(BUILD_DIR)
