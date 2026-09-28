// SPDX-License-Identifier: GPL-3.0-or-later
// Reader for the SWGB: Clone Campaigns empires dat (genie_x1.dat, "VER 5.9").
//
// The layout was written from the openly documented format (genieutils,
// openage) for the single game version we target. Every field is consumed so
// the reader can verify it ends exactly at EOF.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace swgb {
namespace dat {

struct TerrainPassGraphic {
    int32_t exitTileSprite, enterTileSprite, walkTileSprite;
    float walkSpriteRate;
};

struct TerrainRestriction {
    std::vector<float> passableBuildableDmgMultiplier; // per terrain
    std::vector<TerrainPassGraphic> passGraphics;
};

struct PlayerColour {
    int32_t id, playerColorBase, unitOutlineColor, unitSelectionColor1, unitSelectionColor2;
    int32_t minimapColor, minimapColor2, minimapColor3, statisticsText;
};

struct SoundItem {
    std::string fileName;
    int32_t resourceId;
    int16_t probability, civilization, iconSet;
};

struct Sound {
    int16_t id, playDelay;
    int32_t cacheTime;
    std::vector<SoundItem> items;
};

struct GraphicDelta {
    int16_t graphicId;
    int16_t offsetX, offsetY;
    int16_t displayAngle;
};

struct GraphicAngleSound {
    int16_t frame[3], sound[3];
};

struct Graphic {
    bool exists = false;
    std::string name, fileName;
    int32_t slp = -1;
    uint8_t layer = 0;
    int16_t playerColor = -1;
    uint8_t transparentSelection = 0;
    int16_t coordinates[4] = {};
    int16_t soundId = -1;
    int16_t frameCount = 0, angleCount = 0;
    float speedMultiplier = 0, frameDuration = 0, replayDelay = 0;
    uint8_t sequenceType = 0;
    int16_t id = -1;
    uint8_t mirroringMode = 0;
    uint8_t editorFlag = 0;
    std::vector<GraphicDelta> deltas;
    std::vector<GraphicAngleSound> angleSounds;
};

struct FrameData {
    int16_t frameCount, angleCount, shapeId;
};

struct Terrain {
    uint8_t enabled = 0, random = 0;
    std::string name, name2;
    int32_t slp = -1, soundId = -1;
    int32_t blendPriority = 0, blendType = 0;
    uint8_t colors[3] = {};
    uint8_t cliffColors[2] = {};
    uint8_t passableTerrain = 0, impassableTerrain = 0;
    uint8_t isAnimated = 0;
    int16_t animationFrames = 0, pauseFrames = 0;
    float interval = 0, pauseBetweenLoops = 0;
    std::array<FrameData, 19> elevationGraphics{};
    int16_t terrainToDraw = -1;
    int16_t terrainDimensions[2] = {}; // rows, cols of tiles in the SLP
    std::vector<int16_t> borders;
    int16_t terrainUnitId[30] = {}, terrainUnitDensity[30] = {};
    uint8_t terrainUnitCentering[30] = {};
    int16_t numTerrainUnitsUsed = 0;
};

struct TerrainBorder {
    uint8_t enabled = 0, random = 0;
    std::string name, name2;
    int32_t slp = -1, soundId = -1;
    uint8_t colors[3] = {};
    int16_t drawTerrain = -1, underlayTerrain = -1, borderStyle = 0;
};

struct TerrainBlock {
    int32_t mapWidth, mapHeight, worldWidth, worldHeight;
    struct TileSize { int16_t width, height, deltaY; };
    std::array<TileSize, 19> tileSizes{};
    std::vector<Terrain> terrains;
    std::vector<TerrainBorder> borders;
    int16_t terrainsUsed = 0, bordersUsed = 0, maxTerrain = 0;
    int16_t tileWidth = 0, tileHeight = 0, tileHalfHeight = 0, tileHalfWidth = 0, elevHeight = 0;
};

struct EffectCommand {
    uint8_t type;
    int16_t a, b, c;
    float d;
};

struct Effect {
    std::string name;
    std::vector<EffectCommand> commands;
};

struct UnitLine {
    int16_t id;
    std::string name;
    std::vector<int16_t> unitIds;
};

struct Task {
    int16_t taskType, id;
    uint8_t isDefault;
    int16_t actionType, classId, unitId, terrainId;
    int16_t resourceIn, resourceMultiplier, resourceOut, unusedResource;
    float workValue1, workValue2, workRange;
    uint8_t autoSearchTargets;
    float searchWaitTime;
    uint8_t enableTargeting, combatLevelFlag;
    int16_t gatherType, workFlag2;
    uint8_t targetDiplomacy, carryCheck, pickForConstruction;
    int16_t movingGraphic, proceedingGraphic, workingGraphic, carryingGraphic;
    int16_t resourceGatheringSound, resourceDepositSound;
};

struct UnitHeader {
    bool exists = false;
    std::vector<Task> tasks;
};

struct ResourceStorage { int16_t type; float amount; uint8_t flag; };
struct DamageGraphic { int16_t graphicId, damagePercent; uint8_t applyMode; };
struct AttackOrArmor { int16_t cls, amount; };
struct ResourceCost { int16_t type, amount, flag; };
struct BuildingAnnex { int16_t unitId; float misplacementX, misplacementY; };

enum UnitType : uint8_t {
    UT_EyeCandy = 10, UT_Trees = 15, UT_Flag = 20, UT_25 = 25, UT_DeadFish = 30, UT_Bird = 40,
    UT_Combatant = 50, UT_Projectile = 60, UT_Creatable = 70, UT_Building = 80, UT_AoeTrees = 90,
};

struct Unit {
    bool exists = false;
    uint8_t type = 0;
    int16_t id = -1;
    int16_t languageDllName = 0, languageDllCreation = 0;
    int16_t cls = 0;
    int16_t standingGraphic[2] = {-1, -1};
    int16_t dyingGraphic = -1, undeadGraphic = -1;
    uint8_t undeadMode = 0;
    int16_t hitPoints = 0;
    float lineOfSight = 0;
    uint8_t garrisonCapacity = 0;
    float collisionSize[3] = {};
    int16_t trainSound = -1, damageSound = -1, deadUnitId = -1;
    uint8_t sortNumber = 0, canBeBuiltOn = 0;
    int16_t iconId = -1;
    uint8_t hideInEditor = 0;
    int16_t oldPortraitPict = -1;
    uint8_t enabled = 0, disabled = 0;
    int16_t placementSideTerrain[2] = {}, placementTerrain[2] = {};
    float clearanceSize[2] = {};
    uint8_t hillMode = 0, fogVisibility = 0;
    int16_t terrainRestriction = 0;
    uint8_t flyMode = 0;
    int16_t resourceCapacity = 0;
    float resourceDecay = 0;
    uint8_t blastDefenseLevel = 0, combatLevel = 0, interactionMode = 0, minimapMode = 0, interfaceKind = 0;
    float multipleAttributeMode = 0;
    uint8_t minimapColor = 0;
    int32_t languageDllHelp = 0, languageDllHotKeyText = 0, hotKeyId = 0;
    uint8_t recyclable = 0, enableAutoGather = 0, createDoppelgangerOnDeath = 0, resourceGatherGroup = 0;
    uint8_t occlusionMode = 0, obstructionType = 0, obstructionClass = 0, trait = 0, civilization = 0;
    uint8_t selectionEffect = 0, editorSelectionColour = 0;
    float outlineSize[3] = {};
    std::array<ResourceStorage, 3> resourceStorages{};
    std::vector<DamageGraphic> damageGraphics;
    int16_t selectionSound = -1, dyingSound = -1;
    uint8_t oldAttackReaction = 0, convertTerrain = 0;
    std::string name, name2;
    int16_t unitLine = -1;
    uint8_t minTechLevel = 0;
    int16_t copyId = -1, baseId = -1;

