// daynight - one sunrise and one sunset per calendar day, as a tesmioloader
// plugin.
//
// THE BASE GAME RUNS THE TWO CLOCKS AT DIFFERENT SPEEDS
//
// The world object carries the calendar in two fields:
//
//   game+0x590   int    day of the year, 0..364
//   game+0x594   int    the year
//   game+0x59C   float  how far into the current day we are, 0..60
//
// and the day tick at 0x3346E0 is the whole of it: it adds
// `C3D_TIMER::PowerTime` to +0x59C every frame and, the moment that passes the
// 60.0f at 0x90AA90, zeroes it, increments +0x590 and wraps at 365 into +0x594.
// One calendar day is therefore 60 units of scaled game time and nothing else.
//
// The day and night themselves come from a completely separate state machine at
// 0x333B30, and it steps once per **calendar day**:
//
//   phase = (day of year) % 13
//
//     0..5, 11, 12   daylight, with whatever weather the random roll picked
//     6              weathers/sunset2.ini
//     7              weathers/night.ini, night factor ramping 0 -> 1
//     8              weathers/night.ini, full night
//     9              weathers/sunset2.ini, night factor ramping 1 -> 0
//     10             weathers/day1.ini, forced clear
//
// so a full day/night cycle takes **thirteen in-game days**, and the night part
// of it lasts two of them. That is the desynchronisation: the date in the
// corner advances thirteen times for every sunrise.
//
//
// EXACTLY THREE PLACES DIVIDE BY 13, AND ONLY TWO OF THEM ARE THE WORLD'S
//
// Scanning .text for the magic constant 0x4EC4EC4F - what the compiler emits
// for a signed `% 13` - finds three sites and no more:
//
//   0x333BA5   in the weather state machine at 0x333B30. Reads game+0x590
//   0x5CFBB1   in the light factor at 0x5CFB70. Reads game+0x590
//   0x9795     in the lighting cross-fade at 0x8F50. Reads 0x9D54A0, which is
//              **not** the world object - a separate clock. Left alone.
//
// 0x5CFB70 is the second copy of the same 13-phase logic, written out again. It
// returns a float - how lit the world is - and is read by the renderer and by
// two of the electricity functions. Patching only the state machine would have
// moved the sky and left the street lights on the old thirteen-day schedule.
//
//
// WHAT THIS PLUGIN DOES: IT LIES TO BOTH OF THEM ABOUT THE DATE
//
// Nothing is patched. Both functions are hooked, and each is handed a **day and
// a time-in-day re-derived from the position inside the current calendar day**
// rather than from the calendar itself:
//
//   u  = (day mod cycle_days) * 60 + time, over cycle_days * 60      0 .. 1
//   k  = floor(u * 13)                        which of the 13 phases  0 .. 12
//   t' = (u * 13 - k) * 60                    where inside it         0 .. 60
//   d' = a day congruent to k modulo 13, near the real one
//
// With `cycle_days = 13` this is the identity: k is day % 13, t' is t, d' is
// day. **Installing the plugin and leaving it at 13 would change nothing at
// all**; the default is 1, which is the synchronisation the plugin exists for.
//
// The two hooks differ in how the lie is delivered, and deliberately:
//
//   0x333B30  WRITES to the world object - the weather index, the night factor
//             and two flags - so it has to be handed the real object. The two
//             fields are overwritten, the original is called, and they are put
//             back. It runs on the simulation tick, immediately after the day
//             tick that owns those fields (both are called back to back from
//             0x30D100), so nothing else is looking at them in between.
//
//   0x5CFB70  READS and returns a float; it writes nothing. Every object field
//             it touches was read out of its 482 bytes of disassembly -
//             +0x5C4, +0x590, +0x59C, +0xE70, +0xE28, and no others - so it is
//             handed a **scratch object of this plugin's own** with those five
//             fields filled in. The world object is never touched, which is
//             what makes it safe from the render thread.
//
// D' IS PICKED SO THAT IT LANDS IN THE SAME SEASON AS THE REAL DATE, and that
// is not decoration. 0x333B30 does not only take `day % 13` off the field: it
// also calls the snow-season test at 0x334340 with it, and compares it against
// 284/326/328 for the overcast season. A residue class modulo 13 has a
// representative every thirteen days, so version 1.x's "the next day at or
// after the real one" swept a thirteen-day window over the course of a single
// calendar day - and near a winter boundary that window straddles it, so the
// weather flickered in and out of winter thirteen times a day. The plugin now
// walks the representatives outward from the real date and takes the first one
// the game's own 0x334340 puts in the same season. Every season is at least a
// hundred days long, so one always exists.
//
//
// AND THE CYCLE THEN RUNS THIRTEEN TIMES TOO FAST, WHICH IS THE OTHER HALF
//
// Compressing the cycle into one calendar day does not slow the calendar, so
// the cycle comes out thirteen times faster than the base game's. A calendar
// day is 60 units of scaled game time, which is a few seconds of real time, so
// the whole thirteen-step cycle goes by in that. It reads as "night tried to
// start and changed its mind", because that is exactly what it looks like.
//
// `day_scale` is what makes a calendar day last longer, and **it scales the
// whole simulation clock, not the calendar alone**. That distinction is the
// entire content of version 2.0 and the reason it exists:
//
//   Version 1.x stretched the day by giving back most of what the day tick had
//   just added to +0x59C. The calendar then advanced thirteen times slower in
//   real time - and NOTHING ELSE DID. Every rate in the game is integrated from
//   C3D_TIMER::PowerTime per frame, so production, consumption, heating and
//   pollution kept running at their vanilla real-time pace while everything
//   counted in calendar days ran thirteen times slower. That is not a cosmetic
//   mismatch; it breaks the economy in specific, reproducible ways:
//
//     * POLLUTION. The decay pass is FUN_1404d5e80 at 0x4D5E80, called once per
//       calendar day from the day tick and from nowhere else (its only xref is
//       0x334A64, inside 0x3346E0). It subtracts a flat 0.06 and 0.005 from
//       every cell of the pollution grid at game+0x120A8. Emission is per
//       frame, off PowerTime. Thirteen times the emission between two
//       subtractions is thirteen times the pollution, and it never settles.
//
//     * WINTER. Buildings with no district heating burn fuel locally and that
//       emits pollution, so the runaway above peaks in winter - and the same
//       daily pass accumulates each residential building's exposure at
//       +0x11B0, clamped to 1.0, from a sample capped at 3.0. With the grid
//       thirteen times too high every home pins at full exposure, citizens
//       sicken, and the population falls. The season boundaries themselves are
//       days of the year (25/50/52/128 and 245/247/326 by climate, in
//       0x334340), so a thirteen-times-slower calendar also makes each winter
//       thirteen times longer in real time.
//
//     * LOANS. The loan list is the 0x28-byte vector at game+0x10BD0 and
//       FUN_1404b93f0 is its daily payment: it decrements the remaining TERM IN
//       DAYS at record+0x10, compounds the interest, and pays
//       balance / days-left. It is driven by FUN_1404b95c0, called once per
//       calendar day from the day tick (only xref 0x334C48). Thirteen times
//       fewer payments against an income that did not slow is a loan that never
//       moves. Anything else scheduled by date - used-vehicle offers, expiring
//       notifications at 0x4CD2B0, the random-event roll at 0x483880 - is the
//       same story.
//
// So the fix is not to compensate each of them. It is to stop desynchronising
// the two in the first place: the simulation clock itself is scaled, and every
// per-day and every per-frame quantity slows by exactly the same factor. The
// game is then bit-for-bit its vanilla self, running slower in real time.
//
// HOW: THE SIMULATION CLOCK IS ONE C3D_TIMER AND THE GAME ALREADY SCALES IT
//
// The whole simulation reads one timer, the C3D_TIMER at 0x9D4EE0 - 337 sites
// reference it, the shop tick and the mine tick among them. C3D_TIMER::PowerTime
// is four instructions:
//
//   if (this[0xC] && !ignorePause) return 0;
//   if (this[0xD] && !realTime)    return 0;
//   return v * K / (realTime ? this[4] : this[0]);
//
// this[0] is the frame rate the game time is divided by and this[4] is the real
// one, which is why `realTime` callers must not be touched. And the game's own
// speed control is a multiplication of exactly that field: 0x105A90 calls
// C3D_TIMER::Start on it every frame and then multiplies it by 0.35, 0.05 or
// 0.01 for the three speeds, or by 3, 5 or 1000 for the slow modes. **Scaling
// it is not a new mechanism, it is one more speed step**, which is also why a
// factor of thirteen is safe: the engine already ships a mode that divides the
// step by a thousand.
//
// It is done as an import swap on the three C3D_TIMER::Power* the executable
// imports rather than as a write to the field, because that is stateless: it
// cannot compound if a frame runs the site twice and it cannot be missed if a
// frame skips it. Only calls on 0x9D4EE0 with `realTime` false are scaled, so
// the engine's own internal timing and every deliberate real-time query are
// left exactly alone.
//
// THE COST IS REAL AND IT IS HONEST: the game runs `day_scale` times slower in
// real time, all of it, in step. Raising the game speed cancels it exactly, and
// now that really is true rather than merely claimed.
//
// The lighting cross-fade duration at game+0xED4 is the one number that does
// not scale itself. 0x333F80 writes 15.0f into it on every weather change - a
// quarter of a calendar day. Its own clock is the timer at 0xA558A0, not the
// simulation one, so it is not slowed and the fade is rewritten to the same
// fraction of the new phase after every tick.
//
// WHAT IS NOT FIXED, AND IS THE BASE GAME'S: 0x333F80 allocates two
// C3D_LIGHTING objects on every weather change and never frees the previous
// pair. Four changes per cycle used to mean four per thirteen days; it now
// means four per day. Roughly 5 KB of leak per real minute at 1x speed - the
// textures behind them come from the managed cache and are not duplicated.
//
// Every address is SOVIET64.exe v1.1.1.9 and is verified byte for byte before
// anything is hooked. See docs/15-daynight.md.

