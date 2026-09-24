# Appearance

Qt StripTool uses Qt's Fusion style by default, providing compact controls across desktop environments. Choose another installed style with a standard Qt option, for example:

```sh
qtstriptool -style adwaita configuration.stp
```

The Controls **Appearance** tab sets foreground, background, grid color, X/Y grid density, colored Y-axis labels, and graph line width. Each curve has its own color. These values round-trip through `.stp` where the format defines them.

Qt uses native/platform fonts, dialogs, keyboard focus, DPI handling, and print integration. Exact Motif pixels are not a compatibility requirement. Visual review should check that values, scales, time labels, curve identity, focus, and required controls remain legible and usable.

The [appearance and visual review record](https://github.com/rtsoliday/StripTool/blob/master/docs/qtstriptool-appearance.md) describes the fixture and human review checklist.
