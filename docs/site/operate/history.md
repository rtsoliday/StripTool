# Historical data

Choose **View > Historical Range** to request a time range from an EPICS Archiver Appliance. The dialog starts with the graph's current visible range. When you select **OK**, the graph moves to the chosen range, auto-scroll turns off, and the archived samples join the live data as they arrive. Requests run in the background, and a new Historical Range request replaces any that are still pending.

<figure class="doc-shot doc-shot--compact">
  <img src="../assets/screenshots/history-range.png" alt="Historical Time Range dialog with From and To date and time fields and OK and Cancel buttons" />
  <figcaption>Choose the start and end time before requesting archived samples.</figcaption>
</figure>

The default retrieval root is `http://asddtn03.aps4.anl.gov:17668/retrieval`. Set `QTSTRIPTOOL_ARCHIVER_URL` before launch to use another retrieval root or a `data/getData.json` endpoint. Requests use the Archiver's `lastSample` reduction, with bins of at least one second sized so that each PV returns at most about 10,000 points. Numeric archived samples retain archive timestamps, status, and severity; archived strings and waveforms are ignored.

When a Channel Access PV is added, Qt StripTool also attempts a background request for the configured **History length** in the Timing tab. The default is five minutes. Set a different startup window with `qtstriptool -history=3600 -sampleCount=7200`, or set it in a configuration file. The sample count bounds the live buffer independently of archived samples. If that backfill is unavailable, live plotting continues. Zooming out or panning earlier than the retrieved history automatically requests the newly exposed span once the view settles, and joins it to the plotted data. These extension requests are also silent, and a span that failed is not requested again. An explicit Historical Range request reports errors instead. Once live updates exceed the sample count, the oldest live samples are discarded and the graph leaves a gap between the archived data and the oldest retained live sample rather than holding the last archived value across it. That span is not requested automatically; use **View > Historical Range** to retrieve it. The local `CPU_Usage` curve is live only and is not sent to the Archiver.

See [environment variables](../reference/environment) for the override and [compatibility](../understand/compatibility) for provider differences from Motif StripTool.
