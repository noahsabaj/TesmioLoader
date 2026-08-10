// easystart - a citizen's needs arrive with the century, as a tesmioloader plugin.
//
// A map that starts pre-populated hands the player a town whose citizens want
// electricity, heating, running water and a cinema on day one. They lived
// without all of it the day before. This plugin holds each need back until a
// year in `plugins/easystart.ini`, and the point is that it holds back the
// *consequences* only: a cinema built in 1925 is still visited, it simply does
// not feed happiness yet, and its absence does not cost any.
//
//
// WHAT A "NEED" ACTUALLY IS IN THIS GAME
//
// The game keeps its own itemised ledger of why happiness and health moved, and
// it is what the statistics window draws. Two arrays, found from the two
// drawers (0x8D490 for happiness, 0x8C3C0 for health) - each walks a float
// array followed by an int array of the same length, with a base language id
// incremented once per entry:
//
//   happiness  23 reasons, ids 54031..54053   floats 0x9E5970  counts 0x9E59CC
//   health     12 reasons, ids 54003..54014   floats 0x9E58B0  counts 0x9E58E0
//
// Scanning .text for `inc dword ptr [rip+X]` landing in either count array maps
// every reason to the code that raises it, and that is the whole of what can be
// gated. Nine are demands the citizen carries, five come from the building
// side, and the rest are events - prison, relocation, a child's death - which
// are none of this plugin's business.
//
//
// NINE NEEDS IN ONE FUNCTION, AND NO CODE PATCH FOR ANY OF THEM
//
//   rva 0x83A4F0   FUN(game, person)
//
// One caller, at 0x8338E0 inside the per-person tick 0x832CB0, immediately
// after that tick has run the daily planner. It walks `person+0x118` - the
// demand array the planner has just rebuilt - moves the person's happiness at
// `+0xD8` and health at `+0xE0`, and attributes each change to a reason. Which
// reason is decided by two things and nothing else:
//
//   demand+0x08 == 3    alcohol       demand+0x10 == game[0xC300]  food
//   demand+0x08 == 4    church        demand+0x10 == game[0xC310]  meat
//   demand+0x08 == 5    culture       demand+0x10 == game[0xC318]  clothes
//   demand+0x08 == 6    sport         demand+0x10 == game[0xC320]  electronics
//   demand+0x08 == 10   hospital
//
// (the four resource comparisons are reached from kinds 1 and 2, the two a shop
// serves. game+0xC300.. are the base game's four cached shop-goods records -
// the same four `needs` had to work around in 0x198670.)
//
// So a demand that is not in the array when this function runs affects nothing,
// and one that *is* in the array everywhere else still routes the citizen to
// the pub, the church and the shop. That is exactly the shape the feature
// wants, so the demand half is a **pre/post bracket and no patch**: compact the
// locked entries out of the array, call the original, put the array back byte
// for byte. Nothing downstream ever sees the shortened list, because the
// movement and shopping code runs in other functions on other ticks.
//
// Removing the entry removes the bonus with it. That is deliberate and is what
// "the early cinema changes nothing" means; there is no arrangement that keeps
// the reward and drops the penalty, because the function attributes only the
// downward half and computes both from the same demand.
//
//
// THE STATUS FLOATS HAVE TO BE HELD, OR THE UNLOCK IS A CATASTROPHE
//
// `person+0xD8` is eleven floats in the script VM's own order - happiness,
// food, health, soviet, alcohol, culture, sport, religion, clothing,
// electronic, crime - and the *planner* decays them, not the evaluator. Left
// alone, twenty years of locked culture would rot the culture status to zero
// and the whole republic would turn miserable on the day the cinema starts
// counting. So a locked need's own status is written back to `locked_status`
// after every evaluation.
//
// Health is deliberately **not** pinned: it is the citizen's real health, fed
// by pollution, alcohol, water and cold as well, and holding it at 1.0 would
// make everybody unkillable. Happiness is not pinned either, for the same
// reason in reverse - it is the thing being protected, not a per-need status.
//
//
// THE BUILDING SIDE IS TWELVE RATES IN .rdata, AND THEY ARE REPOINTED
//
// Electricity, water and interior temperature never reach a demand array. They
// are subtracted from every inhabitant by three building functions:
//
//   0x1BC1A0   the living tick    happiness: "No electricity" (5), "No water" (6)
//   0x488AE0   interior temp      happiness (8) and health (8)
//   0x1B08E0   drinking water     health (6)
//
// Every one has the same shape: an amount is computed **once, before the loop
// over the inhabitants**, scaled by the "Unsatisfied citizens reaction" setting
// at game+0x5C8 (x0.8 / x1.0 / x1.2), and then
//
//     movss  xmm0,[person+0xD8] ; subss xmm0,amount ; movss [person+0xD8],xmm0
//     ...
//     ucomiss amount_scaled,0 ; je skip ; reason_float += it ; reason_count++
//
// so an amount of zero is a complete no-op - no subtraction, no clamp, and the
// statistics counter is skipped too, which is what a need that does not exist
// yet should look like. Each amount is finalised by one rip-relative
// `mulsd`/`mulss`/`movss` reading a .rdata constant, and **repointing that one
// displacement at a value this plugin owns** is the whole patch: twelve 4-byte
// writes, no code cave, no hook, and the same technique `walking` uses.
//
// Overwriting the .rdata constants in place was the alternative and is wrong
// for the usual reason - 0x909AD4 and 0x909ADC are each read by two different
// blocks, and the literal pool is shared.
//
// Sewage is in neither ledger. It has no penalty of its own: a full sewage
// store stops the building drawing water and the player sees "No water". It
// therefore rides on the water gate and needs nothing here.
//
//
// SCHOOL AND KINDERGARTEN ARE ONE SWITCH THE GAME ALREADY HAS
//
// Neither raises a happiness or a health reason, so neither is a need in the
// sense above. What they are is the "Education simulation" option (language ids
// 710/711/712), whose two documented effects are exactly the two things wanted:
// id 713, "children automatically reach basic education (no elementary schools
// needed) - parents can work even while their children are under 6 (no
// kindergarten needed)". It lives at **game+0x5BC**, `> 0` meaning Complex.
//
// Setting that field to 0 is not an option: the interface reads it too, and a
// kindergarten that cannot be built is worse than one that is not needed. So
// the twelve `cmp dword ptr [reg+0x5BC], 0` sites belonging to the
// *simulation* are rewritten to `cmp dword ptr [rip+ours], 0` - the same length
// in the no-REX form, one byte shorter with REX - and every interface site is
// left alone.
//
// **School and kindergarten cannot be given different years yet**, because the
// site that blocks a parent has not been told apart from the ones that test a
// workplace's required education. `education` is therefore one key, and it is
// off by default. See docs/16-easystart.md.
//
// Addresses are for SOVIET64.exe v1.1.1.9 and every one of them is compared
// against the bytes this build is known to have before anything is written.

