# Qt StripTool performance and soak testing

Performance acceptance combines a broad automated regression guard with
measurements on representative deployment hardware. The repository tests are
not production benchmarks and should not be quoted as operational capacity.

## Automated regression target

Run:

```sh
make test-qtstriptool-performance
```

The current target fills ten curve buffers with 10,000 samples each, renders a
large range, and repeatedly constructs and destroys application windows. Its
ten-second limits are intentionally generous so shared CI load does not create
noise; they catch severe algorithmic or lifecycle regressions rather than set
an APS latency objective.

On the local Linux x86_64/Qt 5 verification on 2026-09-17, the buffer/render
and repeated window lifecycle performance checks passed as part of the
non-IOC suite. This records a pass/fail, not stable timing numbers. Qt 6,
macOS, Windows, production archive latency, and a 24-hour run were not measured.

## Release-candidate procedure

For a reproducible local side-by-side baseline, run
`make benchmark-resource-usage`. The harness and workload are documented in
`benchmarks/resource-usage/README.md`; it records raw CPU/RSS/PSS time series
and JSON/Markdown summaries for four fast, three slow, and three constant PVs.

Use the same release build, `.stp` file, CA environment, window size, desktop
style, and host for comparisons. Record OS/kernel, CPU, memory, display/DPI,
Qt and EPICS versions, compiler/options, IOC/archive sources, sample/refresh
settings, and measurement tools.

1. **Idle:** start with no curves for ten minutes. Record average/peak CPU,
   resident memory, wakeups if available, and graph refresh behavior.
2. **High rate:** configure ten representative high-update-rate PVs and an
   agreed refresh interval for 30 minutes. Channel Access monitor updates are
   recorded as received; the sample interval applies only to `CPU_Usage`.
   Record CPU, memory, callback/sample loss, repaint latency, UI responsiveness,
   and disconnect/reconnect behavior.
3. **Long duration:** run the representative operational configuration for at
   least 24 hours. Sample resident memory and buffer sizes periodically. There
   must be no sustained unbounded growth; investigate step changes and retain
   logs.
4. **Large history:** after a production archive provider is selected, request
   the agreed large range for all ten curves. Record request completion,
   cancellation, first paint, pan/zoom latency, memory peak, and the live/history
   join. This gate cannot pass with `NoHistoryProvider`.
5. **Startup/shutdown:** measure at least 20 clean launches and exits, including
   a configuration with ten active subscriptions. Report median and worst case
   and verify that CA subscriptions and history requests are released cleanly.
6. **Side-by-side:** repeat applicable scenarios with Motif `StripTool` on the
   same host. Explain material differences instead of imposing a universal
   ratio across unlike platforms.

The site owner must set numeric CPU, latency, and startup thresholds before
the release candidate is measured. Minimum acceptance also requires no lost
required data, no UI stall that prevents control, bounded configured buffers,
clean shutdown, and no unbounded memory trend.

## Interpreting results

Acquisition and display refresh are independent: increasing repaint frequency
can raise GUI cost without changing Channel Access monitor delivery. Buffer
capacity, time span, visible density, antialiasing, desktop scaling, and archive
result size also affect measurements. Preserve those inputs with each result.

If a regression appears, reproduce it with the split core/UI/performance
targets, then profile the same scenario. Do not loosen the broad automated
limit to hide a functional hang, and do not treat one fast developer-host run
as a substitute for the release soak record.
