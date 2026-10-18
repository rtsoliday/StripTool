# Qt StripTool README screenshots

Captured from the running application on October 18, 2026:

- Qt StripTool 1.0.0, repository revision `72c874c`.
- macOS 26.6.2, Apple Silicon, Qt 6.11.2, Fusion style, dark desktop theme.
- Platform-default font and display scaling; JPEG captures are 1200 x 762
  pixels for the graph and 1280 x 512 pixels for Controls.
- The configuration is [beam-demo.stp](beam-demo.stp). The graph uses auto-scale;
  selecting a legend entry chooses the displayed Y axis. The legend reports
  each curve's latest value and plotted limits, including in a historical view.

These are application screenshots, not mockups. All values are illustrative:
`DEMO:BeamCurrent` (mA), `DEMO:RFPower` (kW), and `DEMO:Vacuum` (nTorr) came from
a temporary local soft IOC and a synthetic Archiver Appliance-compatible HTTP
fixture. The fixture simulated an RF trip, a vacuum-pressure spike, and beam
recovery. No operational PVs or archive services were used.

The IOC listened on `127.0.0.1:49229`, with automatic Channel Access discovery
disabled and `EPICS_CA_ADDR_LIST=127.0.0.1:49229`. The archive override was
`QTSTRIPTOOL_ARCHIVER_URL=http://127.0.0.1:49231/retrieval`.
Use equivalent isolated services before opening the demo configuration; the
configuration alone does not generate data or override network settings.

| Screenshot | Application state |
| --- | --- |
| `graph-overview.jpg` | Fifteen-minute historical range, three curves, auto-scale on |
| `controls-curves.jpg` | All ten curve slots visible; three local PVs receiving live updates |
| `graph-annotations.jpg` | Time zoom applied; notes added through the Plot Annotation dialog |

The two annotations are runtime graph state and are not stored in the `.stp`
configuration.
