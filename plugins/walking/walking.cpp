// walking - how far a citizen will walk, as a tesmioloader plugin.
//
// A building carries a list of the buildings a citizen can reach from it on
// foot - `nWalkingBuildingNum` in SOVIETInstructions.txt - and everything the
// simulation does with walking reads that list. It is not recomputed per
// citizen and there is no "is this close enough" test anywhere in the shop, job
// or service code: if the pair is in the list, the walk happens.
//
//   building+0xCA8 .. +0xCB0   vector<WalkingConnection>, stride 0xF0
//                              +0x08  the other building
//                              +0x98  the path length that was found
//   building+0xCC0             the same thing for parking / personal cars
//
// The lists are built by a path search, and the search carries the limit in the
// query object it is handed. `query+0x3C` is the **longest path the search will
// accept**; three places in the path expander read it, all with the same shape
// (0x5799B5, 0x57A278, 0x57ABFD):
//
//   limit = query[0x3C];
//   if (limit > 0.0f && node[0x24] > limit) return -1.0f;   // node+0x24 = length so far
//
// so a zero there means "no limit at all" and anything positive is a distance
// in world units, which for this game are metres.
//
//
// EIGHT SITES, AND THE SAME DISTANCE IS SPELLED OUT IN FOUR FUNCTIONS
//
// The engine writes that field in three different roles, and the walking
// distance appears in all of them separately - there is no shared constant to
// change:
//
//   the builders   clear a building's connection vector and fill it in again.
//                  **Their limit is the walking distance.** There are two for
//                  walking, with the same logic written out twice:
//                    0x12E1D0  a whole set of buildings at once, called
//                              straight from the save loader. 480 as an
//                              immediate, at 0x12E2DD
//                    0x12E6E0  one building, drained twenty at a time from the
//                              queue at game+0x11F88 by 0x12EC50 - the path
//                              everything in a running game takes. 480 from
//                              0x90AF38, at 0x12E7AF
//                  and one for parking, 0x12F830, off the queue at +0x11FA0,
//                  at 2500.
//   the collectors 0x12DE30 walking, 0x12F480 parking. After a road changes,
//                  these search outward from it to decide *which buildings need
//                  rebuilding*, and hand that set to the builders. Their limit
//                  is a search radius - 530 and 2600 - deliberately a little
//                  wider than the builders'.
//   the overlay    0x43EF10 walking, 0x43FEA0 parking - what the building
//                  window's walking-distance button highlights. **It does not
//                  read the connection vector; it runs the search again**, with
//                  its own copies of 480 (twice, at 0x43F04A and 0x43F835) and
//                  2500 (at 0x43FFB3), and draws the path polylines and the
//                  metre labels from the result.
//
// So a patched simulation and an unpatched overlay disagree, and the button is
// the thing the player looks at: 1.2 made citizens walk 1000 m and drew the
// old 480 m highlight. All eight sites are patched now, and the collectors are
// held at max(limit, vanilla radius) - a building 900 m from a road that
// changed has to be in the rebuild set, or it never notices the road at all.
//
// AND ALL EIGHT OR NONE. Verifying each site and writing it in the same breath
// would let a game update that moved one of them recreate the 1.2 bug exactly:
// seven sites patched, one refusing, simulation and overlay disagreeing again.
// So TsmPluginStart checks the whole set first and only then writes any of it -
// the same discipline plugins/construction uses for the office-range ceiling.
// The writing half is held to it too: a write is a page turned writable and then
// four bytes copied, and only the first half can fail, so every page in the set
// is turned writable before the first byte of any of them lands.
//
// Seven of the eight read .rdata and are **repointed, not overwritten**: both
// 480.0f and 530.0f sit in the shared literal pool with dozens of unrelated
// readers, panel widths and GUI coordinates among them, so writing over either
// constant would move far more than walking. Rewriting one displacement touches
// exactly one read. That is the same technique as the loader's main-menu
// version line. The eighth is an immediate and is simply replaced.
//
//
// WHY A CITY THAT ALREADY EXISTS DOES NOT CHANGE ON ITS OWN
//
// Connections are stored in the save, not derived at load, so raising the limit
// only affects what is built afterwards. The base game has the same problem
// whenever it raises the figure itself, and it already ships the fix: the save
// loader at 0x430F20 regenerates every walking and parking connection in the
// world when the save it is reading predates the change.
//
//   0x438366   cmp dword [0x9E9C3C],110      ; save format version, currently 124
//   0x43836D   jge  <skip>
//   0x438373   test al,al                    ; al = byte [0x9E9C50]
//   0x438375   jnz  <skip>
//   0x43837B   "Import - Regenerating walking and parking connections "
//
// `regen_on_load` makes both conditions true - the compare gets 127, and the
// `test al,al` becomes `xor al,al`, which is the same two bytes and always sets
// ZF. The game then does its own full regeneration on every load, with the
// limits this plugin installed. Nothing is reimplemented here.
//
// (`0x9E9C50` is set from the terrain name at 0x431070 - it is one value for
// exactly two DLC maps and the other for everything else - which is why the
// guard is neutralised rather than trusted.)
//
// Everything here is addresses for SOVIET64.exe v1.1.1.9, verified byte for
// byte before anything is written. See docs/12-walking.md.