#include "../../src/tesmio_plugin.h"

// ---------------------------------------------------------------- the sites

// The weather and day/night state machine. Called once per tick with the world
// object in rcx and a "this is a fresh world" flag in dl.
#define RVA_WEATHER_TICK  0x333BD0

// How lit the world is, 0..1. Pure, reads five fields of the world object and
// returns a float in xmm0.
#define RVA_LIGHT_FACTOR  0x5CFC40

// `char IsSnowSeason(world)` - reads world+0x590 and the climate out of the
// settings object, and answers whether that day of the year is in the snow
// part of it. Called by the weather tick, which is why the shadowed date has
// to agree with the real one about it.
#define RVA_SNOW_SEASON   0x3343E0

// The C3D_TIMER every simulation rate in the game is integrated from.
#define RVA_SIM_TIMER     0x9D4EE0

// In the day tick at 0x3346E0: `lea rcx,[sim timer]` / `xor r9d,r9d` /
// `xor r8d,r8d` / `call qword ptr [PowerTime]` / `addss xmm0,[rbp+0x59C]`.
// Verified as a unit, because between them those bytes prove that 0x9D4EE0 is a
// C3D_TIMER, that this call is on it, and that **what comes back is added
// straight into the calendar** - which is what makes the return address a
// reliable way to tell the calendar's own tick from every other consumer.
#define RVA_DAYTICK_POWER 0x334AA0
#define RVA_DAYTICK_RET   (RVA_DAYTICK_POWER + 0x13)   // the instruction after the call