#include "../../src/tesmio_plugin.h"

// ---------------------------------------------------------------- the needs

enum
{
    NEED_FOOD = 0,
    NEED_MEAT,
    NEED_CLOTHES,
    NEED_ELECTRONICS,
    NEED_ALCOHOL,
    NEED_RELIGION,
    NEED_CULTURE,
    NEED_SPORT,
    NEED_HEALTH,
    NEED_ELECTRICITY,
    NEED_WATER,
    NEED_HEATING,
    NEED_EDUCATION,
    NEED_COUNT
};

// The key in [unlock], the status float the need owns as an index into the
// eleven at person+0xD8, and the year it defaults to. Meat shares the food
// status; the utilities and education have none; health is the citizen's real
// health and is never written.
static const struct
{
    const char* key;
    int         status;
    int         year;
} kNeeds[NEED_COUNT] = {
    { "food",         1, 1921 },   // fStatusFood, and it is satiety
    { "meat",        -1, 1927 },
    { "clothes",      8, 1927 },
    { "electronics",  9, 1955 },
    { "alcohol",      4, 1932 },
    { "religion",     7, 1921 },
    { "culture",      5, 1940 },
    { "sport",        6, 1932 },
    { "health",      -1, 1940 },   // fStatusHealth, deliberately not pinned
    { "electricity", -1, 1945 },
    { "water",       -1, 1950 },
    { "heating",     -1, 1950 },
    { "education",   -1, 0x7FFFFFFF },
};

#define YEAR_ALWAYS  0            // unlocked from the first day
#define YEAR_NEVER   0x7FFFFFFF   // `off` - never matters at all

// ---------------------------------------------------------------- offsets

#define GAME_YEAR        0x594    // int, the absolute year: 1970 in a fresh save
#define GAME_EDUCATION   0x5BC    // int, "Education simulation", >0 = Complex
#define GAME_RES_FOOD    0xC300   // the four cached shop-goods records
#define GAME_RES_MEAT    0xC310
#define GAME_RES_CLOTHES 0xC318
#define GAME_RES_ELECTR  0xC320

#define PERSON_STATUS    0xD8     // eleven floats
#define PERSON_DCOUNT    0x110
#define PERSON_DEMANDS   0x118
#define PERSON_UNSAT     0x4F0    // count, then 16-byte entries at +0x4F8
#define PERSON_UNSAT_E   0x4F8

#define DEMAND_STRIDE    0x80
#define DEMAND_MAX       7        // (0x4F0 - 0x118) / 0x80, arithmetic not a check
#define DEMAND_KIND      0x08
#define DEMAND_RES       0x10

