/* Reserves meter: runs inside the game (USA CUSA13795 1.21), called from the hooks in hooks.s.
 *
 * - Boost update (every frame, every kart): for the local player's kart, works out the reserves
 *   (the seconds of boost the next boost would carry on, as the game does at 0xe2e9ab).
 * - HUD update (every frame, local player): once per HUD, copies two of the game's own HUD pieces
 *   (the lap counter text and the classic turbo meter bar) into the HUD root, outside every boost
 *   bar style, so they show in Classic, Speedometer and Nitro Fueled. Then shows the reserves on
 *   them: "12.4" and a bar.
 * One local player only (no split screen yet). Notes: ../notes/reserves-research.md
 */
#include <stdint.h>

#define GAME __attribute__((visibility("hidden")))

/* Game code */
extern float game_maintained(void *boost) GAME;                     /* extra time of a held boost pad */
extern void *game_find_component(void *entity, void *meta, int zero) GAME;
extern void *game_spawn_placeable(void *project, void *source, void *parent) GAME;
extern void game_set_text(void *placeable, const char *text) GAME;
extern void game_set_position(void *placeable, const float *v) GAME;
extern void game_set_scale(void *placeable, const float *v) GAME;
extern void game_set_opacity(void *placeable, float opacity) GAME;
extern void game_set_hidden(void *placeable, int hidden) GAME;
extern void game_handle_resolve(void **slot) GAME;
extern int game_local_players(void *list) GAME;
extern char *game_string(void *pool, const char *text) GAME;         /* new reference */
extern void game_string_release(void *pool, char *header) GAME;
/* Game data */
extern void *g_boost_meta GAME;                                      /* CKartBoostComponent */
extern char *g_players GAME;
extern void *g_string_pool GAME;

#define TEXT_TEMPLATE "[LapCounterText]"
#define BAR_TEMPLATE "[octane_ui_hud_boost_bg.tga]"
#define FILL_SPRITE "[octane_ui_hud_boost_fill.tga]"
#define TEXT_NAME "[ReservesText]"
#define BAR_NAME "[ReservesBar]"
#define FILL_FULL 2.77f     /* the game's own fill scale for a full turbo meter */
#define LAYOUT_VERSION 1

struct state {
    void *boosts;           /* boost component of the local player's kart */
    float reserves;         /* seconds; written by the boost update */
    void *root;             /* HUD root our pieces were added to */
    void *text, *bar, *fill;
    int shown;              /* tenths of a second shown now; -1 = nothing shown yet */
    int hidden;
    /* Layout in HUD space (1920 x 1080). tools/tune.py may change these in the running game and
     * then set layout_changed. */
    int layout_version;
    int layout_changed;
    float text_pos[3], bar_pos[3];
    float full;             /* seconds of reserves for a full bar */
};
_Static_assert(sizeof(struct state) <= 0x80, "state must fit 0x2beef80-0x2bef000");
extern struct state rv GAME;

/* Field access on game objects */
#define AT(object, offset, type) (*(type *)((char *)(object) + (offset)))