// The three per-calendar-day passes worth running at the world's pace when the
// world and the calendar are deliberately given different speeds. Each takes
// the world object and nothing else, each is called from the day tick and from
// nowhere else, and each is pure bookkeeping - so calling it again is exactly
// "another day's worth happened", with no event, allocation or notification
// invented that the game would not have made anyway.
#define RVA_DAILY_POLLUTION 0x4D5E80   // decay the grid, accumulate exposure
#define RVA_DAILY_LOANS     0x4B95C0   // one day of interest and repayment
#define RVA_DAILY_NOTICES   0x4CD2B0   // age the notification list, drop the expired

// The 60.0f one calendar day lasts, as the day tick at 0x3346E0 reads it.
#define RVA_DAY_LENGTH    0x90AA78
#define VANILLA_DAY_LEN   60.0f

// `mov dword ptr [r10+0xED4], 0x41700000` in 0x333F80 - the cross-fade duration
// written on every weather change. The opcode is checked and the immediate is
// read rather than assumed.
#define RVA_FADE_MOV      0x334224
#define RVA_FADE_IMM      (RVA_FADE_MOV + 7)
#define VANILLA_FADE      15.0f

// The three C3D_TIMER::Power* the executable imports. All three have the same
// shape and the same two frame-rate fields; the game uses PowerTime for time,
// Power for a plain rate and PowerKmh for vehicle speeds, so scaling one and
// not the others would leave the traffic running at full speed through a slow
// world.
#define SYM_POWER         "?Power@C3D_TIMER@@QEAAMM_N0@Z"
#define SYM_POWERTIME     "?PowerTime@C3D_TIMER@@QEAAMM_N0@Z"
#define SYM_POWERKMH      "?PowerKmh@C3D_TIMER@@QEAAMM_N0@Z"

// World object fields. Everything here was read off the disassembly of the two
// hooked functions and the day tick.
#define OFF_WEATHER_ON    0x5C4   // int   weather simulated at all
#define OFF_DAY           0x590   // int   day of the year, 0..364
#define OFF_YEAR          0x594   // int
#define OFF_DAYTIME       0x59C   // float 0..60 into the current day
#define OFF_SNOWING       0xE28   // int   read by the light factor
#define OFF_WEATHER       0xE70   // int   current weathers/*.ini
#define OFF_NIGHT         0xE74   // float night factor, 0..1
#define OFF_FADE_ELAPSED  0xED0   // float how far into a lighting cross-fade
#define OFF_FADE_LEN      0xED4   // float how long a lighting cross-fade takes

// The highest object offset the light factor reads, rounded up. Its scratch
// copy has to be at least this big.
#define LIGHT_SCRATCH     0xE30

// The snow-season test at 0x334340 reads one field of its argument, +0x590.
#define SEASON_SCRATCH    0x5A0

// The number of phases in the game's own cycle. It is not a setting: it is how
// many cases 0x333B30 and 0x5CFB70 are written with.
#define PHASES            13

#define DAYS_IN_YEAR      365

// The most extra daily passes one tick may run, so a stall or a bad figure
// cannot turn into a freeze.
#define MAX_CATCHUP       64

static const BYTE kWeatherTickPrologue[] = {
    0x40, 0x53,                                  // push rbx
    0x41, 0x55,                                  // push r13
    0x48, 0x83, 0xEC, 0x58,                      // sub  rsp,0x58
    0x83, 0xB9, 0xC4, 0x05, 0x00, 0x00, 0x00     // cmp  dword ptr [rcx+0x5C4],0
};

static const BYTE kLightFactorPrologue[] = {
    0x40, 0x53,                                  // push rbx
    0x48, 0x83, 0xEC, 0x50,                      // sub  rsp,0x50
    0x83, 0xB9, 0xC4, 0x05, 0x00, 0x00, 0x00,    // cmp  dword ptr [rcx+0x5C4],0
    0x48, 0x8B, 0xD9                             // mov  rbx,rcx
};

// `mov dword ptr [r10+0xED4], imm32`
static const BYTE kFadeMov[] = { 0x41, 0xC7, 0x82, 0xD4, 0x0E, 0x00, 0x00 };

// The snow-season test, minus the displacement of its one rip-relative load.
static const BYTE kSnowHead[]  = { 0x48, 0x8B, 0x05 };                     // mov rax,[rip+X]
static const BYTE kSnowTail[]  = { 0x48, 0x8B, 0x90, 0xD8, 0x0E, 0x00, 0x00,   // mov rdx,[rax+0xED8]
                                   0x44, 0x8B, 0x82, 0xEC, 0x08, 0x00, 0x00 }; // mov r8d,[rdx+0x8EC]

// ---------------------------------------------------------------- settings

// What `day_scale` stretches - see daynight.ini for the full case against
// SLOW_CALENDAR. SLOW_WORLD is 2.0's fix (the shared C3D_TIMER, scaling
// everything the simulation runs off alike); SLOW_CALENDAR is 1.x's, kept only
// as the control that reproduces the three bugs 2.0 fixed; SLOW_NONE is
// `day_scale = 1`, no stretch at all.
enum { SLOW_NONE = 0, SLOW_CALENDAR = 1, SLOW_WORLD = 2 };

static int   g_enabled    = 1;
static int   g_cycleDays  = 1;          // PHASES is the base game exactly
static float g_offset     = 0.0f;       // rotates the cycle, 0..1 of a cycle
static int   g_fade       = 1;
static int   g_probe      = 0;
static int   g_slow       = SLOW_WORLD;

