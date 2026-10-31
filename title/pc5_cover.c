/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: covers from Steam, SteamGridDB, GOG, PCGamingWiki and Wikidata (pc5_cover.h), and
 * the games' backgrounds from Steam, SteamGridDB and GOG.
 *
 *   Steam      store.steampowered.com/api/storesearch: the store's own search,
 *              then the app's 600x900 library picture on Steam's image server
 *              (its file name comes from IStoreBrowseService/GetItems when the
 *              plain one is not there, as for newer games); its background is
 *              the library's hero picture, found the same way
 *   SteamGridDB  www.steamgriddb.com/api/v2, with the player's key: the game by its Steam
 *              app (the folder's, or the Steam match's), else by name (search/autocomplete);
 *              then its covers (grids, 600x900 or 660x930, still pictures only) and its
 *              backgrounds (heroes, 1920x620 or 3840x1240), each one request
 *   GOG        catalog.gog.com/v1/catalog: each product's coverVertical, and its
 *              galaxyBackgroundImage for the background
 *   PCGamingWiki  www.pcgamingwiki.com/w/api.php: the pages found, with their
 *              text, whose infobox names the cover (fetched through
 *              Special:FilePath) and the game's Steam app (its library hero,
 *              the background)
 *   Wikidata   www.wikidata.org/w/api.php: the items found by name, then their
 *              Steam application ID (P1733): that app's pictures on Steam's
 *              image server, for games Steam's own search doesn't find by that name
 *
 * Only SteamGridDB needs a key (an account's, free); each sees PC5's own User-Agent. */
#include "pc5_cover.h"
#include "pc5_image.h"
#include "pc5_json.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STEAM_IMAGES "https://shared.akamai.steamstatic.com/store_item_assets/"
#define GRIDDB_API "https://www.steamgriddb.com/api/v2/"
/* Still pictures, JPEG or PNG, nothing a game's page wouldn't show. */
#define GRIDDB_PLAIN "&types=static&mimes=image%%2Fpng%%2Cimage%%2Fjpeg&nsfw=false&humor=false&epilepsy=false"

enum { ANSWER_MAX = 2 * 1024 * 1024, PICTURE_MAX = 16 * 1024 * 1024, MIN_SIMILARITY = 50, WIKITEXT = 16 * 1024 };

static const char *const source_names[PC5_PICTURE_SOURCES] = { "Steam", "SteamGridDB", "GOG", "PCGamingWiki", "Wikidata" };
static const char *const source_keys[PC5_PICTURE_SOURCES] = { "steam", "steamgriddb", "gog", "pcgamingwiki", "wikidata" };
enum { SOURCE_STEAM, SOURCE_GRIDDB, SOURCE_GOG, SOURCE_PCGAMINGWIKI, SOURCE_WIKIDATA };

const char *pc5_picture_source_name(int index)
{
    return index >= 0 && index < PC5_PICTURE_SOURCES ? source_names[index] : "";
}

const char *pc5_picture_source_key(int index)
{
    return index >= 0 && index < PC5_PICTURE_SOURCES ? source_keys[index] : "";
}

int pc5_url_encode(const char *text, char *out, size_t size)
{
    static const char hexdigits[] = "0123456789ABCDEF";
    size_t n = 0;

    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("-._~", *p)) {
            if (n + 1 >= size) return -1;
            out[n++] = (char)*p;
        } else {
            if (n + 3 >= size) return -1;
            out[n++] = '%';
            out[n++] = hexdigits[*p >> 4];
            out[n++] = hexdigits[*p & 15];
        }
    }
    if (n >= size) return -1;
    out[n] = 0;
    return 0;
}

/* ---- names ------------------------------------------------------------------------- */

/* Words that say nothing about which game it is. */
static int filler(const char *word)
{
    static const char *const words[] = { "the", "a", "an", "of", "and", "edition", "complete", "goty", "game",
                                         "year", "definitive", "enhanced", "remastered", "deluxe", "ultimate",
                                         "collection", "gold", "premium", "standard", "directors", "cut",
                                         "anniversary", "special", "classic", "hd", "pc", "windows", "steam",
                                         "gog", "repack", "x64", "x86", "win64", "win32", "multi", "final" };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (!strcmp(word, words[i])) return 1;
    return 0;
}

enum { MAX_WORDS = 24, WORD = 32 };

/* Lower-case words of letters and digits; roman numerals stay words. */
static int words_of(const char *text, char words[MAX_WORDS][WORD], int keep_filler)
{
    int count = 0;
    size_t n = 0;
    char word[WORD];

    for (const unsigned char *p = (const unsigned char *)text;; p++) {
        int c = *p;
        int letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80;
        if (letter && c != '\'' && n + 1 < WORD) {
            word[n++] = (char)pc5_lower(c);
            continue;
        }
        if (c == '\'') continue;   /* "Assassin's" is one word */
        if (n) {
            word[n] = 0;
            if ((keep_filler || !filler(word)) && count < MAX_WORDS) memcpy(words[count++], word, n + 1);
            n = 0;
        }
        if (!c) break;
    }
    return count;
}

/* Does short (e.g. "gta") spell the first letters of long's words from i on? */
static int initials(const char *short_word, char words[MAX_WORDS][WORD], int count, int i)
{
    size_t length = strlen(short_word);

    if (length < 2 || i + (int)length > count) return 0;
    for (size_t k = 0; k < length; k++)
        if (words[i + (int)k][0] != short_word[k]) return 0;
    return (int)length;
}

/* Words of a matched against b: a's word counts when b has it, or when it is
 * the initials of a run of b's words (each of those then counts as matched). */