#include "../../src/tesmio_plugin.h"

// ---------------------------------------------------------------- the sites

// v1.1.1.9. The walking limit in the batch builder. An immediate, so there is
// nothing to repoint: mov dword ptr [rsp+0x7C], 480.0f. Confirmed by the
// opcode and the 480.0f operand together, not by the offset alone; was 0x12E2DD.
#define RVA_WALK_LIMIT   0x12E2CD
static const BYTE kWalkLimitOp[] = { 0xC7, 0x44, 0x24, 0x7C };
#define VANILLA_WALK     480.0f

// Everything else is `movss xmm0,[rip+disp32]`, and only the displacement is
// rewritten - and only after the address it resolves to and the value in there
// both check out. .rdata moved by a uniform -0x18 in this build.
#define RVA_WALK_CONST   0x90AF20   // 480, as the queued builder and the overlay read it - was 0x90AF38
#define RVA_CAR_CONST    0x90B104   // was 0x90B11C
#define VANILLA_CAR      2500.0f

#define RVA_WALK_RADIUS  0x90AF58   // was 0x90AF70
#define VANILLA_WALK_R   530.0f

#define RVA_CAR_RADIUS   0x90B108   // was 0x90B120
#define VANILLA_CAR_R    2600.0f

// The four values, and the slot each site points at.
#define SLOT_WALK    0
#define SLOT_CAR     1
#define SLOT_WALK_R  2
#define SLOT_CAR_R   3

struct Site
{
    DWORD       rva;        // the movss
    DWORD       constRva;   // what its displacement resolves to today
    float       vanilla;    // and what is in there
    int         slot;
    const char* label;
};

// Every place the game spells one of these distances out, other than the batch
// builder's immediate. The overlay pair is why the button drew nothing new:
// the highlight does not read the connections, it runs the search again.
// v1.1.1.9 rvas; each one re-found by its opcode still resolving to the right
// .rdata constant (see docs/02-findings.md). Old rvas, in the same order:
// 0x12E7AF, 0x43F04A, 0x43F835, 0x12F926, 0x43FFB3, 0x12DEA7, 0x12F502.
static const Site kSites[] = {
    { 0x12E79F, RVA_WALK_CONST,  VANILLA_WALK,   SLOT_WALK,   "walk build queued" },
    { 0x43F0EA, RVA_WALK_CONST,  VANILLA_WALK,   SLOT_WALK,   "walk overlay"      },
    { 0x43F8D5, RVA_WALK_CONST,  VANILLA_WALK,   SLOT_WALK,   "walk overlay 2"    },
    { 0x12F916, RVA_CAR_CONST,   VANILLA_CAR,    SLOT_CAR,    "car build"         },
    { 0x440053, RVA_CAR_CONST,   VANILLA_CAR,    SLOT_CAR,    "car overlay"       },
    { 0x12DE97, RVA_WALK_RADIUS, VANILLA_WALK_R, SLOT_WALK_R, "walk rebuild"      },
    { 0x12F4F2, RVA_CAR_RADIUS,  VANILLA_CAR_R,  SLOT_CAR_R,  "car rebuild"       },
};

