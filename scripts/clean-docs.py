#!/usr/bin/env python3
"""Remove generated documentation while preserving authored pages."""

import argparse
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dependencies', action='store_true')
args = parser.parse_args()
paths = [root / 'docs/html', root / 'docs/site/dist',
         root / 'docs/site/.vitepress/cache']
if args.dependencies:
    paths.append(root / 'docs/site/node_modules')
for path in paths:
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()
