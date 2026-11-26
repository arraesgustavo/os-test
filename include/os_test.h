#ifndef OS_TEST_H
#define OS_TEST_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define OS_TEST_DETAIL_MAX 512

typedef enum {
    OS_TEST_PASS = 0,
    OS_TEST_FAIL = 1,
    OS_TEST_SKIP = 2
} os_test_status_t;

typedef int (*os_test_fn)(char *detail_buf, size_t detail_size);

typedef struct {
    const char *suite;
    const char *name;
    os_test_fn  fn;
} os_test_case_t;

typedef struct {
    os_test_status_t status;
    uint64_t         duration_us;
    char             detail_msg[OS_TEST_DETAIL_MAX];
} os_test_result_t;

static inline uint64_t os_test_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline uint64_t os_test_elapsed_us(uint64_t start_ns, uint64_t end_ns) {
    if (end_ns <= start_ns) {
        return 0;
    }
    return (end_ns - start_ns) / 1000ULL;
}

static inline const char *os_test_status_str(os_test_status_t status) {
    switch (status) {
    case OS_TEST_PASS:
        return "PASS";
    case OS_TEST_FAIL:
        return "FAIL";
    case OS_TEST_SKIP:
        return "SKIP";
    default:
        return "UNKNOWN";
    }
}

static inline os_test_result_t os_test_run_case(const os_test_case_t *tc) {
    os_test_result_t res;
    res.status = OS_TEST_FAIL;
    res.duration_us = 0;
    res.detail_msg[0] = '\0';

    if (tc == NULL || tc->fn == NULL) {
        snprintf(res.detail_msg, sizeof(res.detail_msg), "invalid test case");
        return res;
    }

    uint64_t start_ns = os_test_now_ns();
    int rc = tc->fn(res.detail_msg, sizeof(res.detail_msg));
    uint64_t end_ns = os_test_now_ns();

    res.duration_us = os_test_elapsed_us(start_ns, end_ns);

    if (rc == OS_TEST_PASS || rc == OS_TEST_FAIL || rc == OS_TEST_SKIP) {
        res.status = (os_test_status_t)rc;
    } else {
        res.status = OS_TEST_FAIL;
        if (res.detail_msg[0] == '\0') {
            snprintf(res.detail_msg, sizeof(res.detail_msg),
                     "unexpected test return code: %d", rc);
        }
    }

    return res;
}

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            if (detail_buf != NULL && detail_size > 0) {                       \
                snprintf(detail_buf, detail_size, "%s:%d: %s",                 \
                         __FILE__, __LINE__, (msg));                           \
            }                                                                  \
            return OS_TEST_FAIL;                                               \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(expected, actual, msg)                                       \
    do {                                                                       \
        long long os_test_exp_ = (long long)(expected);                        \
        long long os_test_act_ = (long long)(actual);                          \
        if (os_test_exp_ != os_test_act_) {                                    \
            if (detail_buf != NULL && detail_size > 0) {                       \
                snprintf(detail_buf, detail_size,                              \
                         "%s:%d: %s (expected=%lld, actual=%lld)",             \
                         __FILE__, __LINE__, (msg),                            \
                         os_test_exp_, os_test_act_);                          \
            }                                                                  \
            return OS_TEST_FAIL;                                               \
        }                                                                      \
    } while (0)

#define SKIP_TEST(msg)                                                         \
    do {                                                                       \
        if (detail_buf != NULL && detail_size > 0) {                           \
            snprintf(detail_buf, detail_size, "%s", (msg));                    \
        }                                                                      \
        return OS_TEST_SKIP;                                                   \
    } while (0)

#define PASS_WITH_INFO(...)                                                    \
    do {                                                                       \
        if (detail_buf != NULL && detail_size > 0) {                           \
            snprintf(detail_buf, detail_size, __VA_ARGS__);                    \
        }                                                                      \
        return OS_TEST_PASS;                                                   \
    } while (0)

/* Scheduler / concurrency test suite (src/test_scheduler.c) */
int test_mutex_contention(char *detail_buf, size_t detail_size);
int test_condvar_producer_consumer(char *detail_buf, size_t detail_size);
int test_context_switch_latency(char *detail_buf, size_t detail_size);

/* IPC test suite (src/test_ipc.c) */
int test_unix_socketpair(char *detail_buf, size_t detail_size);
int test_anonymous_shared_memory(char *detail_buf, size_t detail_size);

/* Virtual memory protection test suite (src/test_memory.c) */
int test_mmap_page_alignment(char *detail_buf, size_t detail_size);
int test_mprotect_readonly_fault(char *detail_buf, size_t detail_size);
int test_mprotect_guard_page(char *detail_buf, size_t detail_size);

#endif /* OS_TEST_H */
