# Command-line options

The Qt StripTool application syntax is:

```text
qtstriptool [--help] [--version] [configuration.stp]
```

| Argument | Behavior |
| --- | --- |
| `--help` | Print usage and exit. |
| `--version` | Print version information and exit. |
| `configuration.stp` | Open one configuration file; a bare name is searched in the current directory and then `STRIP_FILE_SEARCH_PATH`. |

Use at most one configuration file. Other application-specific switches are not defined. Qt consumes its standard options, such as `-style adwaita`, before the application sees its arguments. The default style is Fusion.

If an explicit file cannot be loaded, Qt StripTool tries `StripTool.stp` in the current directory and otherwise starts with compiled defaults. An absolute path or path containing a directory component is used directly; it is not searched along `STRIP_FILE_SEARCH_PATH`.

```sh
qtstriptool beam.stp
qtstriptool -style adwaita beam.stp
```

For variable behavior, see [environment variables](./environment). For file contents, see [configuration files](./configuration).
