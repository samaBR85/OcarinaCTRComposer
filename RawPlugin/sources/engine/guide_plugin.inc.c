static int g_pgMode = 0, g_pgCur = 0, g_pgPage = 0, g_pgScroll = 0; // resume state
static void ToolPluginGuide(void)
{
    GuideBottom("How to use OcarinaCTRComposer");
    while (1)
    {
        int npg; const GuidePage *pages = PG_Pages(&npg);
        if (g_pgPage >= npg) g_pgPage = 0;
        if (g_pgMode == 1) // reading a page
        {
            int r = GuideReader(pages[g_pgPage].title, pages[g_pgPage].body, &g_pgScroll);
            if (r == 0) return;   // SELECT: resume here
            g_pgMode = 0;
        }
        else
        {
            const char *labels[32];
            int n = npg; if (n > 32) n = 32;
            for (int i = 0; i < n; ++i) labels[i] = pages[i].title;
            int r = GuideList(T("Plugin Guide"), labels, n, g_pgCur, &g_pgCur);
            if (r == -2) { g_quitToGame = 1; return; }
            if (r == -1) return;
            g_pgPage = r; g_pgScroll = 0; g_pgMode = 1;
        }
    }
}
