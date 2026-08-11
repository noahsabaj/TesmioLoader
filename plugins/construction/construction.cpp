// construction - how far a construction office reaches for work, as a
// tesmioloader plugin.
//
// THE FEATURE: a construction office only picks up jobs near itself. This is
// meant to make that reach configurable - `office_range` in the ini, 10 km by
// default - so an office serves a whole district instead of a few streets.
//
// STATE: THE RANGE HAS NOT BEEN FOUND YET, so this release cannot change it.
// What is here is the half that finds it: the probe brackets the range by
// watching which sites an office takes and which it refuses, then hunts the
// executable for a number in that bracket. `patch` is 0 and both site tables
// are empty; a table of zeroes logs "no address yet" rather than refusing,
// which is the right way round for a plugin whose first release is a question.
//
// A count cap is the other thing the limit could be - an office that stops at
// N jobs however close they are - so the probe measures both and keeps them in
// separate tables, because finding one must not block patching the other.
//
//
// WHAT AUTOMATIC ASSIGNMENT IS
//
// A construction office services a set of construction sites, and offers to
// pick them up without being told to. The game's own script API names both
// halves of the relationship - media_soviet/scripts/SOVIETInstructions.txt:
//
//     defineStructVariable(Building, int, nConstructionBuildingNum);
//     defineStructVariable(Building, int, nConstructionOfficeNum);
//     defineInstruction(Building_ConstructionBuilding_GetID, 11015, ...);
//     defineInstruction(Building_ConstructionOffice_GetID,   11016, ...);
//     BUILDINGTYPE_CONSTRUCTION_OFFICE      = 12;
//     BUILDINGTYPE_CONSTRUCTION_OFFICE_RAIL = 28;
//
// so an office holds a list of sites and a site holds a list of offices - the
// same reciprocal shape as nWalkingBuildingNum and building+0xCA8. Two lists
// to find, and each one validates the other.
//
// Those declarations give the fields NAMES and not OFFSETS. The script API's
// declaration order is not the struct's layout order - nStorageNum is declared
// thirty lines after nWalkingBuildingNum while +0x970 sits far before +0xCA8 -
// so betting on it would be a guess written down as a finding, which is the
// one mistake docs/07-pitfalls.md singles out as having cost a feature.
//
//
// WHERE THE CAP IS NOT
//
// It is not in building.ini. All six stock construction offices plus
// rail_construction_office declare $TYPE_CONSTRUCTION_OFFICE, storages,
// workers and $WORKING_VEHICLES_NEEDED, and not one of them carries a radius
// or a site count. So the limit is global, and that leaves exactly two shapes,
// which want different patches:
//
//   a literal in .text/.rdata      -> repoint the read, walking.cpp's Repoint
//   a field of the game object     -> repoint the comparisons, the way
//                                     easystart does for game+0x5DC
//
// The probe tells those apart for free by reporting WHERE the number lives: on
// the office object or the type descriptor for the first, at a fixed offset in
// the game object for the second.
//
//
// WHY A PROBE AND NOT A PATCH
//
// Because nothing about this subsystem has ever been located, and
// docs/03-reverse-engineering.md ranks runtime observation above static
// analysis for exactly this case: one game session answers "which offset is
// the list" and "what value does it stop at" together, and the second number
// is the whole of what a .text scan would then be looking for.
//
//
// WHAT THIS DELIBERATELY DOES NOT DO
//
// Walking distance and personal-car distance. plugins/walking (v1.3) owns
// those and is published on the Steam Workshop as "Increase Walking Distance";
// `distance` and `car_distance` in walking.ini are the supported way to get
// them. Nothing here touches query+0x3C for feet or for cars.
//
// It also does not ship a `trip_limit` setting. Whether this game times a long
// journey out at all is unestablished - docs/12-walking.md records that nothing
// downstream re-checks a distance once a pair is in the connection list - so
// the probe reports on it and no key pretends to control it. A key that
// provably does nothing is worse than no key.
//
//
// WHERE IT RUNS, AND THE ONE RACE IT HAS
//
// An import swap on C3D_TERRAIN::Render, exactly as plugins/aging does, so it
// chains with depletion's hook on the same slot and needs no address of its
// own. The building dispatcher would be the natural home and is not available:
// plugins/accumulator already owns an inline hook on it at 0x139A70, and a
// second InstallInlineHook on that target would compare its expected bytes
// against accumulator's jump and refuse.
//
// The cost is that this reads simulation-thread structures from the render
// thread. Three things answer that, and the third is new relative to aging:
// nothing is cached across samples, everything is guarded, and a candidate is
// only accepted when it has held across two consecutive samples - a torn read
// of a vector mid-push_back otherwise looks exactly like a field that moved
// and moved back.
//
// Everything here is addresses for SOVIET64.exe v1.1.1.9, verified before
// anything is read. See docs/17-construction.md.

#include "../../src/tesmio_plugin.h"
#include <math.h>

// ---------------------------------------------------------------- the sites

// The game object is a STATIC, inline in .data - not a pointer to one. It is
// the object carrying the building vector, the date and the resource vector.
//
// Do not confuse it with the pointer at 0x9941F0, which plugins/depletion and
// plugins/deposits use: that is a different object, the one holding the
// terrain at +0xED8 and the resource maps at +0xF00/+0xF08. Both are called
// "the game object" in places and they are not the same thing.
#define P_GAMEOBJ            0x9D4F10

// The lea the save loader itself uses to address it, two instructions before
// it hands a building's position to the city search. Verified before anything
// below runs, the same way plugins/cities does it - if this no longer resolves
// to P_GAMEOBJ then .data has moved and nothing here is safe.
#define RVA_GAMEOBJ_LEA      0x43970A   // v1.1.1.9; was 0x43966A

