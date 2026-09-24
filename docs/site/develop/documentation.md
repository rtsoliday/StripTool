# Maintain these docs

The documentation is a VitePress site with Markdown source under `docs/site/`. It uses the same layout, typography, search, affiliation links, responsive behavior, and color theme as the QtEDM and QtALH documentation sites. The static HTML output is `docs/html/`.

## Build and view locally

Use Node.js 22 or newer, npm, and Python 3. Qt and EPICS are not required for the website build.

```sh
make docs
python3 -m http.server 8000 --directory docs/html --bind 127.0.0.1
```

Open `http://127.0.0.1:8000/`. Serve the directory over HTTP so the site modules and local search load correctly.

## Host in a subdirectory

Build with the exact URL prefix, including leading and trailing slashes:

```sh
make docs DOCS_BASE=/manuals/QtStripTool/
```

Copy the complete contents of `docs/html/` to that server directory. A root build copied into a subdirectory will have broken asset and navigation URLs. Rebuild when the prefix changes. Use `make docs-clean` to remove generated HTML without deleting authored pages.

## Edit and verify

Edit task pages under `docs/site/`, navigation in `.vitepress/config.mts`, and shared presentation in `.vitepress/theme/`. Keep the original Markdown guides under `docs/` aligned when behavior changes. `make docs` checks generated page titles, local links, and anchors. Run `python3 scripts/build-docs.py --check-only --base /manuals/QtStripTool/` to validate an existing build made with that prefix.