// Plus the batch builder's immediate, which is the site the set is always one
// larger than kSites for.
static const int kSiteCount = (int)(sizeof(kSites) / sizeof(kSites[0]));
static const int kWriteCount = kSiteCount + 1;

// The save loader's "this save predates the longer distances" test. v1.1.1.9;
// was 0x438366/0x438373 - same shape, cmp+jge then a separate test al,al/jne,
// both landing on the same target, confirmed by disassembly.
#define RVA_REGEN_CMP    0x438406   // 83 3D <disp32> 6E   cmp dword[ver],110
#define RVA_REGEN_TEST   0x438413   // 84 C0 0F 85 ...     test al,al / jnz
#define RVA_SAVE_VERSION 0x9E9C3C

static const BYTE kMovssRip[]  = { 0xF3, 0x0F, 0x10, 0x05 };
static const BYTE kRegenTest[] = { 0x84, 0xC0, 0x0F, 0x85 };
static const BYTE kXorAl[]     = { 0x30, 0xC0 };

// ---------------------------------------------------------------- settings

static int   g_enabled  = 1;
static float g_walk     = 1000.0f;
static float g_car      = VANILLA_CAR;
static int   g_regen    = 1;
static int   g_probe    = 0;

// The four values, in memory this plugin owns, indexed by SLOT_*. AllocNear
// puts them within +-2GB of the executable, which is what makes a 32-bit
// displacement reach.
static float* g_slot;

// Sanity, not balance. The path expander is breadth-first over the road graph,
// so the work grows with the square of the limit and a five-figure distance
// makes placing a building take seconds. Zero is passed through deliberately -
// the engine reads it as "no limit" - but it is not a good idea.
#define LIMIT_MAX 20000.0f

static float Larger(float a, float b) { return a > b ? a : b; }

// ---------------------------------------------------------------- patching

// A patch that has been worked out but not yet made: where it goes, the bytes,
// and the protection its page carried before we asked for it. Nothing here is
// wider than a rel32 or an imm32.
struct Staged
{
    BYTE*       at;
    BYTE        bytes[4];
    size_t      n;
    DWORD       prot;
    const char* what;
};

// The half of a write that can fail. Do it for the whole set, then copy.
static bool Unprotect(Staged* w)
{
    if (VirtualProtect(w->at, w->n, PAGE_EXECUTE_READWRITE, &w->prot)) return true;
    Logf("walking  %s: the page holding %p would not turn writable - refusing",
         w->what, w->at);
    return false;
}

static void Apply(Staged* w) { memcpy(w->at, w->bytes, w->n); }

// Sites share pages - two of the walking sites live in the same 4K - so a set is
// always put back youngest first. In that order the *first* Unprotect of a page,
// the only one that saw the real protection, is also the last to hand it back;
// oldest first would leave the page permanently writable instead.
static void Restore(Staged* w)
{
    DWORD prot = 0;
    VirtualProtect(w->at, w->n, w->prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), w->at, w->n);
}

static void RestoreAll(Staged* w, int n)
{
    for (int i = n - 1; i >= 0; i--) Restore(&w[i]);
}

// EVERY SITE IS VERIFIED BEFORE ANY SITE IS WRITTEN.
//
// The check half and the write half are separate functions for one reason,
// spelled out at the top of this file: the walking limit is written out in four
// different functions, and the overlay runs the search AGAIN rather than reading
// the connection vector. Patch the simulation and miss the overlay and the
// player walks 1000 m while the button still highlights 480 - which is the bug
// v1.2 shipped. It follows that a run where some sites verify and some do not
// must write NOTHING, and that cannot be expressed by a loop that checks each
// site and writes it in the same breath, which is what this used to be.

