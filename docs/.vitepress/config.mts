import { defineConfig } from 'vitepress';

const repo = 'https://github.com/yayitazale/dekatron';
const base = process.env.DOCS_BASE ?? '/dekatron/';

// Published by GitHub Pages to https://yayitazale.github.io/dekatron/. The creator (public/creator/) is a page
// of its own that tools/build_site.py builds, and public/themes/ is built by tools/build_themes.py.
export default defineConfig({
  title: 'Dekatron',
  description: 'Open firmware for the EleksTube IPS Gen2 clock: a web page in the clock, themes that cross the six tubes, clock faces, and updates over the air. Installed by an AI assistant.',
  lang: 'en-GB',
  base,
  cleanUrls: true,
  lastUpdated: true,
  // Some pages of the reference point to files of the repository that are not part of the site.
  ignoreDeadLinks: [/^\.\.\//, /^\.\/\.\.\//],
  vite: {
    define: { __DOCS_BASE__: JSON.stringify(base) },
  },
  head: [
    ['link', { rel: 'icon', type: 'image/svg+xml', href: `${base}favicon.svg` }],
    ['meta', { name: 'theme-color', content: '#1b1b1f' }],
  ],
  themeConfig: {
    logo: '/logo.svg',
    nav: [
      { text: 'Guide', link: '/guide/getting-started' },
      { text: 'Themes', link: '/themes' },
      { text: 'Create', link: '/create/' },
      { text: 'Reference', link: '/api' },
      { text: 'Changelog', link: `${repo}/blob/main/CHANGELOG.md` },
    ],
    sidebar: [
      {
        text: 'Guide',
        items: [
          { text: 'Install', link: '/guide/getting-started' },
          { text: 'Using the clock', link: '/guide/using' },
          { text: 'Updating', link: '/guide/updating' },
          { text: 'Is it safe?', link: '/guide/safety' },
        ],
      },
      {
        text: 'Themes and faces',
        items: [
          { text: 'Gallery', link: '/themes' },
          { text: 'The creator', link: '/create/' },
          { text: 'Create with an AI', link: '/create/ai' },
          { text: 'From the command line', link: '/making-bins' },
        ],
      },
      {
        text: 'Reference',
        items: [
          { text: 'HTTP API', link: '/api' },
          { text: 'The page of the clock', link: '/web' },
          { text: 'File format', link: '/format' },
        ],
      },
    ],
    socialLinks: [{ icon: 'github', link: repo }],
    editLink: { pattern: `${repo}/edit/main/docs/:path`, text: 'Edit this page on GitHub' },
    search: { provider: 'local' },
    footer: {
      message: 'Code under the GNU GPL v3 or later, themes and faces under CC BY-SA 4.0. Compatible with the EleksTube IPS clocks, with no affiliation with EleksTube.',
      copyright: 'Copyright © 2026 Joseba Egia and Dekatron contributors',
    },
  },
});
