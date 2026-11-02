/* Reserves meter: runs inside the game (USA CUSA13795 1.21), called from the hooks in hooks.s.
 *
 * - Boost update (every frame, every kart): for the local player's kart, works out the reserves
 *   (the seconds of boost the next boost would carry on, as the game does at 0xe2e9ab).
 * - HUD update (every frame, local player): once per HUD, copies two of the game's own HUD pieces
 *   (the lap counter text and the classic turbo meter bar) into the HUD's top level, outside every
 *   boost bar style, so they show in Classic, Speedometer and Nitro Fueled. Then shows the reserves on
 *   them: "12.4" and a bar. Each `full` seconds fill one layer; more reserves fill the next layer, in
 *   a new color, on top of the full one below.
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
extern void game_set_pivot(void *placeable, const float *v) GAME;
extern void game_set_color(void *placeable, const uint8_t *rgba) GAME;
extern void game_set_opacity(void *placeable, float opacity) GAME;
extern void game_set_hidden(void *placeable, int hidden) GAME;
extern void game_handle_resolve(void **slot) GAME;
extern int game_local_players(void *list) GAME;
extern char *game_string(void *pool, const char *text) GAME;         /* new reference */
extern void game_string_release(void *pool, char *header) GAME;
/* Game data */
extern void *g_boost_meta GAME;                                      /* CKartBoostComponent */
extern char *g_players GAME;
/* Set by the Time Trial Spectator Mode mod: the watched ghost's kart during a spectator run, else 0.
 * The memory is 0 when that mod is not installed. */
extern void *g_spectate_kart GAME;
/* The Replay Mode mod's state (0 without that mod): its second word, with bit 31 set, is the watched
 * racer's recorded reserves in a race replay (1/100 s) */
extern uint32_t *g_race_replay GAME;
extern void *g_string_pool GAME;
extern const char g_composition_vtable[] GAME;                       /* igGuiComposition */

#define TEXT_TEMPLATE "[LapCounterText]"
#define BAR_TEMPLATE "[octane_ui_hud_boost_bg.tga]"
#define FILL_SPRITE "[octane_ui_hud_boost_fill.tga]"
#define MASK_SPRITE "BoostMask"     /* the Nitro Fueled bar's mask, with its fill and white fill inside */
#define TEXT_NAME "[ReservesText]"
#define BAR_NAME "[ReservesBar]"
#define FILL_NAME "[ReservesFill]"
#define TOP_NAME "[ReservesFillTop]"
#define BAR_TEXTURE_WIDTH 244.0f
#define FILL_FULL 2.77f     /* the game's own fill scale for a full turbo meter */
#define LAYOUT_VERSION 7

/* Colors (RGBA) of the 3 layers: green (as the charging turbo bar), red, then blue-white (sacred fire).
 * Above 3 layers the bar stays full. */
#define LAYERS 3
static const uint8_t COLORS[LAYERS][4] = {{16, 200, 0, 255}, {230, 20, 0, 255}, {120, 190, 255, 255}};
static const uint8_t FRAME_COLORS[LAYERS][4] = {{255, 255, 255, 255}, {255, 70, 50, 255}, {150, 200, 255, 255}};