// How many times longer a calendar day lasts than the base game's. 0 means
// `auto`: PHASES / cycle_days, which is the value that leaves one whole
// day/night cycle taking exactly as long in real time as the base game's did.
static float g_dayScale   = 0.0f;

static float g_dayLen     = VANILLA_DAY_LEN;
static float g_fadeLen    = VANILLA_FADE;

// Edge detection for "this is the first tick of a new phase". The base game
// gets that for free - a phase is a day, and the day tick leaves +0x59C at
// exactly 0.0f - and the weather roll and the overcast timer both key on it.
static int   g_lastPhase  = -1;

// What the day timer was left at last tick, for SLOW_CALENDAR only.
static float g_written    = -1.0f;

static unsigned  g_ticks       = 0;

// Probe bookkeeping: real milliseconds per phase, per weather change and per
// calendar day, which is the only way to tell "too fast to see" from "not
// happening".
static ULONGLONG g_phaseAt     = 0;
static ULONGLONG g_dayAt       = 0;
static ULONGLONG g_weatherAt   = 0;
static int       g_lastWeather = -1;
static int       g_lastDay     = -1;
static int       g_phasesInDay = 0;

typedef void  (*t_WeatherTick)(void* world, char fresh);
typedef float (*t_LightFactor)(void* world);
typedef char  (*t_SnowSeason)(void* world);

// float C3D_TIMER::Power*(float v, bool ignorePause, bool realTime) - rcx is
// `this`, xmm1 the value, r8b and r9b the two flags. One shape for all three.
typedef float (*t_Power)(void* self, float v, char ignorePause, char realTime);

static t_WeatherTick o_WeatherTick;
static t_LightFactor o_LightFactor;
static t_SnowSeason  g_SnowSeason;      // called, never hooked

static t_Power o_Power;
static t_Power o_PowerTime;
static t_Power o_PowerKmh;

// The one timer the simulation is integrated from, and what its game-time
// answers are divided by. Both are written once, before any hook is installed.
static void*  g_simTimer = 0;
static float  g_simDiv   = 1.0f;

// Cleared for good the first time reading the season faults, which can only
// happen if 0x334340's globals are not up yet. The date then falls back to the
// nearest representative, which is what version 1.x always used.
static int    g_seasonSafe = 1;

// The light factor's stand-in object. Thread local and zero initialised once,
// so the render thread and the simulation thread cannot be handed the same one
// and nothing but the five fields below is ever written into it.
static __declspec(thread) BYTE t_scratch[LIGHT_SCRATCH];

// And the snow-season test's, which needs one field.
static __declspec(thread) BYTE t_season[SEASON_SCRATCH];

// -------------------------------------------------------- the clock scale
//
// One wrapper per imported Power*, and they are deliberately as small as they
// look: PowerTime alone is called thousands of times a frame.
//
// `realTime` true means the caller asked for wall-clock time on purpose - the
// callee divides by this[4], the unscaled frame rate, instead of this[0]. Those
// callers are the ones that must keep running at full speed whatever the game
// speed is, so they are passed through untouched.

static float h_Power(void* self, float v, char ignorePause, char realTime)
{
    float r = o_Power(self, v, ignorePause, realTime);
    if (!realTime && self == g_simTimer) r /= g_simDiv;
    return r;
}

static float h_PowerTime(void* self, float v, char ignorePause, char realTime)
{
    float r = o_PowerTime(self, v, ignorePause, realTime);
    if (!realTime && self == g_simTimer) r /= g_simDiv;
    return r;
}

static float h_PowerKmh(void* self, float v, char ignorePause, char realTime)
{
    float r = o_PowerKmh(self, v, ignorePause, realTime);
    if (!realTime && self == g_simTimer) r /= g_simDiv;
    return r;
}

// ---------------------------------------------------------------- the mapping

// Where inside the 13-phase cycle the calendar currently is: which phase, and
// where inside it rescaled onto the 0..60 the game expects, so every
// proportion inside a phase survives untouched.
static void Phase(int day, float t, float* outTime, int* outPhase)
{
    // cycle_days = 13 is the base game, and is short circuited rather than
    // computed: the general path would come back to the same answer only to
    // within a rounding error, and "changing nothing changes nothing" is worth
    // more than the three lines.
    if (g_cycleDays == PHASES && g_offset == 0.0f)
    {
        int k = day % PHASES;
        if (k < 0) k += PHASES;
        *outTime = t; *outPhase = k;
        return;
    }

    if (t < 0.0f)       t = 0.0f;
    if (t > g_dayLen)   t = g_dayLen;

    int dc = day % g_cycleDays;
    if (dc < 0) dc += g_cycleDays;

    // 0..1 across one whole cycle, then rotated by `offset`.
    float u = ((float)dc * g_dayLen + t) / ((float)g_cycleDays * g_dayLen) + g_offset;
    u -= (float)(int)u;
    if (u < 0.0f) u += 1.0f;

    float up = u * (float)PHASES;
    int   k  = (int)up;
    if (k < 0)          k = 0;
    if (k > PHASES - 1) k = PHASES - 1;

    *outTime  = (up - (float)k) * g_dayLen;
    *outPhase = k;
}

