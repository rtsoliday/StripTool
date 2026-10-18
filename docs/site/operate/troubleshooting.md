# Troubleshooting

| Symptom | Check |
| --- | --- |
| Build cannot find EPICS Base | Set `EPICS_BASE` to a built Base tree containing `include/cadef.h` and host libraries. |
| Build cannot find Qt | Install Qt Widgets, Network, and PrintSupport development files and the matching `moc`/`rcc` tools, or choose an installed major with `QT_VERSION=5` or `6`. |
| PV remains disconnected | Verify the PV with site Channel Access tools, the `EPICS_CA_*` address settings, and the network path. |
| Startup `.stp` file is not found | Use an absolute path or check `STRIP_FILE_SEARCH_PATH`. A name containing a directory component is not searched along that path. |
| Historical Range fails | Check the Archiver endpoint and `QTSTRIPTOOL_ARCHIVER_URL`. Local `CPU_Usage` has no archive data. |
| Help should open site documentation | Set `STRIP_HELP_PATH` to a local file or URL; **Help > Online Manual** then opens it. **Help Topics** (F1) always shows the guide built into the application. |
| Print or snapshot differs from Motif | Check the active Qt style, platform fonts, DPI scale, and print backend. |

If a curve's Controls status shows **Stale**, a quiet Channel Access PV may simply have no new monitor event. An actual disconnect is different: the trace ends and a gap appears. An **Error** status from a failed read, such as revoked read access, returns to **Live** once monitor updates resume. See [configure curves](./curves) and [historical data](./history).