#define UNSAT_STRIDE     0x10     // { float amount, int kind, Resource* }
#define UNSAT_KIND       0x04
#define UNSAT_RES        0x08
#define UNSAT_MAX        10

// A resource record is 832 bytes and opens with its name.
#define RESOURCE_NAME    0x00

// ---------------------------------------------------------------- the sites

// The evaluator. rcx is the world, rdx the person; the return value is dead at
// the one call site - 0x8338E5 overwrites rax immediately. v1.1.1.9; was
// 0x83A4F0 - confirmed by the prologue and by the body still reading all four
// of game+0xC300/0xC310/0xC318/0xC320.
#define RVA_EVALUATE 0x83A6D0
static const BYTE kEvaluatePrologue[] = {
    0x48, 0x8B, 0xC4,               // mov  rax,rsp
    0x55,                           // push rbp
    0x56,                           // push rsi
    0x57,                           // push rdi
    0x41, 0x54,                     // push r12
    0x41, 0x55,                     // push r13
    0x41, 0x56,                     // push r14
    0x41, 0x57                      // push r15
};

// One repointed rate. `wide` marks a double rather than a float, which the two
// electricity sites are: they go cvtss2sd / mulsd / cvtpd2ps.
struct Rate
{
    DWORD       rva;        // the instruction
    const BYTE* op;         // its opcode bytes, up to the displacement
    int         opLen;
    DWORD       constRva;   // what the displacement resolves to today
    double      vanilla;    // and what is in there
    int         wide;       // 1 = double, 0 = float
    int         need;
    const char* label;
};

static const BYTE kMulsdXmm1[]   = { 0xF2, 0x0F, 0x59, 0x0D };       // mulsd xmm1,[rip]
static const BYTE kMulssXmm3[]   = { 0xF3, 0x0F, 0x59, 0x1D };       // mulss xmm3,[rip]
static const BYTE kMulssXmm2[]   = { 0xF3, 0x0F, 0x59, 0x15 };       // mulss xmm2,[rip]
static const BYTE kMulssXmm0[]   = { 0xF3, 0x0F, 0x59, 0x05 };       // mulss xmm0,[rip]
static const BYTE kMulssXmm1[]   = { 0xF3, 0x0F, 0x59, 0x0D };       // mulss xmm1,[rip]
static const BYTE kMovssXmm7[]   = { 0xF3, 0x0F, 0x10, 0x3D };       // movss xmm7,[rip]
static const BYTE kMovssXmm8[]   = { 0xF3, 0x44, 0x0F, 0x10, 0x05 }; // movss xmm8,[rip]

// Twelve sites. The electricity pair and the two heating pairs are separate
// branches of the same function - a climate split for heating, a two-way split
// on how the shortfall is measured for electricity - and each carries its own
// constant, so each is repointed on its own.
//
// v1.1.1.9 rvas throughout. .rdata moved by a uniform -0x18 in this build (see
// docs/02-findings.md), so every constRva below is old-0x18 - and every
// vanilla value is unchanged at its new address, confirmed by value rather
// than by the shift alone. Each instruction rva was then re-found as the one
// occurrence of its opcode, near the old site, whose displacement resolves to
// that exact new constRva.
static const Rate kRates[] = {
    // 0x1BC1A0, the living tick: happiness for electricity and for water.
    { 0x1BC39B, kMulsdXmm1, 4, 0x909E38, 0.00021666666666666666, 1, NEED_ELECTRICITY, "electricity a"  },
    { 0x1BC575, kMulsdXmm1, 4, 0x909E60, 0.00033333333333333332, 1, NEED_ELECTRICITY, "electricity b"  },
    { 0x1BC74B, kMulssXmm3, 4, 0x909AF8, 0.0009999999310821295,  0, NEED_WATER,       "water happy"    },

    // 0x488AE0, interior temperature: happiness in xmm7, health in xmm8, and a
    // second pair for the colder setting.
    { 0x488BEA, kMovssXmm7, 4, 0x909B18, 0.001500000013038516,   0, NEED_HEATING,     "heat happy a"   },
    { 0x488C1C, kMovssXmm7, 4, 0x909B04, 0.0011699999449774623,  0, NEED_HEATING,     "heat happy b"   },
    { 0x488C06, kMovssXmm8, 5, 0x909B20, 0.0017000000225380063,  0, NEED_HEATING,     "heat health a"  },
    { 0x488C24, kMovssXmm8, 5, 0x909B10, 0.0013259999686852098,  0, NEED_HEATING,     "heat health b"  },

    // 0x1B08E0, drinking water: health, in two blocks. The second picks its
    // rate from the difficulty; the first has a bonus term for clean water,
    // which is zeroed as well - a locked need must not pay either way.
    { 0x1B0B37, kMulssXmm2, 4, 0x909ABC, 0.0002500000118743628,  0, NEED_WATER,       "water health a" },
    { 0x1B0B44, kMulssXmm0, 4, 0x909AC4, 0.00031250002211891115, 0, NEED_WATER,       "water health b" },
    { 0x1B1203, kMulssXmm1, 4, 0x909AA8, 0.0001875000016298145,  0, NEED_WATER,       "water health c" },
    { 0x1B1213, kMulssXmm1, 4, 0x909ABC, 0.0002500000118743628,  0, NEED_WATER,       "water health d" },
    { 0x1B1223, kMulssXmm1, 4, 0x909AC4, 0.00031250002211891115, 0, NEED_WATER,       "water health e" },
};