// Every day of the year congruent to `k` modulo 13, ordered by how far it is
// from the real date and clipped to the year. The candidates are thirteen
// apart, so eight of them span a hundred days either way and at least one
// always survives the clipping.
static int Candidates(int day, int k, int* out, int max)
{
    int m = (k - day) % PHASES;
    if (m < 0) m += PHASES;

    int d[8], dist[8], n = 0;
    for (int j = -3; j <= 4 && n < 8; j++)
    {
        int c = day + m - PHASES * j;
        if (c < 0 || c >= DAYS_IN_YEAR) continue;

        int e = c - day;
        if (e < 0) e = -e;

        int p = n++;
        while (p > 0 && dist[p - 1] > e) { dist[p] = dist[p - 1]; d[p] = d[p - 1]; p--; }
        dist[p] = e;
        d[p]    = c;
    }

    int r = (n < max) ? n : max;
    for (int i = 0; i < r; i++) out[i] = d[i];
    return r;
}

// The day to hand the state machine: congruent to the phase modulo 13, as near
// the real date as that allows, and - when the game's own snow-season test can
// be reached - on the same side of every winter boundary as the real date.
//
// Without the second condition a single calendar day sweeps a thirteen-day
// window over the date, and near a boundary that window straddles it: the
// weather then flips in and out of winter thirteen times a day. Every season in
// 0x334340 is at least a hundred days long and the representatives are thirteen
// apart, so a matching one always exists.
//
// The test is asked on a **scratch object of the plugin's own**, the same trick
// the light factor uses: 0x334340's 120 bytes reference their argument exactly
// twice and both are `[arg+0x590]`, everything else being an absolute global.
// So the world object is never written to answer a question about it.
static int PickDay(int day, int k)
{
    int cand[8];
    int n = Candidates(day, k, cand, 8);
    if (n == 0) return day;
    if (cand[0] == day) return day;             // includes the identity case

    int chosen = cand[0];
    if (g_seasonSafe && g_SnowSeason)
    {
        BYTE* s = t_season;
        __try
        {
            *(int*)(s + OFF_DAY) = day;
            int want = g_SnowSeason(s) ? 1 : 0;

            for (int i = 0; i < n; i++)
            {
                *(int*)(s + OFF_DAY) = cand[i];
                if ((g_SnowSeason(s) ? 1 : 0) == want) { chosen = cand[i]; break; }
            }
        }
        __except (FaultFilter("daynight season", GetExceptionInformation()))
        {
            g_seasonSafe = 0;
            chosen = cand[0];
            Logf("daynight  the snow-season test faulted - the shadowed date is "
                 "the nearest one from here on, and winter may start a few days out");
        }
    }
    return chosen;
}

// ------------------------------------------------------- SLOW_CALENDAR only
//
// Version 1.x's day stretch, kept because it is the control that reproduces the
// bugs version 2.0 fixes and nothing else will. It gives back most of what the
// day tick just added to +0x59C, which slows the calendar and NOTHING ELSE -
// see the header. `raw` below the last written value means the day tick rolled
// the field over and a new day has started.
static float SlowTheCalendar(BYTE* world)
{
    float raw = *(float*)(world + OFF_DAYTIME);
    float out = raw;

    if (g_written < 0.0f) { g_written = raw; return raw; }

    if (g_dayScale > 1.0f && raw >= g_written)
        out = g_written + (raw - g_written) / g_dayScale;

    if (out > g_dayLen) out = g_dayLen;
    *(float*)(world + OFF_DAYTIME) = out;
    g_written = out;
    return out;
}

// ---------------------------------------------------------------- the hooks

