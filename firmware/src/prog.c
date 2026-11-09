/* SPDX-License-Identifier: GPL-3.0-only
 * NEON: HARMONY, a chord progression the song follows (included by seq.c after scale_snap).
 *
 * The project holds a progression of up to PROG_N chords (song.prog: each a degree of the scale, 0 = i .. 6 = VII,
 * PROG_END ends it) and how many bars each lasts (song.prog_bars: 1, 2 or 4). A track whose QUANTIZE is PROG
 * follows it: every note it plays (its steps, its keys, its arp, its chord keys) is written as if the chord were the
 * first degree and moves with the chord, by scale steps (diatonic: the chords keep the scale's own qualities), the
 * shorter way (up to three steps up, else down: a bass walks i -> VI as a third down, not a sixth up). So a pattern
 * written once, on the tonic, plays the whole progression: pad, bass and arp change chord together.
 * A PROG track's notes are written in C (its steps, the phrases; its keys: C plays the tonic) and ROOT is the key: they
 * move by ROOT (the nearer way: up to F# up, from G down) before they follow. The scale is the track's (SCALE); CHR (no
 * scale of its own) follows the natural minor of ROOT.
 * The chord in play: the bar since PLAY (prog_bar, counted from the sequencer's beat) / BARS, round the progression;
 * the block that crosses into a bar already plays that bar's chord (the steps on the bar line get the new chord).
 * Stopped, the keys follow the chord under the PROG page's SLOT (song.prog_cur): audition a chord by selecting it.
 * MOOD (MOOD_*) fills the progression with a classic one; editing a chord makes it OWN. */
#define PROG_N 8u
#define PROG_END 7u
enum { MOOD_OWN, MOOD_DRIVE, MOOD_DAWN, MOOD_NIGHT, MOOD_MYST, MOOD_HEART, MOOD_RISE, MOOD_COUNT };
static const uint8_t PROG_MOOD[MOOD_COUNT][PROG_N] = {
    {0, 7, 7, 7, 7, 7, 7, 7},          /* OWN: (MOOD itself never loads it) */
    {0, 5, 2, 6, 7, 7, 7, 7},          /* DRIVE: i VI III VII */
    {2, 6, 0, 5, 7, 7, 7, 7},          /* DAWN: III VII i VI (I V vi IV of the relative major) */
    {0, 3, 5, 4, 7, 7, 7, 7},          /* NIGHT: i iv VI v */
    {0, 6, 5, 6, 7, 7, 7, 7},          /* MYST: i VII VI VII */
    {0, 3, 6, 2, 7, 7, 7, 7},          /* HEART: i iv VII III (round the fifths) */
    {5, 6, 0, 0, 7, 7, 7, 7},          /* RISE: VI VII i i */
};
static volatile uint32_t prog_bar;     /* whole bars since PLAY (seq.c: the beat's wrap) */
static volatile uint8_t prog_deg;      /* the degree playing now (events_block, before the steps) */

static uint32_t prog_len(void)
{
    uint32_t n = 0;
    while (n < PROG_N && song.prog[n] < PROG_END)
        n++;
    return n;
}
static uint32_t prog_bars(void) { return 1u << (song.prog_bars < 3u ? song.prog_bars : 0u); }
/* the progression's chord (its index) at bar b */
static uint32_t prog_at(uint32_t b)
{
    uint32_t n = prog_len();
    return n ? (b / prog_bars()) % n : 0u;
}
/* playing: the chord of this block (ahead 1: it crosses into the next bar); stopped: the PROG page's slot. Out of line,
 * as prog_follow (once a block; their short loops stay out of the audio ISR's: target_budget.py) */
static __attribute__((noinline)) void prog_block(uint32_t ahead)
{
    uint32_t n = prog_len(), i;
    if (!n) {
        prog_deg = 0;
        return;
    }
    i = song.playing ? prog_at(prog_bar + ahead) : (song.prog_cur < n ? song.prog_cur : 0u);
    prog_deg = song.prog[i];
}

