#include "os_test.h"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* --------------------------------------------------------------------------
 * 1. Mutex contention test (4 threads x 50,000 increments = 200,000)
 * -------------------------------------------------------------------------- */

#define MUTEX_NUM_THREADS   4
#define MUTEX_ITERS_PER_THR 50000L

typedef struct {
    pthread_mutex_t lock;
    long            counter;
} mutex_bench_ctx_t;

static void *mutex_worker(void *arg) {
    mutex_bench_ctx_t *ctx = (mutex_bench_ctx_t *)arg;
    for (long i = 0; i < MUTEX_ITERS_PER_THR; i++) {
        pthread_mutex_lock(&ctx->lock);
        ctx->counter++;
        pthread_mutex_unlock(&ctx->lock);
    }
    return NULL;
}

int test_mutex_contention(char *detail_buf, size_t detail_size) {
    mutex_bench_ctx_t ctx;
    ctx.counter = 0;

    int rc = pthread_mutex_init(&ctx.lock, NULL);
    ASSERT_EQ(0, rc, "pthread_mutex_init failed");

    pthread_t threads[MUTEX_NUM_THREADS];
    int created = 0;
    int create_err = 0;

    for (int i = 0; i < MUTEX_NUM_THREADS; i++) {
        rc = pthread_create(&threads[i], NULL, mutex_worker, &ctx);
        if (rc != 0) {
            create_err = rc;
            break;
        }
        created++;
    }

    for (int i = 0; i < created; i++) {
        (void)pthread_join(threads[i], NULL);
    }
    (void)pthread_mutex_destroy(&ctx.lock);

    ASSERT_EQ(0, create_err, "pthread_create failed");
    ASSERT_EQ((long)MUTEX_NUM_THREADS * MUTEX_ITERS_PER_THR,
              ctx.counter,
              "shared counter mismatch under mutex contention");

    PASS_WITH_INFO("threads=%d, iterations=%ld, final_counter=%ld",
                   MUTEX_NUM_THREADS, MUTEX_ITERS_PER_THR, ctx.counter);
}

/* --------------------------------------------------------------------------
 * 2. Condvar producer-consumer test with circular buffer
 * -------------------------------------------------------------------------- */

#define PC_RING_CAPACITY 16
#define PC_TOTAL_ITEMS   4096

typedef struct {
    int             buffer[PC_RING_CAPACITY];
    size_t          head;
    size_t          tail;
    size_t          count;
    int             total_items;
    int             consumed_count;
    int             order_errors;
    pthread_mutex_t lock;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
} pc_ring_t;

static void *pc_producer(void *arg) {
    pc_ring_t *ring = (pc_ring_t *)arg;

    for (int i = 0; i < ring->total_items; i++) {
        pthread_mutex_lock(&ring->lock);
        while (ring->count == PC_RING_CAPACITY) {
            pthread_cond_wait(&ring->not_full, &ring->lock);
        }
        ring->buffer[ring->tail] = i;
        ring->tail = (ring->tail + 1U) % PC_RING_CAPACITY;
        ring->count++;
        pthread_cond_signal(&ring->not_empty);
        pthread_mutex_unlock(&ring->lock);
    }

    return NULL;
}

static void *pc_consumer(void *arg) {
    pc_ring_t *ring = (pc_ring_t *)arg;

    for (int expected = 0; expected < ring->total_items; expected++) {
        pthread_mutex_lock(&ring->lock);
        while (ring->count == 0U) {
            pthread_cond_wait(&ring->not_empty, &ring->lock);
        }
        int item = ring->buffer[ring->head];
        ring->head = (ring->head + 1U) % PC_RING_CAPACITY;
        ring->count--;
        pthread_cond_signal(&ring->not_full);
        pthread_mutex_unlock(&ring->lock);

        if (item != expected) {
            ring->order_errors++;
        }
        ring->consumed_count++;
    }

    return NULL;
}