// The state machine. It writes the weather index, the night factor and two
// flags into the world object, so it gets the real object with two fields
// temporarily rewritten rather than a copy.
static void h_WeatherTick(BYTE* world, char fresh)
{
    int   dayWas   = 0;
    float timeWas  = 0.0f;
    int   phase    = -1;
    bool  shadowed = false;

    // A non-zero second argument is the world-load path, and the only caller
    // that takes it is 0x294318. Forget the last phase there: the first tick
    // after a load has no previous one to have crossed a boundary from.
    if (fresh)
    {
        g_lastPhase = -1;
        g_written   = -1.0f;
    }

    if (g_enabled && world)
    {
        __try
        {
            dayWas  = *(int*)(world + OFF_DAY);
            timeWas = (g_slow == SLOW_CALENDAR) ? SlowTheCalendar(world)
                                                : *(float*)(world + OFF_DAYTIME);

            float time = timeWas;
            Phase(dayWas, timeWas, &time, &phase);
            int day = PickDay(dayWas, phase);

            // A phase boundary has to look exactly like a day boundary did:
            // the game's random weather roll and its "clear the overcast"
            // timer both fire off +0x59C being 0.0f on the nose. The very
            // first tick is not a boundary, and must pass the real figure
            // through - that is what keeps cycle_days = 13 the identity.
            if (g_lastPhase >= 0 && phase != g_lastPhase)
            {
                time = 0.0f;
                if (g_probe)
                {
                    ULONGLONG now = GetTickCount64();
                    Logf("daynight  phase %2d -> %2d  day %3d (as %3d)  t %6.2f  weather %d  "
                         "night %.2f  (%llu ms in the last phase)",
                         g_lastPhase, phase, dayWas, day, timeWas,
                         *(int*)(world + OFF_WEATHER), *(float*)(world + OFF_NIGHT),
                         g_phaseAt ? now - g_phaseAt : 0);
                    g_phaseAt = now;
                }
                g_phasesInDay++;
            }
            g_lastPhase = phase;

            *(int*)  (world + OFF_DAY)     = day;
            *(float*)(world + OFF_DAYTIME) = time;
            shadowed = true;
        }
        __except (FaultFilter("daynight weather tick", GetExceptionInformation()))
        {
            g_enabled = 0;
            shadowed  = false;
            Logf("daynight  disabled after a fault - the game keeps its own cycle");
        }
    }

    o_WeatherTick(world, fresh);

    if (shadowed)
    {
        __try
        {
            // Unconditionally, including over the one write the original makes
            // to +0x59C itself at 0x333C03. That write zeroes the *calendar
            // day timer* when the weather changes under the editor flag at
            // +0x1090 - harmless when a phase was a day, and a lost day now.
            *(int*)  (world + OFF_DAY)     = dayWas;
            *(float*)(world + OFF_DAYTIME) = timeWas;

            // 0x333F80 has just written a fixed 15.0f in here if the weather
            // changed. A phase is shorter now, so the fade has to be.
            if (g_fade) *(float*)(world + OFF_FADE_LEN) = g_fadeLen;
        }
        __except (FaultFilter("daynight restore", GetExceptionInformation()))
        {
            g_enabled = 0;
            Logf("daynight  disabled after a fault putting the date back");
        }
    }

    // Everything below is the probe, and it is all after the restore so it
    // reads what the rest of the game will see. Three edges are worth a line
    // each, and between them they say whether the cycle is running at all and
    // how fast: the date, the lighting the engine was told to load, and a
    // periodic figure to catch a value that is stuck.
    if (g_probe && world)
    {
        __try
        {
            ULONGLONG now     = GetTickCount64();
            int       weather = *(int*)  (world + OFF_WEATHER);
            float     night   = *(float*)(world + OFF_NIGHT);

            if (weather != g_lastWeather)
            {
                static const char* kName[] = { "day1", "sunset2", "night", "overcast1" };
                Logf("daynight  lighting %-9s -> %-9s  day %3d  t %6.2f  phase %2d  "
                     "fade %.2f  (%llu ms since the last change)",
                     (g_lastWeather >= 0 && g_lastWeather < 4) ? kName[g_lastWeather] : "?",
                     (weather >= 0 && weather < 4) ? kName[weather] : "?",
                     dayWas, timeWas, phase,
                     *(float*)(world + OFF_FADE_LEN),
                     g_weatherAt ? now - g_weatherAt : 0);
                g_lastWeather = weather;
                g_weatherAt   = now;
            }

            if (dayWas != g_lastDay)
            {
                if (g_lastDay >= 0)
                    Logf("daynight  day %3d -> %3d  took %llu ms of real time, "
                         "%d phase change(s) in it",
                         g_lastDay, dayWas, g_dayAt ? now - g_dayAt : 0, g_phasesInDay);
                g_lastDay     = dayWas;
                g_dayAt       = now;
                g_phasesInDay = 0;
            }

            if ((++g_ticks & 0xFF) == 0)
                Logf("daynight  probe day %3d  t %6.2f/%.0f  phase %2d  weather %d  "
                     "night %.3f  fade %.2f/%.2f  sim clock /%.2f",
                     dayWas, timeWas, g_dayLen, phase, weather, night,
                     *(float*)(world + OFF_FADE_ELAPSED),
                     *(float*)(world + OFF_FADE_LEN), g_simDiv);
        }
        __except (FaultFilter("daynight probe", GetExceptionInformation())) { }
    }
}

// How lit the world is. Pure, so it is handed a stand-in object rather than the
// real one - nothing shared is written and the render thread cannot see a
// half-applied date. The season does not reach this one: it reads the snowing
// flag at +0xE28 straight off the real world, so the nearest representative of
// the phase is all it needs and no world write is done from the render side.
static float h_LightFactor(BYTE* world)
{
    if (!g_enabled || !world) return o_LightFactor(world);

    BYTE* s = t_scratch;
    __try
    {
        int   day  = *(int*)  (world + OFF_DAY);
        float time = *(float*)(world + OFF_DAYTIME);
        int   phase = 0;
        Phase(day, time, &time, &phase);

        int cand[2];
        if (Candidates(day, phase, cand, 2) > 0) day = cand[0];

        *(int*)  (s + OFF_WEATHER_ON) = *(int*)(world + OFF_WEATHER_ON);
        *(int*)  (s + OFF_WEATHER)    = *(int*)(world + OFF_WEATHER);
        *(int*)  (s + OFF_SNOWING)    = *(int*)(world + OFF_SNOWING);
        *(int*)  (s + OFF_DAY)        = day;
        *(float*)(s + OFF_DAYTIME)    = time;
    }
    __except (FaultFilter("daynight light factor", GetExceptionInformation()))
    {
        g_enabled = 0;
        Logf("daynight  disabled after a fault reading the world object");
        return o_LightFactor(world);
    }

    return o_LightFactor(s);
}

// ---------------------------------------------------------------- setup

static void ReadSettings()
{
    const char* ini = "plugins\\daynight.ini";
    char v[64];

    g_enabled   = H->configInt(ini, "daynight", "enabled", g_enabled);
    g_cycleDays = H->configInt(ini, "daynight", "cycle_days", g_cycleDays);
    g_fade      = H->configInt(ini, "daynight", "fade", g_fade);
    g_probe     = H->configInt(ini, "daynight", "probe", g_probe);

    if (H->configString(ini, "daynight", "offset", v, sizeof(v), "") && v[0])
        g_offset = (float)atof(v);

    if (H->configString(ini, "daynight", "slow", v, sizeof(v), "world") && v[0])
    {
        Trim(v);
        if      (_stricmp(v, "none")     == 0) g_slow = SLOW_NONE;
        else if (_stricmp(v, "calendar") == 0) g_slow = SLOW_CALENDAR;
        else                                   g_slow = SLOW_WORLD;
    }

    if (g_cycleDays < 1)            g_cycleDays = 1;
    if (g_cycleDays > DAYS_IN_YEAR) g_cycleDays = DAYS_IN_YEAR;

    g_offset -= (float)(int)g_offset;
    if (g_offset < 0.0f) g_offset += 1.0f;

    // `auto` is PHASES / cycle_days, and it is the value that leaves one whole
    // day/night cycle taking exactly as long in real time as the base game's
    // did - which is what makes the result watchable rather than a flicker. It
    // is also 1.0 at cycle_days = 13, so that case still changes nothing.
    g_dayScale = 0.0f;
    if (H->configString(ini, "daynight", "day_scale", v, sizeof(v), "auto") && v[0])
    {
        Trim(v);
        if (_stricmp(v, "auto") != 0) g_dayScale = (float)atof(v);
    }
    if (g_dayScale <= 0.0f) g_dayScale = (float)PHASES / (float)g_cycleDays;
    if (g_dayScale < 1.0f)  g_dayScale = 1.0f;      // shortening the day is not on offer
    if (g_dayScale > 100.0f) g_dayScale = 100.0f;

    if (g_slow == SLOW_NONE) g_dayScale = 1.0f;
}