    // Type >= Flag
    float speed = 0;
    // Type >= DeadFish
    int16_t walkingGraphic = -1, runningGraphic = -1;
    float rotationSpeed = 0;
    int16_t trackingUnit = -1;
    uint8_t trackingUnitMode = 0;
    float trackingUnitDensity = 0;
    float turnRadius = 0, turnRadiusSpeed = 0;
    // Type >= Bird
    int16_t defaultTaskId = -1;
    float searchRadius = 0, workRate = 0;
    int16_t dropSites[2] = {-1, -1};
    uint8_t taskSwapGroup = 0;
    int16_t attackSound = -1, moveSound = -1;
    uint8_t runPattern = 0;
    // Type >= Combatant
    int16_t baseArmor = 0;
    std::vector<AttackOrArmor> attacks, armours;
    int16_t defenseTerrainBonus = -1;
    float maxRange = 0, blastWidth = 0, reloadTime = 0;
    int16_t projectileUnitId = -1, accuracyPercent = 0;
    int16_t frameDelay = 0;
    float graphicDisplacement[3] = {};
    uint8_t blastAttackLevel = 0;
    float minRange = 0, accuracyDispersion = 0;
    int16_t attackGraphic = -1;
    int16_t displayedMeleeArmour = 0, displayedAttack = 0;
    float displayedRange = 0, displayedReloadTime = 0;
    // Type == Projectile
    uint8_t projectileType = 0, smartMode = 0, hitMode = 0, vanishMode = 0, areaEffectSpecials = 0;
    float projectileArc = 0;
    // Type >= Creatable
    std::array<ResourceCost, 3> costs{};
    int16_t trainTime = 0, trainLocationId = -1;
    uint8_t buttonId = 0;
    float rearAttackModifier = 0, flankAttackModifier = 0;
    uint8_t creatableType = 0, heroMode = 0;
    int32_t garrisonGraphic = -1;
    float totalProjectiles = 0;
    uint8_t maxTotalProjectiles = 0;
    float projectileSpawningArea[3] = {};
    int32_t secondaryProjectileUnit = -1, specialGraphic = -1;
    uint8_t specialAbility = 0;
    int16_t displayedPierceArmour = 0;
    // Type == Building
    int16_t constructionGraphic = -1, snowGraphic = -1;
    uint8_t adjacentMode = 0;
    int16_t graphicsAngle = 0;
    uint8_t disappearsWhenBuilt = 0;
    int16_t stackUnitId = -1, foundationTerrainId = -1, oldOverlayId = -1, techId = -1;
    uint8_t canBurn = 0;
    std::array<BuildingAnnex, 4> annexes{};
    int16_t headUnit = -1, transformUnit = -1, transformSound = -1, constructionSound = -1;
    uint8_t garrisonType = 0;
    float garrisonHealRate = 0, garrisonRepairRate = 0;
    int16_t pileUnit = -1;
    uint8_t lootingTable[6] = {};
};

struct Civ {
    uint8_t playerType = 0;
    std::string name, name2;
    int16_t techTreeId = -1, teamBonusId = -1;
    int16_t uniqueUnitsTechs[4] = {};
    std::vector<float> resources;
    uint8_t iconSet = 0;
    std::vector<Unit> units; // index = unit id; exists=false for gaps
};

struct Tech {
    int16_t requiredTechs[6] = {};
    struct Cost { int16_t type, amount; uint8_t flag; } costs[3] = {};
    int16_t requiredTechCount = 0, civ = -1, fullTechMode = 0;
    int16_t locationId = -1, languageDllName = 0, languageDllDescription = 0;
    int16_t researchTime = 0, effectId = -1, type = 0, iconId = -1;
    uint8_t buttonId = 0;
    int32_t languageDllHelp = 0, languageDllTechTree = 0, hotKeyId = 0;
    std::string name, name2;
};

struct DatFile {
    std::string version;
    int32_t waypointSprite = -1, moveToSprite = -1, garrisonSound = -1, ungarrisonSound = -1;
    int16_t terrainsUsed = 0;
    std::vector<TerrainRestriction> terrainRestrictions;
    std::vector<PlayerColour> playerColours;
    std::vector<Sound> sounds;
    std::vector<Graphic> graphics; // index = graphic id
    TerrainBlock terrainBlock;
    std::vector<Effect> effects;
    std::vector<UnitLine> unitLines;
    std::vector<UnitHeader> unitHeaders;
    std::vector<Civ> civs;
    std::vector<Tech> techs;
    int32_t timeSlice = 0, unitKillRate = 0, unitKillTotal = 0, unitHitPointRate = 0;
    int32_t unitHitPointTotal = 0, razingKillRate = 0, razingKillTotal = 0;
    // Tech tree is parsed for validation but not stored yet.

    bool load(const std::string &path, std::string *err = nullptr);
    bool loadFromCompressed(const std::vector<uint8_t> &compressed, std::string *err = nullptr);
    bool loadFromRaw(const std::vector<uint8_t> &raw, std::string *err = nullptr);

    const Graphic *graphic(int32_t id) const {
        return (id >= 0 && (size_t)id < graphics.size() && graphics[id].exists) ? &graphics[id] : nullptr;
    }
};

// Inflates a raw-deflate stream (the dat compression).
bool inflateRaw(const std::vector<uint8_t> &in, std::vector<uint8_t> &out, std::string *err = nullptr);

} // namespace dat
} // namespace swgb