static int matched(char a[MAX_WORDS][WORD], int na, char b[MAX_WORDS][WORD], int nb, int *b_matched)
{
    int hits = 0, used[MAX_WORDS] = { 0 };

    *b_matched = 0;
    for (int i = 0; i < na; i++) {
        int found = 0;
        for (int j = 0; j < nb && !found; j++)
            if (!used[j] && !strcmp(a[i], b[j])) { used[j] = 1; found = 1; (*b_matched)++; }
        for (int j = 0; j < nb && !found; j++) {
            int run = initials(a[i], b, nb, j);
            if (run) {
                for (int k = 0; k < run; k++) if (!used[j + k]) { used[j + k] = 1; (*b_matched)++; }
                found = 1;
            }
        }
        hits += found;
    }
    return hits;
}

int pc5_cover_similarity(const char *a, const char *b)
{
    char wa[MAX_WORDS][WORD], wb[MAX_WORDS][WORD];
    int na = words_of(a, wa, 0), nb = words_of(b, wb, 0), ab, ba, b_from_a, a_from_b;

    if (!na) na = words_of(a, wa, 1);
    if (!nb) nb = words_of(b, wb, 1);
    if (!na || !nb) return 0;
    ab = matched(wa, na, wb, nb, &b_from_a);
    ba = matched(wb, nb, wa, na, &a_from_b);
    /* The share of both names explained by the other, the weaker of the two
     * ways round counting most. */
    double one = (double)(ab > a_from_b ? ab : a_from_b) / na, two = (double)(ba > b_from_a ? ba : b_from_a) / nb;
    if (one > 1) one = 1;
    if (two > 1) two = 1;
    return (int)(100.0 * (one < two ? one * 0.7 + two * 0.3 : two * 0.7 + one * 0.3) + 0.5);
}

/* Words a release's name carries that aren't the game's: stores, platforms, scene and repack
 * groups, and release tags. Edition words (Deluxe, GOTY, Definitive) stay. */
static int release_word(const char *word)
{
    static const char *const words[] = {
        "gog", "steam", "repack", "x64", "x86", "win64", "win32", "multi", "pc", "p2p", "codex", "rune", "flt",
        "skidrow", "plaza", "cpy", "empress", "tenoke", "razor1911", "reloaded", "prophet", "hoodlum", "dodi",
        "fitgirl", "elamigos", "kaos", "rld", "dinobytes", "darksiders", "goldberg", "portable", "build", "update",
        "incl", "dlc", "dlcs", "crack", "cracked", "emu", "rip", "proper", "readnfo", "internal", NULL,
    };
    char lower[64];
    size_t k = 0;
    for (; word[k] && k + 1 < sizeof(lower); k++) lower[k] = (char)pc5_lower((unsigned char)word[k]);
    lower[k] = 0;
    for (int i = 0; words[i]; i++)
        if (!strcmp(lower, words[i])) return 1;
    /* MULTi2 to MULTi20: how many languages */
    if (!strncmp(lower, "multi", 5) && lower[5] >= '0' && lower[5] <= '9') return 1;
    return 0;
}

/* A folder's name made a search: release tags, versions and brackets out, scene and repack
 * groups' names out, dots and underscores to spaces, words split where the case changes. */
static void clean_name(const char *name, char *out, size_t size)
{
    char raw[PC5_NAME * 2], kept[PC5_NAME * 2], text[PC5_NAME * 2];
    size_t n = 0, r = 0, k = 0;
    int depth = 0, after_build = 0;

    /* Versions first ("v1.2.0", "1.0.3", "v2"), while their dots still tie them together. */
    for (const char *p = name; *p && r + 1 < sizeof(raw);) {
        int boundary = p == name || strchr(" ._-+[(", p[-1]);
        const char *q = p;
        if (boundary && (*q == 'v' || *q == 'V') && q[1] >= '0' && q[1] <= '9') q++;
        if (boundary && *q >= '0' && *q <= '9') {
            const char *e = q;
            int dots = 0;
            while ((*e >= '0' && *e <= '9') || (*e == '.' && e[1] >= '0' && e[1] <= '9')) dots += *e++ == '.';
            if ((dots || q != p) && (!*e || strchr(" ._-+])", *e))) { p = e; continue; }
        }
        raw[r++] = *p++;
    }
    raw[r] = 0;
    /* Brackets out, then the words between the separators, release words left out. */
    for (const char *p = raw; *p;) {
        char word[PC5_NAME];
        size_t w = 0;
        if (*p == '[' || *p == '(' || *p == '{') { depth++; p++; continue; }
        if (*p == ']' || *p == ')' || *p == '}') { if (depth) depth--; p++; continue; }
        if (depth || strchr(" ._-+", *p)) { p++; continue; }
        while (*p && !strchr(" ._-+[({])}", *p) && w + 1 < sizeof(word)) word[w++] = *p++;
        while (*p && !strchr(" ._-+[({])}", *p)) p++;
        word[w] = 0;
        /* a build's or update's number goes with it ("Build.12345", "Update 7") */
        int number = w && strspn(word, "0123456789") == w;
        if (number && after_build) { after_build = 0; continue; }
        after_build = !pc5_casecmp(word, "build") || !pc5_casecmp(word, "update");
        if (!w || release_word(word) || k + w + 2 >= sizeof(kept)) continue;
        if (k) kept[k++] = ' ';
        memcpy(kept + k, word, w);
        k += w;
    }
    kept[k] = 0;
    for (const char *p = kept; *p && n + 2 < sizeof(text); p++) {
        char c = *p;
        /* "HalfLife2" -> "Half Life 2", but "GTAIV" stays one word */
        if (n && c != ' ' && text[n - 1] != ' ' &&
            ((c >= 'A' && c <= 'Z' && text[n - 1] >= 'a' && text[n - 1] <= 'z') ||
             (c >= '0' && c <= '9' && ((text[n - 1] >= 'a' && text[n - 1] <= 'z') ||
                                        (text[n - 1] >= 'A' && text[n - 1] <= 'Z')))))
            text[n++] = ' ';
        text[n++] = c;
    }
    text[n] = 0;
    pc5_copy(out, size, text);
    /* cut at a word's end when it doesn't fit */
    if (strlen(text) >= size) {
        char *space = strrchr(out, ' ');
        if (space) *space = 0;
    }
}