#define RATE_COUNT ((int)(sizeof(kRates) / sizeof(kRates[0])))

// `cmp dword ptr [reg+0x5BC], 0`, in the two encodings the simulation uses.
// None of these is in the interface: the build menu and the new-game dialog
// read the same field from 0x76A7B0, 0x775180, 0x7A4870 and 0x76B2F0 and are
// deliberately absent from this list.
static const DWORD kEduSites[] = {
    0x1A9CF0, 0x1A9D60, 0x1A9DD0,   // a workplace requires basic education
    0x1AA03F,                       // the wrapper that picks between them
    0x1A772A,                       // a workplace's own education demand
    0x1AD827,                       // productivity from education
    0x3BB29A,                       // the same test, from a person
    0x831A23,                       // a school/university workplace check
    0x822FDE, 0x823129,             // a new citizen's education at birth
    0x825FD4, 0x8260C6,             // the job search's education filter
};

#define EDU_SITE_COUNT ((int)(sizeof(kEduSites) / sizeof(kEduSites[0])))

// ---------------------------------------------------------------- settings

static int   g_enabled     = 1;
static int   g_demands     = 1;
static int   g_utilities   = 1;
static int   g_pinStatus   = 1;
static int   g_clearUnsat  = 1;
static float g_lockedLevel = 1.0f;
static int   g_probe       = 0;

static int   g_unlock[NEED_COUNT];

// [unlock_resource]: a mod good declared by `needs` gets a year of its own,
// matched on the resource record's name because this plugin has no record
// pointer to compare against.
#define MOD_MAX 32
static struct { char name[48]; int year; } g_mod[MOD_MAX];
static int g_modCount = 0;

// ---------------------------------------------------------------- state

typedef void (*t_Evaluate)(void* game, void* person);
static t_Evaluate o_Evaluate;

// Everything the repointed instructions read, plus the education flag. One
// allocation within +-2GB of the executable, because that is what makes a
// 32-bit displacement reach.
static BYTE*  g_slots;
static int*   g_eduFlag;

static int      g_year       = -1;   // the year the state below was built for
static int      g_locked[NEED_COUNT];
static int      g_anyLocked  = 0;    // nothing to do at all when this is 0
static int      g_ratesOk[RATE_COUNT];
static int      g_eduPatched = 0;

// ---------------------------------------------------------------- patching