static int same_text(const char *a, const char *b)
{
    if (!a) return 0;
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* The object a handle field points to, or 0 (as the game reads them, e.g. 0xe32076) */
static void *handle_object(void **slot)
{
    for (int depth = 0; depth < 8; depth++) {
        int32_t *h = *slot;
        if (!h) return 0;
        if (*h & 0x10000000) {
            game_handle_resolve(slot);
            h = *slot;
            if (!h) return 0;
        }
        void *object = AT(h, 0x28, void *);
        if (!(*h & 0x20000000) || !object) return object;
        slot = (void **)((char *)object + 0x10);    /* the handle points to another handle */
    }
    return 0;
}

/* igGuiPlaceable children: +0x48 list (+0xc count, +0x20 items) */
static int child_count(void *placeable)
{
    void *list = AT(placeable, 0x48, void *);
    return list ? AT(list, 0xc, int32_t) : 0;
}

static void *child(void *placeable, int i)
{
    return AT(AT(placeable, 0x48, void *), 0x20, void **)[i];
}

static int is_child(void *parent, void *placeable)
{
    int n = child_count(parent);
    for (int i = 0; i < n; i++)
        if (child(parent, i) == placeable) return 1;
    return 0;
}

static const char *sprite_name(void *placeable)
{
    void *sprite = handle_object(&AT(placeable, 0x38, void *));
    return sprite ? AT(sprite, 0x10, const char *) : 0;
}

/* First placeable below `placeable` whose name (or sprite name) is `name` */
static void *find(void *placeable, const char *name, int by_sprite, int depth)
{
    if (!placeable || depth > 24) return 0;
    if (same_text(by_sprite ? sprite_name(placeable) : AT(placeable, 0x28, const char *), name))
        return placeable;
    int n = child_count(placeable);
    for (int i = 0; i < n; i++) {
        void *found = find(child(placeable, i), name, by_sprite, depth + 1);
        if (found) return found;
    }
    return 0;
}

static void rename(void *placeable, const char *name)
{
    char *old = AT(placeable, 0x28, char *);
    AT(placeable, 0x28, char *) = game_string(g_string_pool, name);
    if (old) game_string_release(g_string_pool, old - 0x10);
}

/* A copy of a template placeable, added at the end of the HUD root */
static void *copy(void *project, void *template, const char *name)
{
    void *source = AT(template, 0x18, void *);      /* _sourcePlaceable, as the game spawns from */
    void *placeable = game_spawn_placeable(project, source ? source : template, 0);
    if (!placeable) return 0;
    rename(placeable, name);
    game_set_opacity(placeable, 1.0f);
    game_set_hidden(placeable, 0);
    return placeable;
}

static void set_layout(void)
{
    if (rv.layout_version != LAYOUT_VERSION) {
        rv.layout_version = LAYOUT_VERSION;
        rv.text_pos[0] = 1640.0f, rv.text_pos[1] = 900.0f, rv.text_pos[2] = 0.0f;
        rv.bar_pos[0] = 1640.0f, rv.bar_pos[1] = 950.0f, rv.bar_pos[2] = 0.0f;
        rv.full = 20.0f;
    }
    if (rv.text) game_set_position(rv.text, rv.text_pos);
    if (rv.bar) game_set_position(rv.bar, rv.bar_pos);
    rv.layout_changed = 0;
    rv.shown = -1;
}

/* Called after the game updates a kart's boosts (once per frame per kart). */
void rv_boosts_updated(void *component)
{
    if (!component || component != rv.boosts) return;
    /* The running boost: the fastest one that is not fading out (as the game picks it, 0xe2e946) */
    void *list = AT(component, 0x28, void *);
    int n = list ? AT(list, 0xc, int32_t) : 0;
    void *active = 0;
    float speed = 0.0f;
    for (int i = 0; i < n; i++) {
        void *boost = AT(list, 0x20, void **)[i];
        void *data = boost ? AT(boost, 0x10, void *) : 0;
        if (!data || AT(boost, 0x48, uint8_t)) continue;
        if (!active || AT(data, 0xc, float) >= speed) active = boost, speed = AT(data, 0xc, float);
    }
    float left = 0.0f;
    if (active) {
        void *data = AT(active, 0x10, void *);
        left = AT(data, 0x1c, float) + AT(active, 0x28, float) + game_maintained(active) - AT(active, 0x18, float);
    }
    rv.reserves = left > 0.0f ? left : 0.0f;
}

static void show(int tenths, float reserves)
{
    char text[8];
    int n = 0, whole = tenths / 10;
    if (whole > 999) whole = 999, tenths = 9999;
    char digits[4];
    int d = 0;
    do digits[d++] = (char)('0' + whole % 10), whole /= 10; while (whole);
    while (d) text[n++] = digits[--d];
    text[n++] = '.';
    text[n++] = (char)('0' + tenths % 10);
    text[n] = 0;
    if (rv.text) game_set_text(rv.text, text);
    if (rv.fill) {
        float ratio = rv.full > 0.0f ? reserves / rv.full : 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        float scale[3] = {FILL_FULL * ratio, AT(rv.fill, 0x88, float), AT(rv.fill, 0x8c, float)};
        game_set_scale(rv.fill, scale);
    }
}

static void set_hidden(int hidden)
{
    if (rv.hidden == hidden) return;
    rv.hidden = hidden;
    if (rv.text) game_set_hidden(rv.text, hidden);
    if (rv.bar) game_set_hidden(rv.bar, hidden);
}

/* Called at the start of the racer HUD's per-frame update. */
void rv_hud_update(void *hud)
{
    if (!hud || !AT(hud, 0x80, uint8_t)) return;                     /* _isInitialized */
    if (!g_players || game_local_players(AT(g_players, 0x10, void *)) != 1) return;

    void *kart = AT(hud, 0x18, void *);
    rv.boosts = kart && g_boost_meta ? game_find_component(kart, g_boost_meta, 0) : 0;

    void *project = handle_object(&AT(hud, 0x38, void *));
    void *root = project ? AT(project, 0x10, void *) : 0;
    if (!root) return;
    /* A new HUD, or our pieces are gone: add them again. Old pointers are only compared, never used. */
    if (root != rv.root || !rv.text || !is_child(root, rv.text) || (rv.bar && !is_child(root, rv.bar))) {
        rv.root = root;
        rv.text = rv.bar = rv.fill = 0;
        rv.hidden = 0;
        void *text = find(root, TEXT_TEMPLATE, 0, 0), *bar = find(root, BAR_TEMPLATE, 1, 0);
        if (!text) return;
        rv.text = copy(project, text, TEXT_NAME);
        if (bar && (rv.bar = copy(project, bar, BAR_NAME)))
            rv.fill = find(rv.bar, FILL_SPRITE, 1, 0);
        set_layout();
    }
    if (rv.layout_changed || rv.layout_version != LAYOUT_VERSION) set_layout();

    /* Hidden after the finish line, as the rest of the race HUD */
    void *racer = handle_object(&AT(hud, 0x28, void *));
    set_hidden(racer && AT(racer, 0x8c, uint8_t));

    float reserves = rv.reserves;
    int tenths = (int)(reserves * 10.0f + 0.5f);
    if (tenths != rv.shown) {
        rv.shown = tenths;
        show(tenths, reserves);
    }
}