#define G_BUILDINGS          0x11B08    // game -> vector<Building*> begin
#define G_BUILDINGS_END      0x11B10    // ...and end
#define G_DAY                0x590      // int, day of the year
#define G_YEAR               0x594      // int

#define B_TYPEDESC           0x318      // the type record; null on scenery
#define B_NODE               0x320      // C3D_NODE, for GetPosition
#define B_BBOX               0x5D0      // ->+0x20 min, ->+0x28 max
#define B_FINISHED           0x604      // float; >= 1.0 means built
#define B_DEMOLISHED         0xEA8      // non-zero means going away
#define B_PARKING            0xCC0      // vector<ParkingConnection>, stride 0xF0
#define T_BUILDING_TYPE      0x360      // the BUILDINGTYPE_* number

// SOVIETInstructions.txt:2125-2126.
#define BT_OFFICE            12
#define BT_OFFICE_RAIL       28

// Offsets that are already known to be vectors on a building, and so are not
// candidates. Excluding them by name is cheaper and more honest than
// re-deriving that they are not the one we want.
#define OFF_STORAGES         0x970
#define OFF_WIRES            0xA10
#define OFF_OCCUPANTS        0xBD8
#define OFF_WALKING          0xCA8
#define OFF_PARKING          0xCC0
#define OFF_CATCHMENT        0x10C8

// void C3D_TERRAIN::Render(bool, C3D_CAMERA*, C3D_CAMERA*, int, int), out of
// the engine's export table rather than written out by hand.
#define SYM_TERRAIN_RENDER "?Render@C3D_TERRAIN@@QEAAX_NPEAVC3D_CAMERA@@0HH@Z"
#define SYM_NODE_GETPOS    "?GetPosition@C3D_NODE@@QEAA?AVC3DVECTOR3@@XZ"

// --- the patch table ------------------------------------------------------
//
// Empty until the probe has named an address. `rva == 0` means "not found
// yet", which makes the group UNAVAILABLE rather than FAILED - it logs and is
// skipped, and `patch = 1` on an empty table does nothing rather than
// refusing loudly about a build that is fine.

enum SiteKind
{
    SITE_IMM32,       // mov dword ptr [mem], imm32   - replace the immediate
    SITE_RIP_FLOAT,   // movss/mulss xmm,[rip+disp32] - repoint the read
    SITE_RIP_INT32,   // cmp/mov r32,[rip+disp32]     - repoint, integer cap
    SITE_CMP_IMM8     // cmp dword[reg+disp32],imm8   - rewrite rip-relative
};

#define SLOT_OFFICE_RANGE  0
#define SLOT_OFFICE_LIMIT  1
#define SLOT_COUNT         2

struct Site
{
    DWORD       rva;        // 0 = unknown; the group is skipped, not refused
    SiteKind    kind;
    const BYTE* op;         // the opcode bytes this build must carry
    int         opLen;
    DWORD       constRva;   // what the displacement must resolve to
    double      vanilla;    // and the value that must be sitting in there
    int         slot;
    const char* label;
};

struct Group
{
    const Site* sites;
    int         count;
    int*        ok;         // one flag per site, easystart's g_ratesOk shape
    const char* label;
};

// v1.1.1.9, and both tables are empty until the probe fills them in.
//
// THE RANGE is the one this plugin exists for: how far from itself an office
// will pick up a job. Budget for more than one row - if this behaves like every
// other distance in this game there is the simulation's copy and the office
// panel's own copy of it, and patching one while the other draws the old number
// is the bug plugins/walking took four versions to stop shipping.
static const Site kRangeSites[] = {
    { 0, SITE_RIP_FLOAT, NULL, 0, 0, 0.0, SLOT_OFFICE_RANGE, "office pickup range" },
};

// THE COUNT is the other thing the limit could turn out to be, kept separate so
// that finding one does not block patching the other.
static const Site kLimitSites[] = {
    { 0, SITE_RIP_INT32, NULL, 0, 0, 0.0, SLOT_OFFICE_LIMIT, "office assign cap" },
};

static int g_rangeOk[sizeof(kRangeSites) / sizeof(kRangeSites[0])];
static int g_limitOk[sizeof(kLimitSites) / sizeof(kLimitSites[0])];

static const Group kRangeGroup = {
    kRangeSites, sizeof(kRangeSites) / sizeof(kRangeSites[0]), g_rangeOk, "range"
};
static const Group kLimitGroup = {
    kLimitSites, sizeof(kLimitSites) / sizeof(kLimitSites[0]), g_limitOk, "cap"
};

// ---------------------------------------------------------------- settings

static int g_enabled  = 1;
static int g_patch    = 0;      // the second half; off until the table is filled
static int g_probe    = 1;      // and the first half, which is what 0.1 is for

static int g_officeRange = 10000;  // metres of reach. The point of the plugin
static int g_officeLimit = 0;      // 0 = no cap at all

static int g_probeFrom   = 0x300;
static int g_probeTo     = 0x1800;
static int g_probePeriod = 5;   // seconds between reports
static int g_probeSites  = 8;   // how many list members to print per office

// The .rdata window searched for the range once it has been bracketed. The
// default spans the pool the game keeps its layout constants and rates in -
// where walking's 480 and 530, cities' 1000 and the deposit radii all live.
static int g_rdataFrom = 0x909000;
static int g_rdataTo   = 0x90C000;

// ---------------------------------------------------------------- state

#define MAX_OFFICES   16
#define MAX_ELEMS     256       // most members a candidate list may hold
#define MAX_TYPE      128       // histogram buckets
#define EXTENT_CHUNK  0x400

typedef void   (*t_TerrainRender)(void*, bool, void*, void*, int, int);
typedef float* (*t_VecGetter)(void*, float*);

static t_TerrainRender o_TerrainRender;
static t_VecGetter     o_NodeGetPosition;

struct Office
{
    BYTE* building;
    int   type;