// Checks the imm32 form: `mov dword ptr [rsp+disp8],imm32`.
static bool CheckImmediate(DWORD siteRva, const BYTE* op, float vanilla,
                           const char* label)
{
    BYTE* site = g_exeBase + siteRva;

    // Nothing else in this plugin asked before dereferencing an rva. The launcher
    // gates the game version, so these are the right addresses for the build that
    // got this far - but "should be readable" is not "is readable", and every
    // other plugin in the tree checks.
    if (!ReadablePtr(site, 8))
    {
        Logf("walking  %s: rva 0x%X is not readable - refusing", label, siteRva);
        return false;
    }

    if (memcmp(site, op, 4) != 0)
    {
        Logf("walking  %s: rva 0x%X is %02X %02X %02X %02X, not the expected mov - refusing",
             label, siteRva, site[0], site[1], site[2], site[3]);
        return false;
    }

    float now = *(const float*)(site + 4);
    if (now != vanilla)
    {
        Logf("walking  %s: rva 0x%X holds %.1f, expected %.1f - refusing",
             label, siteRva, now, vanilla);
        return false;
    }
    return true;
}

static void StageImmediate(DWORD siteRva, float value, const char* label, Staged* w)
{
    w->at   = g_exeBase + siteRva + 4;
    w->n    = sizeof(value);
    w->what = label;
    memcpy(w->bytes, &value, sizeof(value));
}

// Checks one `movss xmm0,[rip+disp32]`: the instruction, the address its
// displacement resolves to, the value sitting in there, and that the slot we
// mean to point it at is within rel32 reach.
static bool CheckRepoint(DWORD siteRva, DWORD constRva, float vanilla,
                         const float* slot, const char* label)
{
    BYTE* site = g_exeBase + siteRva;

    if (!ReadablePtr(site, 8) || !ReadablePtr(g_exeBase + constRva, 4))
    {
        Logf("walking  %s: rva 0x%X or 0x%X is not readable - refusing",
             label, siteRva, constRva);
        return false;
    }

    if (memcmp(site, kMovssRip, sizeof(kMovssRip)) != 0)
    {
        Logf("walking  %s: rva 0x%X is %02X %02X %02X %02X, not a movss - refusing",
             label, siteRva, site[0], site[1], site[2], site[3]);
        return false;
    }

    DWORD resolved = (DWORD)(siteRva + 8 + *(const int*)(site + 4));
    if (resolved != constRva)
    {
        Logf("walking  %s: rva 0x%X reads 0x%X, expected 0x%X - refusing",
             label, siteRva, resolved, constRva);
        return false;
    }

    float now = *(const float*)(g_exeBase + constRva);
    if (now != vanilla)
    {
        Logf("walking  %s: 0x%X holds %.1f, expected %.1f - refusing",
             label, constRva, now, vanilla);
        return false;
    }

    __int64 rel = (BYTE*)slot - (site + 8);
    if (rel < (__int64)INT_MIN || rel > (__int64)INT_MAX)
    {
        Logf("walking  %s: slot %p is out of rel32 range of %p - refusing",
             label, slot, site);
        return false;
    }
    return true;
}

// Points the site at `slot`. The value is already in there and was already in
// there when CheckRepoint measured this same pointer for rel32 reach - the slots
// are filled once, before anything is verified, and nothing writes them again.
static void StageRepoint(DWORD siteRva, const float* slot, const char* label, Staged* w)
{
    BYTE* site = g_exeBase + siteRva;
    int   disp = (int)((const BYTE*)slot - (site + 8));

    w->at   = site + 4;
    w->n    = sizeof(disp);
    w->what = label;
    memcpy(w->bytes, &disp, sizeof(disp));
}

