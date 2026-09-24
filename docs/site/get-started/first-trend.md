# Plot your first PV

You need a built `qtstriptool` and a reachable EPICS Channel Access process variable. Check your site's `EPICS_CA_ADDR_LIST` and `EPICS_CA_AUTO_ADDR_LIST` settings if the PV cannot connect.

## Open the graph and Controls

Run `qtstriptool` without a configuration file. The graph and Controls windows open together. Later, use **Window > Show Controls** in the graph to reopen Controls.

## Connect a curve

1. Enter a PV name in the **PV** field and select **Connect**, or enter the name in an unused curve row.
2. Keep **Plot** enabled. Select **Modify** to apply other row edits, or press Return in that row's Name, Minimum, or Maximum field.
3. Watch the graph and the Controls row. The graph shows Channel Access monitor updates as they arrive; the Controls row shows connection and stale status.

If the PV is quiet, its last value can remain visible as a step line. A **Stale** status does not by itself remove the plotted value. An actual disconnect creates a gap.

## Explore and save

Use **View > Auto Scale** to fit visible data. Use **View > Reset View** to return to configured curve limits. The time axis shows relative minutes above local clock time. The rightmost relative label is zero.

Choose **File > Save As** to write a `.stp` configuration. Open it later with `qtstriptool configuration.stp` or **File > Open**. The file holds supported configuration settings; runtime annotations and sampled data are not saved in it.

Continue with [curve controls](../operate/curves), [graph navigation](../operate/graph), or [troubleshooting](../operate/troubleshooting).
