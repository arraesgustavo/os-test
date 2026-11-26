<div align="center">

```text
+------------------------------------------------------------------+
|   _____   _____           _______  ______   _____   _______      |
|  / ___ \ / ____|         |__   __||  ____| / ____| |__   __|     |
| | |   | | |___    _____     | |   | |__   | |___      | |        |
| | |   | |\___ \  |_____|    | |   |  __|   \___ \     | |        |
| | |___| |____| |            | |   | |____  ____| |    | |        |
|  \_____/|_____/             |_|   |______||_____/     |_|        |
+------------------------------------------------------------------+
```

**Portable C11 POSIX, Linux Kernel, and Android OS Security Validation Runner**

[![C11](https://img.shields.io/badge/C11-A8B9CC?style=for-the-badge&logo=c&logoColor=black)](#build--usage)
[![Android OS](https://img.shields.io/badge/Android_OS-3DDC84?style=for-the-badge&logo=android&logoColor=black)](#test-suites)
[![Linux Kernel](https://img.shields.io/badge/Linux_Kernel-FCC624?style=for-the-badge&logo=linux&logoColor=black)](#test-suites)
[![POSIX Zero Deps](https://img.shields.io/badge/POSIX_Zero_Deps-222222?style=for-the-badge&logo=gnu&logoColor=white)](#architecture)
[![Make](https://img.shields.io/badge/Make-427819?style=for-the-badge&logo=cmake&logoColor=white)](#build--usage)

</div>

## Overview

`os-test` is a zero-dependency C11 CLI test runner that validates POSIX concurrency, IPC, and virtual memory primitives alongside Linux kernel hardening and Android OS security boundaries. The same source tree compiles cleanly for Linux desktop (glibc) and Android ARM64 (Bionic NDK or static cross-build), inspecting runtime enforcement of SELinux, Android Verified Boot (AVB), UID/capability sandboxing, seccomp, and `/dev/binder` isolation directly from userspace.

## Test Suites

In addition to a baseline `core` smoke check (`CLOCK_MONOTONIC` and page size), the runner executes 5 test suites:

| Suite | Validated Mechanisms | Platforms |
| :--- | :--- | :--- |
| **`android`** | SELinux domain & enforcing mode (`/proc/self/attr/current`, `/sys/fs/selinux/enforce`), AVB & build properties (`__system_property_get`), UID role & `CapEff == 0`, `/data/data` & `/data/system` denial (`EACCES`), `/dev/binder` presence | Android (full), Linux (UID/fs checks; AVB/SELinux skip if absent) |
| **`linux_sec`** | ASLR (`randomize_va_space` + `mmap` entropy), `kptr_restrict` & `dmesg_restrict`, Yama `ptrace_scope`, `PR_SET_NO_NEW_PRIVS` + `seccomp(SECCOMP_SET_MODE_FILTER)` on `fork()` child, active LSM stack (`/sys/kernel/security/lsm`) | Linux, Android |
| **`memory`** | `mmap` page alignment, `mprotect(PROT_READ)` write-fault `SIGSEGV` delivery, `PROT_NONE` guard-page fault isolation in `fork()` children | Linux, Android |
| **`scheduler`** | `pthread_mutex_t` multi-thread contention correctness, `pthread_cond_t` bounded producer-consumer queue, `sched_yield()` context-switch latency measurement | Linux, Android |
| **`ipc`** | Bidirectional `AF_UNIX` `socketpair()` framing, anonymous shared memory (`MAP_SHARED | MAP_ANONYMOUS`) cross-process synchronization | Linux, Android |

## Architecture

```text
                      +------------------------------+
                      |  Single C11 Source Base      |
                      |  include/os_test.h           |
                      |  src/main.c                  |
                      |  src/test_android_sec.c      |
                      |  src/test_linux_sec.c        |
                      |  src/test_memory.c           |
                      |  src/test_scheduler.c        |
                      |  src/test_ipc.c              |
                      +--------------+---------------+
                                     |
             +-----------------------+-----------------------+
             |                                               |
             v                                               v
+-------------------------+                     +-------------------------------+
| Linux Desktop (x86_64)  |                     | Android ARM64 (aarch64)       |
| gcc / clang (-pthread)  |                     |                               |
|                         |                     | [1] Android NDK Clang         |
| -> build/os_test        |                     |     (Bionic libc, PIE)        |
|    (glibc)              |                     |     -> build/os_test_android  |
| -> build/os_test_asan   |                     |                               |
|    (ASan + UBSan)       |                     | [2] aarch64-linux-gnu-gcc     |
+-------------------------+                     |     or zig cc (-static)       |
                                                |     -> os_test_android_static |
                                                +---------------+---------------+
                                                                |
                                                                v  scripts/run_android.sh
                                                +-------------------------------+
                                                | adb push -> /data/local/tmp   |
                                                | chmod +x -> adb shell exec    |
                                                | trap EXIT -> rm -f binary     |
                                                +-------------------------------+
```

## Build & Usage

### Linux Desktop

```sh
# Standard release build (-std=c11 -Wall -Wextra -Werror -O2)
make linux
./build/os_test

# AddressSanitizer + UndefinedBehaviorSanitizer build
make asan
./build/os_test_asan
```

### Android ARM64 Cross-Compilation

```sh
# Option 1: Android NDK (dynamic PIE binary linked against Bionic libc)
export ANDROID_NDK_HOME=/path/to/android-ndk-r27
make android ANDROID_API=28

# Option 2: Static ARM64 binary without downloading the NDK
make android-static
# Or override the cross-compiler (e.g. Zig):
make android-static CROSS_CC="zig cc -target aarch64-linux-musl"
```

### Running on an Android Device or Emulator

`scripts/run_android.sh` verifies `adb` and an attached `device`, pushes the binary to `/data/local/tmp/os_test`, executes it while preserving the exit code, and deletes the remote binary on exit.

```sh
# Default binary (build/os_test_android)
make test-android

# Pass CLI flags via Makefile or directly to the script
make test-android ANDROID_ARGS="--verbose --suite android"
./scripts/run_android.sh --verbose --suite android

# Run the static build with extra flags
./scripts/run_android.sh build/os_test_android_static --verbose
```

### CLI Options

```sh
./build/os_test --list                  # List all registered suites and test cases
./build/os_test --suite linux_sec       # Run a single suite (android, linux_sec, memory, scheduler, ipc, core)
./build/os_test --verbose               # Print diagnostic details for PASS and SKIP results
./build/os_test --help                  # Show usage
```

## Sample Output

### Linux Desktop (`./build/os_test`)

```text
================================================================================
 os-test :: POSIX / Linux / Android OS Validation Runner
================================================================================
 OS      : Linux 6.18.36-1-lts
 Arch    : x86_64
 CPUs    : 8 online
 UID     : 1000
 Build   : Linux (glibc)
================================================================================
+--------------+----------------------------------+--------+------------+
| SUITE        | TEST                             | STATUS | TIME       |
+--------------+----------------------------------+--------+------------+
| core         | monotonic_timer_and_pagesize     | [PASS] |    1.11 ms |
| scheduler    | mutex_contention                 | [PASS] |   18.71 ms |
| scheduler    | condvar_producer_consumer        | [PASS] |    2.10 ms |
| scheduler    | context_switch_latency           | [PASS] |    8.37 ms |
| ipc          | unix_socketpair                  | [PASS] |      25 us |
| ipc          | anonymous_shared_memory          | [PASS] |     394 us |
| memory       | mmap_page_alignment              | [PASS] |      14 us |
| memory       | mprotect_readonly_fault          | [PASS] |      25 us |
| memory       | mprotect_guard_page              | [PASS] |      22 us |
| linux_sec    | aslr_status                      | [PASS] |     186 us |
| linux_sec    | kernel_info_leak_protections     | [PASS] |     193 us |
| linux_sec    | yama_ptrace_scope                | [PASS] |      88 us |
| linux_sec    | no_new_privs_and_seccomp         | [PASS] |     445 us |
| linux_sec    | lsm_stack                        | [SKIP] |     123 us |
+--------------+----------------------------------+--------+------------+
================================================================================
 Summary: total=14  pass=13  fail=0  skip=1  time=31.79 ms
================================================================================
```

### Android Device via ADB (`./scripts/run_android.sh --verbose --suite android`)

```text
[os-test] Pushing 'build/os_test_android' -> '/data/local/tmp/os_test'...
[os-test] Running on Android device...
================================================================================
 os-test :: POSIX / Linux / Android OS Validation Runner
================================================================================
 OS      : Linux 6.1.75-android14-11
 Arch    : aarch64
 CPUs    : 8 online
 UID     : 2000
 Build   : Android (Bionic)
================================================================================
+--------------+----------------------------------+--------+------------+
| SUITE        | TEST                             | STATUS | TIME       |
+--------------+----------------------------------+--------+------------+
| android      | selinux_domain_and_enforcing     | [PASS] |      62 us |
  -> domain=u:r:shell:s0, enforce=1 (enforcing)
| android      | verified_boot_and_build          | [PASS] |      48 us |
  -> avb=green, device_state=locked, build=user, debuggable=0, secure=1, sdk=34, patch=2025-11-05
| android      | uid_sandbox_and_capabilities     | [PASS] |      51 us |
  -> uid=2000, euid=2000, role=AID_SHELL, CapEff=0000000000000000
| android      | filesystem_and_binder_isolation  | [PASS] |      39 us |
  -> /data/data=EACCES, /data/system=EACCES, /dev/binder=present(chr=1,mode=0666)
+--------------+----------------------------------+--------+------------+
================================================================================
 Summary: total=4  pass=4  fail=0  skip=0  time=200 us
================================================================================
```
