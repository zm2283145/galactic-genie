// SPDX-License-Identifier: GPL-3.0-or-later
#include "genie_dat.h"

#include "bytes.h"

#include <cstdio>
#include <zlib.h>

namespace swgb {
namespace dat {

namespace {

constexpr int kTerrainCount = 55;      // fixed-size terrain table in SWGB/CC
constexpr int kTerrainBorderCount = 16;
constexpr int kTileTypeCount = 19;
constexpr int kTerrainUnits = 30;
constexpr int kTechTreeSlots = 20;     // SWGB widened AoK's 10 slots
constexpr int kTechTreeZones = 20;
constexpr int kTechTreeAges = 5;

using R = ByteReader;

void readRestriction(R &r, TerrainRestriction &t, int terrains) {
    t.passableBuildableDmgMultiplier = r.vec<float>(terrains);
    t.passGraphics.resize(terrains);
    for (auto &g : t.passGraphics) {
        g.exitTileSprite = r.i32();
        g.enterTileSprite = r.i32();
        g.walkTileSprite = r.i32();
        g.walkSpriteRate = r.f32();
    }
}

void readPlayerColour(R &r, PlayerColour &c) {
    c.id = r.i32();
    c.playerColorBase = r.i32();
    c.unitOutlineColor = r.i32();
    c.unitSelectionColor1 = r.i32();
    c.unitSelectionColor2 = r.i32();
    c.minimapColor = r.i32();
    c.minimapColor2 = r.i32();
    c.minimapColor3 = r.i32();
    c.statisticsText = r.i32();
}

void readSound(R &r, Sound &s) {
    s.id = r.i16();
    s.playDelay = r.i16();
    int16_t n = r.i16();
    s.cacheTime = r.i32();
    s.items.resize(n);
    for (auto &it : s.items) {
        it.fileName = r.fixedStr(27);
        it.resourceId = r.i32();
        it.probability = r.i16();
        it.civilization = r.i16();
        it.iconSet = r.i16();
    }
}

void readGraphic(R &r, Graphic &g) {
    g.exists = true;
    g.name = r.fixedStr(25);
    g.fileName = r.fixedStr(25);
    g.slp = r.i32();
    r.u8(); // is loaded
    r.u8(); // old color flag
    g.layer = r.u8();
    g.playerColor = r.i16();
    g.transparentSelection = r.u8();
    r.array(g.coordinates, 4);
    int16_t deltaCount = r.i16();
    g.soundId = r.i16();
    uint8_t angleSoundsUsed = r.u8();
    g.frameCount = r.i16();
    g.angleCount = r.i16();
    g.speedMultiplier = r.f32();
    g.frameDuration = r.f32();
    g.replayDelay = r.f32();
    g.sequenceType = r.u8();
    g.id = r.i16();
    g.mirroringMode = r.u8();
    g.editorFlag = r.u8();
    g.deltas.resize(deltaCount);
    for (auto &d : g.deltas) {
        d.graphicId = r.i16();
        r.i16(); // padding
        r.i32(); // sprite ptr
        d.offsetX = r.i16();
        d.offsetY = r.i16();
        d.displayAngle = r.i16();
        r.i16(); // padding
    }
    if (angleSoundsUsed) {
        g.angleSounds.resize(g.angleCount);
        for (auto &a : g.angleSounds)
            for (int i = 0; i < 3; i++) {
                a.frame[i] = r.i16();
                a.sound[i] = r.i16();
            }
    }
}

void readTerrain(R &r, Terrain &t) {
    t.enabled = r.u8();
    t.random = r.u8();
    t.name = r.fixedStr(17);
    t.name2 = r.fixedStr(17);
    t.slp = r.i32();
    r.i32(); // shape ptr
    t.soundId = r.i32();
    t.blendPriority = r.i32();
    t.blendType = r.i32();
    r.array(t.colors, 3);
    r.array(t.cliffColors, 2);
    t.passableTerrain = r.u8();
    t.impassableTerrain = r.u8();
    t.isAnimated = r.u8();
    t.animationFrames = r.i16();
    t.pauseFrames = r.i16();
    t.interval = r.f32();
    t.pauseBetweenLoops = r.f32();
    r.i16(); // frame
    r.i16(); // draw frame
    r.f32(); // animate last
    r.u8();  // frame changed
    r.u8();  // drawn
    for (auto &e : t.elevationGraphics) {
        e.frameCount = r.i16();
        e.angleCount = r.i16();
        e.shapeId = r.i16();
    }
    t.terrainToDraw = r.i16();
    r.array(t.terrainDimensions, 2);
    t.borders = r.vec<int16_t>(kTerrainCount);
    r.array(t.terrainUnitId, kTerrainUnits);
    r.array(t.terrainUnitDensity, kTerrainUnits);
    r.array(t.terrainUnitCentering, kTerrainUnits);
    t.numTerrainUnitsUsed = r.i16();
}

void readTerrainBorder(R &r, TerrainBorder &b) {
    b.enabled = r.u8();
    b.random = r.u8();
    b.name = r.fixedStr(13);
    b.name2 = r.fixedStr(13);
    b.slp = r.i32();
    r.i32(); // shape ptr
    b.soundId = r.i32();
    r.array(b.colors, 3);
    r.u8();  // is animated
    r.i16(); // animation frames
    r.i16(); // pause frames
    r.f32(); // interval
    r.f32(); // pause between loops
    r.i16(); // frame
    r.i16(); // draw frame
    r.f32(); // animate last
    r.u8();  // frame changed
    r.u8();  // drawn
    r.skip((size_t)kTileTypeCount * 12 * 6); // per tile type, 12 FrameData
    b.drawTerrain = r.i16();
    b.underlayTerrain = r.i16();
    b.borderStyle = r.i16();
}

void readTerrainBlock(R &r, TerrainBlock &tb) {
    r.u32(); // vfptr
    r.u32(); // map pointer
    tb.mapWidth = r.i32();
    tb.mapHeight = r.i32();
    tb.worldWidth = r.i32();
    tb.worldHeight = r.i32();
    for (auto &ts : tb.tileSizes) {
        ts.width = r.i16();
        ts.height = r.i16();
        ts.deltaY = r.i16();
    }
    r.i16(); // padding
    tb.terrains.resize(kTerrainCount);
    for (auto &t : tb.terrains) readTerrain(r, t);
    tb.borders.resize(kTerrainBorderCount);
    for (auto &b : tb.borders) readTerrainBorder(r, b);
    r.u32();  // map row offset
    r.skip(6 * 4); // map min/max x/y (+1) floats
    tb.terrainsUsed = r.i16();
    tb.bordersUsed = r.i16();
    tb.maxTerrain = r.i16();
    tb.tileWidth = r.i16();
    tb.tileHeight = r.i16();
    tb.tileHalfHeight = r.i16();
    tb.tileHalfWidth = r.i16();
    tb.elevHeight = r.i16();
    r.skip(6 * 2); // cur row/col, block beg/end row/col
    r.u32();       // search map ptr
    r.u32();       // search map rows ptr
    r.u8();        // any frame change
    r.u8();        // map visible flag
    r.u8();        // fog flag
    r.skip(25);    // unknown bytes (SWGB)
    r.skip(157 * 4);
}

void readRandomMaps(R &r) {
    uint32_t count = r.u32();
    r.i32(); // ptr
    // Pass 1: map headers (with map id). Pass 2: headers again + contents.
    for (uint32_t i = 0; i < count; i++) {
        r.skip(4 + 9 * 4);
        r.skip(4 * 8); // 4x (count, ptr)
    }
    for (uint32_t i = 0; i < count; i++) {
        r.skip(9 * 4);
        uint32_t n = r.u32(); r.i32(); r.skip((size_t)n * 44); // lands
        n = r.u32(); r.i32(); r.skip((size_t)n * 24);          // terrains
        n = r.u32(); r.i32(); r.skip((size_t)n * 44);          // units
        n = r.u32(); r.i32(); r.skip((size_t)n * 24);          // elevations
    }
}

void readTask(R &r, Task &t) {
    t.taskType = r.i16();
    t.id = r.i16();
    t.isDefault = r.u8();
    t.actionType = r.i16();
    t.classId = r.i16();
    t.unitId = r.i16();
    t.terrainId = r.i16();
    t.resourceIn = r.i16();
    t.resourceMultiplier = r.i16();
    t.resourceOut = r.i16();
    t.unusedResource = r.i16();
    t.workValue1 = r.f32();
    t.workValue2 = r.f32();
    t.workRange = r.f32();
    t.autoSearchTargets = r.u8();
    t.searchWaitTime = r.f32();
    t.enableTargeting = r.u8();
    t.combatLevelFlag = r.u8();
    t.gatherType = r.i16();
    t.workFlag2 = r.i16();
    t.targetDiplomacy = r.u8();
    t.carryCheck = r.u8();
    t.pickForConstruction = r.u8();
    t.movingGraphic = r.i16();
    t.proceedingGraphic = r.i16();
    t.workingGraphic = r.i16();
    t.carryingGraphic = r.i16();
    t.resourceGatheringSound = r.i16();
    t.resourceDepositSound = r.i16();
}

std::string sizedStr(R &r) {
    int16_t n = r.i16();
    if (n < 0) throw FormatError("negative string length");
    return r.fixedStr((size_t)n);
}

void readUnit(R &r, Unit &u) {
    u.exists = true;
    u.type = r.u8();
    int16_t nameLen = r.i16();
    u.id = r.i16();
    u.languageDllName = r.i16();
    u.languageDllCreation = r.i16();
    u.cls = r.i16();
    r.array(u.standingGraphic, 2);
    u.dyingGraphic = r.i16();
    u.undeadGraphic = r.i16();
    u.undeadMode = r.u8();
    u.hitPoints = r.i16();
    u.lineOfSight = r.f32();
    u.garrisonCapacity = r.u8();
    r.array(u.collisionSize, 3);
    u.trainSound = r.i16();
    u.damageSound = r.i16();
    u.deadUnitId = r.i16();
    u.sortNumber = r.u8();
    u.canBeBuiltOn = r.u8();
    u.iconId = r.i16();
    u.hideInEditor = r.u8();
    u.oldPortraitPict = r.i16();
    u.enabled = r.u8();
    u.disabled = r.u8();
    r.array(u.placementSideTerrain, 2);
    r.array(u.placementTerrain, 2);
    r.array(u.clearanceSize, 2);
    u.hillMode = r.u8();
    u.fogVisibility = r.u8();
    u.terrainRestriction = r.i16();
    u.flyMode = r.u8();
    u.resourceCapacity = r.i16();
    u.resourceDecay = r.f32();
    u.blastDefenseLevel = r.u8();
    u.combatLevel = r.u8();
    u.interactionMode = r.u8();
    u.minimapMode = r.u8();
    u.interfaceKind = r.u8();
    u.multipleAttributeMode = r.f32();
    u.minimapColor = r.u8();
    u.languageDllHelp = r.i32();
    u.languageDllHotKeyText = r.i32();
    u.hotKeyId = r.i32();
    u.recyclable = r.u8();
    u.enableAutoGather = r.u8();
    u.createDoppelgangerOnDeath = r.u8();
    u.resourceGatherGroup = r.u8();
    u.occlusionMode = r.u8();
    u.obstructionType = r.u8();
    u.obstructionClass = r.u8();
    u.trait = r.u8();
    u.civilization = r.u8();
    r.i16(); // nothing
    u.selectionEffect = r.u8();
    u.editorSelectionColour = r.u8();
    r.array(u.outlineSize, 3);
    for (auto &s : u.resourceStorages) {
        s.type = r.i16();
        s.amount = r.f32();
        s.flag = r.u8();
    }
    uint8_t dmgCount = r.u8();
    u.damageGraphics.resize(dmgCount);
    for (auto &d : u.damageGraphics) {
        d.graphicId = r.i16();
        d.damagePercent = r.i16();
        d.applyMode = r.u8();
    }
    u.selectionSound = r.i16();
    u.dyingSound = r.i16();
    u.oldAttackReaction = r.u8();
    u.convertTerrain = r.u8();
    if (nameLen < 0) throw FormatError("negative unit name length");
    u.name = r.fixedStr((size_t)nameLen);
    u.name2 = sizedStr(r);
    u.unitLine = r.i16();
    u.minTechLevel = r.u8();
    u.copyId = r.i16();
    u.baseId = r.i16();

    if (u.type == UT_AoeTrees || u.type < UT_Flag) return;
    u.speed = r.f32();

    if (u.type >= UT_DeadFish) {
        u.walkingGraphic = r.i16();
        u.runningGraphic = r.i16();
        u.rotationSpeed = r.f32();
        r.u8(); // old size class
        u.trackingUnit = r.i16();
        u.trackingUnitMode = r.u8();
        u.trackingUnitDensity = r.f32();
        r.u8(); // old move algorithm
        u.turnRadius = r.f32();
        u.turnRadiusSpeed = r.f32();
        r.f32(); // max yaw per second moving
        r.f32(); // stationary yaw revolution time
        r.f32(); // max yaw per second stationary
    }
    if (u.type >= UT_Bird) {
        u.defaultTaskId = r.i16();
        u.searchRadius = r.f32();
        u.workRate = r.f32();
        r.array(u.dropSites, 2);
        u.taskSwapGroup = r.u8();
        u.attackSound = r.i16();
        u.moveSound = r.i16();
        u.runPattern = r.u8();
    }
    if (u.type >= UT_Combatant) {
        u.baseArmor = r.i16();
        int16_t n = r.i16();
        u.attacks.resize(n);
        for (auto &a : u.attacks) { a.cls = r.i16(); a.amount = r.i16(); }
        n = r.i16();
        u.armours.resize(n);
        for (auto &a : u.armours) { a.cls = r.i16(); a.amount = r.i16(); }
        u.defenseTerrainBonus = r.i16();
        u.maxRange = r.f32();
        u.blastWidth = r.f32();
        u.reloadTime = r.f32();
        u.projectileUnitId = r.i16();
        u.accuracyPercent = r.i16();
        u.breakOffCombat = r.u8();
        u.frameDelay = r.i16();
        r.array(u.graphicDisplacement, 3);
        u.blastAttackLevel = r.u8();
        u.minRange = r.f32();
        u.accuracyDispersion = r.f32();
        u.attackGraphic = r.i16();
        u.displayedMeleeArmour = r.i16();
        u.displayedAttack = r.i16();
        u.displayedRange = r.f32();
        u.displayedReloadTime = r.f32();
    }
    if (u.type == UT_Projectile) {
        u.projectileType = r.u8();
        u.smartMode = r.u8();
        u.hitMode = r.u8();
        u.vanishMode = r.u8();
        u.areaEffectSpecials = r.u8();
        u.projectileArc = r.f32();
    }
    if (u.type >= UT_Creatable) {
        for (auto &c : u.costs) { c.type = r.i16(); c.amount = r.i16(); c.flag = r.i16(); }
        u.trainTime = r.i16();
        u.trainLocationId = r.i16();
        u.buttonId = r.u8();
        u.rearAttackModifier = r.f32();
        u.flankAttackModifier = r.f32();
        u.creatableType = r.u8();
        u.heroMode = r.u8();
        u.garrisonGraphic = r.i32();
        u.totalProjectiles = r.f32();
        u.maxTotalProjectiles = r.u8();
        r.array(u.projectileSpawningArea, 3);
        u.secondaryProjectileUnit = r.i32();
        u.specialGraphic = r.i32();
        u.specialAbility = r.u8();
        u.displayedPierceArmour = r.i16();
    }
    if (u.type == UT_Building) {
        u.constructionGraphic = r.i16();
        u.snowGraphic = r.i16();
        u.adjacentMode = r.u8();
        u.graphicsAngle = r.i16();
        u.disappearsWhenBuilt = r.u8();
        u.stackUnitId = r.i16();
        u.foundationTerrainId = r.i16();
        u.oldOverlayId = r.i16();
        u.techId = r.i16();
        u.canBurn = r.u8();
        for (auto &a : u.annexes) {
            a.unitId = r.i16();
            a.misplacementX = r.f32();
            a.misplacementY = r.f32();
        }
        u.headUnit = r.i16();
        u.transformUnit = r.i16();
        u.transformSound = r.i16();
        u.constructionSound = r.i16();
        u.garrisonType = r.u8();
        u.garrisonHealRate = r.f32();
        u.garrisonRepairRate = r.f32();
        u.pileUnit = r.i16();
        r.array(u.lootingTable, 6);
    }
}

void readCiv(R &r, Civ &c) {
    c.playerType = r.u8();
    c.name = r.fixedStr(20);
    int16_t resCount = r.i16();
    c.techTreeId = r.i16();
    c.teamBonusId = r.i16();
    c.name2 = r.fixedStr(20);
    r.array(c.uniqueUnitsTechs, 4);
    c.resources = r.vec<float>(resCount);
    c.iconSet = r.u8();
    int16_t unitCount = r.i16();
    std::vector<int32_t> ptrs = r.vec<int32_t>(unitCount);
    c.units.resize(unitCount);
    for (int16_t i = 0; i < unitCount; i++)
        if (ptrs[i]) readUnit(r, c.units[i]);
}

void readTech(R &r, Tech &t) {
    r.array(t.requiredTechs, 6);
    for (auto &c : t.costs) { c.type = r.i16(); c.amount = r.i16(); c.flag = r.u8(); }
    t.requiredTechCount = r.i16();
    t.civ = r.i16();
    t.fullTechMode = r.i16();
    t.locationId = r.i16();
    t.languageDllName = r.i16();
    t.languageDllDescription = r.i16();
    t.researchTime = r.i16();
    t.effectId = r.i16();
    t.type = r.i16();
    t.iconId = r.i16();
    t.buttonId = r.u8();
    t.languageDllHelp = r.i32();
    t.languageDllTechTree = r.i32();
    t.hotKeyId = r.i32();
    t.name = sizedStr(r);
    t.name2 = sizedStr(r);
}

// --- tech tree ---
void readCommon(R &r, TechTreeCommon &c) {
    c.used = r.i32();
    for (int i = 0; i < kTechTreeSlots; ++i) c.ids[(size_t)i] = r.i32();
    for (int i = 0; i < kTechTreeSlots; ++i) c.modes[(size_t)i] = r.i32();
}
void readI32List(R &r, std::vector<int32_t> &out) {
    const uint8_t n = r.u8();
    out.resize(n);
    for (uint8_t i = 0; i < n; ++i) out[i] = r.i32();
}

void readTechTree(R &r, bool wideUnitCount, TechTree &tree) {
    tree = TechTree{};
    uint8_t ages = r.u8();
    uint8_t buildings = r.u8();
    int unitsCount = wideUnitCount ? r.i16() : r.u8();
    uint8_t researches = r.u8();
    r.i32(); // total unit tech groups
    for (int i = 0; i < ages; i++) {
        TechTreeAge age;
        age.id = r.i32();
        age.status = r.u8();
        readI32List(r, age.buildings);
        readI32List(r, age.units);
        readI32List(r, age.techs);
        readCommon(r, age.common);
        r.u8(); r.skip(kTechTreeZones); r.skip(kTechTreeZones); r.u8(); r.i32();
        tree.ages.push_back(std::move(age));
    }
    for (int i = 0; i < buildings; i++) {
        TechTreeBuilding b;
        b.id = r.i32();
        b.status = r.u8();
        readI32List(r, b.buildings);
        readI32List(r, b.units);
        readI32List(r, b.techs);
        readCommon(r, b.common);
        b.locationInAge = r.u8();
        for (int k = 0; k < kTechTreeAges; ++k) b.totals[(size_t)k] = r.u8();
        for (int k = 0; k < kTechTreeAges; ++k) b.firsts[(size_t)k] = r.u8();
        b.lineMode = r.i32();
        b.enablingResearch = r.i32();
        tree.buildings.push_back(std::move(b));
    }
    for (int i = 0; i < unitsCount; i++) {
        TechTreeUnit u;
        u.id = r.i32();
        u.status = r.u8();
        u.upperBuilding = r.i32();
        readCommon(r, u.common);
        u.verticalLine = r.i32();
        readI32List(r, u.units);
        u.locationInAge = r.i32();
        u.requiredResearch = r.i32();
        u.lineMode = r.i32();
        u.enablingResearch = r.i32();
        tree.units.push_back(std::move(u));
    }
    for (int i = 0; i < researches; i++) {
        TechTreeResearch t;
        t.id = r.i32();
        t.status = r.u8();
        t.upperBuilding = r.i32();
        readI32List(r, t.buildings);
        readI32List(r, t.units);
        readI32List(r, t.techs);
        readCommon(r, t.common);
        t.verticalLine = r.i32();
        t.locationInAge = r.i32();
        t.lineMode = r.i32();
        tree.researches.push_back(std::move(t));
    }
}

} // namespace

bool inflateRaw(const std::vector<uint8_t> &in, std::vector<uint8_t> &out, std::string *err) {
    z_stream zs{};
    if (inflateInit2(&zs, -15) != Z_OK) {
        if (err) *err = "inflateInit2 failed";
        return false;
    }
    out.clear();
    out.resize(in.size() * 6 + 1024);
    zs.next_in = const_cast<Bytef *>(in.data());
    zs.avail_in = (uInt)in.size();
    int ret;
    do {
        if (zs.total_out == out.size()) out.resize(out.size() * 2);
        zs.next_out = out.data() + zs.total_out;
        zs.avail_out = (uInt)(out.size() - zs.total_out);
        ret = inflate(&zs, Z_NO_FLUSH);
    } while (ret == Z_OK);
    size_t total = zs.total_out;
    inflateEnd(&zs);
    if (ret != Z_STREAM_END) {
        if (err) *err = "inflate failed";
        return false;
    }
    out.resize(total);
    return true;
}

bool DatFile::load(const std::string &path, std::string *err) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> buf(sz > 0 ? (size_t)sz : 0);
    bool ok = buf.empty() || fread(buf.data(), 1, buf.size(), fp) == buf.size();
    fclose(fp);
    if (!ok) {
        if (err) *err = "read error " + path;
        return false;
    }
    return loadFromCompressed(buf, err);
}

bool DatFile::loadFromCompressed(const std::vector<uint8_t> &compressed, std::string *err) {
    std::vector<uint8_t> raw;
    if (!inflateRaw(compressed, raw, err)) return false;
    return loadFromRaw(raw, err);
}

bool DatFile::loadFromRaw(const std::vector<uint8_t> &raw, std::string *err) {
    size_t techTreeStart = 0;
    try {
        R r(raw);
        version = r.fixedStr(8);
        if (version != "VER 5.9") {
            if (err) *err = "unsupported dat version '" + version + "' (expected SWGB:CC VER 5.9)";
            return false;
        }
        r.i16(); // civ count (repeated later)
        waypointSprite = r.i32();
        moveToSprite = r.i32();
        garrisonSound = r.i32();
        ungarrisonSound = r.i32();

        int16_t restrictionCount = r.i16();
        terrainsUsed = r.i16();
        r.skip((size_t)restrictionCount * 4 * 2); // float-table ptrs, pass-graphic ptrs
        terrainRestrictions.resize(restrictionCount);
        for (auto &t : terrainRestrictions) readRestriction(r, t, terrainsUsed);

        playerColours.resize(r.i16());
        for (auto &c : playerColours) readPlayerColour(r, c);

        sounds.resize(r.i16());
        for (auto &s : sounds) readSound(r, s);

        int16_t graphicCount = r.i16();
        std::vector<int32_t> gptr = r.vec<int32_t>(graphicCount);
        graphics.assign(graphicCount, Graphic{});
        for (int16_t i = 0; i < graphicCount; i++)
            if (gptr[i]) readGraphic(r, graphics[i]);

        readTerrainBlock(r, terrainBlock);
        readRandomMaps(r);

        effects.resize(r.i32());
        for (auto &e : effects) {
            e.name = r.fixedStr(31);
            e.commands.resize(r.i16());
            for (auto &c : e.commands) {
                c.type = r.u8();
                c.a = r.i16();
                c.b = r.i16();
                c.c = r.i16();
                c.d = r.f32();
            }
        }

        unitLines.resize(r.i16());
        for (auto &l : unitLines) {
            l.id = r.i16();
            l.name = sizedStr(r);
            l.unitIds = r.vec<int16_t>(r.i16());
        }

        unitHeaders.resize(r.i32());
        for (auto &h : unitHeaders) {
            h.exists = r.u8() != 0;
            if (h.exists) {
                h.tasks.resize(r.i16());
                for (auto &t : h.tasks) readTask(r, t);
            }
        }

        civs.resize(r.i16());
        for (auto &c : civs) readCiv(r, c);

        r.u8(); // techs start marker
        techs.resize(r.i16());
        for (auto &t : techs) readTech(r, t);
        r.u8(); // techs end marker

        timeSlice = r.i32();
        unitKillRate = r.i32();
        unitKillTotal = r.i32();
        unitHitPointRate = r.i32();
        unitHitPointTotal = r.i32();
        razingKillRate = r.i32();
        razingKillTotal = r.i32();

        // The tech-tree unit-connection count is 8-bit in AoK; try that
        // first and fall back to 16-bit, requiring an exact EOF either way.
        techTreeStart = r.pos();
        bool parsed = false;
        for (int wide = 0; wide < 2 && !parsed; wide++) {
            R t(raw);
            t.seek(techTreeStart);
            try {
                readTechTree(t, wide != 0, techTree);
                parsed = t.remaining() == 0;
            } catch (const FormatError &) {
            }
        }
        if (!parsed) {
            if (err) *err = "tech tree did not end at EOF (layout mismatch)";
            return false;
        }
    } catch (const FormatError &e) {
        if (err) *err = std::string("dat parse error: ") + e.what();
        return false;
    }
    return true;
}

} // namespace dat
} // namespace swgb