static bool WriteCode(void* at, const void* bytes, size_t n, const char* what)
{
    DWORD prot = 0;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &prot))
    {
        Logf("easystart  VirtualProtect failed at %p (%s)", at, what);
        return false;
    }
    memcpy(at, bytes, n);
    VirtualProtect(at, n, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

// Where site i's value lives inside g_slots. Doubles need eight bytes and
// natural alignment; giving every slot eight keeps the arithmetic trivial.
static BYTE* SlotOf(int i) { return g_slots + 16 + (size_t)i * 8; }

static void WriteSlot(int i, double v)
{
    if (kRates[i].wide) *(double*)SlotOf(i) = v;
    else                *(float*)SlotOf(i)  = (float)v;
}

// Points one rip-relative load at a slot this plugin owns. Refuses unless the
// opcode, the address the displacement resolves to and the value in there are
// all exactly what this build is known to have.
static bool RepointRate(int i)
{
    const Rate* r    = &kRates[i];
    BYTE*       site = g_exeBase + r->rva;

    if (memcmp(site, r->op, (size_t)r->opLen) != 0)
    {
        Logf("easystart  %s: rva 0x%X is %02X %02X %02X %02X, not the expected load"
             " - refusing", r->label, r->rva, site[0], site[1], site[2], site[3]);
        return false;
    }

    DWORD resolved = (DWORD)(r->rva + r->opLen + 4 + *(const int*)(site + r->opLen));
    if (resolved != r->constRva)
    {
        Logf("easystart  %s: rva 0x%X reads 0x%X, expected 0x%X - refusing",
             r->label, r->rva, resolved, r->constRva);
        return false;
    }

    double now = r->wide ? *(const double*)(g_exeBase + r->constRva)
                         : (double)*(const float*)(g_exeBase + r->constRva);
    if (now != r->vanilla)
    {
        Logf("easystart  %s: 0x%X holds %.12g, expected %.12g - refusing",
             r->label, r->constRva, now, r->vanilla);
        return false;
    }

    BYTE*   slot = SlotOf(i);
    __int64 rel  = slot - (site + r->opLen + 4);
    if (rel < (__int64)INT_MIN || rel > (__int64)INT_MAX)
    {
        Logf("easystart  %s: slot %p is out of rel32 range of %p - refusing",
             r->label, slot, site);
        return false;
    }

    // Vanilla until the first tick tells us the year: a plugin that has not
    // decided anything yet must behave exactly as the base game.
    WriteSlot(i, r->vanilla);

    int disp = (int)rel;
    if (!WriteCode(site + r->opLen, &disp, sizeof(disp), r->label)) return false;

    Logf("easystart  rate %-15s rva 0x%X now reads %p (was 0x%X = %.12g)",
         r->label, r->rva, slot, r->constRva, r->vanilla);
    return true;
}

// Rewrites one `cmp dword ptr [reg+0x5BC], 0` into `cmp dword ptr [rip+X], 0`.
// The no-REX form is seven bytes and so is the replacement; the REX.B form is
// eight and gets a trailing nop.
static bool RepointEduSite(DWORD rva)
{
    BYTE* site = g_exeBase + rva;
    int   len;

    if (site[0] == 0x83 && (site[1] >> 6) == 2 && ((site[1] >> 3) & 7) == 7)
        len = 7;
    else if (site[0] == 0x41 && site[1] == 0x83 && (site[2] >> 6) == 2
             && ((site[2] >> 3) & 7) == 7)
        len = 8;
    else
    {
        Logf("easystart  education 0x%X: %02X %02X %02X is not cmp dword[reg+d32],imm8"
             " - refusing", rva, site[0], site[1], site[2]);
        return false;
    }

    const BYTE* modrm = site + (len == 8 ? 2 : 1);
    DWORD       disp  = *(const DWORD*)(modrm + 1);
    BYTE        imm   = modrm[5];
    if (disp != GAME_EDUCATION || imm != 0)
    {
        Logf("easystart  education 0x%X: compares +0x%X against %d, expected +0x%X"
             " against 0 - refusing", rva, disp, imm, GAME_EDUCATION);
        return false;
    }

    __int64 rel = (BYTE*)g_eduFlag - (site + 7);
    if (rel < (__int64)INT_MIN || rel > (__int64)INT_MAX)
    {
        Logf("easystart  education 0x%X: flag %p out of rel32 range - refusing",
             rva, g_eduFlag);
        return false;
    }

    BYTE out[8];
    out[0] = 0x83;                       // cmp r/m32, imm8
    out[1] = 0x3D;                       // mod=00 reg=/7 rm=101 -> rip+disp32
    *(int*)(out + 2) = (int)rel;
    out[6] = 0x00;
    out[7] = 0x90;                       // written only for the REX form

    if (!WriteCode(site, out, (size_t)len, "education gate")) return false;
    Logf("easystart  education 0x%X now reads %p", rva, g_eduFlag);
    return true;
}

// ---------------------------------------------------------------- the year

static bool Locked(int need) { return g_locked[need] != 0; }

// Rebuilds everything that depends on the date. Cheap and idempotent: the
// evaluator calls it for every citizen on every tick and it does nothing at all
// until the year actually turns.
static void SyncYear(BYTE* game)
{
    int year = *(const int*)(game + GAME_YEAR);
    if (year == g_year) return;
    g_year = year;

    g_anyLocked = 0;
    for (int i = 0; i < NEED_COUNT; i++)
    {
        g_locked[i] = (year < g_unlock[i]) ? 1 : 0;
        if (g_locked[i]) g_anyLocked = 1;
    }
    for (int i = 0; i < g_modCount; i++)
        if (year < g_mod[i].year) g_anyLocked = 1;

    if (g_utilities)
        for (int i = 0; i < RATE_COUNT; i++)
            if (g_ratesOk[i])
                WriteSlot(i, Locked(kRates[i].need) ? 0.0 : kRates[i].vanilla);

    // The player's own choice while the need is live, Simple while it is not.
    if (g_eduPatched)
        *g_eduFlag = Locked(NEED_EDUCATION) ? 0 : *(const int*)(game + GAME_EDUCATION);

    char line[512];
    int  n = _snprintf_s(line, sizeof(line), _TRUNCATE, "easystart  year %d, still to come:", year);
    int  any = 0;
    for (int i = 0; i < NEED_COUNT && n > 0 && n < (int)sizeof(line) - 24; i++)
        if (g_locked[i])
        {
            any = 1;
            n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " %s", kNeeds[i].key);
        }
    for (int i = 0; i < g_modCount && n > 0 && n < (int)sizeof(line) - 56; i++)
        if (year < g_mod[i].year)
        {
            any = 1;
            n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " %s", g_mod[i].name);
        }

    if (any) Logf("%s", line);
    else     Logf("easystart  year %d, every declared need is live", year);
}

