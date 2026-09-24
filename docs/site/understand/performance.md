# Performance and soak testing

The automated regression target exercises ten curve buffers with 10,000 samples each, a large render range, and repeated window creation and destruction:

```sh
make test-qtstriptool-performance
```

Its time limits are broad regression guards, not production capacity claims. For release acceptance, compare Qt and Motif on the same host and `.stp` file, with the same PV workload, window size, style, and Channel Access settings. Record CPU and memory, update delivery, repaint latency, reconnects, large archive requests, startup/shutdown, and a long run without unbounded memory growth.

The [performance and soak procedure](https://github.com/rtsoliday/StripTool/blob/master/docs/qtstriptool-performance.md) gives the full measurement plan and known evidence. Site owners should set numeric acceptance thresholds before measuring a release candidate.
