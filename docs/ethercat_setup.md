# EtherCAT master/slave setup notes

This document records the current known setup used for the EtherKit RZ/N2L
slave optimization and IgH master testing.

## Master

- Board: NanoPC-T6-LTS / RK3588.
- Login used during testing: `pi@192.168.31.50`.
- OS: Ubuntu 22.04.4 based system.
- Kernel: `6.1.141`.
- Kernel header package used for IgH build:
  `/opt/archives/linux-headers-6.1.141_6.1.141-16_arm64.deb`.
- IgH master branch/build: built locally against the official `6.1.141`
  headers and Module.symvers.
- EtherCAT service: `ethercat.service`.
- RT tuning service: `ethercat-rt-tune.service`.

### Master NICs

- `eth0`
  - MAC: `22:e2:78:a8:06:78`
  - Link observed as 1000 Mbps full duplex.
  - Driver: `r8125`.
- `eth1`
  - MAC: `26:e2:78:a8:06:78`
  - Link observed as 100 Mbps full duplex when connected to the EtherCAT
    slave. This is expected for 100BASE-TX EtherCAT slave links.
  - Driver: `r8125`.
  - Used by IgH as the EtherCAT interface.

### IgH configuration

`/usr/local/etc/sysconfig/ethercat`:

```sh
MASTER0_DEVICE="26:e2:78:a8:06:78"
DEVICE_MODULES="generic"
UPDOWN_INTERFACES="eth1"
```

`/etc/modprobe.d/ec_master_run_cpu.conf`:

```sh
options ec_master run_on_cpu=7
```

RT tuning currently used:

- CPU governor set to `performance`.
- `eth1` RPS disabled: `rx*/rps_cpus = 00`.
- `eth1` XPS pinned toward CPU7: `tx*/xps_cpus = 80`.

### IgH modules

Installed modules:

- `ec_master`
- `ec_generic`

Module location:

```sh
/lib/modules/6.1.141/ethercat/
```

Observed vermagic:

```text
6.1.141 SMP mod_unload modversions aarch64
```

## Slave

- Target project:
  `C:\RT-ThreadStudio\workspace\etherkit_ethercat_coe`
- MCU/ESC: Renesas RZ/N2L with integrated EtherCAT slave controller.
- RTOS: RT-Thread.
- EtherCAT stack: Beckhoff SSC-based CoE slave through Renesas FSP
  `rm_ethercat_ssc_port`.
- Slave name observed by IgH:
  `EtherKit Simple I/O Slave 2port`.
- Vendor ID used in tests: `0x00000766`.
- Product ID used in tests: `0x00000912`.

### Flashing

J-Link path:

```text
C:\RT-ThreadStudio\repo\Extract\Debugger_Support_Packages\SEGGER\J-Link\v7.50a\JLink.exe
```

Device:

```text
R9A07G084M04
```

Typical flash command:

```powershell
& 'C:\RT-ThreadStudio\repo\Extract\Debugger_Support_Packages\SEGGER\J-Link\v7.50a\JLink.exe' `
  -device R9A07G084M04 `
  -if SWD `
  -speed 1000 `
  -CommanderScript C:\RT-ThreadStudio\workspace\etherkit_ethercat_coe\download_rtthread.jlink
```

## Master test programs

Stored in this repository:

```text
tools/igh_master_tests/
```

Files:

- `etherkit_dc200us_test.c`
- `etherkit_dc500us_test.c`

Build on master:

```sh
gcc -O2 -Wall -o etherkit_dc200us_test etherkit_dc200us_test.c -lethercat
gcc -O2 -Wall -o etherkit_dc500us_test etherkit_dc500us_test.c -lethercat
```

Important runtime behavior:

- CPU affinity: CPU7.
- Scheduler: `SCHED_FIFO`, priority 99.
- Memory locked with `mlockall()`.
- Uses `ecrt_master_sync_reference_clock_to(master, next_ns)`.
- Optional arguments:
  - test seconds
  - Sync0 shift in ns

Known useful 200 us test command:

```sh
sudo ./etherkit_dc200us_test 300 -50000
```

## Current optimization state

### Slave-side changes already made

- Removed GPIO access from the high-frequency SYNC0/PDO path.
- Added shadow variables for DIP switch, LED, and KEY2 state.
- Moved low-priority board IO to `APPL_LowPriorityIo()`.
- Simplified process data mapping for the simple I/O test.
- Made 0x1603 and 0x1A03 PDO mapping entries writable in PREOP to avoid
  IgH SDO aborts during mapping.
- Removed startup/cyclic `rt_kprintf()` noise from hot paths.
- Disabled RT-Thread debug/FinSH/system workqueue/overflow-check paths for
  the timing test build.
- Raised EtherCAT thread priority to 5.
- Lowered main thread priority to 20.
- FSP interrupt priority tuning:
  - ESC CAT: 6
  - SYNC0: 4
  - SYNC1: 5
  - timer: 8
  - UART: 14
  - CAN: 10
- `MIN_PD_CYCLE_TIME` currently set to `200000U` for 200 us class testing.

### Master-side changes already made

- IgH rebuilt against the correct official `6.1.141` headers.
- IgH configured to use `eth1`.
- `ec_master` run CPU set to CPU7.
- Basic NIC/CPU RT tuning added through `ethercat-rt-tune.service`.

## Known test results

Best known 200 us Sync0 shift:

```text
-50000 ns
```

Before the latest slave RT-Thread thread trimming, a 5-minute 200 us test with
`shift=-50000 ns` completed without userspace cycle overruns:

```text
cycles=1499999 expected=1500000
overruns=0
```

Kernel log counters from that run included a small number of unmatched/skipped
datagrams and WC0 events:

```text
UNMATCHED=6
SKIPPED=4
WC0=2
```

After the latest slave trimming, a 60-second 200 us run showed:

```text
cycles=299999 expected=300000
overruns=0
SDO_ABORT=0
UNMATCHED=1
SKIPPED=1
WC0=0
```

There can still be an IgH log message during OP transition:

```text
Slave did not sync after 5000 ms
```

This needs more investigation, but the later short run had no WC0 samples
after OP was established.

## F407/LAN9252 comparison

Reference project:

```text
C:\RT-ThreadStudio\workspace\ESC_407SPI_SSC_v8_ORDERED_DC
```

Key difference observed:

- The F407/LAN9252 project has an `ORDERED_DC` event design.
- AL/SM2, SYNC0, and SYNC1 interrupts only enqueue timestamped events.
- A highest-priority SSC thread drains the queue and calls
  `PDI_Isr()`, `Sync0_Isr()`, and `Sync1_Isr()` in real arrival order.
- The RZ/N2L FSP port currently calls those ISR handlers directly from the
  hardware interrupt path.

Current conclusion:

The remaining 200 us instability is more likely caused by event ordering,
interrupt/service latency, or master timing margin than by raw RZ/N2L ESC
memory access bandwidth. The next major slave-side optimization should be to
port the F407-style ordered-event mechanism to the RZ/N2L FSP port.