/* Product names that only say what made the program. */
static int generic_product(const char *product)
{
    static const char *const names[] = { "unity", "unity player", "unreal engine", "ue4", "ue5", "game",
                                         "launcher", "bootstrapper", "microsoft windows operating system",
                                         "microsoft® windows® operating system", "godot engine", "gamemaker",
                                         "rpg maker", "nw.js", "electron", "love", "application" };
    char lower[PC5_NAME];
    size_t i = 0;

    for (; product[i] && i + 1 < sizeof(lower); i++) lower[i] = (char)pc5_lower((unsigned char)product[i]);
    lower[i] = 0;
    for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++)
        if (!strcmp(lower, names[k])) return 1;
    return strlen(lower) < 2;
}

int pc5_cover_queries(const Pc5CoverHint *hint, char queries[3][PC5_NAME])
{
    char candidates[4][PC5_NAME];
    int count = 0, n = 0;

    candidates[0][0] = candidates[1][0] = candidates[2][0] = candidates[3][0] = 0;
    /* the store's own name for it (GOG's note in the folder) first: it is the game's */
    if (hint->name[0]) pc5_copy(candidates[n++], PC5_NAME, hint->name);
    if (hint->product[0] && !generic_product(hint->product))
        pc5_copy(candidates[n++], PC5_NAME, hint->product);
    clean_name(hint->folder, candidates[n], PC5_NAME);
    if (candidates[n][0]) n++;
    clean_name(hint->program, candidates[n], PC5_NAME);
    if (candidates[n][0] && !generic_product(candidates[n])) n++;
    for (int i = 0; i < n && count < 3; i++) {
        int seen = 0;
        for (int k = 0; k < count; k++) seen |= !pc5_casecmp(queries[k], candidates[i]);
        if (!seen) memcpy(queries[count++], candidates[i], PC5_NAME);
    }
    return count;
}

/* ---- the search ------------------------------------------------------------------- */

typedef enum Plan {
    PLAN_STEAM_APP, PLAN_STEAM, PLAN_GRIDDB_GAME, PLAN_GRIDDB_COVERS, PLAN_GRIDDB_HEROES, PLAN_GRIDDB_SQUARES, PLAN_GOG,
    PLAN_PCGAMINGWIKI,
    PLAN_WIKIDATA, PLAN_WIKIDATA_APPS, PLAN_DONE
} Plan;

/* What step n does: the given app first, Steam for the first two queries, SteamGridDB (three
 * steps, with a key), GOG, PCGamingWiki and Wikidata (two steps) for the first; the sources
 * turned off left out. */
static Plan plan(const Pc5CoverSearch *search, int step, int *query)
{
    Plan order[16];
    int queries[16], n = 0;
    uint32_t off = search->hint.off;

    if (search->hint.steam_appid && !(off & PC5_PICTURES_STEAM)) { order[n] = PLAN_STEAM_APP; queries[n++] = 0; }
    if (search->query_count > 0 && !(off & PC5_PICTURES_STEAM)) { order[n] = PLAN_STEAM; queries[n++] = 0; }
    if (search->query_count > 1 && !(off & PC5_PICTURES_STEAM)) { order[n] = PLAN_STEAM; queries[n++] = 1; }
    if ((search->query_count > 0 || search->hint.steam_appid) && !(off & PC5_PICTURES_STEAMGRIDDB) &&
        search->hint.steamgriddb_key[0]) {
        order[n] = PLAN_GRIDDB_GAME; queries[n++] = 0;
        if (search->hint.icons) {
            order[n] = PLAN_GRIDDB_SQUARES; queries[n++] = 0;
        } else {
            order[n] = PLAN_GRIDDB_COVERS; queries[n++] = 0;
            order[n] = PLAN_GRIDDB_HEROES; queries[n++] = 0;
        }
    }
    /* Square pictures: only SteamGridDB has them; Steam above told it the game. */
    if (search->hint.icons) {
        if (step >= n) return PLAN_DONE;
        *query = queries[step];
        return order[step];
    }
    if (search->query_count > 0 && !(off & PC5_PICTURES_GOG)) { order[n] = PLAN_GOG; queries[n++] = 0; }
    if (search->query_count > 0 && !(off & PC5_PICTURES_PCGAMINGWIKI)) { order[n] = PLAN_PCGAMINGWIKI; queries[n++] = 0; }
    if (search->query_count > 0 && !(off & PC5_PICTURES_WIKIDATA)) {
        order[n] = PLAN_WIKIDATA; queries[n++] = 0;
        order[n] = PLAN_WIKIDATA_APPS; queries[n++] = 0;
    }
    if (step >= n) return PLAN_DONE;
    *query = queries[step];
    return order[step];
}

void pc5_cover_search_start(Pc5CoverSearch *search, const Pc5CoverHint *hint)
{
    memset(search, 0, sizeof(*search));
    search->hint = *hint;
    search->query_count = pc5_cover_queries(hint, search->queries);
    /* Every source turned off: nothing to wait for, as when they answered without a picture. */
    if (!hint->steamgriddb_key[0]) search->hint.off |= PC5_PICTURES_STEAMGRIDDB;   /* none without a key */
    if ((search->hint.off & ((1u << PC5_PICTURE_SOURCES) - 1)) == (1u << PC5_PICTURE_SOURCES) - 1) search->answered = 1;
    /* The player's own picture comes before anything found online. */
    if (hint->picture[0]) {
        Pc5CoverChoice *own = &search->choices[search->count++];
        *own = (Pc5CoverChoice){ .score = 1000000 };
        pc5_copy(own->source, sizeof(own->source), "your picture");
        pc5_copy(own->title, sizeof(own->title), hint->folder);
        pc5_copy(own->url, sizeof(own->url), hint->picture);
        search->answered = 1;
    }
}