    DWORD listOff;      // the accepted offset, 0 while unknown
    int   stride;
    int   elemPtrOff;   // 0 or 8 - where the Building* sits inside an element

    DWORD pendOff;      // a candidate awaiting a second confirming sample
    int   pendStride;
    int   pendPtrOff;

    int   held;
    int   heldPrev;
    int   frozen;       // consecutive reports held did not move while work waited
    int   maxHeld;
    int   growable;     // cap > end, i.e. a real vector rather than a fixed array

    // The range evidence. Everything inside claimedMax was picked up; the
    // nearest thing it refused is unclaimedMin. When those two do not overlap
    // the range is bracketed between them, and that bracket is the whole point
    // of this plugin.
    float claimedMin;
    float claimedMax;
    float unclaimedMin;
    int   scanned;      // the .rdata hunt is done once per bracket, not per report
};

static Office g_office[MAX_OFFICES];
static int    g_offices;

static bool  g_armed;
static bool  g_typedOnce;       // the histogram is printed once
static int   g_reports;
static DWORD g_lastReport;

static float* g_slot;

// ---------------------------------------------------------------- reading

// The lea check plugins/cities does. If .data has moved, everything below is
// pointed at nothing and the honest thing is to say so and stop.
static bool VerifyGameObject(void)
{
    BYTE* site = g_exeBase + RVA_GAMEOBJ_LEA;
    if (!ReadablePtr(site, 7)) return false;
    if (site[0] != 0x48 || site[1] != 0x8D || site[2] != 0x0D) return false;

    DWORD resolved = (DWORD)(RVA_GAMEOBJ_LEA + 7 + *(const int*)(site + 3));
    if (resolved != P_GAMEOBJ)
    {
        Logf("construct  game object: rva 0x%X points at 0x%X, expected 0x%X - refusing",
             RVA_GAMEOBJ_LEA, resolved, P_GAMEOBJ);
        return false;
    }
    return true;
}

static BYTE* GameObject(void) { return g_exeBase + P_GAMEOBJ; }

// begin/end of the building vector, with the sanity checks that cost nothing.
// A count of half a million is a wrong address, not a big republic.
static int Buildings(BYTE* game, BYTE*** outBegin)
{
    if (!ReadablePtr(game + G_BUILDINGS, 16)) return 0;

    BYTE** begin = *(BYTE***)(game + G_BUILDINGS);
    BYTE** end   = *(BYTE***)(game + G_BUILDINGS_END);
    if (!begin || !end || end < begin) return 0;

    size_t bytes = (size_t)((BYTE*)end - (BYTE*)begin);
    if (bytes % sizeof(void*)) return 0;

    size_t n = bytes / sizeof(void*);
    if (n == 0 || n > 500000) return 0;
    if (!ReadablePtr(begin, bytes < 4096 ? bytes : 4096)) return 0;

    *outBegin = begin;
    return (int)n;
}

static int BuildingType(BYTE* b)
{
    if (!b || !ReadablePtr(b + B_TYPEDESC, 8)) return -1;
    BYTE* td = *(BYTE**)(b + B_TYPEDESC);
    if (!td || !ReadablePtr(td + T_BUILDING_TYPE, 4)) return -1;
    return *(const int*)(td + T_BUILDING_TYPE);
}

// The dispatcher's own two tests for "this is a live construction site".
static bool IsUnfinished(BYTE* b)
{
    if (!ReadablePtr(b + B_FINISHED, 4) || !ReadablePtr(b + B_DEMOLISHED, 1))
        return false;
    return *(const float*)(b + B_FINISHED) < 1.0f &&
           *(const char*)(b + B_DEMOLISHED) == 0;
}

// A cheap pre-filter before the expensive membership scan: anything that is
// really a Building has a readable type descriptor carrying a plausible type
// number. This is what keeps the candidate scan from running a linear search
// over several thousand pointers for every one of several hundred offsets.
static bool LooksLikeBuilding(BYTE* p)
{
    int t = BuildingType(p);
    return t >= 0 && t < MAX_TYPE;
}

static bool InBuildingVector(BYTE* p, BYTE** begin, int count)
{
    for (int i = 0; i < count; i++)
        if (begin[i] == p) return true;
    return false;
}

// How far into this object it is safe to read, asked in chunks rather than as
// one span. sizeof(Building) is not known, and asking ReadablePtr about a
// fixed large span is the mistake docs/07-pitfalls.md records under "One
// VirtualQuery is not a range check" - it starts answering no partway through
// a session with every byte still perfectly readable.
//
// What this returns is a READABILITY BOUND, not an object size. A heap object
// sits inside a much larger committed region; anything past the real end of
// the building belongs to somebody else and is reported as such.
static DWORD ReadableExtent(BYTE* b, DWORD want)
{
    DWORD got = 0;
    while (got < want)
    {
        DWORD step = want - got < EXTENT_CHUNK ? want - got : EXTENT_CHUNK;
        if (!ReadablePtr(b + got, step)) break;
        got += step;
    }
    return got;
}

// ---------------------------------------------------------------- geometry
//
// The node position, overwritten by the bounding-box midpoint when the
// building has no type record - which is what the game's own reassignment
// pass computes, and copied from plugins/cities rather than re-derived.

static bool BuildingCentre(BYTE* b, float* out)
{
    if (!o_NodeGetPosition) return false;
    if (!ReadablePtr(b + B_BBOX, 8)) return false;

    float tmp[4] = { 0, 0, 0, 0 };
    const float* p = o_NodeGetPosition(b + B_NODE, tmp);
    if (!p) return false;
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2];

    if (*(void**)(b + B_TYPEDESC) != NULL) return true;

    BYTE* bb = *(BYTE**)(b + B_BBOX);
    if (!bb || !ReadablePtr(bb + 0x20, 16)) return true;
    const float* mn = *(const float**)(bb + 0x20);
    const float* mx = *(const float**)(bb + 0x28);
    if (!mn || !mx) return true;
    if (!ReadablePtr(mn, 16) || !ReadablePtr(mx, 16)) return true;

    out[0] = (mn[1] + mx[1]) * 0.5f;
    out[1] = (mn[2] + mx[2]) * 0.5f;
    out[2] = (mn[3] + mx[3]) * 0.5f;
    return true;
}