// ---------------------------------------------------------------- demands

// Resolving a mod resource means reading its name out of the record, and
// SafeReadStr costs a VirtualQuery. This runs for every citizen on every tick,
// so the answer is cached: a 16-entry direct-mapped table keyed on the record
// pointer, holding both hits and misses. A record's address does not change
// while the engine's vector holds it, and an address that *is* reused lands on
// its own key and misses, so a stale hit is not reachable.
#define MODC_SLOTS 16
static struct { const void* res; int mod; } g_modCache[MODC_SLOTS];

static int ModIndexOf(const void* res)
{
    unsigned k = (unsigned)(((ULONG_PTR)res >> 6) & (MODC_SLOTS - 1));
    if (g_modCache[k].res == res) return g_modCache[k].mod;

    int found = -1;
    char name[48];
    if (SafeReadStr((const BYTE*)res + RESOURCE_NAME, name, sizeof(name)))
        for (int i = 0; i < g_modCount; i++)
            if (_stricmp(name, g_mod[i].name) == 0) { found = i; break; }

    g_modCache[k].res = res;
    g_modCache[k].mod = found;
    return found;
}

// Which need a demand belongs to, or -1 for one this plugin has no opinion on -
// which includes every extra need `needs` adds unless [unlock_resource] names
// it. A match there answers through `modLocked` and returns -2.
static int NeedOfDemand(BYTE* game, const BYTE* demand, int* modLocked)
{
    *modLocked = 0;

    switch (*(const int*)(demand + DEMAND_KIND))
    {
    case 3:  return NEED_ALCOHOL;
    case 4:  return NEED_RELIGION;
    case 5:  return NEED_CULTURE;
    case 6:  return NEED_SPORT;
    case 10: return NEED_HEALTH;
    case 1:
    case 2:  break;
    default: return -1;
    }

    const void* res = *(const void* const*)(demand + DEMAND_RES);
    if (!res) return -1;

    if (res == *(const void* const*)(game + GAME_RES_FOOD))    return NEED_FOOD;
    if (res == *(const void* const*)(game + GAME_RES_MEAT))    return NEED_MEAT;
    if (res == *(const void* const*)(game + GAME_RES_CLOTHES)) return NEED_CLOTHES;
    if (res == *(const void* const*)(game + GAME_RES_ELECTR))  return NEED_ELECTRONICS;

    if (g_modCount)
    {
        int i = ModIndexOf(res);
        if (i >= 0)
        {
            *modLocked = (g_year < g_mod[i].year) ? 1 : 0;
            return -2;
        }
    }
    return -1;
}

static bool DemandIsLocked(BYTE* game, const BYTE* demand)
{
    int mod  = 0;
    int need = NeedOfDemand(game, demand, &mod);
    if (need == -2) return mod != 0;
    if (need < 0)   return false;
    return Locked(need);
}

// The planner left whatever the last cycle could not satisfy in the list at
// +0x4F0, and that list is what raises "N Citizen(s) were unable to get X". A
// locked need has no business appearing in it.
static void ClearUnsatisfied(BYTE* game, BYTE* person)
{
    int n = *(const int*)(person + PERSON_UNSAT);
    if (n <= 0 || n > UNSAT_MAX) return;

    int keep = 0;
    for (int i = 0; i < n; i++)
    {
        BYTE* e = person + PERSON_UNSAT_E + (size_t)i * UNSAT_STRIDE;

        // The entry carries the same kind and the same record the demand did,
        // in a shorter form: { float amount, int kind, Resource* }. Only those
        // two fields are read, so a stub is enough to ask the same question.
        BYTE stub[DEMAND_RES + 8];
        memset(stub, 0, sizeof(stub));
        *(int*)(stub + DEMAND_KIND)   = *(const int*)(e + UNSAT_KIND);
        *(void**)(stub + DEMAND_RES)  = *(void**)(e + UNSAT_RES);

        if (DemandIsLocked(game, stub)) continue;

        if (keep != i)
            memcpy(person + PERSON_UNSAT_E + (size_t)keep * UNSAT_STRIDE, e, UNSAT_STRIDE);
        keep++;
    }
    if (keep != n) *(int*)(person + PERSON_UNSAT) = keep;
}