static uint32_t bits12(uint32_t m)
{
    uint32_t c = 0;
    for (; m; m &= m - 1u)
        c++;
    return c;
}
/* the scale PROG moves in: the track's; CHR: the natural minor */
static uint32_t prog_mask(const track_t *t)
{
    uint32_t m = scale_mask(t) & 0xFFFu;
    return m == 0xFFFu || !m ? SCALE_MASK[2] : m;
}
/* n moved down onto mask (root r) */
static int32_t snap_mask(int32_t n, uint32_t mask, int32_t r)
{
    uint32_t guard = 12;
    while (guard-- && !((mask >> (uint32_t)((n - r + 120) % 12)) & 1u))
        n--;
    return n;
}
/* n (on the scale) moved by d degrees of the scale mask (root r) */
static int32_t deg_move(int32_t n, int32_t d, uint32_t mask, int32_t r)
{
    int32_t cnt = (int32_t)bits12(mask), rel = n - r + 120, oct = rel / 12 - 10, pc = rel % 12, idx = 0, i;
    if (!d || cnt <= 0)
        return n;
    for (i = 0; i < pc; i++)
        idx += (int32_t)((mask >> (uint32_t)i) & 1u);
    idx += d;
    while (idx < 0) {
        idx += cnt;
        oct--;
    }
    oct += idx / cnt;
    idx %= cnt;
    for (i = 0; i < 12; i++)
        if ((mask >> (uint32_t)i) & 1u) {
            if (!idx)
                break;
            idx--;
        }
    return r + 12 * oct + i;
}
/* a note of a PROG track, as the chord now playing has it (a key, a step's note: out of line) */
static __attribute__((noinline)) int32_t prog_follow(const track_t *t, int32_t n)
{
    uint32_t mask = prog_mask(t);
    int32_t r = t->p[P_ROOT], cnt = (int32_t)bits12(mask), d = prog_deg;
    if (d >= cnt)
        d %= cnt;
    if (d > cnt / 2 - (cnt & 1 ? 0 : 1))           /* the shorter way: 7 notes: 0..3 up, 4..6 down */
        d -= cnt;
    n += r > 6 ? r - 12 : r;                        /* written in C: ROOT is the key (the nearer way: A is 3 down) */
    n = snap_mask(n, mask, r);
    return deg_move(n, d, mask, r);
}
/* the triad's quality of degree d in the track's scale: 0 major, 1 minor, 2 diminished, 3 augmented / other */
static uint32_t prog_quality(const track_t *t, uint32_t d)
{
    uint32_t mask = prog_mask(t);
    int32_t r = t->p[P_ROOT], b = deg_move(r + 60, (int32_t)d, mask, r);
    int32_t third = deg_move(b, 2, mask, r) - b, fifth = deg_move(b, 4, mask, r) - b;
    return third == 4 && fifth == 7 ? 0u : third == 3 && fifth == 7 ? 1u : third == 3 && fifth == 6 ? 2u : 3u;
}
/* degree d's root, a pitch class (0..11) */
static uint32_t prog_root_pc(const track_t *t, uint32_t d)
{
    uint32_t mask = prog_mask(t);
    int32_t r = t->p[P_ROOT];
    return (uint32_t)(deg_move(r + 60, (int32_t)d, mask, r) % 12);
}
/* "i" .. "VII" (upper case: major / augmented; lower: minor; "o" after: diminished), "-" for the end */
static void prog_roman(char *b, const track_t *t, uint32_t d)
{
    static const char *const R[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    uint32_t q, i = 0;
    const char *s;
    if (d >= 7u) {
        b[0] = '-';
        b[1] = 0;
        return;
    }
    q = prog_quality(t, d);
    for (s = R[d]; *s; s++)
        b[i++] = (char)(q == 1u || q == 2u ? *s - 'A' + 'a' : *s);
    if (q == 2u)
        b[i++] = 'o';
    b[i] = 0;
}
/* CHORD's names for the PROG page (params.c N_PCHORD): the selected track's scale */
static void prog_names(void)
{
    uint32_t d;
    for (d = 0; d < 8u; d++)
        prog_roman(prog_nm[d], TSEL, d);
}
/* MOOD: its progression in (OWN: as it is) */
static void prog_set_mood(uint32_t m)
{
    uint32_t i;
    song.prog_mood = (uint8_t)(m < MOOD_COUNT ? m : 0u);
    if (song.prog_mood == MOOD_OWN)
        return;
    for (i = 0; i < PROG_N; i++)
        song.prog[i] = PROG_MOOD[song.prog_mood][i];
}