static void add_choice(Pc5CoverSearch *search, const char *source, const char *title, const char *url,
                       const char *background, uint32_t appid, int score)
{
    Pc5CoverChoice *slot = NULL;

    if (!url[0] || strncmp(url, "https://", 8)) return;
    for (int i = 0; i < search->count; i++)
        if (!strcmp(search->choices[i].url, url)) {
            if (score > search->choices[i].score) search->choices[i].score = score;
            return;
        }
    if (search->count < PC5_COVER_CHOICES) {
        slot = &search->choices[search->count++];
    } else {
        int worst = 0;
        for (int i = 1; i < search->count; i++) if (search->choices[i].score < search->choices[worst].score) worst = i;
        if (search->choices[worst].score >= score) return;
        slot = &search->choices[worst];
    }
    *slot = (Pc5CoverChoice){ .steam_appid = appid, .score = score };
    pc5_copy(slot->source, sizeof(slot->source), source);
    pc5_copy(slot->title, sizeof(slot->title), title);
    pc5_copy(slot->url, sizeof(slot->url), url);
    if (background && !strncmp(background, "https://", 8)) pc5_copy(slot->background, sizeof(slot->background), background);
}

/* Steam's library hero for an app, by its plain name (newer apps: the asset list's). */
static void steam_hero(uint32_t appid, char *out, size_t size)
{
    snprintf(out, size, STEAM_IMAGES "steam/apps/%u/library_hero.jpg", (unsigned)appid);
}