static void PinStatuses(BYTE* person)
{
    for (int i = 0; i < NEED_COUNT; i++)
    {
        if (!g_locked[i] || kNeeds[i].status < 0) continue;
        *(float*)(person + PERSON_STATUS + (size_t)kNeeds[i].status * 4) = g_lockedLevel;
    }
}

static void ProbeOnce(BYTE* person, const BYTE* saved, int before, int after)
{
    static int done = 0;
    if (done) return;
    done = 1;

    Logf("easystart  probe  person %p: %d demand(s), %d hidden, %d left for the"
         " evaluator", person, before, before - after, after);
    for (int i = 0; i < before; i++)
    {
        const BYTE* d = saved + (size_t)i * DEMAND_STRIDE;
        Logf("easystart  probe    [%d] kind %-3d res %p  %.5f of %.5f", i,
             *(const int*)(d + DEMAND_KIND), *(const void* const*)(d + DEMAND_RES),
             *(const float*)d, *(const float*)(d + 4));
    }
    const float* s = (const float*)(person + PERSON_STATUS);
    Logf("easystart  probe    status happy %.2f food %.2f health %.2f soviet %.2f"
         " alcohol %.2f culture %.2f sport %.2f religion %.2f clothing %.2f"
         " electronic %.2f",
         s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9]);
}

// ---------------------------------------------------------------- the hook

static void h_Evaluate(void* game, void* person)
{
    BYTE* g = (BYTE*)game;
    BYTE* p = (BYTE*)person;

    BYTE saved[DEMAND_MAX * DEMAND_STRIDE];

    // volatile because it is written inside a __try and read after it: a
    // non-volatile local modified in a guarded block has an indeterminate value
    // once the handler has run, and the restore below is exactly the case that
    // matters.
    volatile int savedCount = -1;

    __try
    {
        SyncYear(g);

        if (g_demands && g_anyLocked)
        {
            int n = *(const int*)(p + PERSON_DCOUNT);
            if (n > 0 && n <= DEMAND_MAX)
            {
                // Scan before copying: most citizens carry nothing locked, and
                // this runs for every person on every tick.
                int locked = 0;
                for (int i = 0; i < n; i++)
                    if (DemandIsLocked(g, p + PERSON_DEMANDS + (size_t)i * DEMAND_STRIDE))
                    { locked = 1; break; }

                if (locked)
                {
                    memcpy(saved, p + PERSON_DEMANDS, (size_t)n * DEMAND_STRIDE);
                    savedCount = n;      // set before the edit, so a fault still restores

                    int keep = 0;
                    for (int i = 0; i < n; i++)
                    {
                        const BYTE* d = saved + (size_t)i * DEMAND_STRIDE;
                        if (DemandIsLocked(g, d)) continue;
                        if (keep != i)
                            memcpy(p + PERSON_DEMANDS + (size_t)keep * DEMAND_STRIDE,
                                   d, DEMAND_STRIDE);
                        keep++;
                    }
                    *(int*)(p + PERSON_DCOUNT) = keep;
                    if (g_probe) ProbeOnce(p, saved, n, keep);
                }
            }

            if (g_clearUnsat) ClearUnsatisfied(g, p);
        }
    }
    __except (FaultFilter("easystart demand gate", GetExceptionInformation()))
    {
        // Whatever was mid-edit is put back below, and this citizen is then
        // evaluated exactly as the base game would have.
    }

    o_Evaluate(game, person);

    __try
    {
        if (savedCount >= 0)
        {
            memcpy(p + PERSON_DEMANDS, saved, (size_t)savedCount * DEMAND_STRIDE);
            *(int*)(p + PERSON_DCOUNT) = savedCount;
        }
        if (g_pinStatus && g_anyLocked) PinStatuses(p);
    }
    __except (FaultFilter("easystart demand restore", GetExceptionInformation()))
    {
    }
}

// ---------------------------------------------------------------- settings

static int ParseYear(const char* v, int fallback)
{
    char t[64];
    _snprintf_s(t, sizeof(t), _TRUNCATE, "%s", v);
    Trim(t);
    if (!t[0]) return fallback;
    if (_stricmp(t, "off") == 0 || _stricmp(t, "never") == 0) return YEAR_NEVER;
    if (_stricmp(t, "always") == 0 || _stricmp(t, "on") == 0) return YEAR_ALWAYS;
    int y = atoi(t);
    return (y < 0) ? fallback : y;
}