int test_condvar_producer_consumer(char *detail_buf, size_t detail_size) {
    pc_ring_t ring;
    memset(&ring, 0, sizeof(ring));
    ring.total_items = PC_TOTAL_ITEMS;

    ASSERT_EQ(0, pthread_mutex_init(&ring.lock, NULL),
              "pthread_mutex_init failed");
    if (pthread_cond_init(&ring.not_full, NULL) != 0) {
        (void)pthread_mutex_destroy(&ring.lock);
        ASSERT_TRUE(0, "pthread_cond_init(not_full) failed");
    }
    if (pthread_cond_init(&ring.not_empty, NULL) != 0) {
        (void)pthread_cond_destroy(&ring.not_full);
        (void)pthread_mutex_destroy(&ring.lock);
        ASSERT_TRUE(0, "pthread_cond_init(not_empty) failed");
    }

    pthread_t prod_thr;
    pthread_t cons_thr;
    int prod_ok = (pthread_create(&prod_thr, NULL, pc_producer, &ring) == 0);
    int cons_ok = 0;

    if (prod_ok) {
        cons_ok = (pthread_create(&cons_thr, NULL, pc_consumer, &ring) == 0);
    }

    if (prod_ok && !cons_ok) {
        /* Drain buffer in main thread so producer can exit cleanly */
        (void)pc_consumer(&ring);
    }

    if (prod_ok) {
        (void)pthread_join(prod_thr, NULL);
    }
    if (cons_ok) {
        (void)pthread_join(cons_thr, NULL);
    }

    (void)pthread_cond_destroy(&ring.not_empty);
    (void)pthread_cond_destroy(&ring.not_full);
    (void)pthread_mutex_destroy(&ring.lock);

    ASSERT_TRUE(prod_ok && cons_ok, "failed to create producer/consumer threads");
    ASSERT_EQ(PC_TOTAL_ITEMS, ring.consumed_count,
              "consumer did not receive expected item count");
    ASSERT_EQ(0, ring.order_errors,
              "items were delivered out of order");

    PASS_WITH_INFO("items=%d, ring_capacity=%d, order_errors=0",
                   PC_TOTAL_ITEMS, PC_RING_CAPACITY);
}

/* --------------------------------------------------------------------------
 * 3. Context switch / thread wakeup latency test (pipe ping-pong)
 * -------------------------------------------------------------------------- */

#define CSW_ROUNDS 1000

typedef struct {
    int read_fd;
    int write_fd;
    int rounds;
    int error;
} pingpong_ctx_t;

static int write_byte(int fd, uint8_t b) {
    for (;;) {
        ssize_t n = write(fd, &b, 1);
        if (n == 1) {
            return 0;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }
}

static int read_byte(int fd, uint8_t *b) {
    for (;;) {
        ssize_t n = read(fd, b, 1);
        if (n == 1) {
            return 0;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }
}

static void *pingpong_worker(void *arg) {
    pingpong_ctx_t *ctx = (pingpong_ctx_t *)arg;
    uint8_t token = 0;

    for (int i = 0; i < ctx->rounds; i++) {
        if (read_byte(ctx->read_fd, &token) != 0) {
            ctx->error = 1;
            return NULL;
        }
        if (write_byte(ctx->write_fd, token) != 0) {
            ctx->error = 2;
            return NULL;
        }
    }

    return NULL;
}

int test_context_switch_latency(char *detail_buf, size_t detail_size) {
    int p2w[2] = { -1, -1 };
    int w2p[2] = { -1, -1 };

    ASSERT_EQ(0, pipe(p2w), "pipe(p2w) failed");
    if (pipe(w2p) != 0) {
        (void)close(p2w[0]);
        (void)close(p2w[1]);
        ASSERT_TRUE(0, "pipe(w2p) failed");
    }

    pingpong_ctx_t ctx;
    ctx.read_fd = p2w[0];
    ctx.write_fd = w2p[1];
    ctx.rounds = CSW_ROUNDS;
    ctx.error = 0;

    pthread_t worker;
    int thr_rc = pthread_create(&worker, NULL, pingpong_worker, &ctx);
    if (thr_rc != 0) {
        (void)close(p2w[0]);
        (void)close(p2w[1]);
        (void)close(w2p[0]);
        (void)close(w2p[1]);
        ASSERT_EQ(0, thr_rc, "pthread_create failed");
    }

    int io_err = 0;
    uint8_t token = 0x5AU;
    uint8_t reply = 0;

    uint64_t start_ns = os_test_now_ns();
    for (int i = 0; i < CSW_ROUNDS; i++) {
        if (write_byte(p2w[1], token) != 0 || read_byte(w2p[0], &reply) != 0) {
            io_err = 1;
            break;
        }
    }
    uint64_t end_ns = os_test_now_ns();

    /* Close write end first so worker unblocks if an error occurred early */
    (void)close(p2w[1]);
    (void)pthread_join(worker, NULL);
    (void)close(p2w[0]);
    (void)close(w2p[0]);
    (void)close(w2p[1]);

    ASSERT_EQ(0, io_err, "pipe ping-pong I/O error in main thread");
    ASSERT_EQ(0, ctx.error, "pipe ping-pong I/O error in worker thread");
    ASSERT_EQ(token, reply, "ping-pong token mismatch");
    ASSERT_TRUE(end_ns > start_ns, "measured context switch time must be positive");

    uint64_t total_switches = (uint64_t)CSW_ROUNDS * 2ULL;
    double avg_us = (double)(end_ns - start_ns) / (double)total_switches / 1000.0;

    PASS_WITH_INFO("rounds=%d, switches=%" PRIu64 ", avg_wakeup_us=%.2f",
                   CSW_ROUNDS, total_switches, avg_us);
}
