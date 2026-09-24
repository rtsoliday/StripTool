#!/usr/bin/env python3
"""Build and validate the Qt StripTool static documentation site."""

import argparse
from html.parser import HTMLParser
import os
from pathlib import Path
import re
import shutil
import subprocess
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / 'docs/site'
OUTPUT = SITE / 'dist'


class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids = set()
        self.links = []
        self.title = False

    def handle_starttag(self, tag, attrs):
        data = dict(attrs)
        if 'id' in data:
            self.ids.add(data['id'])
        if tag == 'title':
            self.title = True
        for attr in ('href', 'src'):
            if data.get(attr):
                self.links.append(data[attr])


def verify(base):
    pages = {}
    for path in OUTPUT.rglob('*.html'):
        parser = Page()
        parser.feed(path.read_text(encoding='utf-8'))
        pages[path.resolve()] = parser
    if not pages:
        raise RuntimeError('No generated HTML pages found')

    issues = []
    count = 0
    for path, page in pages.items():
        if not page.title:
            issues.append(f'{path.relative_to(OUTPUT)}: missing title')
        for url in page.links:
            split = urlsplit(url)
            if split.scheme or split.netloc:
                continue
            name = unquote(split.path)
            if name.startswith('/'):
                if not name.startswith(base):
                    issues.append(f'{path.relative_to(OUTPUT)}: URL escapes base {base}: {url}')
                    continue
                dest = (OUTPUT / name[len(base):]).resolve()
            else:
                dest = (path.parent / name).resolve() if name else path
            if not dest.is_relative_to(OUTPUT.resolve()):
                issues.append(f'{path.relative_to(OUTPUT)}: URL escapes site: {url}')
                continue
            if dest.is_dir():
                dest /= 'index.html'
            if not dest.exists() and not dest.suffix:
                dest = dest.with_suffix('.html')
            count += 1
            if not dest.exists():
                issues.append(f'{path.relative_to(OUTPUT)}: missing {url}')
            elif split.fragment and dest in pages and unquote(split.fragment) not in pages[dest].ids:
                issues.append(f'{path.relative_to(OUTPUT)}: missing anchor {url}')
    if issues:
        raise RuntimeError('\n'.join(sorted(set(issues))))
    print(f'Validated {len(pages)} HTML pages and {count} local links/assets.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default=os.environ.get('DOCS_BASE', '/'))
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch(r'/(?:[A-Za-z0-9_.-]+/)*', args.base):
        parser.error('--base must have leading/trailing slashes and no dot segments')
    if not args.check_only:
        npm = shutil.which('npm') or shutil.which('npm.cmd')
        if not npm:
            raise RuntimeError('Node.js and npm are required to build the site')
        if not (SITE / 'node_modules/vitepress').is_dir():
            subprocess.run([npm, 'ci', '--no-audit', '--no-fund'], cwd=SITE, check=True)
        subprocess.run([npm, 'run', 'build'], cwd=SITE, check=True,
                       env={**os.environ, 'DOCS_BASE': args.base})
    if not (OUTPUT / 'index.html').is_file():
        raise RuntimeError('No built site found; run make docs first')
    verify(args.base)
    if not args.check_only:
        destination = ROOT / 'docs/html'
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(OUTPUT, destination)
        print(f'Built {destination / "index.html"} for {args.base}')


if __name__ == '__main__':
    main()