static void ReadSettings()
{
    const char* ini = "plugins\\easystart.ini";
    char v[256];

    g_enabled    = H->configInt(ini, "easystart", "enabled",           g_enabled);
    g_demands    = H->configInt(ini, "easystart", "demands",           g_demands);
    g_utilities  = H->configInt(ini, "easystart", "utilities",         g_utilities);
    g_pinStatus  = H->configInt(ini, "easystart", "pin_status",        g_pinStatus);
    g_clearUnsat = H->configInt(ini, "easystart", "clear_unsatisfied", g_clearUnsat);
    g_probe      = H->configInt(ini, "easystart", "probe",             g_probe);

    if (H->configString(ini, "easystart", "locked_status", v, sizeof(v), "") && v[0])
        g_lockedLevel = (float)atof(v);
    if (g_lockedLevel < 0.0f) g_lockedLevel = 0.0f;
    if (g_lockedLevel > 1.0f) g_lockedLevel = 1.0f;

    for (int i = 0; i < NEED_COUNT; i++)
    {
        char def[32];
        if (kNeeds[i].year == YEAR_NEVER) strcpy_s(def, sizeof(def), "off");
        else _snprintf_s(def, sizeof(def), _TRUNCATE, "%d", kNeeds[i].year);

        H->configString(ini, "unlock", kNeeds[i].key, v, sizeof(v), def);
        g_unlock[i] = ParseYear(v, kNeeds[i].year);
    }

    // [unlock_resource] has no fixed key set, so the names are listed once in
    // `list` - the host's config API reads a value, not a whole section.
    if (H->configString(ini, "unlock_resource", "list", v, sizeof(v), "") && v[0])
    {
        char* ctx = 0;
        for (char* t = strtok_s(v, ",", &ctx); t && g_modCount < MOD_MAX;
             t = strtok_s(0, ",", &ctx))
        {
            Trim(t);
            if (!t[0]) continue;
            char year[32];
            H->configString(ini, "unlock_resource", t, year, sizeof(year), "off");
            _snprintf_s(g_mod[g_modCount].name, sizeof(g_mod[0].name), _TRUNCATE, "%s", t);
            g_mod[g_modCount].year = ParseYear(year, YEAR_NEVER);
            g_modCount++;
        }
    }
}

// ---------------------------------------------------------------- lifecycle

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    TsmBind(host);
    info->name    = "easystart";
    info->version = "1.0";

    ReadSettings();
    if (!g_enabled)
    {
        Logf("easystart  disabled");
        return 1;
    }
    return 0;
}

extern "C" __declspec(dllexport) int TsmPluginStart(void)
{
    // Sixteen bytes of head room so the education flag sits at the front on its
    // own alignment, then eight per rate.
    size_t bytes = 16 + (size_t)RATE_COUNT * 8 + 16;
    g_slots = AllocNear(g_exeBase, bytes);
    if (!g_slots)
    {
        Logf("easystart  no allocation within reach of the executable - inactive");
        return 1;
    }
    memset(g_slots, 0, bytes);
    g_eduFlag  = (int*)g_slots;
    *g_eduFlag = 0;                      // Simple until the first tick says otherwise

    int rates = 0;
    if (g_utilities)
        for (int i = 0; i < RATE_COUNT; i++)
            if ((g_ratesOk[i] = RepointRate(i) ? 1 : 0) != 0) rates++;

    if (g_unlock[NEED_EDUCATION] != YEAR_NEVER)
    {
        int ok = 0;
        for (int i = 0; i < EDU_SITE_COUNT; i++)
            if (RepointEduSite(kEduSites[i])) ok++;
        g_eduPatched = (ok == EDU_SITE_COUNT) ? 1 : 0;
        Logf("easystart  education gate: %d of %d site(s) repointed%s", ok, EDU_SITE_COUNT,
             g_eduPatched ? "" : " - PARTIAL, the simulation would disagree with itself");
        if (!g_eduPatched) Logf("easystart  education gate inactive");
    }

    bool hooked = InstallInlineHook(g_exeBase + RVA_EVALUATE, (void*)h_Evaluate,
                                   (void**)&o_Evaluate, kEvaluatePrologue,
                                   sizeof(kEvaluatePrologue), "citizen need evaluation");
    if (!hooked)
    {
        Logf("easystart  the evaluator hook did not install - the nine demand needs"
             " are NOT gated; %d utility rate(s) still are", rates);
        if (!rates) return 1;
    }

    Logf("easystart  ready: demand gate %s, %d of %d utility rate(s), locked status"
         " held at %.2f", hooked ? "on" : "OFF", rates, RATE_COUNT, g_lockedLevel);

    for (int i = 0; i < NEED_COUNT; i++)
    {
        if (g_unlock[i] == YEAR_NEVER)
            Logf("easystart    %-12s off - never gated", kNeeds[i].key);
        else if (g_unlock[i] == YEAR_ALWAYS)
            Logf("easystart    %-12s live from the start", kNeeds[i].key);
        else
            Logf("easystart    %-12s from %d", kNeeds[i].key, g_unlock[i]);
    }
    for (int i = 0; i < g_modCount; i++)
        Logf("easystart    %-12s from %d (mod resource)", g_mod[i].name, g_mod[i].year);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
