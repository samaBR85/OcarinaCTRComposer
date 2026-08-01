static const GuideCat *GG_Cats(int *n)
{
    if (g_ggNCats) { *n = g_ggNCats; return g_ggCatsBuf; }
    *n = GUIDE_NCATS; return GUIDE_CATS;
}
static const GuidePage *PG_Pages(int *n)
{
    if (g_pgNPages) { *n = g_pgNPages; return g_pgPagesBuf; }
    *n = PLUGIN_NPAGES; return PLUGIN_PAGES;
}