// Reads the two constants this plugin scales against out of the executable
// rather than carrying copies of them, and refuses if either is not what this
// build is known to have.
static bool ReadConstants()
{
    float dayLen = *(const float*)(g_exeBase + RVA_DAY_LENGTH);
    if (dayLen != VANILLA_DAY_LEN)
    {
        Logf("daynight  day length at 0x%X is %.3f, expected %.0f - refusing",
             RVA_DAY_LENGTH, dayLen, VANILLA_DAY_LEN);
        return false;
    }
    g_dayLen = dayLen;

    const BYTE* mov = g_exeBase + RVA_FADE_MOV;
    if (memcmp(mov, kFadeMov, sizeof(kFadeMov)) != 0)
    {
        Logf("daynight  rva 0x%X is not the cross-fade mov - fade left alone", RVA_FADE_MOV);
        g_fade = 0;
        return true;
    }

    float fade = *(const float*)(g_exeBase + RVA_FADE_IMM);
    if (fade != VANILLA_FADE)
    {
        Logf("daynight  cross-fade at 0x%X is %.3f, expected %.0f - fade left alone",
             RVA_FADE_IMM, fade, VANILLA_FADE);
        g_fade = 0;
        return true;
    }

    // The same fraction of a phase the base game used: a quarter of it. The
    // day scale belongs in here because the cross-fade's own clock is not the
    // simulation one - 0x8F50 accumulates +0xED0 from the timer at 0xA558A0,
    // which this plugin does not slow - so a phase lasts `day_scale` times
    // longer in the units the fade is measured in. With `day_scale = auto` the
    // two cancel and the answer is the vanilla 15 again.
    g_fadeLen = fade * (float)g_cycleDays * g_dayScale / (float)PHASES;
    return true;
}

// The snow-season test, checked instruction by instruction apart from the
// displacement of its one rip-relative load. Not fatal: without it the date
// handed to the state machine is simply the nearest representative, which is
// what version 1.x always used.
static void ResolveSeason()
{
    const BYTE* p = g_exeBase + RVA_SNOW_SEASON;
    if (memcmp(p, kSnowHead, sizeof(kSnowHead)) == 0 &&
        memcmp(p + 7, kSnowTail, sizeof(kSnowTail)) == 0)
    {
        g_SnowSeason = (t_SnowSeason)p;
        return;
    }
    Logf("daynight  rva 0x%X is not the snow-season test - the shadowed date will "
         "be the nearest one, and winter may start a few days out", RVA_SNOW_SEASON);
}

// Proves that 0x9D4EE0 is the C3D_TIMER the calendar is driven from, out of the
// day tick's own call to PowerTime on it:
//
//   lea  rcx,[rip+X]          ->  0x9D4EE0
//   xor  r9d,r9d              ->  realTime  = false
//   xor  r8d,r8d              ->  ignorePause = false
//   call qword ptr [rip+Y]    ->  the PowerTime import slot
//
// Both displacements are resolved and compared rather than matched as bytes,
// so this survives the executable moving and refuses if anything else does.
static bool VerifySimTimer()
{
    const BYTE* p = g_exeBase + RVA_DAYTICK_POWER;

    if (p[0] != 0x48 || p[1] != 0x8D || p[2] != 0x0D)
    {
        Logf("daynight  rva 0x%X is not a `lea rcx` - cannot confirm the simulation clock",
             RVA_DAYTICK_POWER);
        return false;
    }
    const BYTE* timer = p + 7 + *(const int*)(p + 3);
    if (timer != g_exeBase + RVA_SIM_TIMER)
    {
        Logf("daynight  the day tick's timer is +0x%llX, expected +0x%X - refusing to scale it",
             (unsigned long long)(timer - g_exeBase), RVA_SIM_TIMER);
        return false;
    }

    static const BYTE kFlags[] = { 0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0 };  // xor r9d / xor r8d
    if (memcmp(p + 7, kFlags, sizeof(kFlags)) != 0 || p[13] != 0xFF || p[14] != 0x15)
    {
        Logf("daynight  rva 0x%X is not the day tick's PowerTime call - refusing to scale the clock",
             RVA_DAYTICK_POWER);
        return false;
    }

    void** site = (void**)(p + 19 + *(const int*)(p + 15));
    void** slot = FindIatSlot(g_exe, DLL_ENGINE, SYM_POWERTIME);
    if (!slot || site != slot)
    {
        Logf("daynight  the day tick does not call PowerTime through the import slot "
             "- refusing to scale the clock");
        return false;
    }
    return true;
}

