#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>

#include <ecrt.h>

#define NSEC_PER_SEC 1000000000LL
#define PERIOD_NS 200000LL
#define DEFAULT_SECONDS 10

#define VENDOR_ID  0x00000766
#define PRODUCT_ID 0x00000912

static volatile int run = 1;

static ec_master_t *master;
static ec_domain_t *domain;
static uint8_t *domain_pd;

static ec_master_state_t master_state;
static ec_domain_state_t domain_state;

static unsigned int off_led0, off_led1, off_led2, off_key2;
static unsigned int bit_led0, bit_led1, bit_led2, bit_key2;

static ec_pdo_entry_reg_t domain_regs[] = {
    {0, 0, VENDOR_ID, PRODUCT_ID, 0x7001, 0x00, &off_led0, &bit_led0},
    {0, 0, VENDOR_ID, PRODUCT_ID, 0x7002, 0x00, &off_led1, &bit_led1},
    {0, 0, VENDOR_ID, PRODUCT_ID, 0x7003, 0x00, &off_led2, &bit_led2},
    {0, 0, VENDOR_ID, PRODUCT_ID, 0x6001, 0x00, &off_key2, &bit_key2},
    {}
};

static void prefault_stack(void)
{
    volatile char stack[64 * 1024];
    memset((void *)stack, 0, sizeof(stack));
}

static void tune_realtime(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
    }

    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall");
    }
    prefault_stack();

    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        perror("sched_setscheduler");
    }

    printf("rt: cpu=%d policy=SCHED_FIFO priority=%d\n",
           cpu, param.sched_priority);
}

static int64_t ts_to_ns(const struct timespec *ts)
{
    return (int64_t)ts->tv_sec * NSEC_PER_SEC + ts->tv_nsec;
}

static struct timespec ns_to_ts(int64_t ns)
{
    struct timespec ts;
    ts.tv_sec = ns / NSEC_PER_SEC;
    ts.tv_nsec = ns % NSEC_PER_SEC;
    return ts;
}

static void on_signal(int sig)
{
    (void)sig;
    run = 0;
}

static void print_states(void)
{
    ec_master_state_t ms;
    ec_domain_state_t ds;

    ecrt_master_state(master, &ms);
    ecrt_domain_state(domain, &ds);

    printf("state: slaves=%u al=0x%02x link=%u wc=%u wc_state=%u\n",
           ms.slaves_responding, ms.al_states, ms.link_up,
           ds.working_counter, ds.wc_state);

    master_state = ms;
    domain_state = ds;
}

