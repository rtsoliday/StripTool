# Environment variables

| Variable | Current Qt behavior |
| --- | --- |
| `EPICS_CA_ADDR_LIST`, `EPICS_CA_AUTO_ADDR_LIST`, and other EPICS CA variables | Used by EPICS Channel Access for PV discovery and connections. |
| `STRIP_FILE_SEARCH_PATH` | Platform path-list used to find a bare startup `.stp` filename. |
| `STRIP_HELP_PATH` | Local file or URL opened by **Help > Help**. If it cannot open, the application shows brief built-in guidance. |
| `QTSTRIPTOOL_ARCHIVER_URL` | Overrides the EPICS Archiver Appliance retrieval root or `data/getData.json` endpoint. |
| `QT_QPA_PLATFORM`, `QT_STYLE_OVERRIDE`, and other standard Qt variables | Interpreted by Qt for platform and appearance behavior. |

The default Archiver retrieval root is `http://asddtn03.aps4.anl.gov:17668/retrieval`. Set an override before launching Qt StripTool when your site uses another service. See [historical data](../operate/history).

Legacy X resource and printer environment variables are not consumed by the Qt program. See [compatibility](../understand/compatibility) for the known differences.
