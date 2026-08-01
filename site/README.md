# Project page — source

This folder is the **source** of <https://samabr85.github.io/OcarinaCTRComposer/>.
It is *not* what GitHub serves.

| | |
|---|---|
| `site/` (this folder, on `main`) | the source you edit, versioned alongside the plugin |
| branch `gh-pages` | the deployed copy — this is what GitHub Pages actually serves |

The split keeps the two histories readable: `main` logs plugin work (cheats, RAM addresses,
engine fixes), `gh-pages` logs the site. Neither buries the other.

## Files

| File | What it is |
|---|---|
| `index.html` | the whole page — self-contained, no build step, no external CSS/JS |
| `favicon.svg` | tab icon (the gold cog) |
| `og-image.png` | 630×630 Open Graph image for link previews (WhatsApp, Telegram, Discord…) |
| `unistore-qr.png` | QR code for the shared **samaBR85** UniStore |
| `google4d93d72f4cbcabba.html` | Google Search Console ownership proof — **must** stay at the site root, at this exact filename, or verification breaks |

## Publishing

Edit here, then copy the folder's contents onto the `gh-pages` branch and push. Use a throwaway
clone so your working copy's git state is never involved:

```bash
git clone --branch gh-pages --single-branch https://github.com/samaBR85/OcarinaCTRComposer.git /tmp/ghp && cp site/* /tmp/ghp/ && cd /tmp/ghp && git add -A && git commit -m "Update project page" && git push
```

Pages rebuilds on push. Watch it finish:

```bash
gh api repos/samaBR85/OcarinaCTRComposer/pages --jq '.status'
```

`built` means live. Then hard-reload the page — Pages caches aggressively.

## Gotchas

- **Copy every file, not just `index.html`.** The verification file lives only here and on the
  branch; dropping it silently un-verifies the site in Search Console.
- **`.wrap` vs `section` padding.** Both are class selectors, so they have equal specificity, and a
  `padding` *shorthand* on one wipes the other's shorthand entirely. They deliberately use
  non-overlapping longhands (`padding-left/right` vs `padding-top/bottom`). Don't collapse them back
  into shorthands — sections lose their vertical spacing with no error anywhere.
- **Link previews cache.** WhatsApp and friends cache the first fetch per URL. After changing an
  `og:` tag, test in a fresh chat or the old preview keeps showing.