// Makes the save loader's own "regenerate walking and parking connections" pass
// run on every load rather than only for saves older than format 110.
static bool PatchRegenOnLoad()
{
    BYTE* cmp  = g_exeBase + RVA_REGEN_CMP;
    BYTE* test = g_exeBase + RVA_REGEN_TEST;

    if (!ReadablePtr(cmp, 7) || !ReadablePtr(test, sizeof(kRegenTest)))
    {
        Logf("walking  regen: rva 0x%X or 0x%X is not readable - refusing",
             RVA_REGEN_CMP, RVA_REGEN_TEST);
        return false;
    }

    if (cmp[0] != 0x83 || cmp[1] != 0x3D)
    {
        Logf("walking  regen: rva 0x%X is not a cmp dword[rip],imm8 - refusing",
             RVA_REGEN_CMP);
        return false;
    }

    DWORD resolved = (DWORD)(RVA_REGEN_CMP + 7 + *(const int*)(cmp + 2));
    if (resolved != RVA_SAVE_VERSION || cmp[6] != 110)
    {
        Logf("walking  regen: rva 0x%X compares 0x%X against %d, expected 0x%X against 110"
             " - refusing", RVA_REGEN_CMP, resolved, cmp[6], RVA_SAVE_VERSION);
        return false;
    }

    if (memcmp(test, kRegenTest, sizeof(kRegenTest)) != 0)
    {
        Logf("walking  regen: rva 0x%X is not test al,al / jnz - refusing", RVA_REGEN_TEST);
        return false;
    }

    // Both bytes or neither. Bumping the version compare while the terrain guard
    // still stands is the worst of the three outcomes: the loader would skip the
    // regeneration on two DLC maps and take it everywhere else, and the log would
    // say the same thing either way.
    //
    // 127 is the largest an imm8 compare can carry, and the format version is
    // 124 today. If a future patch takes it past 127 this stops working - and
    // it stops by doing nothing, which is the right way round.
    Staged w[2];
    w[0].at = cmp + 6; w[0].n = 1;                w[0].what = "save-version compare";
    w[1].at = test;    w[1].n = sizeof(kXorAl);   w[1].what = "terrain-name guard";
    w[0].bytes[0] = 127;
    memcpy(w[1].bytes, kXorAl, sizeof(kXorAl));

    if (!Unprotect(&w[0])) return false;
    if (!Unprotect(&w[1])) { RestoreAll(w, 1); return false; }

    Apply(&w[0]);
    Apply(&w[1]);
    RestoreAll(w, 2);

    Logf("walking  regen on: save-version compare now 127, terrain guard now xor al,al"
         " - every load rebuilds all walking and parking connections");
    return true;
}

// ---------------------------------------------------------------- setup

static void ReadSettings()
{
    const char* ini = "plugins\\walking.ini";
    char v[64];

    g_enabled = H->configInt(ini, "walking", "enabled", g_enabled);

    if (H->configString(ini, "walking", "distance", v, sizeof(v), "") && v[0])
        g_walk = (float)atof(v);
    if (H->configString(ini, "walking", "car_distance", v, sizeof(v), "") && v[0])
        g_car = (float)atof(v);

    g_regen = H->configInt(ini, "walking", "regen_on_load", g_regen);
    g_probe = H->configInt(ini, "walking", "probe", g_probe);

    if (g_walk < 0.0f) g_walk = VANILLA_WALK;
    if (g_car  < 0.0f) g_car  = VANILLA_CAR;

    if (g_walk > LIMIT_MAX)
    {
        Logf("walking  distance %.0f is past the %.0f ceiling - clamped", g_walk, LIMIT_MAX);
        g_walk = LIMIT_MAX;
    }
    if (g_car > LIMIT_MAX)
    {
        Logf("walking  car_distance %.0f is past the %.0f ceiling - clamped", g_car, LIMIT_MAX);
        g_car = LIMIT_MAX;
    }
}

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    TsmBind(host);
    info->name    = "walking";
    info->version = "1.3";

    ReadSettings();
    if (!g_enabled)
    {
        Logf("walking  disabled");
        return 1;
    }
    return 0;
}