int main(int argc, char **argv)
{
    int seconds = DEFAULT_SECONDS;
    int32_t sync0_shift_ns = 0;
    if (argc > 1) {
        seconds = atoi(argv[1]);
        if (seconds <= 0) {
            seconds = DEFAULT_SECONDS;
        }
    }
    if (argc > 2) {
        sync0_shift_ns = (int32_t)strtol(argv[2], NULL, 0);
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    tune_realtime(7);

    master = ecrt_request_master(0);
    if (!master) {
        fprintf(stderr, "failed to request master 0\n");
        return 1;
    }

    domain = ecrt_master_create_domain(master);
    if (!domain) {
        fprintf(stderr, "failed to create domain\n");
        return 1;
    }

    ec_slave_config_t *sc = ecrt_master_slave_config(master, 0, 0,
                                                     VENDOR_ID, PRODUCT_ID);
    if (!sc) {
        fprintf(stderr, "failed to get EtherKit slave config\n");
        return 1;
    }

    if (ecrt_domain_reg_pdo_entry_list(domain, domain_regs)) {
        fprintf(stderr, "failed to register PDO entries\n");
        return 1;
    }

    ecrt_slave_config_dc(sc, 0x0700, PERIOD_NS, sync0_shift_ns, 0, 0);

    printf("activating master, DC SYNC0 period=%lld ns shift=%d ns\n",
           (long long)PERIOD_NS, sync0_shift_ns);
    if (ecrt_master_activate(master)) {
        fprintf(stderr, "failed to activate master\n");
        return 1;
    }

    domain_pd = ecrt_domain_data(domain);
    if (!domain_pd) {
        fprintf(stderr, "failed to get domain data\n");
        return 1;
    }

    printf("offsets: led0=%u.%u led1=%u.%u led2=%u.%u key2=%u.%u\n",
           off_led0, bit_led0, off_led1, bit_led1, off_led2, bit_led2,
           off_key2, bit_key2);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t next_ns = ts_to_ns(&now) + PERIOD_NS;
    int64_t end_ns = ts_to_ns(&now) + (int64_t)seconds * NSEC_PER_SEC;
    int64_t last_start_ns = 0;
    int64_t lat_min = INT64_MAX, lat_max = INT64_MIN;
    int64_t per_min = INT64_MAX, per_max = INT64_MIN;
    int64_t exec_min = INT64_MAX, exec_max = INT64_MIN;
    uint64_t cycle = 0;
    uint64_t overruns = 0, wc0_seen = 0;
    uint64_t win_overruns = 0, win_wc0_seen = 0;

    while (run) {
        struct timespec wake = ns_to_ts(next_ns);
        int rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, NULL);
        if (rc && rc != EINTR) {
            errno = rc;
            perror("clock_nanosleep");
        }

        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        int64_t start_ns = ts_to_ns(&start);

        int64_t latency = start_ns - next_ns;
        if (latency > PERIOD_NS) {
            overruns++;
            win_overruns++;
        }
        if (latency < lat_min) lat_min = latency;
        if (latency > lat_max) lat_max = latency;
        if (last_start_ns) {
            int64_t period = start_ns - last_start_ns;
            if (period < per_min) per_min = period;
            if (period > per_max) per_max = period;
        }
        last_start_ns = start_ns;

        ecrt_master_application_time(master, (uint64_t)next_ns);
        ecrt_master_receive(master);
        ecrt_domain_process(domain);
        ecrt_domain_state(domain, &domain_state);

        int led = (cycle / 2500) & 1;
        EC_WRITE_BIT(domain_pd + off_led0, bit_led0, led);
        EC_WRITE_BIT(domain_pd + off_led1, bit_led1, !led);
        EC_WRITE_BIT(domain_pd + off_led2, bit_led2, led);

        if ((cycle % 10) == 0) {
            ecrt_master_sync_reference_clock_to(master, (uint64_t)next_ns);
        }
        ecrt_master_sync_slave_clocks(master);

        ecrt_domain_queue(domain);
        ecrt_master_send(master);

        struct timespec done;
        clock_gettime(CLOCK_MONOTONIC, &done);
        int64_t exec = ts_to_ns(&done) - start_ns;
        if (exec < exec_min) exec_min = exec;
        if (exec > exec_max) exec_max = exec;
        if (domain_state.working_counter == 0) {
            wc0_seen++;
            win_wc0_seen++;
        }

        cycle++;
        if ((cycle % 5000) == 0) {
            printf("t=%llums cycles=%llu key2=%u period=%lld..%lld ns latency=%lld..%lld ns exec=%lld..%lld ns overruns=%llu wc0=%llu\n",
                   (long long)(cycle * PERIOD_NS / 1000000),
                   (unsigned long long)cycle,
                   EC_READ_BIT(domain_pd + off_key2, bit_key2),
                   (long long)per_min, (long long)per_max,
                   (long long)lat_min, (long long)lat_max,
                   (long long)exec_min, (long long)exec_max,
                   (unsigned long long)win_overruns,
                   (unsigned long long)win_wc0_seen);
            print_states();
            lat_min = INT64_MAX; lat_max = INT64_MIN;
            per_min = INT64_MAX; per_max = INT64_MIN;
            exec_min = INT64_MAX; exec_max = INT64_MIN;
            win_overruns = 0; win_wc0_seen = 0;
        }

        next_ns += PERIOD_NS;
        if (next_ns >= end_ns) {
            break;
        }
    }

    EC_WRITE_BIT(domain_pd + off_led0, bit_led0, 0);
    EC_WRITE_BIT(domain_pd + off_led1, bit_led1, 0);
    EC_WRITE_BIT(domain_pd + off_led2, bit_led2, 0);
    ecrt_domain_queue(domain);
    ecrt_master_send(master);

    printf("done cycles=%llu expected=%lld\n",
           (unsigned long long)cycle,
           (long long)((int64_t)seconds * NSEC_PER_SEC / PERIOD_NS));
    printf("summary: overruns=%llu wc0_samples=%llu\n",
           (unsigned long long)overruns,
           (unsigned long long)wc0_seen);
    print_states();

    ecrt_release_master(master);
    return 0;
}
