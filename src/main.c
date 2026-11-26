#include "os_test.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#ifdef __ANDROID__
#define OS_TEST_BUILD_ENV "Android (Bionic)"
#else
#define OS_TEST_BUILD_ENV "Linux (glibc)"
#endif

typedef struct {
    bool        show_help;
    bool        list_only;
    bool        verbose;
    const char *suite_filter;
} cli_options_t;

static int test_core_smoke(char *detail_buf, size_t detail_size) {
    uint64_t t1 = os_test_now_ns();
    ASSERT_TRUE(t1 > 0, "CLOCK_MONOTONIC returned zero");

    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 1000000L; /* 1 ms */
    (void)nanosleep(&req, NULL);

    uint64_t t2 = os_test_now_ns();
    ASSERT_TRUE(t2 > t1, "CLOCK_MONOTONIC did not advance");

    long page_size = sysconf(_SC_PAGESIZE);
    ASSERT_TRUE(page_size > 0, "sysconf(_SC_PAGESIZE) must be > 0");
    ASSERT_EQ(0, page_size & (page_size - 1),
              "page size must be a power of two");

    PASS_WITH_INFO("monotonic_delta_us=%" PRIu64 ", page_size=%ld",
                   os_test_elapsed_us(t1, t2), page_size);
}

static const os_test_case_t g_tests[] = {
    { "core",      "monotonic_timer_and_pagesize", test_core_smoke },
    { "scheduler", "mutex_contention",             test_mutex_contention },
    { "scheduler", "condvar_producer_consumer",    test_condvar_producer_consumer },
    { "scheduler", "context_switch_latency",       test_context_switch_latency },
    { "ipc",       "unix_socketpair",              test_unix_socketpair },
    { "ipc",       "anonymous_shared_memory",      test_anonymous_shared_memory },
    { "memory",    "mmap_page_alignment",          test_mmap_page_alignment },
    { "memory",    "mprotect_readonly_fault",      test_mprotect_readonly_fault },
    { "memory",    "mprotect_guard_page",          test_mprotect_guard_page },
};

static const size_t g_test_count = sizeof(g_tests) / sizeof(g_tests[0]);

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  --help            Show this help message and exit\n");
    printf("  --list            List registered test suites and test cases\n");
    printf("  --suite <name>    Run only tests belonging to <name>\n");
    printf("  --verbose         Show detail messages for PASS and SKIP tests\n");
}

static int parse_args(int argc, char **argv, cli_options_t *opts) {
    memset(opts, 0, sizeof(*opts));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts->show_help = true;
        } else if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "-l") == 0) {
            opts->list_only = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--suite") == 0 || strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a suite name argument.\n", argv[i]);
                return -1;
            }
            opts->suite_filter = argv[++i];
        } else {
            fprintf(stderr, "Error: unknown option '%s'. Use --help for usage.\n", argv[i]);
            return -1;
        }
    }

    return 0;
}

static void format_duration(uint64_t duration_us, char *buf, size_t buf_size) {
    if (duration_us >= 1000ULL) {
        double ms = (double)duration_us / 1000.0;
        snprintf(buf, buf_size, "%.2f ms", ms);
    } else {
        snprintf(buf, buf_size, "%" PRIu64 " us", duration_us);
    }
}

static void print_environment(void) {
    struct utsname uts;
    const char *sysname = "unknown";
    const char *release = "unknown";
    const char *machine = "unknown";

    if (uname(&uts) == 0) {
        sysname = uts.sysname;
        release = uts.release;
        machine = uts.machine;
    }

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    uid_t uid = getuid();

    printf("================================================================================\n");
    printf(" os-test :: POSIX / Linux / Android OS Validation Runner\n");
    printf("================================================================================\n");
    printf(" OS      : %s %s\n", sysname, release);
    printf(" Arch    : %s\n", machine);
    if (cpus > 0) {
        printf(" CPUs    : %ld online\n", cpus);
    } else {
        printf(" CPUs    : unknown\n");
    }
    printf(" UID     : %u\n", (unsigned int)uid);
    printf(" Build   : %s\n", OS_TEST_BUILD_ENV);
    printf("================================================================================\n");
}

static int list_tests(const cli_options_t *opts) {
    size_t matched = 0;

    printf("+--------------+----------------------------------+\n");
    printf("| SUITE        | TEST                             |\n");
    printf("+--------------+----------------------------------+\n");

    for (size_t i = 0; i < g_test_count; i++) {
        if (opts->suite_filter != NULL &&
            strcmp(g_tests[i].suite, opts->suite_filter) != 0) {
            continue;
        }
        printf("| %-12s | %-32s |\n", g_tests[i].suite, g_tests[i].name);
        matched++;
    }

    printf("+--------------+----------------------------------+\n");
    printf("Registered tests: %zu\n", matched);

    if (opts->suite_filter != NULL && matched == 0) {
        fprintf(stderr, "Error: no tests found for suite '%s'.\n", opts->suite_filter);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    cli_options_t opts;
    if (parse_args(argc, argv, &opts) != 0) {
        return 1;
    }

    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (opts.list_only) {
        return list_tests(&opts);
    }

    print_environment();

    size_t total = 0;
    size_t passed = 0;
    size_t failed = 0;
    size_t skipped = 0;
    uint64_t total_us = 0;

    printf("+--------------+----------------------------------+--------+------------+\n");
    printf("| SUITE        | TEST                             | STATUS | TIME       |\n");
    printf("+--------------+----------------------------------+--------+------------+\n");

    for (size_t i = 0; i < g_test_count; i++) {
        if (opts.suite_filter != NULL &&
            strcmp(g_tests[i].suite, opts.suite_filter) != 0) {
            continue;
        }

        os_test_result_t res = os_test_run_case(&g_tests[i]);
        char time_buf[32];
        char status_buf[16];

        format_duration(res.duration_us, time_buf, sizeof(time_buf));
        snprintf(status_buf, sizeof(status_buf), "[%s]",
                 os_test_status_str(res.status));

        printf("| %-12s | %-32s | %-6s | %10s |\n",
               g_tests[i].suite, g_tests[i].name, status_buf, time_buf);

        bool show_detail = false;
        if (res.detail_msg[0] != '\0') {
            if (res.status == OS_TEST_FAIL) {
                show_detail = true;
            } else if (opts.verbose &&
                       (res.status == OS_TEST_PASS || res.status == OS_TEST_SKIP)) {
                show_detail = true;
            }
        }

        if (show_detail) {
            printf("  -> %s\n", res.detail_msg);
        }

        total++;
        total_us += res.duration_us;
        if (res.status == OS_TEST_PASS) {
            passed++;
        } else if (res.status == OS_TEST_FAIL) {
            failed++;
        } else if (res.status == OS_TEST_SKIP) {
            skipped++;
        }
    }

    printf("+--------------+----------------------------------+--------+------------+\n");

    if (opts.suite_filter != NULL && total == 0) {
        fprintf(stderr, "Error: no tests found for suite '%s'.\n", opts.suite_filter);
        return 1;
    }

    char total_time_buf[32];
    format_duration(total_us, total_time_buf, sizeof(total_time_buf));

    printf("================================================================================\n");
    printf(" Summary: total=%zu  pass=%zu  fail=%zu  skip=%zu  time=%s\n",
           total, passed, failed, skipped, total_time_buf);
    printf("================================================================================\n");

    return (failed == 0) ? 0 : 1;
}