/* Store entries that are not the game itself. */
static int extra_item(const char *title)
{
    static const char *const words[] = { " soundtrack ", " ost ", " artbook ", " art book ", " season pass ", " dlc ",
                                         " demo ", " dedicated server ", " sdk ", " editor ", " bonus content ",
                                         " upgrade ", " costume ", " pack " };
    char padded[132];
    size_t n = 1;
    padded[0] = ' ';
    for (const unsigned char *p = (const unsigned char *)title; *p && n + 2 < sizeof(padded); p++) {
        int c = pc5_lower(*p);
        padded[n++] = (char)((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80 ? c : ' ');
    }
    padded[n++] = ' ';
    padded[n] = 0;
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (strstr(padded, words[i])) return 1;
    return 0;
}

/* The best similarity of a found title to any of the queries (and to the
 * folder's own name). */
static int best_similarity(const Pc5CoverSearch *search, const char *title)
{
    char plain[128];
    int best = 0;
    size_t n = 0;

    /* "Half-Life (video game)": the part in brackets is Wikipedia's, not the game's. */
    for (const char *p = title; *p && n + 1 < sizeof(plain) && *p != '('; p++) plain[n++] = *p;
    plain[n] = 0;
    for (int i = 0; i < search->query_count; i++) {
        int s = pc5_cover_similarity(search->queries[i], plain);
        if (s > best) best = s;
    }
    return extra_item(plain) ? best - 40 : best;
}

static int ask(const Pc5Http *http, const char *url, Pc5Json *json, Pc5Buf *answer)
{
    int status = 0;

    answer->len = 0;
    if (http->get(http->context, url, answer, ANSWER_MAX, &status) || status != 200 || pc5_buf_terminate(answer))
        return -1;
    return pc5_json_parse(json, (const char *)answer->data, answer->len, 200000);
}

static void steam_search(Pc5CoverSearch *search, const Pc5Http *http, const char *query)
{
    char term[3 * PC5_NAME + 1], url[512], name[128], picture[256], hero[256];
    Pc5Buf answer = { 0 };
    Pc5Json json;

    if (pc5_url_encode(query, term, sizeof(term)) ||
        pc5_format(url, sizeof(url), "https://store.steampowered.com/api/storesearch/?term=%s&cc=US&l=english", term))
        return;
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        search->answered_stores++;
        uint32_t items = pc5_json_get(&json, 0, "items");
        for (uint32_t i = 0; i < 8; i++) {
            uint32_t item = pc5_json_item(&json, items, i);
            if (!item) break;
            uint32_t id = (uint32_t)pc5_json_number(&json, pc5_json_get(&json, item, "id"), 0);
            char type[16];
            pc5_json_text(&json, pc5_json_get(&json, item, "type"), type, sizeof(type));
            if (!id || strcmp(type, "app") || pc5_json_text(&json, pc5_json_get(&json, item, "name"), name, sizeof(name)))
                continue;
            int similarity = best_similarity(search, name);
            if (similarity < MIN_SIMILARITY) continue;
            snprintf(picture, sizeof(picture), STEAM_IMAGES "steam/apps/%u/library_600x900_2x.jpg", (unsigned)id);
            steam_hero(id, hero, sizeof(hero));
            add_choice(search, source_names[SOURCE_STEAM], name, picture, hero, id, similarity + 30 - (int)i * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

static void gog_search(Pc5CoverSearch *search, const Pc5Http *http, const char *query)
{
    char term[3 * PC5_NAME + 1], url[512], name[128], picture[512], background[512];
    Pc5Buf answer = { 0 };
    Pc5Json json;

    if (pc5_url_encode(query, term, sizeof(term)) ||
        pc5_format(url, sizeof(url), "https://catalog.gog.com/v1/catalog?query=like:%s&limit=5&order=desc:score", term))
        return;
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        search->answered_stores++;
        uint32_t products = pc5_json_get(&json, 0, "products");
        for (uint32_t i = 0; i < 5; i++) {
            uint32_t product = pc5_json_item(&json, products, i);
            if (!product) break;
            if (pc5_json_text(&json, pc5_json_get(&json, product, "title"), name, sizeof(name)) ||
                pc5_json_text(&json, pc5_json_get(&json, product, "coverVertical"), picture, sizeof(picture)))
                continue;
            int similarity = best_similarity(search, name);
            if (similarity < MIN_SIMILARITY) continue;
            if (pc5_json_text(&json, pc5_json_get(&json, product, "galaxyBackgroundImage"), background, sizeof(background)))
                background[0] = 0;
            add_choice(search, source_names[SOURCE_GOG], name, picture, background, 0, similarity + 20 - (int)i * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

/* A parameter of PCGamingWiki's {{Infobox game}} in a page's text: "|name = value" (any
 * spaces), its value to the end of its line, trimmed. 0, or -1 when it isn't there or is
 * empty. */
static int infobox_value(const char *text, const char *name, char *out, size_t size)
{
    const char *box = strstr(text, "{{Infobox game");
    size_t n = strlen(name);

    out[0] = 0;
    if (!box) return -1;
    for (const char *p = box; (p = strchr(p, '|')); p++) {
        const char *q = p + 1, *end;
        while (*q == ' ' || *q == '\t') q++;
        if (strncmp(q, name, n)) continue;
        q += n;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '=') continue;
        for (q++; *q == ' ' || *q == '\t'; q++) {
        }
        end = q;
        while (*end && *end != '\n' && *end != '\r' && *end != '|' && strncmp(end, "}}", 2)) end++;
        while (end > q && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if (end == q || (size_t)(end - q) >= size) return -1;
        memcpy(out, q, (size_t)(end - q));
        out[end - q] = 0;
        return 0;
    }
    return -1;
}

static void pcgamingwiki_search(Pc5CoverSearch *search, const Pc5Http *http, const char *query)
{
    char term[3 * PC5_NAME + 1], url[768], name[128], file[256], encoded[3 * 256 + 1], picture[1024], hero[256],
         appid_text[64];
    char *text = pc5_alloc(WIKITEXT);
    Pc5Buf answer = { 0 };
    Pc5Json json;

    if (!text) return;
    if (pc5_url_encode(query, term, sizeof(term)) ||
        pc5_format(url, sizeof(url),
                   "https://www.pcgamingwiki.com/w/api.php?action=query&format=json&formatversion=2&generator=search"
                   "&gsrnamespace=0&gsrlimit=3&gsrsearch=%s&prop=revisions&rvprop=content&rvslots=main",
                   term)) {
        pc5_free(text);
        return;
    }
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        uint32_t pages = pc5_json_get(&json, pc5_json_get(&json, 0, "query"), "pages");
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t page = pc5_json_item(&json, pages, i);
            if (!page) break;
            uint32_t revision = pc5_json_item(&json, pc5_json_get(&json, page, "revisions"), 0);
            uint32_t content = pc5_json_get(&json, pc5_json_get(&json, pc5_json_get(&json, revision, "slots"), "main"),
                                            "content");
            if (pc5_json_text(&json, pc5_json_get(&json, page, "title"), name, sizeof(name)) ||
                pc5_json_text(&json, content, text, WIKITEXT) || infobox_value(text, "cover", file, sizeof(file)))
                continue;
            int similarity = best_similarity(search, name);
            if (similarity < MIN_SIMILARITY) continue;
            const char *bare = !strncmp(file, "File:", 5) ? file + 5 : file;
            if (pc5_url_encode(bare, encoded, sizeof(encoded)) ||
                pc5_format(picture, sizeof(picture), "https://www.pcgamingwiki.com/wiki/Special:FilePath/%s", encoded))
                continue;
            uint32_t appid = 0;
            if (!infobox_value(text, "steam appid", appid_text, sizeof(appid_text)))
                appid = (uint32_t)strtoul(appid_text, NULL, 10);
            if (appid) steam_hero(appid, hero, sizeof(hero));
            int rank = (int)pc5_json_number(&json, pc5_json_get(&json, page, "index"), 3);
            add_choice(search, source_names[SOURCE_PCGAMINGWIKI], name, picture, appid ? hero : NULL, appid, similarity + 10 - rank * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
    pc5_free(text);
}

/* Wikidata, first step: the items its search finds for the name, kept for the next. */
static void wikidata_find(Pc5CoverSearch *search, const Pc5Http *http, const char *query)
{
    char term[3 * PC5_NAME + 1], url[768], id[16];
    Pc5Buf answer = { 0 };
    Pc5Json json;

    search->wikidata_count = 0;
    if (pc5_url_encode(query, term, sizeof(term)) ||
        pc5_format(url, sizeof(url),
                   "https://www.wikidata.org/w/api.php?action=wbsearchentities&format=json&language=en&uselang=en"
                   "&type=item&limit=5&search=%s",
                   term))
        return;
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        uint32_t found = pc5_json_get(&json, 0, "search");
        for (uint32_t i = 0; i < 5 && search->wikidata_count < 5; i++) {
            uint32_t item = pc5_json_item(&json, found, i);
            if (!item) break;
            if (pc5_json_text(&json, pc5_json_get(&json, item, "id"), id, sizeof(id)) || id[0] != 'Q' || !id[1]) continue;
            int digits = 1;
            for (const char *p = id + 1; *p; p++) digits &= *p >= '0' && *p <= '9';
            if (digits) pc5_copy(search->wikidata[search->wikidata_count++], sizeof(search->wikidata[0]), id);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

/* Wikidata, second step: those items' names and Steam application IDs (P1733). */
static void wikidata_apps(Pc5CoverSearch *search, const Pc5Http *http)
{
    char ids[5 * 20], url[768], name[128], appid_text[32], picture[256], hero[256];
    Pc5Buf answer = { 0 };
    Pc5Json json;
    size_t n = 0;

    if (!search->wikidata_count) {
        search->requests--;   /* nothing to ask */
        return;
    }
    ids[0] = 0;
    for (int i = 0; i < search->wikidata_count; i++)
        n += (size_t)snprintf(ids + n, sizeof(ids) - n, "%s%s", i ? "%7C" : "", search->wikidata[i]);
    if (pc5_format(url, sizeof(url),
                   "https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=claims%%7Clabels"
                   "&languages=en&ids=%s",
                   ids))
        return;
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        uint32_t entities = pc5_json_get(&json, 0, "entities");
        for (int i = 0; i < search->wikidata_count; i++) {
            uint32_t entity = pc5_json_get(&json, entities, search->wikidata[i]);
            uint32_t label = pc5_json_get(&json, pc5_json_get(&json, pc5_json_get(&json, entity, "labels"), "en"), "value");
            uint32_t claim = pc5_json_item(&json, pc5_json_get(&json, pc5_json_get(&json, entity, "claims"), "P1733"), 0);
            uint32_t value = pc5_json_get(&json, pc5_json_get(&json, pc5_json_get(&json, claim, "mainsnak"), "datavalue"),
                                          "value");
            if (!entity || pc5_json_text(&json, label, name, sizeof(name)) ||
                pc5_json_text(&json, value, appid_text, sizeof(appid_text)))
                continue;
            uint32_t appid = (uint32_t)strtoul(appid_text, NULL, 10);
            int similarity = best_similarity(search, name);
            if (!appid || similarity < MIN_SIMILARITY) continue;
            snprintf(picture, sizeof(picture), STEAM_IMAGES "steam/apps/%u/library_600x900_2x.jpg", (unsigned)appid);
            steam_hero(appid, hero, sizeof(hero));
            add_choice(search, source_names[SOURCE_WIKIDATA], name, picture, hero, appid, similarity + 25 - i * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

/* ---- SteamGridDB ------------------------------------------------------------------- */

/* One of SteamGridDB's answers (path after /api/v2/), asked with the player's key: 0 with
 * json, else -1 (its status kept: 401 or 403 for a key refused, 429 for too many requests). */
static int griddb_ask(Pc5CoverSearch *search, const Pc5Http *http, const char *path, Pc5Json *json, Pc5Buf *answer)
{
    char url[768], authorization[128];
    int status = 0, failed;

    answer->len = 0;
    if (!http->get_authorized || pc5_format(url, sizeof(url), GRIDDB_API "%s", path) ||
        pc5_format(authorization, sizeof(authorization), "Bearer %s", search->hint.steamgriddb_key))
        return -1;
    failed = http->get_authorized(http->context, url, authorization, answer, ANSWER_MAX, &status);
    memset(authorization, 0, sizeof(authorization));
    search->griddb_status = failed ? -1 : status;
    if (failed || status != 200 || pc5_buf_terminate(answer)) return -1;
    return pc5_json_parse(json, (const char *)answer->data, answer->len, 200000);
}

/* The game on SteamGridDB: by the folder's Steam app, or the best Steam match's (no request
 * then), else the best of its search for the first name. */
static void griddb_game(Pc5CoverSearch *search, const Pc5Http *http)
{
    char term[3 * PC5_NAME + 1], path[3 * PC5_NAME + 64], name[128];
    const Pc5CoverChoice *steam = NULL;
    Pc5Buf answer = { 0 };
    Pc5Json json;

    for (int i = 0; i < search->count; i++)
        if (!strcmp(search->choices[i].source, source_names[SOURCE_STEAM]) && search->choices[i].steam_appid &&
            (!steam || search->choices[i].score > steam->score))
            steam = &search->choices[i];
    if (search->hint.steam_appid || steam) {
        search->griddb_app = search->hint.steam_appid ? search->hint.steam_appid : steam->steam_appid;
        pc5_copy(search->griddb_title, sizeof(search->griddb_title), search->hint.steam_appid ? search->hint.folder : steam->title);
        /* Just after Steam's own pictures of the game. */
        search->griddb_score = (search->hint.steam_appid ? 1000 : steam->score) - 5;
        search->requests--;   /* nothing was asked */
        return;
    }
    if (!search->query_count || pc5_url_encode(search->queries[0], term, sizeof(term)) ||
        pc5_format(path, sizeof(path), "search/autocomplete/%s", term))
        return;
    if (!griddb_ask(search, http, path, &json, &answer)) {
        search->answered++;
        uint32_t data = pc5_json_get(&json, 0, "data");
        int best = MIN_SIMILARITY - 1;
        for (uint32_t i = 0; i < 5; i++) {
            uint32_t game = pc5_json_item(&json, data, i);
            if (!game) break;
            uint32_t id = (uint32_t)pc5_json_number(&json, pc5_json_get(&json, game, "id"), 0);
            if (!id || pc5_json_text(&json, pc5_json_get(&json, game, "name"), name, sizeof(name))) continue;
            int similarity = best_similarity(search, name) - (int)i * 2;
            if (similarity <= best) continue;
            best = similarity;
            search->griddb_game = id;
            pc5_copy(search->griddb_title, sizeof(search->griddb_title), name);
            search->griddb_score = similarity + 25;   /* under Steam's +30, over GOG's +20 */
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

/* The path of the game's pictures of a kind ("grids", "heroes"), by its id or its Steam app. */
static int griddb_path(const Pc5CoverSearch *search, const char *kind, const char *dimensions, char *out, size_t size)
{
    if (search->griddb_game)
        return pc5_format(out, size, "%s/game/%u?dimensions=%s" GRIDDB_PLAIN, kind, (unsigned)search->griddb_game, dimensions);
    return pc5_format(out, size, "%s/steam/%u?dimensions=%s" GRIDDB_PLAIN, kind, (unsigned)search->griddb_app, dimensions);
}

enum { GRIDDB_PICTURES = 4 };

/* Its covers: the first four, as SteamGridDB ranks them, among the choices. */
static void griddb_covers(Pc5CoverSearch *search, const Pc5Http *http)
{
    char path[512], picture[PC5_PATH];
    Pc5Buf answer = { 0 };
    Pc5Json json;

    if (!search->griddb_game && !search->griddb_app) {
        search->requests--;   /* nothing to ask */
        return;
    }
    if (griddb_path(search, "grids", "600x900%2C660x930", path, sizeof(path))) return;
    if (!griddb_ask(search, http, path, &json, &answer)) {
        search->answered++;
        uint32_t data = pc5_json_get(&json, 0, "data");
        for (uint32_t i = 0, taken = 0; taken < GRIDDB_PICTURES; i++) {
            uint32_t grid = pc5_json_item(&json, data, i);
            if (!grid) break;
            if (pc5_json_text(&json, pc5_json_get(&json, grid, "url"), picture, sizeof(picture))) continue;
            add_choice(search, source_names[SOURCE_GRIDDB], search->griddb_title, picture, NULL, search->griddb_app,
                       search->griddb_score - (int)taken++ * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

/* Its backgrounds: given to its covers among the choices, the first to the first; 1920x620
 * ones first (3840x1240 is four times the download for a 960x540 background). */
static void griddb_heroes(Pc5CoverSearch *search, const Pc5Http *http)
{
    char path[512], heroes[GRIDDB_PICTURES][PC5_PATH];
    Pc5Buf answer = { 0 };
    Pc5Json json;
    int count = 0;

    if (!search->griddb_game && !search->griddb_app) {
        search->requests--;
        return;
    }
    if (griddb_path(search, "heroes", "1920x620%2C3840x1240", path, sizeof(path))) return;
    if (!griddb_ask(search, http, path, &json, &answer)) {
        search->answered++;
        search->answered_stores++;
        uint32_t data = pc5_json_get(&json, 0, "data");
        for (int small = 1; small >= 0; small--)
            for (uint32_t i = 0; count < GRIDDB_PICTURES; i++) {
                uint32_t hero = pc5_json_item(&json, data, i);
                if (!hero) break;
                int width = (int)pc5_json_number(&json, pc5_json_get(&json, hero, "width"), 0);
                if ((width == 1920) != small ||
                    pc5_json_text(&json, pc5_json_get(&json, hero, "url"), heroes[count], sizeof(heroes[0])) ||
                    strncmp(heroes[count], "https://", 8))
                    continue;
                count++;
            }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
    for (int i = 0, k = 0; i < search->count && count; i++) {
        Pc5CoverChoice *choice = &search->choices[i];
        if (strcmp(choice->source, source_names[SOURCE_GRIDDB]) || choice->background[0]) continue;
        pc5_copy(choice->background, sizeof(choice->background), heroes[k++ % count]);
    }
}

/* Its square pictures, for the game's tile: the first four, as SteamGridDB ranks them, each a
 * choice of its own (a cover's url is where it is, so they never stand for a cover). */
static void griddb_squares(Pc5CoverSearch *search, const Pc5Http *http)
{
    char path[512], picture[PC5_PATH];
    Pc5Buf answer = { 0 };
    Pc5Json json;

    if (!search->griddb_game && !search->griddb_app) {
        search->requests--;
        return;
    }
    if (griddb_path(search, "grids", "512x512%2C1024x1024", path, sizeof(path))) return;
    if (!griddb_ask(search, http, path, &json, &answer)) {
        search->answered++;
        uint32_t data = pc5_json_get(&json, 0, "data");
        for (uint32_t i = 0, taken = 0; taken < GRIDDB_PICTURES; i++) {
            uint32_t grid = pc5_json_item(&json, data, i);
            if (!grid) break;
            int width = (int)pc5_json_number(&json, pc5_json_get(&json, grid, "width"), 0),
                height = (int)pc5_json_number(&json, pc5_json_get(&json, grid, "height"), 0);
            if (width != height ||
                pc5_json_text(&json, pc5_json_get(&json, grid, "url"), picture, sizeof(picture)) ||
                strncmp(picture, "https://", 8))
                continue;
            /* Ahead of the store's covers, which only told SteamGridDB the game. */
            add_choice(search, source_names[SOURCE_GRIDDB], search->griddb_title, picture, NULL, search->griddb_app,
                       search->griddb_score + 100 - (int)taken * 2);
            for (int c = 0; c < search->count; c++)
                if (!strcmp(search->choices[c].url, picture))
                    pc5_copy(search->choices[c].icon, sizeof(search->choices[c].icon), picture);
            taken++;
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

static int by_score(const void *a, const void *b)
{
    const Pc5CoverChoice *x = a, *y = b;
    return y->score - x->score;
}

int pc5_cover_search_step(Pc5CoverSearch *search, const Pc5Http *http)
{
    int query = 0;
    Plan what = search->hint.picture_only ? PLAN_DONE : plan(search, search->step, &query);

    if (what == PLAN_DONE) {
        qsort(search->choices, (size_t)search->count, sizeof(search->choices[0]), by_score);
        return 0;
    }
    search->requests++;
    switch (what) {
    case PLAN_STEAM_APP: {
        char picture[256];
        char hero[256];
        snprintf(picture, sizeof(picture), STEAM_IMAGES "steam/apps/%u/library_600x900_2x.jpg",
                 (unsigned)search->hint.steam_appid);
        steam_hero(search->hint.steam_appid, hero, sizeof(hero));
        add_choice(search, source_names[SOURCE_STEAM], search->hint.folder, picture, hero, search->hint.steam_appid, 1000);
        search->requests--;   /* nothing was asked */
        break;
    }
    case PLAN_STEAM: steam_search(search, http, search->queries[query]); break;
    case PLAN_GOG: gog_search(search, http, search->queries[query]); break;
    case PLAN_PCGAMINGWIKI: pcgamingwiki_search(search, http, search->queries[query]); break;
    case PLAN_GRIDDB_GAME: griddb_game(search, http); break;
    case PLAN_GRIDDB_COVERS: griddb_covers(search, http); break;
    case PLAN_GRIDDB_HEROES: griddb_heroes(search, http); break;
    case PLAN_GRIDDB_SQUARES: griddb_squares(search, http); break;
    case PLAN_WIKIDATA: wikidata_find(search, http, search->queries[query]); break;
    case PLAN_WIKIDATA_APPS: wikidata_apps(search, http); break;
    default: break;
    }
    search->step++;
    if (plan(search, search->step, &query) == PLAN_DONE) {
        qsort(search->choices, (size_t)search->count, sizeof(search->choices[0]), by_score);
        return 0;
    }
    return 1;
}

/* Newer Steam apps keep their library picture under a hashed name, which the
 * store's item list gives. */
/* An asset of the app (its first named field present in its asset list) on Steam's image server. */
static int steam_asset_url(const Pc5Http *http, uint32_t appid, const char *const *fields, char *out, size_t size)
{
    char input[256], encoded[768], url[1024], format[256], file[160];
    Pc5Buf answer = { 0 };
    Pc5Json json;
    int status = -1;

    snprintf(input, sizeof(input),
             "{\"ids\":[{\"appid\":%u}],\"context\":{\"language\":\"english\",\"country_code\":\"US\"},"
             "\"data_request\":{\"include_assets\":true}}", (unsigned)appid);
    if (pc5_url_encode(input, encoded, sizeof(encoded)) ||
        pc5_format(url, sizeof(url), "https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json=%s",
                   encoded))
        return -1;
    if (!ask(http, url, &json, &answer)) {
        uint32_t item = pc5_json_item(&json, pc5_json_get(&json, pc5_json_get(&json, 0, "response"), "store_items"), 0);
        uint32_t assets = pc5_json_get(&json, item, "assets");
        int found = 0;
        for (int f = 0; fields[f] && !found; f++)
            found = !pc5_json_text(&json, pc5_json_get(&json, assets, fields[f]), file, sizeof(file));
        if (!pc5_json_text(&json, pc5_json_get(&json, assets, "asset_url_format"), format, sizeof(format)) && found) {
            char *slot = strstr(format, "${FILENAME}");
            if (slot) {
                *slot = 0;
                status = pc5_format(out, size, STEAM_IMAGES "%s%s%s", format, file, slot + 11);
            }
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
    return status;
}

/* A picture downloaded (in memory only, freed once decoded: a download cut short is no picture). */
static int download_picture(const Pc5Http *http, const char *url, Pc5Image *picture)
{
    Pc5Buf data = { 0 };
    int status = 0, result = -1;

    if (!http->get(http->context, url, &data, PICTURE_MAX, &status) && status == 200)
        result = pc5_image_decode(data.data, data.len, picture);
    pc5_buf_free(&data);
    return result;
}

/* The player's own picture, from its file. */
static int read_picture(const char *path, Pc5Image *picture)
{
    Pc5Buf data = { 0 };
    int result = -1;

    if (!pc5_read_file(path, &data, PICTURE_MAX)) result = pc5_image_decode(data.data, data.len, picture);
    pc5_buf_free(&data);
    return result;
}

int pc5_cover_fetch(const Pc5CoverChoice *choice, const Pc5Http *http, Pc5Image *cover)
{
    Pc5Image picture = { 0 };
    int status;

    *cover = (Pc5Image){ 0 };
    if (choice->url[0] == '/') {
        if (read_picture(choice->url, &picture)) return -1;
    } else if (download_picture(http, choice->url, &picture)) {
        static const char *const capsule[] = { "library_capsule_2x", "library_capsule", NULL };
        char other[512];
        if (!choice->steam_appid || steam_asset_url(http, choice->steam_appid, capsule, other, sizeof(other)) ||
            download_picture(http, other, &picture))
            return -1;
    }
    status = pc5_cover_compose(&picture, cover);
    pc5_image_free(&picture);
    return status;
}

int pc5_background_fetch(const char *url, uint32_t steam_appid, const Pc5Http *http, Pc5Image *background)
{
    static const char *const hero[] = { "library_hero_2x", "library_hero", NULL };
    Pc5Image picture = { 0 };
    char other[512];
    int status;

    *background = (Pc5Image){ 0 };
    if ((!url || !url[0] || download_picture(http, url, &picture)) &&
        (!steam_appid || steam_asset_url(http, steam_appid, hero, other, sizeof(other)) ||
         download_picture(http, other, &picture)))
        return -1;
    status = pc5_background_compose(&picture, background);
    pc5_image_free(&picture);
    return status;
}

int pc5_icon_fetch(const Pc5CoverChoice *choice, const Pc5Http *http, Pc5Image *icon)
{
    Pc5Image picture = { 0 };
    int status;

    *icon = (Pc5Image){ 0 };
    if (!choice->icon[0] || download_picture(http, choice->icon, &picture)) return -1;
    status = pc5_icon_compose(&picture, icon);
    pc5_image_free(&picture);
    return status;
}

int pc5_background_read(const char *path, Pc5Image *background)
{
    Pc5Image picture = { 0 };
    int status;

    *background = (Pc5Image){ 0 };
    if (read_picture(path, &picture)) return -1;
    status = pc5_background_compose(&picture, background);
    pc5_image_free(&picture);
    return status;
}

int pc5_cover_own(const char *dir, const char *const *names, char *out, size_t size)
{
    static const char *const kinds[] = { ".jpg", ".jpeg", ".png" };
    DIR *folder;
    struct dirent *entry;
    int found = -1;

    if (!dir || !names || !(folder = opendir(dir))) return -1;
    while (found && (entry = readdir(folder))) {
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || entry->d_name[0] == '.') continue;
        for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]) && found; k++) {
            if (pc5_casecmp(dot, kinds[k])) continue;
            for (size_t n = 0; names[n] && found; n++) {
                size_t length = strlen(names[n]);
                char base[PC5_PATH];
                if (!length || length != (size_t)(dot - entry->d_name) || length >= sizeof(base)) continue;
                memcpy(base, entry->d_name, length);
                base[length] = 0;
                if (!pc5_casecmp(base, names[n]) && !pc5_join(out, size, dir, entry->d_name) && pc5_is_file(out))
                    found = 0;
            }
        }
    }
    closedir(folder);
    return found;
}

void pc5_cover_tidy(const char *dir)
{
    static const char *const names[] = { "cover-original.jpg", "cover-original.png", "cover.bin.part",
                                         "background.bin.part", "icon.bin.part" };
    char path[PC5_PATH];

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!pc5_join(path, sizeof(path), dir, names[i])) unlink(path);
}

void pc5_cover_delete_all(const char *dir)
{
    static const char *const names[] = { "cover.bin", "background.bin", "background.txt", "background-none.txt",
                                         "background-tries.txt", "icon.bin" };
    char path[PC5_PATH];

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!pc5_join(path, sizeof(path), dir, names[i])) unlink(path);
    pc5_cover_tidy(dir);
}