static float Distance(const float* a, const float* b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------- finding a number
//
// Once the probe has bracketed the range - everything inside R is picked up,
// everything outside it is not - the number R has to be somewhere. This looks
// for it, as a float, in the three places it could live, and reports every hit
// with its offset.
//
// A range is far more likely to be a literal in .rdata than a field of an
// office, because building.ini declares no such thing and every office behaves
// the same. .rdata is therefore scanned too, over the window the game keeps
// its layout constants and rates in - the same pool walking's 480 and 530 and
// cities' 1000 come out of.
//
// Hits are candidates and nothing more. The value 2500 appears in .rdata for
// several unrelated reasons, which is exactly why walking repoints one
// instruction rather than overwriting the constant.
static int ScanFloatRange(BYTE* base, DWORD from, DWORD to,
                          float lo, float hi, const char* what, int maxHits)
{
    int hits = 0;
    for (DWORD off = from; off + 4 <= to && hits < maxHits; off += 4)
    {
        if (!ReadablePtr(base + off, 4)) continue;
        float v = *(const float*)(base + off);
        if (v != v) continue;                       // NaN
        if (v < lo || v > hi) continue;

        Logf("construct      %s +0x%06X = %.3f", what, off, v);
        hits++;
    }
    // The same bytes as an int, for a range stored in whole metres.
    for (DWORD off = from; off + 4 <= to && hits < maxHits; off += 4)
    {
        if (!ReadablePtr(base + off, 4)) continue;
        int v = *(const int*)(base + off);
        if ((float)v < lo || (float)v > hi) continue;

        Logf("construct      %s +0x%06X = %d (int)", what, off, v);
        hits++;
    }
    return hits;
}

// Is this building in the office's personal-car connection list? If every
// claimed site is and every unclaimed one is not, the office's reach IS the
// vehicle path ceiling, plugins/walking already owns it, and this plugin has
// nothing left to do. That is a cheap thing to rule out and an expensive thing
// to discover late.
static bool InParkingList(BYTE* office, BYTE* site)
{
    if (!ReadablePtr(office + B_PARKING, 16)) return false;
    BYTE* begin = *(BYTE**)(office + B_PARKING);
    BYTE* end   = *(BYTE**)(office + B_PARKING + 8);
    if (!begin || !end || end <= begin) return false;

    size_t span = (size_t)(end - begin);
    if (span % 0xF0 || span > 4096 * 0xF0) return false;
    if (!ReadablePtr(begin, span < 4096 ? span : 4096)) return false;

    size_t n = span / 0xF0;
    if (n > 4096) return false;
    for (size_t i = 0; i < n; i++)
        if (*(BYTE**)(begin + i * 0xF0 + 8) == site) return true;
    return false;
}

// ---------------------------------------------------------------- the scan
//
// A {begin, end, cap} triple whose members are all live construction sites.
//
// The shape alone proves nothing - docs/07-pitfalls.md, "A heuristic find
// without an invariant is a write into a stranger's struct": a structure this
// long satisfies "three plausible pointers" by accident. What a lookalike
// cannot fake is where the pointers point, and the decisive test here is that
// EVERY member is an unfinished building. Walking connections, parking
// connections, the electric catchment and the occupant list all carry finished
// buildings; no other list on a building is all-unfinished.

struct Candidate
{
    DWORD off;
    int   stride;
    int   ptrOff;
    int   count;
    int   growable;
};

static const int kStrides[] = { 8, 0x10, 0x60, 0xF0 };
#define STRIDE_COUNT (sizeof(kStrides) / sizeof(kStrides[0]))

static bool KnownOffset(DWORD off)
{
    return off == OFF_STORAGES || off == OFF_WIRES || off == OFF_OCCUPANTS ||
           off == OFF_WALKING  || off == OFF_PARKING || off == OFF_CATCHMENT;
}

// One triple, one stride, one element-pointer position. Returns the member
// count, or -1.
static int TryCandidate(BYTE* b, DWORD off, int stride, int ptrOff,
                        BYTE** bvBegin, int bvCount, int* growable)
{
    if (!ReadablePtr(b + off, 24)) return -1;

    BYTE* begin = *(BYTE**)(b + off);
    BYTE* end   = *(BYTE**)(b + off + 8);
    BYTE* cap   = *(BYTE**)(b + off + 16);

    // I1 - the shape.
    if (!begin || end <= begin || cap < end) return -1;

    size_t span = (size_t)(end - begin);
    if (span % (size_t)stride) return -1;

    size_t n = span / (size_t)stride;
    if (n == 0 || n > MAX_ELEMS) return -1;
    if (!ReadablePtr(begin, span)) return -1;

    // I2 and I3 - every member is a building the game knows about, and every
    // one of them is a live construction site. The cheap filter runs first so
    // the linear membership scan is only ever reached by a handful.
    for (size_t i = 0; i < n; i++)
    {
        BYTE* m = *(BYTE**)(begin + i * (size_t)stride + ptrOff);
        if (!m || !LooksLikeBuilding(m)) return -1;
        if (!IsUnfinished(m))            return -1;
    }
    for (size_t i = 0; i < n; i++)
    {
        BYTE* m = *(BYTE**)(begin + i * (size_t)stride + ptrOff);
        if (!InBuildingVector(m, bvBegin, bvCount)) return -1;
    }

    // Whether raising a cap would be a feature or a heap overrun. A real
    // vector has room past `end`; a fixed inline array does not.
    *growable = cap > end ? 1 : 0;
    return (int)n;
}

static int ScanOffice(BYTE* b, DWORD extent, BYTE** bvBegin, int bvCount,
                      Candidate* out, int max)
{
    int found = 0;
    DWORD to = extent < (DWORD)g_probeTo ? extent : (DWORD)g_probeTo;

    for (DWORD off = (DWORD)g_probeFrom; off + 24 <= to && found < max; off += 8)
    {
        if (KnownOffset(off)) continue;                       // I5
        if (!ReadablePtr(b + off, 24)) continue;

        for (size_t s = 0; s < STRIDE_COUNT && found < max; s++)
        {
            for (int ptrOff = 0; ptrOff <= 8 && found < max; ptrOff += 8)
            {
                if (ptrOff + 8 > kStrides[s]) continue;

                int grow = 0;
                int n = TryCandidate(b, off, kStrides[s], ptrOff,
                                     bvBegin, bvCount, &grow);
                if (n < 0) continue;

                out[found].off      = off;
                out[found].stride   = kStrides[s];
                out[found].ptrOff   = ptrOff;
                out[found].count    = n;
                out[found].growable = grow;
                found++;
            }
        }
    }
    return found;
}

// ---------------------------------------------------------------- the probe

static void TypeHistogram(BYTE** begin, int count)
{
    int hist[MAX_TYPE];
    memset(hist, 0, sizeof(hist));

    int unknown = 0;
    for (int i = 0; i < count; i++)
    {
        int t = BuildingType(begin[i]);
        if (t >= 0 && t < MAX_TYPE) hist[t]++;
        else unknown++;
    }

    char line[1024];
    int  o = 0;
    for (int t = 0; t < MAX_TYPE && o < 900; t++)
        if (hist[t])
            o += _snprintf_s(line + o, sizeof(line) - o, _TRUNCATE, "%d x%d  ", t, hist[t]);
    line[o] = 0;

    Logf("construct  %d building(s), %d with no readable type", count, unknown);
    Logf("construct  types on this map: %s", line);

    // The line that decides whether anything below is worth reading. If 12 and
    // 28 are absent, building+0x360 is not the type on this build and every
    // later line is noise - docs/02-findings.md records that a previous claim
    // about that very offset was a guess and was wrong.
    if (!hist[BT_OFFICE] && !hist[BT_OFFICE_RAIL])
        Logf("construct  WARN no type %d or %d on this map - either there are no "
             "construction offices built, or +0x%X is not the type field",
             BT_OFFICE, BT_OFFICE_RAIL, T_BUILDING_TYPE);
}

static int CollectOffices(BYTE** begin, int count)
{
    int n = 0;
    for (int i = 0; i < count && n < MAX_OFFICES; i++)
    {
        BYTE* b = begin[i];
        int   t = BuildingType(b);
        if (t != BT_OFFICE && t != BT_OFFICE_RAIL) continue;

        // Carry forward what we already learned about this office rather than
        // starting over: the accepted offset and the previous count are the
        // whole of the temporal evidence.
        Office keep;
        memset(&keep, 0, sizeof(keep));
        for (int j = 0; j < g_offices; j++)
            if (g_office[j].building == b) { keep = g_office[j]; break; }

        keep.building = b;
        keep.type     = t;
        g_office[n++] = keep;
    }
    return n;
}

// Everything about one office, once per report.
static void ReportOffice(Office* o, BYTE** bvBegin, int bvCount, int* claimedBy)
{
    BYTE* b = o->building;

    DWORD extent = ReadableExtent(b, (DWORD)g_probeTo);
    float centre[3] = { 0, 0, 0 };
    bool  havePos = BuildingCentre(b, centre);

    Logf("construct  office %p  type %d  %s  readable to +0x%X",
         b, o->type,
         havePos ? "placed" : "position unknown",
         extent);

    // --- the candidate scan, until it settles -----------------------------
    if (!o->listOff)
    {
        Candidate cand[16];
        int n = ScanOffice(b, extent, bvBegin, bvCount, cand, 16);

        for (int i = 0; i < n; i++)
            Logf("construct    candidate +0x%04X  stride 0x%X  ptr+%d  %d member(s)  %s",
                 cand[i].off, cand[i].stride, cand[i].ptrOff, cand[i].count,
                 cand[i].growable ? "growable" : "FIXED ARRAY - a raised cap would overrun");

        if (n == 0)
        {
            Logf("construct    no candidate list on this office - it may simply have "
                 "taken nothing on yet; place a construction site near it");
        }
        else if (n == 1)
        {
            // One candidate, and it has to survive a second sample before it
            // is believed: we read this from the render thread and a vector
            // caught mid-push_back is not evidence of anything.
            if (o->pendOff == cand[0].off && o->pendStride == cand[0].stride &&
                o->pendPtrOff == cand[0].ptrOff)
            {
                o->listOff    = cand[0].off;
                o->stride     = cand[0].stride;
                o->elemPtrOff = cand[0].ptrOff;
                o->growable   = cand[0].growable;
                Logf("construct    ACCEPTED +0x%04X after two consecutive samples - "
                     "open this office's window and check it lists %d site(s)",
                     o->listOff, cand[0].count);
            }
            else
            {
                o->pendOff    = cand[0].off;
                o->pendStride = cand[0].stride;
                o->pendPtrOff = cand[0].ptrOff;
                Logf("construct    +0x%04X held once - waiting for a second sample",
                     cand[0].off);
            }
        }
        else
        {
            Logf("construct    %d candidates survived every invariant - place or "
                 "cancel a site and take whichever one moves", n);
        }
        if (!o->listOff) return;
    }

    // --- the numbers that answer the question -----------------------------
    int   grow  = 0;
    int   held  = TryCandidate(b, o->listOff, o->stride, o->elemPtrOff,
                               bvBegin, bvCount, &grow);
    if (held < 0)
    {
        // The list stopped looking like one. That is information: either the
        // office dropped everything, or the offset was wrong all along.
        Logf("construct    +0x%04X no longer validates - dropping it and rescanning",
             o->listOff);
        o->listOff = 0;
        o->pendOff = 0;
        return;
    }

    o->heldPrev = o->held;
    o->held     = held;
    o->growable = grow;
    if (held > o->maxHeld) o->maxHeld = held;

    // Claimed members, with distances, and whether the car list explains them.
    BYTE* begin = *(BYTE**)(b + o->listOff);
    float claimedMax = -1.0f;
    float claimedMin = -1.0f;
    int   parkingAll = 1;

    for (int i = 0; i < held; i++)
    {
        BYTE* m = *(BYTE**)(begin + (size_t)i * (size_t)o->stride + o->elemPtrOff);
        for (int j = 0; j < bvCount; j++)
            if (bvBegin[j] == m) { claimedBy[j] = 1; break; }

        float mc[3];
        float d = -1.0f;
        if (havePos && BuildingCentre(m, mc)) d = Distance(centre, mc);
        if (d >= 0.0f)
        {
            if (d > claimedMax) claimedMax = d;
            if (claimedMin < 0.0f || d < claimedMin) claimedMin = d;
        }

        bool park = InParkingList(b, m);
        if (!park) parkingAll = 0;

        if (i < g_probeSites)
            Logf("construct      site %2d  %p  type %3d  %7.1f m  car list %s  progress %.2f",
                 i, m, BuildingType(m), d, park ? "yes" : "no",
                 ReadablePtr(m + B_FINISHED, 4) ? *(const float*)(m + B_FINISHED) : -1.0f);
    }

    o->claimedMax = claimedMax;
    o->claimedMin = claimedMin;

    Logf("construct    holds %d site(s)  from %.1f m to %.1f m  %s  %s",
         held, claimedMin, claimedMax,
         o->growable ? "growable" : "FIXED ARRAY",
         parkingAll && held ? "every one is in the car list" : "not all in the car list");
}

static void Report(BYTE* game, BYTE** bvBegin, int bvCount)
{
    int day  = ReadablePtr(game + G_DAY, 8) ? *(const int*)(game + G_DAY)  : -1;
    int year = ReadablePtr(game + G_DAY, 8) ? *(const int*)(game + G_YEAR) : -1;

    // Every live construction site on the map, so "unclaimed" means something.
    int live = 0;
    static int claimedBy[8192];
    int  track = bvCount < 8192 ? bvCount : 8192;
    memset(claimedBy, 0, sizeof(int) * (size_t)track);
    for (int i = 0; i < track; i++)
        if (bvBegin[i] && IsUnfinished(bvBegin[i])) live++;

    Logf("construct  ---- day %d of year %d: %d office(s), %d live site(s) ----",
         day, year, g_offices, live);

    for (int i = 0; i < g_offices; i++)
        ReportOffice(&g_office[i], bvBegin, track, claimedBy);

    // Unclaimed = a live site in nobody's list. Without this number nothing
    // below means anything: an office that stopped taking work on because
    // there was none left has demonstrated no limit of any kind.
    int unclaimed = 0;
    for (int i = 0; i < track; i++)
        if (bvBegin[i] && IsUnfinished(bvBegin[i]) && !claimedBy[i]) unclaimed++;

    Logf("construct  %d live site(s), %d of them unclaimed", live, unclaimed);

    for (int i = 0; i < g_offices; i++)
    {
        Office* o = &g_office[i];
        if (!o->listOff) continue;

        // The nearest thing THIS office refused. Per office, because two
        // offices in different towns have completely different neighbourhoods
        // and averaging them would erase the boundary we are looking for.
        float oc[3];
        o->unclaimedMin = -1.0f;
        if (BuildingCentre(o->building, oc))
        {
            for (int j = 0; j < track; j++)
            {
                BYTE* s = bvBegin[j];
                if (!s || !IsUnfinished(s) || claimedBy[j]) continue;
                float sc[3];
                if (!BuildingCentre(s, sc)) continue;
                float d = Distance(oc, sc);
                if (o->unclaimedMin < 0.0f || d < o->unclaimedMin) o->unclaimedMin = d;
            }
        }

        if (!unclaimed)
        {
            Logf("construct  office %p: holds everything there is - place more sites, "
                 "further out, before this can show a range", o->building);
            continue;
        }

        Logf("construct  office %p: claimed out to %.1f m, nearest refused %.1f m",
             o->building, o->claimedMax, o->unclaimedMin);

        // --- THE RANGE, which is the whole question -----------------------
        //
        // A refused site NEARER than one already taken on means distance is
        // not what is being filtered on. A clean gap the other way brackets
        // the range between the two numbers, and that bracket is what the
        // .rdata hunt below is given.
        if (o->held > 0 && o->unclaimedMin >= 0.0f && o->claimedMax >= 0.0f)
        {
            if (o->unclaimedMin < o->claimedMax)
            {
                Logf("construct  office %p: a refused site at %.1f m sits INSIDE a taken "
                     "one at %.1f m - distance is not the gate here, a count is",
                     o->building, o->unclaimedMin, o->claimedMax);
            }
            else if (!o->scanned)
            {
                float lo = o->claimedMax;
                float hi = o->unclaimedMin;
                Logf("construct  office %p: RANGE IS BETWEEN %.1f m AND %.1f m - hunting "
                     "for it", o->building, lo, hi);

                // Widened a little at both ends: the game measures a path along
                // roads and this measures a straight line, so the constant is
                // at or above the straight-line bracket rather than inside it.
                float wlo = lo * 0.9f;
                float whi = hi * 2.0f;

                int hits = 0;
                DWORD extent = ReadableExtent(o->building, (DWORD)g_probeTo);
                hits += ScanFloatRange(o->building, 0, extent, wlo, whi, "office ", 12);

                BYTE* td = *(BYTE**)(o->building + B_TYPEDESC);
                if (td) hits += ScanFloatRange(td, 0, 0x900, wlo, whi, "type   ", 12);

                hits += ScanFloatRange(game, 0, 0x14000, wlo, whi, "game   ", 12);

                // The likeliest home by far. building.ini declares no range and
                // every office behaves alike, so it is a global - and a global
                // float in this executable lives in the shared literal pool.
                hits += ScanFloatRange(g_exeBase, (DWORD)g_rdataFrom, (DWORD)g_rdataTo,
                                       wlo, whi, ".rdata ", 24);

                Logf("construct  office %p: %d candidate(s) in %.0f..%.0f. The .rdata "
                     "ones are the ones to try first - repoint the read, never "
                     "overwrite the constant.", o->building, hits, wlo, whi);
                o->scanned = 1;
            }
        }

        // --- and the count, which is the other thing it could be ----------
        if (o->held == o->heldPrev)
        {
            o->frozen++;
            if (o->frozen == 3)
                Logf("construct  office %p: held %d over 3 report(s) with %d site(s) "
                     "unclaimed - %d may also be a count cap",
                     o->building, o->held, unclaimed, o->held);
        }
        else
        {
            o->frozen  = 0;
            o->scanned = 0;      // the bracket moved; hunt again when it settles
        }
    }

    g_reports++;
}

static void Tick(void)
{
    BYTE* game = GameObject();

    BYTE** bvBegin = NULL;
    int    bvCount = Buildings(game, &bvBegin);
    if (bvCount <= 0) return;                   // no world loaded yet

    if (!g_typedOnce)
    {
        TypeHistogram(bvBegin, bvCount);
        g_typedOnce = true;
    }

    DWORD now = GetTickCount();
    if (g_armed && now - g_lastReport < (DWORD)g_probePeriod * 1000) return;
    g_lastReport = now;

    g_offices = CollectOffices(bvBegin, bvCount);
    if (!g_offices)
    {
        if (!g_armed)
            Logf("construct  no construction office on the map yet - build one");
        g_armed = true;
        return;
    }

    g_armed = true;
    Report(game, bvBegin, bvCount);
}

// ---------------------------------------------------------------- patching
//
// Nothing here runs while the site table is empty. It is written now rather
// than later so that the shape the probe is aiming at is on the record: what
// the probe has to produce is an rva, an opcode, and the value that must be
// sitting at the address the displacement resolves to.

static bool WriteCode(void* at, const void* bytes, size_t n, const char* what)
{
    DWORD prot = 0;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &prot))
    {
        Logf("construct  VirtualProtect failed at %p (%s)", at, what);
        return false;
    }
    memcpy(at, bytes, n);
    VirtualProtect(at, n, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

// The four checks walking.cpp's Repoint does, with the write removed. Split
// apart on purpose: a group is verified whole before a single byte of it is
// written, because a cap the simulation honours while the office window still
// draws the old one is worse than neither, and is the bug plugins/walking took
// four versions to stop shipping.
static bool CheckSite(const Site* s, const float* slot)
{
    if (!s->rva) return false;

    BYTE* site   = g_exeBase + s->rva;
    int   insLen = s->opLen + 4;

    if (!ReadablePtr(site, (size_t)insLen) ||
        (s->op && memcmp(site, s->op, (size_t)s->opLen) != 0))
    {
        Logf("construct  %s: rva 0x%X is not the expected instruction - refusing",
             s->label, s->rva);
        return false;
    }

    if (s->kind == SITE_IMM32)
    {
        double now = (double)*(const float*)(site + s->opLen);
        if (now != s->vanilla)
        {
            Logf("construct  %s: rva 0x%X holds %.6g, expected %.6g - refusing",
                 s->label, s->rva, now, s->vanilla);
            return false;
        }
        return true;
    }

    DWORD resolved = (DWORD)(s->rva + insLen + *(const int*)(site + s->opLen));
    if (resolved != s->constRva)
    {
        Logf("construct  %s: rva 0x%X reads 0x%X, expected 0x%X - refusing",
             s->label, s->rva, resolved, s->constRva);
        return false;
    }

    double now = (s->kind == SITE_RIP_INT32)
               ? (double)*(const int*)(g_exeBase + s->constRva)
               : (double)*(const float*)(g_exeBase + s->constRva);
    if (now != s->vanilla)
    {
        Logf("construct  %s: 0x%X holds %.6g, expected %.6g - refusing",
             s->label, s->constRva, now, s->vanilla);
        return false;
    }

    __int64 rel = (BYTE*)slot - (site + insLen);
    if (rel < (__int64)INT_MIN || rel > (__int64)INT_MAX)
    {
        Logf("construct  %s: slot %p is out of rel32 range of %p - refusing",
             s->label, slot, site);
        return false;
    }
    return true;
}

static bool WriteSite(const Site* s, float* slot, double value)
{
    BYTE* site   = g_exeBase + s->rva;
    int   insLen = s->opLen + 4;

    if (s->kind == SITE_IMM32)
    {
        float v = (float)value;
        if (!WriteCode(site + s->opLen, &v, sizeof(v), s->label)) return false;
        Logf("construct  %-20s %.6g -> %.6g  (rva 0x%X, immediate)",
             s->label, s->vanilla, value, s->rva);
        return true;
    }

    if (s->kind == SITE_RIP_INT32) *(int*)slot   = (int)value;
    else                           *(float*)slot = (float)value;

    int disp = (int)((BYTE*)slot - (site + insLen));
    if (!WriteCode(site + s->opLen, &disp, sizeof(disp), s->label)) return false;

    Logf("construct  %-20s %.6g -> %.6g  (rva 0x%X now reads %p)",
         s->label, s->vanilla, value, s->rva, slot);
    return true;
}

static int PatchGroup(const Group* g, double value)
{
    for (int i = 0; i < g->count; i++)
        if (!g->sites[i].rva)
        {
            Logf("construct  %s: no address for \"%s\" yet - run with probe = 1",
                 g->label, g->sites[i].label);
            return 0;
        }

    for (int i = 0; i < g->count; i++)
        if (!CheckSite(&g->sites[i], &g_slot[g->sites[i].slot]))
        {
            Logf("construct  %s: %d site(s) did not verify - nothing written",
                 g->label, g->count);
            return 0;
        }

    int done = 0;
    for (int i = 0; i < g->count; i++)
    {
        g->ok[i] = WriteSite(&g->sites[i], &g_slot[g->sites[i].slot], value) ? 1 : 0;
        done += g->ok[i];
    }
    Logf("construct  %s: %d of %d site(s) written", g->label, done, g->count);
    return done;
}

// ---------------------------------------------------------------- the hook

static void h_TerrainRender(void* self, bool a, void* cam1, void* cam2, int b, int c)
{
    o_TerrainRender(self, a, cam1, cam2, b, c);

    if (!g_probe) return;
    __try { Tick(); }
    __except (FaultFilter("construction probe", GetExceptionInformation()))
    {
        Logf("construct  probe disabled after a fault - the game is untouched");
        g_probe = 0;
    }
}

// ---------------------------------------------------------------- setup

static void ReadSettings(void)
{
    const char* ini = "plugins\\construction.ini";

    g_enabled     = H->configInt(ini, "construction", "enabled",      g_enabled);
    g_patch       = H->configInt(ini, "construction", "patch",        g_patch);
    g_probe       = H->configInt(ini, "construction", "probe",        g_probe);
    g_officeRange = H->configInt(ini, "construction", "office_range", g_officeRange);
    g_officeLimit = H->configInt(ini, "construction", "office_limit", g_officeLimit);

    g_probeFrom   = H->configInt(ini, "construction", "probe_from",   g_probeFrom);
    g_probeTo     = H->configInt(ini, "construction", "probe_to",     g_probeTo);
    g_probePeriod = H->configInt(ini, "construction", "probe_period", g_probePeriod);
    g_probeSites  = H->configInt(ini, "construction", "probe_sites",  g_probeSites);
    g_rdataFrom   = H->configInt(ini, "construction", "rdata_from",   g_rdataFrom);
    g_rdataTo     = H->configInt(ini, "construction", "rdata_to",     g_rdataTo);

    if (g_probeFrom < 0)          g_probeFrom = 0;
    if (g_probeTo <= g_probeFrom) g_probeTo = g_probeFrom + 0x800;
    if (g_probeTo > 0x8000)       g_probeTo = 0x8000;
    if (g_probePeriod < 1)        g_probePeriod = 1;
    if (g_probeSites < 0)         g_probeSites = 0;
    if (g_officeLimit < 0)        g_officeLimit = 0;
    if (g_officeRange < 0)        g_officeRange = 0;
    if (g_officeRange > 20000)
    {
        // The same ceiling plugins/walking uses, for the same reason: these
        // searches are breadth-first over the road graph, so the work grows
        // with the square of the limit.
        Logf("construct  office_range %d is past the 20000 ceiling - clamped",
             g_officeRange);
        g_officeRange = 20000;
    }
    if (g_rdataFrom < 0)          g_rdataFrom = 0;
    if (g_rdataTo <= g_rdataFrom) g_rdataTo = g_rdataFrom + 0x1000;
}

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    TsmBind(host);
    info->name    = "construction";
    info->version = "0.1 (probe)";

    ReadSettings();
    if (!g_enabled)
    {
        Logf("construct  disabled");
        return 1;
    }
    return 0;
}