extern "C" __declspec(dllexport) int TsmPluginStart(void)
{
    if (g_probe)
    {
        if (ReadablePtr(g_exeBase + RVA_WALK_LIMIT + 4, 4))
            Logf("walking  probe: %-16s rva 0x%X immediate %.1f", "walk build batch",
                 RVA_WALK_LIMIT, *(const float*)(g_exeBase + RVA_WALK_LIMIT + 4));
        else
            Logf("walking  probe: %-16s rva 0x%X unreadable", "walk build batch",
                 RVA_WALK_LIMIT);

        for (int i = 0; i < kSiteCount; i++)
        {
            if (!ReadablePtr(g_exeBase + kSites[i].constRva, 4))
            {
                Logf("walking  probe: %-16s rva 0x%X -> 0x%X unreadable",
                     kSites[i].label, kSites[i].rva, kSites[i].constRva);
                continue;
            }
            Logf("walking  probe: %-16s rva 0x%X -> 0x%X = %.1f", kSites[i].label,
                 kSites[i].rva, kSites[i].constRva,
                 *(const float*)(g_exeBase + kSites[i].constRva));
        }
    }

    // One allocation for all three repointed values: the game reads them on
    // every path search, so they have to outlive this call and stay put.
    g_slot = (float*)AllocNear(g_exeBase, 64);
    if (!g_slot)
    {
        Logf("walking  no allocation within reach of the executable - not patching");
        return 1;
    }

    // The values, one slot each, and every site below points at one of them.
    // Several sites share a value: the walking limit alone is spelled out in
    // four different functions.
    g_slot[SLOT_WALK]     = g_walk;
    g_slot[SLOT_CAR]      = g_car;
    g_slot[SLOT_WALK_R]   = Larger(g_walk, VANILLA_WALK_R);
    g_slot[SLOT_CAR_R]    = Larger(g_car,  VANILLA_CAR_R);

    // --- pass one: verify all eight, write none of them -------------------
    bool allOk = CheckImmediate(RVA_WALK_LIMIT, kWalkLimitOp, VANILLA_WALK,
                                "walk build batch");
    for (int i = 0; i < kSiteCount; i++)
    {
        const Site* s = &kSites[i];
        if (!CheckRepoint(s->rva, s->constRva, s->vanilla, &g_slot[s->slot], s->label))
            allOk = false;          // keep going: one run should log every failure
    }

    if (!allOk)
    {
        Logf("walking  one or more of the %d sites did not verify - NOTHING has been "
             "patched. A half-patched set is worse than none: the simulation would "
             "use one distance while the building window's overlay drew another, "
             "which is the bug this plugin took four versions to stop shipping.",
             kWriteCount);
        return 1;
    }

    // --- pass two: work out every write, take every page, then copy -------
    // Same shape as pass one and for the same reason. A rebuild radius is no more
    // optional than a limit: a radius narrower than the distance it serves leaves
    // buildings out of the set the builders are handed, so a set that lost only
    // the radii would still be a set that walks 1000 m and rebuilds 530.
    Staged writes[kWriteCount];
    StageImmediate(RVA_WALK_LIMIT, g_walk, "walk build batch", &writes[0]);
    for (int i = 0; i < kSiteCount; i++)
        StageRepoint(kSites[i].rva, &g_slot[kSites[i].slot], kSites[i].label,
                     &writes[i + 1]);

    int taken = 0;
    while (taken < kWriteCount && Unprotect(&writes[taken])) taken++;

    if (taken != kWriteCount)
    {
        // Unprotect named the site. Nothing has been copied yet, so giving the
        // pages back leaves the executable exactly as it was found.
        RestoreAll(writes, taken);
        Logf("walking  %s would not take a write - NOTHING has been patched, and "
             "the other %d sites were left alone rather than shipping the split "
             "the whole check exists to prevent.", writes[taken].what, kWriteCount - 1);
        return 1;
    }

    for (int i = 0; i < kWriteCount; i++) Apply(&writes[i]);
    RestoreAll(writes, kWriteCount);

    Logf("walking  %-16s %.0f -> %.0f  (rva 0x%X, immediate)", "walk build batch",
         VANILLA_WALK, g_walk, RVA_WALK_LIMIT);
    for (int i = 0; i < kSiteCount; i++)
    {
        const Site* s = &kSites[i];
        Logf("walking  %-16s %.0f -> %.0f  (rva 0x%X now reads %p)", s->label,
             s->vanilla, g_slot[s->slot], s->rva, &g_slot[s->slot]);
    }

    if (g_regen && !PatchRegenOnLoad())
        Logf("walking  regen off: only what is built from now on uses the new limit");

    if (g_walk == 0.0f)
        Logf("walking  distance 0 means the search is unbounded - expect long pauses");

    return 0;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
