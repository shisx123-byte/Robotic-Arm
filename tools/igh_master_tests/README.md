# IgH master DC test programs

These small IgH userspace tests were used on the RK3588/NanoPC-T6 master to
exercise the EtherKit RZ/N2L slave with DC Sync0.

## Files

- `etherkit_dc200us_test.c`: 200 us cycle test.
- `etherkit_dc500us_test.c`: 500 us cycle test.

Both programs accept:

```sh
./etherkit_dc200us_test [seconds] [sync0_shift_ns]
./etherkit_dc500us_test [seconds] [sync0_shift_ns]
```

The current best 200 us setting observed during optimization was:

```sh
sudo ./etherkit_dc200us_test 300 -50000
```

## Build on the master

```sh
gcc -O2 -Wall -o etherkit_dc200us_test etherkit_dc200us_test.c -lethercat
gcc -O2 -Wall -o etherkit_dc500us_test etherkit_dc500us_test.c -lethercat
```

The test programs pin themselves to CPU7, request `SCHED_FIFO` priority 99,
lock memory with `mlockall()`, and print cycle/latency/execution statistics.