struct state {
    void *boosts;           /* boost component of the local player's kart */
    float reserves;         /* seconds; written by the boost update */
    void *root;             /* HUD root our pieces were added to */
    void *text, *bar, *fill, *top;  /* fill: the layer below (full); top: the layer that grows */
    int shown;              /* tenths of a second shown now; -1 = nothing shown yet */
    int hidden;
    /* Layout in HUD space (1920 x 1080). tools/tune.py may change these in the running game and
     * then set layout_changed. */
    int layout_version;
    int layout_changed;
    float text_pos[3], bar_pos[3];
    float full;             /* seconds of reserves for a full bar */
    void *project;          /* last HUD's GUI project (for tools/tune.py) */
    float preview;          /* > 0: show this many seconds instead of the real reserves, until the next HUD (tools/tune.py) */
    int style;              /* the HUD style the layout is for (hud +0x8c _boostBarIndex), -1 none yet */
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

/* A placeable that shows a composition (a HUD part made in its own file, e.g.
 * "Octane_RacerHud_LapDisplay") has no children: the content is the composition's _placeable (+0x20). */
static void *content(void *placeable)
{
    void *asset = handle_object(&AT(placeable, 0x38, void *));
    return asset && AT(asset, 0, const void *) == g_composition_vtable ? AT(asset, 0x20, void *) : 0;
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
    return find(content(placeable), name, by_sprite, depth + 1);
}

static void rename(void *placeable, const char *name)
{
    char *old = AT(placeable, 0x28, char *);
    AT(placeable, 0x28, char *) = game_string(g_string_pool, name);
    if (old) game_string_release(g_string_pool, old - 0x10);
}

/* A copy of a template placeable, added at the end of `parent` */
static void *copy(void *project, void *template, void *parent, const char *name)
{
    void *source = AT(template, 0x18, void *);      /* _sourcePlaceable, as the game spawns from */
    void *placeable = game_spawn_placeable(project, source ? source : template, parent);
    if (!placeable) return 0;
    rename(placeable, name);
    game_set_opacity(placeable, 1.0f);
    game_set_hidden(placeable, 0);
    return placeable;
}

/* The game's fill grows from the right end of the bar; ours grows from the left end */
static void fill_from_left(void *fill)
{
    float left = AT(fill, 0x60, float) - AT(fill, 0x6c, float) * AT(fill, 0x90, float) * FILL_FULL;
    float pivot[3] = {0.0f, AT(fill, 0x70, float), AT(fill, 0x74, float)};
    float position[3] = {left, AT(fill, 0x64, float), AT(fill, 0x68, float)};
    game_set_pivot(fill, pivot);
    game_set_position(fill, position);
}

/* A copy of the Nitro Fueled bar's mask (with its color strip and white fill inside), added to the HUD
 * root after the bar: a sprite is drawn over its own children, so inside the bar the dark middle of
 * the bar would cover it. place_layer() puts it where the bar's own fill (`fill`) is. */
static void *layer(void *project, void *mask, void *fill, const char *name)
{
    void *piece = copy(project, mask, rv.root, name);
    if (!piece || !child_count(piece) || !child_count(child(piece, 0))) return piece;
    void *strip = child(piece, 0);
    /* The strip and the white fill cover the whole mask, using a flat full-height part of their slanted textures */
    static const float place[12] = {0, 0, 0, 0.5f, 0.5f, 0.5f, 0, 0, 0, 1, 1, 1};    /* _position to _scale */
    static const float texture[4] = {0.1f, 0.05f, 0.3f, 0.4f};                      /* _texCoordMin, _texCoordMax */
    for (int i = -1; i < child_count(strip); i++) {
        void *inner = i < 0 ? strip : child(strip, i);
        __builtin_memcpy((char *)inner + 0x60, place, sizeof place);
        __builtin_memcpy((char *)inner + 0x90, (char *)fill + 0x90, 12);   /* _extent */
        AT(inner, 0xa0, int32_t) = AT(inner, 0xa4, int32_t) = 2;          /* anchored at the center */
        __builtin_memcpy((char *)inner + 0xa8, texture, sizeof texture);
        AT(inner, 0xc, uint32_t) &= ~1u;    /* not cut by the mask's slanted shape */
        AT(inner, 0xd, uint8_t) |= 0x10;
    }
    return piece;
}

/* `piece` over the bar's own fill (`fill`, a child of the bar: placed from the bar's right end and middle).
 * The game takes the bar's right end from its texture's width (244), not from _extent (392): found in game. */
static void place_layer(void *piece, void *fill)
{
    void *bar = rv.bar;
    float sx = AT(bar, 0x84, float), sy = AT(bar, 0x88, float);
    float place[12] = {AT(bar, 0x60, float) + (BAR_TEXTURE_WIDTH / 2 + AT(fill, 0x60, float)) * sx,
                       AT(bar, 0x64, float) + AT(fill, 0x64, float) * sy, 0,                            /* _position */
                       0, 0, 0.5f, 0, 0, 0,                                                               /* _pivot, _rotation */
                       AT(piece, 0x84, float), AT(fill, 0x88, float) * sy, 1};                          /* _scale (x: see set_fill) */
    __builtin_memcpy((char *)piece + 0x60, place, sizeof place);
    __builtin_memcpy((char *)piece + 0x90, (char *)fill + 0x90, 12);       /* _extent */
    AT(piece, 0xa0, int32_t) = AT(piece, 0xa4, int32_t) = 2;              /* from the root's middle, as the bar */
    AT(piece, 0xd, uint8_t) |= 0x10;
}

/* The meter sits right under the game's turbo bar, in a row: the bar under the bar, its seconds on its left. The
 * game's bars go up by LIFT to make room. Per HUD style (0 Classic, 1 Speedometer, 2 Nitro Fueled): the text's
 * right end and the bar's center, in HUD space (1920 x 1080). */
#define LIFT 20.0f
#define THIN 0.62f              /* the meter's bar is thinner than the game's (told apart, room at the bottom) */
#define TEXT_SIZE 0.8f
static const float PLACES[3][4] = {{1620, 1029, 1732, 1029}, {1585, 1029, 1698, 1029}, {1621, 1029, 1733, 1029}};
/* The height the game gives its bar groups (root parts, by their asset's name), or 0 for other parts */
static float bar_height(const char *name)
{
    return same_text(name, "Octane_BoostBar") ? 1008.0f : same_text(name, "Speedometer") ? 950.0f : same_text(name, "NF_Bars") ? 914.0f : 0.0f;
}

static void set_layout(void)
{
    if (rv.layout_version != LAYOUT_VERSION || rv.style < 0) {
        int style = rv.style >= 0 && rv.style < 3 ? rv.style : 0;
        rv.layout_version = LAYOUT_VERSION;
        rv.text_pos[0] = PLACES[style][0], rv.text_pos[1] = PLACES[style][1], rv.text_pos[2] = 0.0f;
        rv.bar_pos[0] = PLACES[style][2], rv.bar_pos[1] = PLACES[style][3], rv.bar_pos[2] = 0.0f;
        rv.full = 10.0f;
    }
    if (rv.text) game_set_position(rv.text, rv.text_pos);
    /* The bar sprite is placed from the center of its parent (anchor 2), the text from the top left */
    float bar[3] = {rv.bar_pos[0] - AT(rv.root, 0x90, float) / 2, rv.bar_pos[1] - AT(rv.root, 0x94, float) / 2, 0.0f};
    if (rv.bar) game_set_position(rv.bar, bar);
    void *fill = rv.bar ? find(rv.bar, FILL_SPRITE, 1, 0) : 0;
    if (fill && rv.fill && rv.top) place_layer(rv.fill, fill), place_layer(rv.top, fill);
    rv.layout_changed = 0;
    rv.shown = -1;
}

/* The game's bar groups up by LIFT: each frame, as the game sets them again when the style changes */
static void lift_bars(void *root)
{
    for (int i = 0; i < child_count(root); i++) {
        void *part = child(root, i);
        float y = AT(part, 0x64, float), height = bar_height(sprite_name(part));
        if (!height || y < height - 0.5f || y > height + 0.5f) continue;
        float at[3] = {AT(part, 0x60, float), y - LIFT, AT(part, 0x68, float)};
        game_set_position(part, at);
    }
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

/* The layer that grows (0 to LAYERS - 1) and how full it is (0 to 1) */
static int layer_of(float reserves, float *part)
{
    float layers = rv.full > 0.0f ? reserves / rv.full : 0.0f;
    if (layers > LAYERS) layers = LAYERS;
    int layer = (int)layers;
    if (layer > LAYERS - 1) layer = LAYERS - 1;
    *part = layers - (float)layer;
    return layer;
}

static void set_width(void *piece, float ratio)
{
    float scale[3] = {FILL_FULL * AT(rv.bar, 0x84, float) * ratio, AT(piece, 0x88, float), AT(piece, 0x8c, float)};
    game_set_scale(piece, scale);
}

static void paint(void *piece, const uint8_t *rgba)
{
    void *strip = child_count(piece) ? child(piece, 0) : 0;
    for (int i = 0; strip && i < child_count(strip); i++) game_set_color(child(strip, i), rgba);
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
    if (rv.fill && rv.top) {
        float part;
        int layer = layer_of(reserves, &part);
        set_width(rv.fill, layer ? 1.0f : 0.0f);
        set_width(rv.top, part);
        paint(rv.top, COLORS[layer]);
        if (layer) paint(rv.fill, COLORS[layer - 1]);
        game_set_color(rv.bar, FRAME_COLORS[layer]);
    }
}

static void set_hidden(int hidden)
{
    if (rv.hidden == hidden) return;
    rv.hidden = hidden;
    if (rv.text) game_set_hidden(rv.text, hidden);
    if (rv.bar) game_set_hidden(rv.bar, hidden);
    if (rv.fill) game_set_hidden(rv.fill, hidden);
    if (rv.top) game_set_hidden(rv.top, hidden);
}

/* Called at the start of the racer HUD's per-frame update. */
void rv_hud_update(void *hud)
{
    if (!hud || !AT(hud, 0x80, uint8_t)) return;                     /* _isInitialized */
    if (!g_players || game_local_players(AT(g_players, 0x10, void *)) != 1) return;

    void *kart = g_spectate_kart ? g_spectate_kart : AT(hud, 0x18, void *);
    rv.boosts = kart && g_boost_meta ? game_find_component(kart, g_boost_meta, 0) : 0;
    /* The game stops the boost update once the kart has no boosts left (e.g. a boost cut short by
     * a wall), so the last value would stay on screen. */
    void *boosts = rv.boosts ? AT(rv.boosts, 0x28, void *) : 0;
    if (!boosts || !AT(boosts, 0xc, int32_t)) rv.reserves = 0.0f;

    void *project = handle_object(&AT(hud, 0x38, void *));
    rv.project = project;
    /* The project's root shows the "Root" composition; its content holds every HUD part. */
    void *root = project && AT(project, 0x10, void *) ? content(AT(project, 0x10, void *)) : 0;
    if (!root) return;
    /* A new HUD, or our pieces are gone: add them again. Old pointers are only compared, never used. */
    if (root != rv.root || !rv.text || !is_child(root, rv.text) || (rv.bar && !is_child(root, rv.bar))) {
        rv.root = root;
        rv.text = rv.bar = rv.fill = rv.top = 0;
        rv.preview = 0.0f;  /* a preview lasts one race only */
        rv.hidden = 0;
        void *text = find(root, TEXT_TEMPLATE, 0, 0), *bar = find(root, BAR_TEMPLATE, 1, 0);
        void *mask = find(root, MASK_SPRITE, 1, 0), *fill;
        if (!text) return;
        rv.text = copy(project, text, root, TEXT_NAME);
        float size[3] = {TEXT_SIZE, TEXT_SIZE, 1.0f};
        if (rv.text) game_set_scale(rv.text, size);
        if (bar && mask && (rv.bar = copy(project, bar, root, BAR_NAME))) {
            float thin[3] = {AT(rv.bar, 0x84, float), AT(rv.bar, 0x88, float) * THIN, 1.0f};
            game_set_scale(rv.bar, thin);
        }
        if (rv.bar && mask && (fill = find(rv.bar, FILL_SPRITE, 1, 0))) {
            fill_from_left(fill);
            game_set_hidden(fill, 1);
            /* The top layer is added last, so it is drawn over the full layer below it */
            rv.fill = layer(project, mask, fill, FILL_NAME);
            rv.top = layer(project, mask, fill, TOP_NAME);
        }
        set_layout();
    }
    lift_bars(root);
    /* the layout of the HUD style shown (Triangle changes it in a race) */
    int style = AT(hud, 0x8c, int32_t);
    if (style != rv.style) rv.style = style, rv.layout_version = 0;
    if (rv.layout_changed || rv.layout_version != LAYOUT_VERSION) set_layout();

    /* Hidden after the finish line, as the rest of the race HUD */
    void *racer = handle_object(&AT(hud, 0x28, void *));
    set_hidden(racer && AT(racer, 0x8c, uint8_t));

    float reserves = rv.preview > 0.0f ? rv.preview : rv.reserves;
    if (g_race_replay && (g_race_replay[1] & 0x80000000u)) reserves = (float)(g_race_replay[1] & 0x7fffffffu) / 100.0f;
    int tenths = (int)(reserves * 10.0f + 0.5f);
    if (tenths != rv.shown) {
        rv.shown = tenths;
        show(tenths, reserves);
    }

}
