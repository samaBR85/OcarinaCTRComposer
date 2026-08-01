// Navigation state persists so that SELECT-to-game then SELECT-back returns you
// to the exact page and scroll position you were reading.
// mode: 0 = category list, 1 = page list, 2 = reader, 3 = credits reader.
static int g_ggMode = 0, g_ggCatCur = 0, g_ggCat = 0, g_ggPage = 0, g_ggScroll = 0, g_ggCredScroll = 0;
static void ToolGameGuide(void)
{
    GuideBottom("Ocarina of Time 3D");
    while (1)
    {
        int ncats; const GuideCat *cats = GG_Cats(&ncats);
        if (g_ggCat >= ncats) g_ggCat = 0;              // language switch may shrink the set
        if (g_ggMode == 2) // reading a category page
        {
            if (g_ggPage >= cats[g_ggCat].nPages) g_ggPage = 0;
            const GuidePage *pg = &cats[g_ggCat].pages[g_ggPage];
            int r = GuideReader(pg->title, pg->body, &g_ggScroll);
            if (r == 0) return;          // SELECT: stay at mode 2 -> resume here next time
            g_ggMode = 1;                // B -> page list
        }
        else if (g_ggMode == 3) // reading Credits
        {
            int r = GuideReader("Credits", GUIDE_CREDITS, &g_ggCredScroll);
            if (r == 0) return;
            g_ggMode = 0;
        }
        else if (g_ggMode == 0) // category list
        {
            const char *labels[SDG_MAXCATS + 1];
            for (int i = 0; i < ncats; ++i) labels[i] = cats[i].title;
            labels[ncats] = "Credits";
            int r = GuideList(T("Game Guide"), labels, ncats + 1, g_ggCatCur, &g_ggCatCur);
            if (r == -2) { g_quitToGame = 1; return; } // stay at mode 0 -> resume the list
            if (r == -1) return;
            if (r == ncats) g_ggMode = 3;              // Credits
            else { g_ggCat = r; g_ggMode = 1; }
        }
        else // page list
        {
            const GuideCat *c = &cats[g_ggCat];
            const char *labels[20];
            int n = c->nPages; if (n > 20) n = 20;
            for (int i = 0; i < n; ++i) labels[i] = c->pages[i].title;
            int r = GuideList(c->title, labels, n, g_ggPage, &g_ggPage);
            if (r == -2) { g_quitToGame = 1; return; }
            if (r == -1) { g_ggMode = 0; continue; }
            g_ggPage = r; g_ggScroll = 0; g_ggMode = 2; // open the page from the top
        }
    }
}