extern "C" __declspec(dllexport) int TsmPluginStart(void)
{
    if (!g_enabled) return 1;

    if (!VerifyGameObject())
    {
        Logf("construct  the game object is not where this build puts it - inactive");
        return 1;
    }
    Logf("construct  game object 0x%X confirmed by the lea at 0x%X",
         P_GAMEOBJ, RVA_GAMEOBJ_LEA);

    bool armed = false;
    if (g_probe)
    {
        // An import swap, so it chains with depletion's and aging's hooks on
        // the same function rather than fighting them, and needs no address of
        // its own. The building dispatcher is not available: accumulator owns
        // an inline hook on it.
        if (PatchIat(g_exe, DLL_ENGINE, SYM_TERRAIN_RENDER, (void*)h_TerrainRender,
                     (void**)&o_TerrainRender, "C3D_TERRAIN::Render"))
            armed = true;
        else
            Logf("construct  no import slot for C3D_TERRAIN::Render - no probe");

        if (void** s = FindIatSlot(g_exe, DLL_ENGINE, SYM_NODE_GETPOS))
            o_NodeGetPosition = (t_VecGetter)*s;
        else
            Logf("construct  no import slot for C3D_NODE::GetPosition - the report "
                 "will carry lists but no distances");
    }

    int written = 0;
    if (g_patch)
    {
        g_slot = (float*)AllocNear(g_exeBase, 64);
        if (!g_slot)
            Logf("construct  no allocation within reach of the executable - not patching");
        else
        {
            written += PatchGroup(&kRangeGroup, (double)g_officeRange);
            written += PatchGroup(&kLimitGroup, (double)g_officeLimit);
        }
    }

    if (!armed && !written)
    {
        Logf("construct  nothing installed - inactive");
        return 1;
    }

    Logf("construct  ready: probe %s, %d site(s) patched",
         armed ? "armed - it reads the offices and changes nothing" : "off", written);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