// Every Power* the executable imports, or none of them: scaling the time but
// not the vehicle speeds would be a worse desynchronisation than the one this
// is here to remove.
static bool ScaleSimClock()
{
    if (!VerifySimTimer()) return false;

    if (!FindIatSlot(g_exe, DLL_ENGINE, SYM_POWER) ||
        !FindIatSlot(g_exe, DLL_ENGINE, SYM_POWERTIME) ||
        !FindIatSlot(g_exe, DLL_ENGINE, SYM_POWERKMH))
    {
        Logf("daynight  not all three C3D_TIMER::Power* are imported - refusing to "
             "scale the clock, because scaling some of them is worse than none");
        return false;
    }

    g_simTimer = g_exeBase + RVA_SIM_TIMER;
    g_simDiv   = g_dayScale;

    bool ok = PatchIat(g_exe, DLL_ENGINE, SYM_POWERTIME, (void*)h_PowerTime,
                       (void**)&o_PowerTime, "C3D_TIMER::PowerTime");
    ok = PatchIat(g_exe, DLL_ENGINE, SYM_POWER, (void*)h_Power,
                  (void**)&o_Power, "C3D_TIMER::Power") && ok;
    ok = PatchIat(g_exe, DLL_ENGINE, SYM_POWERKMH, (void*)h_PowerKmh,
                  (void**)&o_PowerKmh, "C3D_TIMER::PowerKmh") && ok;

    if (!ok)
    {
        // A slot that would not take the swap leaves that one function
        // unscaled; put back whichever did and run unstretched instead.
        void* back = 0;
        if (o_PowerTime) PatchIat(g_exe, DLL_ENGINE, SYM_POWERTIME, (void*)o_PowerTime, &back, "restore");
        if (o_Power)     PatchIat(g_exe, DLL_ENGINE, SYM_POWER,     (void*)o_Power,     &back, "restore");
        if (o_PowerKmh)  PatchIat(g_exe, DLL_ENGINE, SYM_POWERKMH,  (void*)o_PowerKmh,  &back, "restore");
        g_simTimer = 0;
        g_simDiv   = 1.0f;
        return false;
    }
    return true;
}

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    TsmBind(host);
    info->name    = "daynight";
    info->version = "2.0";

    ReadSettings();
    if (!g_enabled)
    {
        Logf("daynight  disabled - the cycle stays 13 calendar days long");
        return 1;                       // nothing hooked, no reason to stay
    }
    return 0;
}

extern "C" __declspec(dllexport) int TsmPluginStart(void)
{
    if (!g_enabled) return 1;

    if (!ReadConstants()) return 1;
    ResolveSeason();

    if (!InstallInlineHook(g_exeBase + RVA_WEATHER_TICK, (void*)h_WeatherTick,
                           (void**)&o_WeatherTick, kWeatherTickPrologue,
                           sizeof(kWeatherTickPrologue), "weather tick"))
        return 1;

    // The second copy of the same logic. Without it the sky follows the new
    // cycle and the street lights follow the old one, which is worse than
    // either on its own - so a refusal here takes the whole plugin out rather
    // than leaving half of it installed.
    if (!InstallInlineHook(g_exeBase + RVA_LIGHT_FACTOR, (void*)h_LightFactor,
                           (void**)&o_LightFactor, kLightFactorPrologue,
                           sizeof(kLightFactorPrologue), "light factor"))
    {
        Logf("daynight  the light factor could not be hooked - sky and lights "
             "would disagree, so nothing is changed");
        g_enabled = 0;                  // the first hook stays, and does nothing
        return 1;
    }

    if (g_cycleDays == PHASES && g_offset == 0.0f && g_dayScale == 1.0f)
    {
        Logf("daynight  cycle_days = %d, day_scale = 1 is the base game - hooked, "
             "changes nothing", PHASES);
        return 0;
    }

    Logf("daynight  one day/night cycle every %d calendar day(s): "
         "%.2f day units per phase, %.2f of them a fade",
         g_cycleDays, (float)g_cycleDays * g_dayLen / (float)PHASES,
         g_fade ? g_fadeLen : VANILLA_FADE);

    if (g_dayScale > 1.0f && g_slow == SLOW_WORLD)
    {
        if (ScaleSimClock())
            Logf("daynight  the whole simulation clock is scaled %.2fx, so a calendar "
                 "day lasts %.2fx longer and the cycle runs at %.2fx the base game's "
                 "pace. EVERY rate in the game slows with it, in step - production, "
                 "wages, pollution, loans - so the economy is the base game's, only "
                 "%.2fx slower in real time. Raising the game speed cancels it exactly",
                 g_dayScale, g_dayScale,
                 (float)g_cycleDays * g_dayScale / (float)PHASES, g_dayScale);
        else
            Logf("daynight  the simulation clock could not be scaled - the cycle now "
                 "fits in one in-game day, a few seconds of real time, and is too fast "
                 "to watch. Nothing else is affected");
    }
    else if (g_dayScale > 1.0f && g_slow == SLOW_CALENDAR)
    {
        Logf("daynight  slow = calendar: the CALENDAR ALONE is stretched %.2fx and "
             "nothing else is. THIS IS VERSION 1.x AND IT IS KNOWN BROKEN - pollution "
             "runs away, winter starves the cities and loans never repay, because "
             "everything per game-day slows while everything per frame does not. It "
             "is here to reproduce those, not to play with", g_dayScale);
    }
    else
    {
        Logf("daynight  calendar day left at its own length, so the whole cycle now "
             "fits in one in-game day - a few seconds of real time. Set day_scale "
             "= auto if that is too fast to see");
    }

    if (g_offset != 0.0f)
        Logf("daynight  cycle rotated by %.3f of itself", g_offset);
    if (!g_fade)
        Logf("daynight  cross-fade left at %.0f - it may not finish inside a phase",
             VANILLA_FADE);
    if (!g_SnowSeason)
        Logf("daynight  no season test - the weather may change season a few days off");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
