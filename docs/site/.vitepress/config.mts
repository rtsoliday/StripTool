import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'Qt StripTool',
  description: 'Guides and reference for trending EPICS process variables with Qt StripTool.',
  lang: 'en-US',
  base: process.env.DOCS_BASE || '/',
  outDir: './dist',
  srcExclude: ['public/**'],
  cleanUrls: false,
  head: [['meta', { name: 'theme-color', content: '#126384' }]],
  themeConfig: {
    siteTitle: 'Qt StripTool / docs',
    search: { provider: 'local' },
    outline: { level: [2, 3], label: 'On this page' },
    nav: [
      { text: 'Guide', link: '/get-started/first-trend' },
      { text: 'Reference', link: '/reference/command-line' },
      { text: 'Development', link: '/develop/documentation' },
      { text: 'Repository', link: 'https://github.com/rtsoliday/StripTool' }
    ],
    sidebar: [
      { text: 'GET STARTED', items: [
        { text: 'Overview', link: '/' },
        { text: 'Install & build', link: '/get-started/install' },
        { text: 'Plot your first PV', link: '/get-started/first-trend' }
      ]},
      { text: 'OPERATE', collapsed: false, items: [
        { text: 'Configure curves', link: '/operate/curves' },
        { text: 'Read & navigate the graph', link: '/operate/graph' },
        { text: 'Historical data', link: '/operate/history' },
        { text: 'Files, export & printing', link: '/operate/files' },
        { text: 'Troubleshooting', link: '/operate/troubleshooting' }
      ]},
      { text: 'REFERENCE', collapsed: true, items: [
        { text: 'Command line', link: '/reference/command-line' },
        { text: 'Environment variables', link: '/reference/environment' },
        { text: 'Configuration files', link: '/reference/configuration' }
      ]},
      { text: 'PROJECT & DEVELOPMENT', collapsed: true, items: [
        { text: 'Compatibility & release status', link: '/understand/compatibility' },
        { text: 'Appearance', link: '/understand/appearance' },
        { text: 'Performance', link: '/understand/performance' },
        { text: 'Maintain these docs', link: '/develop/documentation' },
        { text: 'Authors', link: '/project/authors' },
        { text: 'License', link: '/project/license' }
      ]}
    ]
  }
})
