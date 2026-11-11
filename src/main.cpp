// 3D Endless Runner - C++17 + raylib
// Controls (PC):     A/D or Left/Right = change lane, W/Up/Space = jump, S/Down = slide, P/Esc = pause
// Controls (mobile): swipe left/right = lane, swipe up = jump, swipe down = slide, tap = buttons
#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// ----------------------------------------------------------------- constants
constexpr float LANE_W     = 2.4f;    // distance between lane centres
constexpr int   SEG_COUNT  = 8;       // number of recycled track segments
constexpr float SEG_LEN    = 20.0f;
constexpr float SPAWN_Z    = -110.0f; // where new obstacles appear (player is at z = 0)
constexpr float GRAVITY    = 26.0f;
constexpr float JUMP_V     = 9.5f;
constexpr float STAND_H    = 1.6f;    // hitbox height while running
constexpr float SLIDE_H    = 0.6f;    // hitbox height while sliding
constexpr float SLIDE_TIME = 0.7f;
constexpr float BASE_SPEED = 14.0f;
constexpr float MAX_SPEED  = 34.0f;
constexpr float HALF_W     = 0.35f;   // player hitbox half width / depth
constexpr float HALF_D     = 0.35f;
constexpr int   MAX_PARTICLES = 220;
// police chase: gap (metres) between the runner and the policeman behind him
constexpr float COP_GAP_BASE = 4.6f, COP_GAP_MIN = 1.9f, COP_STEP = 1.7f, COP_RECOVER = 0.12f, COP_FINAL_GAP = 0.46f;

enum class State  { Menu, Playing, Paused, GameOver };
enum class ObType { Low, High, Block }; // Low: jump over, High: slide under, Block: change lane

struct Obstacle { ObType type; int lane; float z; int street; int seed; };
struct Coin     { int lane; float z; bool taken; };
struct Segment  { float z; int type; bool gate; float r[16]; };
struct Player   { int lane = 1; float x = 0, y = 0, vy = 0, slideTimer = 0; bool onGround = true; };
struct Particle { Vector3 pos, vel; float life, maxLife, size; Color color; };

struct Input {
    bool left = false, right = false, jump = false, slide = false;
    bool confirm = false, pause = false, tap = false;
    Vector2 tapPos{0, 0};
};

struct Pointer { bool tracking = false, swiped = false; Vector2 start{0, 0}; };
struct Textures { Texture2D road, grass, laneLine, crate, dirt, paver, sidewalk, brick, brickwin, facade, shopfront, jpath, jfloor; };
struct Models   { Model head, capsule; };
struct PlayerAnim {
    Model model{};
    ModelAnimation* anims = nullptr;
    int animCount = 0;
    int idxRun = 0, idxJump = 0, idxIdle = 0, idxWalk = 0, idxSlide = 0, idxArrested = 0, idxGrab = 0;
};

float RandF() { return (float)GetRandomValue(0, 1000) / 1000.0f; }
float LaneX(int lane) { return (float)(lane - 1) * LANE_W; }
float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ----------------------------------------------------------------- color helpers (fake lighting / fog)
Color MixC(Color a, Color b, float t) {
    t = ClampF(t, 0.0f, 1.0f);
    return { (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
             (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}
Color ShadeC(Color c, float f) { // f > 1 lightens, f < 1 darkens
    auto ch = [&](unsigned char v) { float r = v * f; return (unsigned char)ClampF(r, 0.0f, 255.0f); };
    return { ch(c.r), ch(c.g), ch(c.b), c.a };
}
Color gHaze = {198, 224, 240, 255};
float gFogNear = 50.0f, gFogRange = 140.0f;          // rain/cloud pull the fog in
Color FogC(Color c, float dist) { return MixC(c, gHaze, ClampF((dist - gFogNear) / gFogRange, 0.0f, 0.85f)); }
// Climate values, refreshed every frame from the Game (used by props, sky and the scene shader)
float gNight = 0, gDusk = 0, gCloud = 0, gRain = 0;
// Vertex-colour alpha 247 marks a surface as "emissive": the scene shader leaves it un-tinted (lamps, lit windows, coins, sun, moon).
Color Glow(Color c) { c.a = 247; return c; }

// ----------------------------------------------------------------- textures
Texture2D LoadGameTexture(const std::string& base, const char* file) {
    Texture2D t = LoadTexture((base + file).c_str());
    if (t.id != 0) {
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    }
    return t;
}
Textures LoadGameTextures() {
    std::string base = std::string(GetApplicationDirectory()) + "assets/textures/";
    Textures t;
    t.road     = LoadGameTexture(base, "road.png");
    t.grass    = LoadGameTexture(base, "grass.png");
    t.laneLine = LoadGameTexture(base, "lane_line.png");
    t.crate    = LoadGameTexture(base, "crate.png");
    t.dirt     = LoadGameTexture(base, "dirt.png");
    t.paver    = LoadGameTexture(base, "paver.png");
    t.sidewalk = LoadGameTexture(base, "sidewalk.png");
    t.brick    = LoadGameTexture(base, "brick.png");
    t.brickwin = LoadGameTexture(base, "brickwin.png");
    t.facade   = LoadGameTexture(base, "facade.png");
    t.shopfront= LoadGameTexture(base, "shopfront.png");
    t.jpath    = LoadGameTexture(base, "jungle_path.png");
    t.jfloor   = LoadGameTexture(base, "jungle_floor.png");
    return t;
}
void UnloadGameTextures(const Textures& t) {
    UnloadTexture(t.road); UnloadTexture(t.grass); UnloadTexture(t.laneLine); UnloadTexture(t.crate);
    UnloadTexture(t.dirt); UnloadTexture(t.paver); UnloadTexture(t.sidewalk); UnloadTexture(t.brick);
    UnloadTexture(t.brickwin); UnloadTexture(t.facade); UnloadTexture(t.shopfront);
    UnloadTexture(t.jpath); UnloadTexture(t.jfloor);
}

// Draws a textured, tileable quad lying flat on the XZ plane (used for ground/lane markings).
// tileU/tileV control how many times the texture repeats across the quad's width/depth.
void DrawGroundQuad(Texture2D tex, Vector3 center, float w, float d, float tileU, float tileV, Color tint) {
    if (tex.id == 0) { DrawCube(center, w, 0.02f, d, tint); return; } // fallback if texture failed to load
    float x0 = center.x - w / 2, x1 = center.x + w / 2;
    float z0 = center.z - d / 2, z1 = center.z + d / 2;
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlNormal3f(0, 1, 0);
    rlTexCoord2f(0, 0);           rlVertex3f(x0, center.y, z0);
    rlTexCoord2f(0, tileV);       rlVertex3f(x0, center.y, z1);
    rlTexCoord2f(tileU, tileV);   rlVertex3f(x1, center.y, z1);
    rlTexCoord2f(tileU, 0);       rlVertex3f(x1, center.y, z0);
    rlEnd();
    rlSetTexture(0);
}
// Same, but standing upright (for a textured obstacle face), facing along -Z.
void DrawWallQuad(Texture2D tex, Vector3 center, float w, float h, float tileU, float tileV, Color tint) {
    if (tex.id == 0) { DrawCube(center, w, h, 0.05f, tint); return; }
    float x0 = center.x - w / 2, x1 = center.x + w / 2;
    float y0 = center.y - h / 2, y1 = center.y + h / 2;
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlNormal3f(0, 0, 1);
    rlTexCoord2f(0, tileV);       rlVertex3f(x0, y0, center.z);
    rlTexCoord2f(tileU, tileV);   rlVertex3f(x1, y0, center.z);
    rlTexCoord2f(tileU, 0);       rlVertex3f(x1, y1, center.z);
    rlTexCoord2f(0, 0);           rlVertex3f(x0, y1, center.z);
    rlEnd();
    rlSetTexture(0);
}

// ----------------------------------------------------------------- 3D models
// Two reusable meshes cover the whole character and both bar obstacles:
//  - "head"    a unit icosphere, uniformly scaled per use.
//  - "capsule" a unit capsule (radius 0.5, axis Z), reused as a vertical pill
//    (torso/limbs, via DrawCapsuleY) or a horizontal one (obstacle bars, via
//    DrawCapsuleX) just by rotating + non-uniformly scaling it in code.
Models LoadGameModels() {
    std::string base = std::string(GetApplicationDirectory()) + "assets/models/";
    Models m;
    m.head    = LoadModel((base + "head.glb").c_str());
    m.capsule = LoadModel((base + "capsule.glb").c_str());
    return m;
}
void UnloadGameModels(const Models& m) { UnloadModel(m.head); UnloadModel(m.capsule); }

// Rigged player character (mesh + skin + 5 baked clips: Run/Jump/Idle/Walk/Slide),
// built from the uploaded Boy_04 model and Mixamo animation clips.
PlayerAnim LoadPlayerAnim() {
    PlayerAnim pa;
    std::string path = std::string(GetApplicationDirectory()) + "assets/models/player.glb";
    pa.model = LoadModel(path.c_str());
    pa.anims = LoadModelAnimations(path.c_str(), &pa.animCount);
    for (int i = 0; i < pa.animCount; i++) {
        std::string nm = pa.anims[i].name;
        if (nm == "Run") pa.idxRun = i;
        else if (nm == "Jump") pa.idxJump = i;
        else if (nm == "Idle") pa.idxIdle = i;
        else if (nm == "Walk") pa.idxWalk = i;
        else if (nm == "Slide") pa.idxSlide = i;
        else if (nm == "Arrested") pa.idxArrested = i;
        else if (nm == "Grab") pa.idxGrab = i;
    }
    return pa;
}
// The policeman: same skeleton as the runner (so he shares every animation clip), but with a
// repainted police-uniform texture; cap, badge, belt etc. are attached to his bones when drawn.
struct CopRig { Model model{}; int bHead = -1, bSpine2 = -1, bHips = -1, bHandL = -1, bHandR = -1; };
CopRig LoadCopRig() {
    CopRig c;
    std::string base = GetApplicationDirectory();
    c.model = LoadModel((base + "assets/models/player.glb").c_str());
    Texture2D tex = LoadTexture((base + "assets/textures/cop_uniform.jpg").c_str());
    if (tex.id != 0 && c.model.meshCount > 0) {
        SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
        MaterialMap& map = c.model.materials[c.model.meshMaterial[0]].maps[MATERIAL_MAP_ALBEDO];
        UnloadTexture(map.texture);                                   // drop the runner's texture, use the uniform
        map.texture = tex;
    }
    for (int i = 0; i < c.model.boneCount; i++) {
        std::string nm = c.model.bones[i].name;
        if (nm == "mixamorig:Head") c.bHead = i; else if (nm == "mixamorig:Spine2") c.bSpine2 = i;
        else if (nm == "mixamorig:Hips") c.bHips = i; else if (nm == "mixamorig:LeftHand") c.bHandL = i;
        else if (nm == "mixamorig:RightHand") c.bHandR = i;
    }
    return c;
}
void UnloadCopRig(CopRig& c) { UnloadModel(c.model); }

void UnloadPlayerAnim(PlayerAnim& pa) {
    if (pa.anims) UnloadModelAnimations(pa.anims, pa.animCount);
    UnloadModel(pa.model);
}

void DrawSphereModel(Model m, Vector3 pos, float radius, Color tint) {
    if (m.meshCount == 0) { DrawSphere(pos, radius, tint); return; }
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlScalef(radius, radius, radius);
    DrawModel(m, {0, 0, 0}, 1.0f, tint);
    rlPopMatrix();
}
// Vertical pill: w = width (X), h = height (Y), d = depth (Z) — same (w,h,d) meaning as DrawCube.
void DrawCapsuleY(Model m, Vector3 pos, float w, float h, float d, Color tint) {
    if (m.meshCount == 0) { DrawCube(pos, w, h, d, tint); return; }
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(90, 1, 0, 0);
    rlScalef(w, d, h * 0.5f);
    DrawModel(m, {0, 0, 0}, 1.0f, tint);
    rlPopMatrix();
}
// Horizontal pill (spans the X axis, e.g. an obstacle bar): same (w,h,d) meaning as DrawCube.
void DrawCapsuleX(Model m, Vector3 pos, float w, float h, float d, Color tint) {
    if (m.meshCount == 0) { DrawCube(pos, w, h, d, tint); return; }
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(90, 0, 1, 0);
    rlScalef(d, h, w * 0.5f);
    DrawModel(m, {0, 0, 0}, 1.0f, tint);
    rlPopMatrix();
}

// ----------------------------------------------------------------- high score
int LoadBest() {
    int v = 0;
#if !defined(PLATFORM_ANDROID)
    if (FILE* f = fopen("highscore.dat", "rb")) {
        if (fread(&v, sizeof v, 1, f) != 1) v = 0;
        fclose(f);
    }
#endif
    return v;
}
void SaveBest(int v) {
#if !defined(PLATFORM_ANDROID)
    if (FILE* f = fopen("highscore.dat", "wb")) { fwrite(&v, sizeof v, 1, f); fclose(f); }
#else
    (void)v;
#endif
}

// ----------------------------------------------------------------- input
Input PollInput(Pointer& ptr, float s) {
    Input in;
    in.left    = IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A);
    in.right   = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D);
    in.jump    = IsKeyPressed(KEY_UP)    || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_SPACE);
    in.slide   = IsKeyPressed(KEY_DOWN)  || IsKeyPressed(KEY_S);
    in.confirm = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER);
    in.pause   = IsKeyPressed(KEY_P)     || IsKeyPressed(KEY_ESCAPE);

    // Mouse / touch (raylib maps touch 0 to the mouse on mobile): swipes + taps
    const float thr = 40.0f * s;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        ptr.tracking = true; ptr.swiped = false; ptr.start = GetMousePosition();
    }
    if (ptr.tracking && !ptr.swiped && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        float dx = m.x - ptr.start.x, dy = m.y - ptr.start.y;
        if (fabsf(dx) > thr && fabsf(dx) > fabsf(dy)) {
            (dx > 0 ? in.right : in.left) = true; ptr.swiped = true;
        } else if (fabsf(dy) > thr) {
            (dy < 0 ? in.jump : in.slide) = true; ptr.swiped = true;
        }
    }
    if (ptr.tracking && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (!ptr.swiped) { in.tap = true; in.tapPos = GetMousePosition(); }
        ptr.tracking = false;
    }
    return in;
}

// ----------------------------------------------------------------- street types + road bending
enum Street { ST_RESIDENTIAL, ST_COMMERCIAL, ST_MAIN, ST_ALLEY, ST_AVENUE, ST_BOULEVARD,
              ST_CULDESAC, ST_ARTERIAL, ST_RURAL, ST_PEDESTRIAN, ST_JUNGLE, ST_COUNT };
enum TexId { TX_ROAD, TX_DIRT, TX_PAVER, TX_GRASS, TX_SIDEWALK, TX_BRICK, TX_JPATH, TX_JFLOOR };
enum Edge  { EDGE_CURB, EDGE_BARRIER, EDGE_NONE, EDGE_BOLLARDS };

struct Theme {
    const char* name; const char* blurb;
    int road;   Color roadTint;                     // road surface
    int ground; Color groundTint;                   // everything beyond the road
    float sidewalk; int sidewalkTex; Color sidewalkTint;
    int edge;                                       // what lines the road edge
    bool lanes; Color laneCol; bool edgeLine; Color edgeCol;
    Color skyTop, skyBot, sign;                     // sky gradient + street-gate colour
    float curveMax, curveLen;                       // max bend strength, metres per bend
    float speedMul, gapMul, coinChance;             // difficulty
    float pLow, pHigh, pBlock;                      // obstacle mix
    int blockStyle;                                 // 0 truck, 1 crate stack, 2 hay bales, 3 market kiosk
    Color lowCol, highCol;
};

const Theme THEMES[ST_COUNT] = {
 /* Residential */ {"Residential Street", "Quiet homes and picket fences",
    TX_ROAD, {255,255,255,255}, TX_GRASS, {255,255,255,255}, 1.8f, TX_SIDEWALK, {235,235,230,255}, EDGE_CURB,
    true, {255,255,255,255}, false, {255,255,255,255}, {70,130,210,255}, {175,218,242,255}, {46,125,80,255},
    0.8f, 140, 1.00f, 1.00f, 0.70f, 0.30f, 0.25f, 0.25f, 0, {255,150,40,255}, {220,50,60,255}},
 /* Commercial  */ {"Commercial Street", "Shops, signs and traffic",
    TX_ROAD, {235,235,240,255}, TX_SIDEWALK, {190,190,190,255}, 3.2f, TX_SIDEWALK, {225,225,225,255}, EDGE_CURB,
    true, {255,255,255,255}, false, {255,255,255,255}, {85,140,215,255}, {185,215,235,255}, {40,90,170,255},
    0.55f, 120, 1.05f, 0.95f, 0.65f, 0.26f, 0.24f, 0.32f, 0, {255,150,40,255}, {235,205,45,255}},
 /* Main        */ {"Main Street", "Brick storefronts and flags",
    TX_ROAD, {225,225,225,255}, TX_SIDEWALK, {190,185,180,255}, 3.0f, TX_PAVER, {240,220,200,255}, EDGE_CURB,
    true, {255,230,120,255}, false, {255,255,255,255}, {110,150,215,255}, {232,216,190,255}, {150,50,40,255},
    0.50f, 130, 1.00f, 1.00f, 0.75f, 0.30f, 0.25f, 0.25f, 0, {235,90,50,255}, {200,60,60,255}},
 /* Alley       */ {"Alley", "Narrow, dark and cluttered",
    TX_ROAD, {165,165,170,255}, TX_ROAD, {90,90,95,255}, 0.0f, TX_SIDEWALK, {200,200,200,255}, EDGE_NONE,
    false, {255,255,255,255}, false, {255,255,255,255}, {40,55,88,255}, {125,130,150,255}, {70,70,82,255},
    1.00f, 90, 0.90f, 0.88f, 0.50f, 0.25f, 0.20f, 0.35f, 1, {70,115,75,255}, {120,120,130,255}},
 /* Avenue      */ {"Avenue", "Wide, tree-lined and busy",
    TX_ROAD, {245,245,245,255}, TX_SIDEWALK, {185,185,185,255}, 2.6f, TX_SIDEWALK, {230,230,228,255}, EDGE_CURB,
    true, {255,255,255,255}, true, {255,255,255,255}, {60,125,215,255}, {190,225,245,255}, {30,110,150,255},
    0.40f, 220, 1.15f, 1.00f, 0.70f, 0.28f, 0.22f, 0.30f, 0, {255,150,40,255}, {220,50,60,255}},
 /* Boulevard   */ {"Boulevard", "Palm medians at golden hour",
    TX_ROAD, {250,245,240,255}, TX_GRASS, {235,235,200,255}, 0.0f, TX_SIDEWALK, {230,230,228,255}, EDGE_CURB,
    true, {255,255,255,255}, true, {255,235,150,255}, {92,98,178,255}, {255,196,138,255}, {185,92,40,255},
    0.35f, 240, 1.10f, 1.00f, 0.85f, 0.30f, 0.25f, 0.20f, 0, {255,150,40,255}, {205,60,125,255}},
 /* Cul-de-sac  */ {"Cul-de-sac", "Round the loop at the dead end",
    TX_ROAD, {255,255,255,255}, TX_GRASS, {255,255,255,255}, 1.6f, TX_SIDEWALK, {232,232,226,255}, EDGE_CURB,
    false, {255,255,255,255}, false, {255,255,255,255}, {75,135,205,255}, {242,208,172,255}, {120,80,150,255},
    1.25f, 400, 0.85f, 1.10f, 0.75f, 0.32f, 0.25f, 0.18f, 0, {255,150,40,255}, {220,50,60,255}},
 /* Arterial    */ {"Arterial Road", "Fast lanes, barriers and billboards",
    TX_ROAD, {190,190,195,255}, TX_GRASS, {165,175,125,255}, 0.0f, TX_SIDEWALK, {200,200,200,255}, EDGE_BARRIER,
    true, {255,255,255,255}, true, {255,215,60,255}, {95,135,192,255}, {208,216,226,255}, {0,112,72,255},
    0.30f, 260, 1.30f, 0.85f, 0.60f, 0.22f, 0.20f, 0.38f, 0, {255,150,40,255}, {235,205,45,255}},
 /* Rural       */ {"Rural Road", "Dirt track, fences and open fields",
    TX_DIRT, {255,255,255,255}, TX_GRASS, {205,205,125,255}, 0.0f, TX_SIDEWALK, {200,200,200,255}, EDGE_NONE,
    false, {255,255,255,255}, false, {255,255,255,255}, {70,140,230,255}, {190,230,250,255}, {112,82,42,255},
    1.00f, 100, 1.00f, 1.05f, 0.80f, 0.30f, 0.25f, 0.22f, 2, {125,85,48,255}, {145,105,62,255}},
 /* Pedestrian  */ {"Pedestrian Street", "Cobblestones, cafes and no cars",
    TX_PAVER, {255,245,235,255}, TX_PAVER, {230,225,215,255}, 0.0f, TX_PAVER, {255,255,255,255}, EDGE_BOLLARDS,
    false, {255,255,255,255}, false, {255,255,255,255}, {100,155,225,255}, {246,236,216,255}, {172,62,112,255},
    0.65f, 130, 0.80f, 1.00f, 0.90f, 0.30f, 0.28f, 0.18f, 3, {85,165,205,255}, {200,125,200,255}},
 /* Jungle      */ {"Jungle", "Thick canopy, vines and hidden ruins",
    TX_JPATH, {255,255,255,255}, TX_JFLOOR, {215,230,215,255}, 0.0f, TX_SIDEWALK, {200,200,200,255}, EDGE_NONE,
    false, {255,255,255,255}, false, {255,255,255,255}, {48,118,96,255}, {158,200,148,255}, {28,98,52,255},
    1.10f, 85, 0.95f, 1.00f, 0.85f, 0.30f, 0.26f, 0.20f, 4, {122,84,48,255}, {88,130,58,255}},
};

// "World bending": every object ahead of the player is pushed sideways by a parabola of its distance,
// so the road sweeps left or right. At the player (z = 0) the offset and slope are zero, so lanes,
// collisions and controls are completely unaffected.
constexpr float BEND_K = 0.0013f;
float BendOff(float curve, float z) { float d = -z; if (d < 0) d = 0; return curve * BEND_K * d * d; }
float BendAng(float curve, float z) { return z >= 0 ? 0.0f : atanf(2.0f * curve * BEND_K * z); }   // radians

// ----------------------------------------------------------------- game
struct Game {
    State state = State::Menu;
    Player p;
    std::vector<Obstacle> obs;
    std::vector<Coin> coins;
    std::vector<Particle> particles;
    Segment segs[SEG_COUNT];
    float speed = BASE_SPEED, distance = 0, time = 0, runAnim = 0, coinSpin = 0;
    float spawnAcc = 0, spawnGap = 16, deadTimer = 0;
    int coinCount = 0, best = 0;
    bool newBest = false;
    // street + curve state
    int genType = ST_RESIDENTIAL, curType = ST_RESIDENTIAL, toastType = 0, culSign = 1;
    float genRemain = 360.0f, curve = 0, curveTarget = 0, curveRemain = 120.0f, speedMul = 1.0f, toast = 0;
    // police chase
    float copGap = COP_GAP_BASE, copVis = COP_GAP_BASE, copGap0 = COP_GAP_BASE, copX = 0, copY = 0, copVy = 0, copSlide = 0;
    int copLane = 1, copClip = 0, copFrame = 0, plClip = 0, plFrame = 0;
    float stumbleT = 0, stumbleSlow = 0, shake = 0, arrestT = 0;
    bool caught = false;
    // time of day + weather. Modes: 0 = automatic. Time: 1 = always day, 2 = always night. Weather: 1 clear, 2 cloudy, 3 rainy.
    int todMode = 0, wxMode = 0, wxState = 0, wxPrev = 0;
    float phase = 0.02f, night = 0, cloud = 0.08f, rain = 0, wxRemain = 55.0f, flashRemain = 14.0f, flash = 0, climateMsgT = 0;
    bool nightPrev = false;
    const char* climateMsg = "";

    int Score() const { return (int)distance + coinCount * 10; }

    // Fills a segment's random decoration values and gives it a street type. Segments are generated
    // at the far end of the track; every few hundred metres the generator switches to a new street type
    // and flags that segment so a street-name gate is built across the road.
    void NewSegment(Segment& s) {
        for (float& v : s.r) v = RandF();
        genRemain -= SEG_LEN; s.gate = false;
        if (genRemain <= 0) {
            int t; do { t = GetRandomValue(0, ST_COUNT - 1); } while (t == genType);
            genType = t; genRemain = 320.0f + RandF() * 220.0f; s.gate = true;
        }
        s.type = genType;
    }
    int TypeAtZ(float z) const {
        for (const Segment& s : segs) if (z <= s.z + SEG_LEN / 2 && z > s.z - SEG_LEN / 2) return s.type;
        return genType;
    }
    void JumpToStreet(int t) {              // preview helper (number keys): switch the whole visible track
        t = t < 0 ? 0 : (t >= ST_COUNT ? ST_COUNT - 1 : t);
        genType = t; genRemain = 320.0f + RandF() * 220.0f; curType = t; toast = 3.2f; toastType = t; curveRemain = 0;
        if (t == ST_CULDESAC) culSign = GetRandomValue(0, 1) ? 1 : -1;
        for (Segment& s : segs) { s.type = t; s.gate = false; }
    }

    void Reset() {
        p = Player();
        obs.clear(); coins.clear(); particles.clear();
        speed = BASE_SPEED; distance = 0; time = 0; runAnim = 0;
        spawnAcc = 0; spawnGap = 16; coinCount = 0; newBest = false; deadTimer = 0;
        genType = curType = ST_RESIDENTIAL; genRemain = 360.0f; speedMul = 1.0f; toast = 0;
        curve = curveTarget = 0; curveRemain = 120.0f;
        copGap = copVis = copGap0 = COP_GAP_BASE; copX = 0; copY = copVy = copSlide = 0; copLane = 1;
        stumbleT = stumbleSlow = shake = arrestT = 0; caught = false;
        for (int i = 0; i < SEG_COUNT; i++) {
            segs[i].z = -(float)i * SEG_LEN; segs[i].type = ST_RESIDENTIAL; segs[i].gate = false;
            for (float& v : segs[i].r) v = RandF();
        }
    }

    void Emit(Vector3 pos, int count, Color color, float speed_, float life, float size, bool up) {
        for (int i = 0; i < count; i++) {
            if ((int)particles.size() >= MAX_PARTICLES) particles.erase(particles.begin());
            float ang = RandF() * 6.2831853f, r = 0.3f + RandF() * 0.7f;
            Particle pt;
            pt.pos = pos;
            pt.vel = { cosf(ang) * r * speed_, up ? (1.5f + RandF() * speed_) : (RandF() * speed_ * 0.4f), sinf(ang) * r * speed_ };
            pt.life = pt.maxLife = life * (0.6f + RandF() * 0.7f);
            pt.size = size * (0.6f + RandF() * 0.8f);
            pt.color = color;
            particles.push_back(pt);
        }
    }

    void Spawn() {
        float z = SPAWN_Z + spawnAcc;
        const Theme& th = THEMES[TypeAtZ(z)];
        int st = TypeAtZ(z);
        bool occ[3] = {false, false, false};
        float r = RandF();
        int lane = GetRandomValue(0, 2);
        if (r < th.pLow) {
            obs.push_back({ObType::Low, lane, z, st, GetRandomValue(0, 99)}); occ[lane] = true;
        } else if (r < th.pLow + th.pHigh) {
            obs.push_back({ObType::High, lane, z, st, GetRandomValue(0, 99)}); occ[lane] = true;
        } else if (r < th.pLow + th.pHigh + th.pBlock) {
            if (GetRandomValue(0, 1)) {               // two blocked lanes, one open
                int open = GetRandomValue(0, 2);
                for (int l = 0; l < 3; l++) if (l != open) { obs.push_back({ObType::Block, l, z, st, GetRandomValue(0, 99)}); occ[l] = true; }
            } else {
                obs.push_back({ObType::Block, lane, z, st, GetRandomValue(0, 99)}); occ[lane] = true;
            }
        }
        // coin line in a free lane
        if (r >= th.pLow + th.pHigh + th.pBlock || RandF() < th.coinChance) {
            int free[3], n = 0;
            for (int l = 0; l < 3; l++) if (!occ[l]) free[n++] = l;
            if (n > 0) {
                int cl = free[GetRandomValue(0, n - 1)];
                for (int i = 0; i < 5; i++) coins.push_back({cl, z - 3.0f - 2.0f * i, false});
            }
        }
    }

    void Die() {                                   // the policeman has caught up: the arrest begins
        state = State::GameOver; deadTimer = 0; caught = true; arrestT = 0; copGap0 = copVis;
        if (Score() > best) { best = Score(); newBest = true; SaveBest(best); }
    }

    // Hitting an obstacle is a stumble: you lose speed and the policeman closes in.
    // If he is already close, the second stumble gets you caught.
    void OnHit(Obstacle& o) {
        if (copGap - COP_STEP <= COP_GAP_MIN) { o.z = 100.0f; Die(); return; }
        copGap -= COP_STEP; stumbleT = 1.4f; stumbleSlow = 1.0f; shake = 0.55f;
        const Theme& th = THEMES[o.street];
        Color col = o.type == ObType::High ? th.highCol : (o.type == ObType::Low ? th.lowCol : Color{170, 130, 90, 255});
        Emit({LaneX(o.lane), 1.0f, 0.0f}, 22, col, 4.5f, 0.7f, 0.16f, true);
        Emit({LaneX(o.lane), 0.5f, 0.0f}, 8, {230, 230, 230, 255}, 3.0f, 0.5f, 0.12f, true);
        o.z = 100.0f;                              // smashed: removed on the next frame
    }

    // The policeman follows the runner's lane, hops low obstacles, slides under high ones and
    // swerves round blockers that the runner has already passed.
    void UpdateCop(float dt) {
        if (stumbleT <= 0) copGap = std::min(COP_GAP_BASE, copGap + COP_RECOVER * dt);
        copVis += (copGap - copVis) * std::min(1.0f, dt * 5.0f);
        bool blocked[3] = {false, false, false};
        for (const Obstacle& o : obs) {
            float d = copVis - o.z;
            if (o.type == ObType::Block && d > -0.8f && d < speed * 0.9f + 0.8f) blocked[o.lane] = true;
        }
        int want = p.lane;
        if (blocked[want]) {
            int bestL = want, bestD = 9;
            for (int l = 0; l < 3; l++) if (!blocked[l] && abs(l - p.lane) < bestD) { bestD = abs(l - p.lane); bestL = l; }
            want = bestL;
        }
        copLane = want;
        copX += (LaneX(copLane) - copX) * std::min(1.0f, 9.0f * dt);
        for (const Obstacle& o : obs) {
            if (fabsf(LaneX(o.lane) - copX) > 1.0f) continue;
            float tta = (copVis - o.z) / std::max(speed, 1.0f);              // seconds until it reaches him
            if (o.type == ObType::Low && copY <= 0.0f && tta > 0.2f && tta < 0.36f) copVy = JUMP_V;
            if (o.type == ObType::High && tta > 0.0f && tta < 0.3f) copSlide = 0.7f;
        }
        copVy -= GRAVITY * dt; copY += copVy * dt;
        if (copY <= 0) { copY = 0; copVy = 0; }
        if (copSlide > 0) copSlide -= dt;
    }

    // Arrest cinematic: the world coasts to a stop, the policeman sprints up and grabs the runner.
    void UpdateArrest(float dt) {
        arrestT += dt; deadTimer += dt;
        speed *= expf(-5.0f * dt);
        float dz = speed * dt;
        for (Segment& sg : segs) { sg.z += dz; if (sg.z - SEG_LEN / 2 > 9.0f) { sg.z -= SEG_COUNT * SEG_LEN; NewSegment(sg); } }
        for (Obstacle& o : obs) o.z += dz;
        for (Coin& c : coins) c.z += dz;
        for (auto it = particles.begin(); it != particles.end();) {
            it->pos.z += dz; it->vel.y -= 9.0f * dt;
            it->pos.x += it->vel.x * dt; it->pos.y += it->vel.y * dt; it->pos.z += it->vel.z * dt; it->life -= dt;
            if (it->life <= 0 || it->pos.y < -1.0f) it = particles.erase(it); else ++it;
        }
        p.vy -= GRAVITY * dt; p.y += p.vy * dt; if (p.y <= 0) { p.y = 0; p.vy = 0; } p.slideTimer = 0;
        float k = ClampF(arrestT / 0.9f, 0.0f, 1.0f); k = k * k * (3.0f - 2.0f * k);
        copVis = copGap0 + (COP_FINAL_GAP - copGap0) * k;
        copX += (p.x - copX) * std::min(1.0f, 10.0f * dt);
        copY = 0; copSlide = 0; coinSpin += 200.0f * dt;
    }

    // ---------------- day / night + weather ----------------
    static float NightFromPhase(float p) {              // 0 = full day, 1 = full night
        auto sm = [](float a, float b, float x) { float t = ClampF((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); };
        return sm(0.42f, 0.54f, p) * (1.0f - sm(0.90f, 1.0f, p));
    }
    void Announce(const char* m) { climateMsg = m; climateMsgT = 3.0f; }
    void CycleTime() {
        todMode = (todMode + 1) % 3;
        Announce(todMode == 0 ? "Time of day: automatic" : (todMode == 1 ? "Time of day: always day" : "Time of day: always night"));
    }
    void CycleWeather() {
        static const char* msg[4] = {"Weather: automatic", "Weather: always clear", "Weather: always cloudy", "Weather: always rainy"};
        wxMode = (wxMode + 1) % 4; Announce(msg[wxMode]);
        if (wxMode == 0) wxRemain = 6.0f;
    }
    // advance = true while the game is actually running (the automatic cycle only moves then)
    void UpdateClimate(float dt, bool advance) {
        float nTarget;
        if (todMode == 1) nTarget = 0.0f;
        else if (todMode == 2) nTarget = 1.0f;
        else { if (advance) { phase += dt / 270.0f; if (phase >= 1.0f) phase -= 1.0f; } nTarget = NightFromPhase(phase); }
        night += (nTarget - night) * std::min(1.0f, dt * (todMode == 0 ? 3.0f : 0.9f));
        bool isNight = nTarget > 0.5f;
        if (isNight != nightPrev) { nightPrev = isNight; if (todMode == 0) Announce(isNight ? "Night falls" : "Sunrise"); }

        if (wxMode == 0) {
            if (advance) {
                wxRemain -= dt;
                if (wxRemain <= 0) {
                    int ns; do { float r = RandF(); ns = r < 0.4f ? 0 : (r < 0.7f ? 1 : 2); } while (ns == wxState);
                    wxState = ns; wxRemain = 50.0f + RandF() * 40.0f;
                }
            }
        } else wxState = wxMode - 1;
        if (wxState != wxPrev) {
            wxPrev = wxState;
            if (wxMode == 0) Announce(wxState == 0 ? "The sky clears" : (wxState == 1 ? "Clouds roll in" : "It starts to rain"));
        }
        float cT = wxState == 0 ? 0.08f : (wxState == 1 ? 0.88f : 1.0f), rT = wxState == 2 ? 1.0f : 0.0f;
        cloud += (cT - cloud) * std::min(1.0f, dt * 0.35f);
        rain  += (rT - rain)  * std::min(1.0f, dt * 0.45f);

        if (advance && rain > 0.7f) {                    // distant lightning during a storm (soft, brief)
            flashRemain -= dt;
            if (flashRemain <= 0) { flash = 1.0f; flashRemain = 10.0f + RandF() * 14.0f; }
        }
        flash = std::max(0.0f, flash - dt * 3.5f);
        if (climateMsgT > 0) climateMsgT -= dt;
    }

    void Update(float dt, const Input& in) {
        time += dt;
        speedMul += (THEMES[curType].speedMul - speedMul) * std::min(1.0f, dt * 0.6f);   // each street has its own pace
        speed = std::min(MAX_SPEED, (BASE_SPEED + time * 0.3f) * speedMul);
        speed *= 1.0f - 0.5f * stumbleSlow;                                               // stumbling costs speed
        stumbleSlow = std::max(0.0f, stumbleSlow - dt / 1.8f);
        if (stumbleT > 0) stumbleT -= dt;
        if (shake > 0) shake = std::max(0.0f, shake - dt * 1.4f);
        float dz = speed * dt;
        distance += dz; runAnim += speed * dt * 0.9f; coinSpin += 200.0f * dt;

        bool wasGrounded = p.onGround;
        bool wasSliding  = p.slideTimer > 0;

        // --- player input
        if (in.left  && p.lane > 0) p.lane--;
        if (in.right && p.lane < 2) p.lane++;
        if (in.jump && p.onGround) { p.vy = JUMP_V; p.onGround = false; p.slideTimer = 0; }
        if (in.slide) { if (!p.onGround) p.vy = -18.0f; p.slideTimer = SLIDE_TIME; }

        // --- player physics
        p.x += (LaneX(p.lane) - p.x) * std::min(1.0f, 16.0f * dt);
        p.vy -= GRAVITY * dt; p.y += p.vy * dt;
        if (p.y <= 0) { p.y = 0; p.vy = 0; p.onGround = true; } else p.onGround = false;
        if (p.slideTimer > 0) p.slideTimer -= dt;
        float h = p.slideTimer > 0 ? SLIDE_H : STAND_H;

        if (!wasGrounded && p.onGround) Emit({p.x, 0.05f, 0}, 7, {225, 215, 195, 255}, 2.2f, 0.32f, 0.11f, true);
        if (in.slide && wasGrounded && !wasSliding) Emit({p.x, 0.05f, 0}, 5, {225, 215, 195, 255}, 1.4f, 0.28f, 0.09f, true);
        if (rain > 0.3f && p.onGround && RandF() < dt * 9.0f) Emit({p.x, 0.05f, 0}, 1, {170, 195, 225, 255}, 1.6f, 0.25f, 0.06f, true);   // splashes

        // --- world scrolls toward the camera (repeating track)
        for (Segment& s : segs) {
            s.z += dz;
            if (s.z - SEG_LEN / 2 > 9.0f) { s.z -= SEG_COUNT * SEG_LEN; NewSegment(s); }
        }

        // --- which street are we on? announce changes
        int nt = TypeAtZ(0.0f);
        if (nt != curType) {
            curType = nt; toast = 3.2f; toastType = nt; curveRemain = 0;
            if (nt == ST_CULDESAC) culSign = GetRandomValue(0, 1) ? 1 : -1;
        }
        if (toast > 0) toast -= dt;

        // --- road bend director: pick a new target bend every few hundred metres, ease towards it
        const Theme& th = THEMES[curType];
        curveRemain -= dz;
        if (curveRemain <= 0) {
            float sign = GetRandomValue(0, 1) ? 1.0f : -1.0f;
            if (curType == ST_CULDESAC)                    curveTarget = culSign * th.curveMax;          // one long loop
            else if (RandF() < 0.28f)                      curveTarget = 0.0f;                           // a straight stretch
            else                                           curveTarget = sign * th.curveMax * (0.45f + 0.55f * RandF());
            curveRemain = th.curveLen * (0.7f + 0.6f * RandF());
        }
        curve += (curveTarget - curve) * std::min(1.0f, dt * 0.9f);
        for (Obstacle& o : obs) o.z += dz;
        for (Coin& c : coins) c.z += dz;
        obs.erase(std::remove_if(obs.begin(), obs.end(), [](const Obstacle& o) { return o.z > 10.0f; }), obs.end());
        coins.erase(std::remove_if(coins.begin(), coins.end(), [](const Coin& c) { return c.z > 10.0f || c.taken; }), coins.end());

        // --- particles: scroll with world, apply simple physics, fade out
        for (auto it = particles.begin(); it != particles.end();) {
            it->pos.z += dz;
            it->vel.y -= 9.0f * dt;
            it->pos.x += it->vel.x * dt; it->pos.y += it->vel.y * dt; it->pos.z += it->vel.z * dt;
            it->life -= dt;
            if (it->life <= 0 || it->pos.y < -1.0f) it = particles.erase(it); else ++it;
        }

        // --- spawning (gap grows with speed so reaction time stays fair)
        spawnAcc += dz;
        if (spawnAcc >= spawnGap) {
            spawnAcc -= spawnGap;
            Spawn();
            spawnGap = std::max(14.0f, speed * (0.75f + RandF() * 0.45f) * THEMES[TypeAtZ(SPAWN_Z)].gapMul);
        }

        // --- collisions
        for (Obstacle& o : obs) {
            if (stumbleT > 0) break;                                                      // brief grace period after a stumble
            float hw = 0.95f, hd, y0, y1;
            switch (o.type) {
                case ObType::Low:   hd = 0.4f; y0 = 0.0f; y1 = 1.0f; break;
                case ObType::High:  hd = 0.3f; y0 = 1.0f; y1 = 2.6f; break;
                default:            hd = 0.5f; y0 = 0.0f; y1 = 2.6f; break;
            }
            if (fabsf(LaneX(o.lane) - p.x) < hw + HALF_W && fabsf(o.z) < hd + HALF_D &&
                p.y < y1 && p.y + h > y0) { OnHit(o); if (state == State::GameOver) return; break; }
        }
        for (Coin& c : coins) {
            if (!c.taken && fabsf(LaneX(c.lane) - p.x) < 0.8f && fabsf(c.z) < 0.7f &&
                p.y < 1.25f && p.y + h > 0.55f) {
                c.taken = true; coinCount++;
                Emit({LaneX(c.lane), 0.9f, c.z}, 10, GOLD, 2.0f, 0.4f, 0.09f, true);
            }
        }
        UpdateCop(dt);
    }
};

// ----------------------------------------------------------------- drawing
void DrawTextC(const char* t, float cx, float y, float size, Color c) {
    int w = MeasureText(t, (int)size);
    DrawText(t, (int)(cx - w / 2.0f) + 2, (int)y + 2, (int)size, Fade(BLACK, 0.6f * c.a / 255.0f));
    DrawText(t, (int)(cx - w / 2.0f),     (int)y,     (int)size, c);
}
void DrawTextL(const char* t, float x, float y, float size, Color c) {
    DrawText(t, (int)x + 2, (int)y + 2, (int)size, Fade(BLACK, 0.6f * c.a / 255.0f));
    DrawText(t, (int)x, (int)y, (int)size, c);
}

float Frac(float x) { return x - floorf(x); }

// Distant sun / moon, hills and drifting clouds. Positioned relative to the camera each frame so they always
// read as "infinitely far away". The sun sinks and turns orange at dusk, the moon rises at night, and the
// cloud layer thickens and darkens with the weather.
void DrawSky(Vector3 camPos, float time, float night, float dusk, float cloud, float rain) {
    float sunX = camPos.x * 0.3f, sunZ = camPos.z - 230.0f;
    float overcast = ClampF(cloud * 0.9f + rain * 0.4f, 0.0f, 1.0f);
    Color skyish = {210, 214, 222, 255};
    float sunVis = (1.0f - ClampF((night - 0.25f) / 0.5f, 0.0f, 1.0f)) * (1.0f - 0.9f * overcast);
    float sunY = 60.0f - 48.0f * ClampF(night * 1.7f, 0.0f, 1.0f);
    if (sunVis > 0.04f) {
        Color sc = MixC({255, 250, 220, 255}, {255, 140, 70, 255}, ClampF(dusk * 1.5f, 0.0f, 1.0f));
        DrawSphere({sunX, sunY, sunZ}, 8.5f, Glow(MixC(skyish, sc, sunVis)));
        DrawSphere({sunX, sunY, sunZ}, 16.0f, Fade(sc, 0.16f * sunVis));
    }
    float moonVis = ClampF((night - 0.45f) / 0.4f, 0.0f, 1.0f) * (1.0f - 0.92f * overcast);
    if (moonVis > 0.04f) {
        float moonY = 14.0f + 44.0f * ClampF((night - 0.4f) / 0.6f, 0.0f, 1.0f);
        DrawSphere({sunX + 46.0f, moonY, sunZ}, 6.0f, Glow(MixC({40, 50, 80, 255}, {236, 240, 252, 255}, moonVis)));
        DrawSphere({sunX + 46.0f, moonY, sunZ}, 11.0f, Fade({190, 210, 255, 255}, 0.12f * moonVis));
    }

    Color hillFar  = FogC({120, 150, 120, 255}, 220.0f);
    Color hillNear = FogC({95, 135, 105, 255}, 170.0f);
    float hz = camPos.z - 190.0f, hx = camPos.x;
    DrawCube({hx - 75, 8,  hz - 15}, 65, 20, 45, hillFar);
    DrawCube({hx - 25, 12, hz},      55, 28, 45, hillNear);
    DrawCube({hx + 35, 10, hz - 10}, 60, 24, 45, hillFar);
    DrawCube({hx + 85, 7,  hz + 5},  55, 18, 45, hillFar);

    Color cc = MixC(MixC({255, 255, 255, 255}, {150, 156, 168, 255}, ClampF(cloud * 0.85f, 0.0f, 1.0f)), {104, 110, 122, 255}, ClampF(rain * 0.8f, 0.0f, 1.0f));
    int count = 5 + (int)(19.0f * cloud);
    for (int i = 0; i < count; i++) {
        float hx2 = Frac(i * 0.6180339f + 0.31f), hy = Frac(i * 0.7548776f + 0.17f), hz2 = Frac(i * 0.5698403f + 0.53f);
        float drift = sinf(time * 0.04f + i * 1.7f) * 8.0f;
        float cx = camPos.x + drift + (hx2 - 0.5f) * 270.0f;
        float cy = 26.0f + hy * (26.0f + 34.0f * cloud);
        float cz = camPos.z - 100.0f - hz2 * 90.0f;
        float sc = 5.0f + hy * 4.0f + 7.0f * cloud;
        DrawSphere({cx, cy, cz}, sc, Fade(cc, 0.85f));
        DrawSphere({cx + sc * 0.8f, cy - sc * 0.2f, cz}, sc * 0.7f, Fade(cc, 0.85f));
        DrawSphere({cx - sc * 0.7f, cy - sc * 0.15f, cz}, sc * 0.65f, Fade(cc, 0.85f));
    }
}

void DrawStars(float amount, int W, int H, float t) {            // 2D, behind the 3D scene (hills hide them)
    if (amount < 0.02f) return;
    for (int i = 0; i < 120; i++) {
        float x = Frac(i * 0.6180339f + 0.1f) * W, y = Frac(i * 0.7548776f + 0.4f * Frac(i * 0.31f)) * H * 0.46f;
        float tw = 0.55f + 0.45f * sinf(t * 2.0f + i * 1.9f);
        int sz = (i % 7 == 0) ? 3 : 2;
        DrawRectangle((int)x, (int)y, sz, sz, Fade(WHITE, amount * tw));
    }
}

void DrawRain(float rain, int W, int H, float t, float s) {      // screen-space rain streaks (two depths) + a grey veil
    if (rain < 0.02f) return;
    DrawRectangle(0, 0, W, H, Fade({58, 68, 84, 255}, 0.10f * rain));
    int n = (int)(rain * 320.0f);
    for (int i = 0; i < n; i++) {
        float h1 = Frac(i * 0.6180339f + 0.13f), h2 = Frac(i * 0.7548776f + 0.71f);
        bool nearDrop = (i % 3) == 0;
        float speed = (nearDrop ? 2300.0f : 1350.0f) * s, len = (nearDrop ? 42.0f : 18.0f) * s;
        float y = fmodf(h2 * (H + len) + t * speed, (float)H + len) - len;
        float x = h1 * (W + 120.0f) - 60.0f - y * 0.12f;
        DrawLineEx({x, y}, {x - 0.12f * len, y + len}, nearDrop ? 2.0f : 1.0f,
                   Fade({210, 224, 244, 255}, (nearDrop ? 0.38f : 0.26f) * ClampF(rain, 0.0f, 1.0f)));
    }
}

Vector3 ColV(Color c) { return {(float)c.r, (float)c.g, (float)c.b}; }
// Sky colours for the current street, time of day and weather.
void SkyTarget(const Theme& th, float night, float dusk, float cloud, float rain, Vector3& top, Vector3& bot) {
    float n = night * night * (3.0f - 2.0f * night);
    top = Vector3Lerp(ColV(th.skyTop), {8, 12, 40}, n);
    bot = Vector3Lerp(ColV(th.skyBot), {26, 34, 78}, n);
    top = Vector3Lerp(top, {70, 70, 140}, dusk * 0.55f);                 // dusk / dawn glow
    bot = Vector3Lerp(bot, {255, 150, 98}, dusk * 0.75f);
    float c = ClampF(cloud * 0.85f + rain * 0.15f, 0.0f, 1.0f), dark = 1.0f - 0.22f * cloud - 0.22f * rain;
    auto grey = [&](Vector3 v) { float l = 0.3f * v.x + 0.59f * v.y + 0.11f * v.z; return Vector3{l * 0.97f, l, l * 1.04f}; };
    top = Vector3Lerp(top, Vector3Scale(grey(top), dark), c * 0.85f);   // overcast: desaturate + darken
    bot = Vector3Lerp(bot, Vector3Scale(grey(bot), dark), c * 0.85f);
}
// Colour multiplier applied to the whole 3D scene by the scene shader.
void ComputeTint(float night, float dusk, float cloud, float rain, float flash, Vector3& tint, float& grey) {
    float n = night * night * (3.0f - 2.0f * night);
    tint = Vector3Lerp({1, 1, 1}, {0.47f, 0.55f, 0.83f}, n);             // moonlit blue (kept bright enough to play in)
    Vector3 warm = Vector3Lerp({1, 1, 1}, {1.0f, 0.78f, 0.62f}, dusk * 0.8f);
    tint = {tint.x * warm.x, tint.y * warm.y, tint.z * warm.z};
    tint = Vector3Scale(tint, (1.0f - 0.16f * cloud - 0.14f * rain) * (1.0f + 0.45f * flash));
    grey = ClampF(0.30f * cloud + 0.22f * rain, 0.0f, 0.5f);
}

// Scene shader: multiplies colour by uTint, desaturates by uGray; surfaces whose vertex alpha is ~0.97 are emissive.
#if defined(PLATFORM_ANDROID) || defined(PLATFORM_WEB)
const char* SCENE_VS = "#version 100\nprecision mediump float;\nattribute vec3 vertexPosition;attribute vec2 vertexTexCoord;attribute vec4 vertexColor;\n"
    "uniform mat4 mvp;varying vec2 fragTexCoord;varying vec4 fragColor;\n"
    "void main(){fragTexCoord=vertexTexCoord;fragColor=vertexColor;gl_Position=mvp*vec4(vertexPosition,1.0);}\n";
const char* SCENE_FS = "#version 100\nprecision mediump float;\nvarying vec2 fragTexCoord;varying vec4 fragColor;\n"
    "uniform sampler2D texture0;uniform vec4 colDiffuse;uniform vec3 uTint;uniform float uGray;\n"
    "void main(){vec4 c=texture2D(texture0,fragTexCoord)*colDiffuse*fragColor;\n"
    "float em=step(0.95,fragColor.a)*(1.0-step(0.99,fragColor.a));float l=dot(c.rgb,vec3(0.299,0.587,0.114));\n"
    "vec3 rgb=mix(c.rgb,vec3(l),uGray);rgb*=mix(uTint,vec3(1.0),em);gl_FragColor=vec4(rgb,mix(c.a,1.0,em));}\n";
#else
const char* SCENE_VS = "#version 330\nin vec3 vertexPosition;in vec2 vertexTexCoord;in vec4 vertexColor;\n"
    "uniform mat4 mvp;out vec2 fragTexCoord;out vec4 fragColor;\n"
    "void main(){fragTexCoord=vertexTexCoord;fragColor=vertexColor;gl_Position=mvp*vec4(vertexPosition,1.0);}\n";
const char* SCENE_FS = "#version 330\nin vec2 fragTexCoord;in vec4 fragColor;\n"
    "uniform sampler2D texture0;uniform vec4 colDiffuse;uniform vec3 uTint;uniform float uGray;out vec4 finalColor;\n"
    "void main(){vec4 c=texture(texture0,fragTexCoord)*colDiffuse*fragColor;\n"
    "float em=step(0.95,fragColor.a)*(1.0-step(0.99,fragColor.a));float l=dot(c.rgb,vec3(0.299,0.587,0.114));\n"
    "vec3 rgb=mix(c.rgb,vec3(l),uGray);rgb*=mix(uTint,vec3(1.0),em);finalColor=vec4(rgb,mix(c.a,1.0,em));}\n";
#endif

// ----------------------------------------------------------------- street drawing helpers
float gDist = 0.0f;                                   // distance of the prop being drawn (for fog)
Color F(Color c) { return FogC(c, gDist); }
Texture2D TexById(const Textures& t, int id) {
    switch (id) { case TX_DIRT: return t.dirt; case TX_PAVER: return t.paver; case TX_GRASS: return t.grass;
                  case TX_SIDEWALK: return t.sidewalk; case TX_BRICK: return t.brick;
                  case TX_JPATH: return t.jpath; case TX_JFLOOR: return t.jfloor; default: return t.road; }
}
Color Pick(const Color* pal, int n, float r) { int i = (int)(r * n); return pal[i >= n ? n - 1 : i]; }
#define PAL(arr, r) Pick(arr, (int)(sizeof(arr) / sizeof(arr[0])), (r))

// A flat, textured strip whose near and far edges can be shifted sideways -> follows the road bend.
void DrawStrip(Texture2D tex, float xc, float w, float y, float zFar, float zNear, float offFar, float offNear,
               float tileU, float vFar, float vNear, Color tint) {
    float xl = xc - w / 2, xr = xc + w / 2;
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlNormal3f(0, 1, 0);
    rlTexCoord2f(0, vFar);       rlVertex3f(xl + offFar,  y, zFar);
    rlTexCoord2f(0, vNear);      rlVertex3f(xl + offNear, y, zNear);
    rlTexCoord2f(tileU, vNear);  rlVertex3f(xr + offNear, y, zNear);
    rlTexCoord2f(tileU, vFar);   rlVertex3f(xr + offFar,  y, zFar);
    rlEnd();
    rlSetTexture(0);
}
void Face(Texture2D tex, Vector3 a, Vector3 b, Vector3 c, Vector3 d, float uMax, float vMax, Color col, bool flipV = false) {
    float v0 = flipV ? 0.0f : vMax, v1 = flipV ? vMax : 0.0f;
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlTexCoord2f(0, v0);    rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(uMax, v0); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(uMax, v1); rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(0, v1);    rlVertex3f(d.x, d.y, d.z);
    rlEnd();
    rlSetTexture(0);
}
// Box with tiled texture on its four sides (tw x th metres per tile) and a plain roof.
void DrawBoxTex(Texture2D tex, Vector3 c, Vector3 s, float tw, float th, Color tint, Color roof) {
    float x0 = c.x - s.x / 2, x1 = c.x + s.x / 2, y0 = c.y - s.y / 2, y1 = c.y + s.y / 2, z0 = c.z - s.z / 2, z1 = c.z + s.z / 2;
    float ux = s.x / tw, uz = s.z / tw, vy = s.y / th;
    Face(tex, {x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, ux, vy, tint);
    Face(tex, {x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, ux, vy, ShadeC(tint, 0.7f));
    Face(tex, {x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, uz, vy, ShadeC(tint, 0.85f));
    Face(tex, {x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, uz, vy, ShadeC(tint, 0.85f));
    Texture2D none{};
    Face(none, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}, 1, 1, roof);
}
// Gable roof: ridge runs along local X.
void DrawGable(float w, float d, float y0, float rh, Color roof, Color gable) {
    rlSetTexture(0);
    rlBegin(RL_QUADS);
    rlColor4ub(roof.r, roof.g, roof.b, 255);
    rlVertex3f(-w/2, y0, d/2);  rlVertex3f(w/2, y0, d/2);  rlVertex3f(w/2, y0 + rh, 0);  rlVertex3f(-w/2, y0 + rh, 0);
    Color back = ShadeC(roof, 0.75f); rlColor4ub(back.r, back.g, back.b, 255);
    rlVertex3f(w/2, y0, -d/2);  rlVertex3f(-w/2, y0, -d/2);  rlVertex3f(-w/2, y0 + rh, 0);  rlVertex3f(w/2, y0 + rh, 0);
    rlColor4ub(gable.r, gable.g, gable.b, 255);
    rlVertex3f(-w/2 + 0.25f, y0, -d/2 + 0.35f);  rlVertex3f(-w/2 + 0.25f, y0, d/2 - 0.35f);  rlVertex3f(-w/2 + 0.25f, y0 + rh * 0.85f, 0);  rlVertex3f(-w/2 + 0.25f, y0 + rh * 0.85f, 0);
    rlVertex3f(w/2 - 0.25f, y0, d/2 - 0.35f);  rlVertex3f(w/2 - 0.25f, y0, -d/2 + 0.35f);  rlVertex3f(w/2 - 0.25f, y0 + rh * 0.85f, 0);  rlVertex3f(w/2 - 0.25f, y0 + rh * 0.85f, 0);
    rlEnd();
}

struct Frame { float x, z, yawDeg; };
Frame FrameAt(float curve, float zc, float side, float lateral) {
    float th = BendAng(curve, zc);
    return { BendOff(curve, zc) + side * lateral * cosf(th), zc - side * lateral * sinf(th), th * RAD2DEG };
}
void PushFrame(const Frame& f, float gy, float faceYaw) {
    rlPushMatrix();
    rlTranslatef(f.x, gy, f.z);
    rlRotatef(f.yawDeg, 0, 1, 0);
    if (faceYaw != 0.0f) rlRotatef(faceYaw, 0, 1, 0);
}

// ----------------------------------------------------------------- props (local space: origin on the ground,
// local +Z faces the road, local X runs along the road)
void DrawTreeL(float scale, Color foliage) {
    Color trunk = {92, 64, 40, 255};
    DrawCylinder({0, 0.02f, 0}, 0.95f * scale, 0.95f * scale, 0.03f, 10, Fade(BLACK, 0.25f));
    DrawCylinder({0, 0, 0}, 0.16f * scale, 0.22f * scale, 1.7f * scale, 8, F(trunk));
    float ty = 1.7f * scale;
    DrawSphere({0, ty + 0.5f * scale, 0}, 1.05f * scale, foliage);
    DrawSphere({-0.5f * scale, ty + 0.85f * scale, -0.25f * scale}, 0.65f * scale, ShadeC(foliage, 1.25f));
    DrawSphere({0.4f * scale, ty + 0.25f * scale, 0.4f * scale}, 0.55f * scale, ShadeC(foliage, 0.8f));
}
void DrawPalm(float h) {
    DrawCylinder({0, 0, 0}, 0.14f, 0.22f, h, 8, F({150, 112, 74, 255}));
    for (int i = 0; i < 7; i++) {
        rlPushMatrix(); rlTranslatef(0, h, 0); rlRotatef(i * 51.4f, 0, 1, 0); rlRotatef(-28.0f, 0, 0, 1);
        DrawCube({1.15f, 0, 0}, 2.3f, 0.05f, 0.55f, F({52, 128, 62, 255}));
        rlPopMatrix();
    }
}
void DrawHouse(float w, float d, float h, Color wall, Color roof) {
    DrawCube({0, h / 2, 0}, w, h, d, F(wall));
    DrawGable(w + 0.6f, d + 0.8f, h, 1.5f, F(roof), F(wall));
    DrawCube({w * 0.3f, h + 1.3f, -d * 0.2f}, 0.6f, 1.6f, 0.6f, F({150, 90, 70, 255}));       // chimney
    DrawCube({w * 0.18f, 1.0f, d / 2 + 0.04f}, 1.0f, 2.0f, 0.08f, F({92, 56, 40, 255}));       // door
    DrawCube({w * 0.18f, 2.12f, d / 2 + 0.05f}, 1.3f, 0.12f, 0.1f, F({245, 245, 240, 255}));
    for (int k = 0; k < 2; k++) {
        float wx = k == 0 ? -w * 0.25f : w * 0.40f;
        DrawCube({wx, 1.7f, d / 2 + 0.04f}, 1.35f, 1.15f, 0.08f, F({245, 245, 240, 255}));
        DrawCube({wx, 1.7f, d / 2 + 0.08f}, 1.1f, 0.9f, 0.08f, gNight > 0.35f ? Glow(Color{255, 212, 128, 255}) : F(Color{120, 165, 200, 255}));
    }
}
void DrawPicketFence(float len, Color c) {
    for (float x = -len / 2 + 0.4f; x < len / 2; x += 0.75f) DrawCube({x, 0.45f, 0}, 0.14f, 0.9f, 0.05f, c);
    DrawCube({0, 0.3f, 0}, len, 0.08f, 0.07f, c);
    DrawCube({0, 0.7f, 0}, len, 0.08f, 0.07f, c);
}
void DrawRailFence(float len) {
    Color wood = F({128, 92, 58, 255});
    for (float x = -len / 2 + 0.2f; x <= len / 2; x += 2.5f) DrawCube({x, 0.6f, 0}, 0.16f, 1.2f, 0.16f, wood);
    DrawCube({0, 0.45f, 0}, len, 0.1f, 0.07f, wood);
    DrawCube({0, 0.95f, 0}, len, 0.1f, 0.07f, wood);
}
void DrawLamp(float h, bool ornate, Color glow) {
    Color pole = F(ornate ? Color{40, 55, 50, 255} : Color{82, 84, 90, 255});
    DrawCylinder({0, 0, 0}, 0.07f, 0.1f, h, 8, pole);
    DrawCube({0, h, 0.6f}, 0.1f, 0.1f, 1.3f, pole);
    Color head = gNight > 0.15f ? Glow(glow) : glow;                                  // lamps light up at night
    DrawCube({0, h - 0.1f, 1.2f}, 0.38f, 0.12f, 0.55f, head);
    if (ornate) DrawSphere({0, h + 0.15f, 0}, 0.2f, head);
    if (gNight > 0.2f) {                                                               // soft additive halo (small for near lamps)
        float k = ClampF(gDist / 30.0f, 0.2f, 1.0f), g2 = gNight;
        BeginBlendMode(BLEND_ADDITIVE);
        DrawSphere({0, h - 0.2f, 1.2f}, 0.45f * k + 0.1f, Color{(unsigned char)(70 * g2), (unsigned char)(60 * g2), (unsigned char)(32 * g2), 247});
        DrawSphere({0, h - 0.2f, 1.2f}, 1.0f * k + 0.2f, Color{(unsigned char)(22 * g2), (unsigned char)(19 * g2), (unsigned char)(10 * g2), 247});
        DrawSphere({0, h - 0.2f, 1.2f}, 1.8f * k + 0.3f, Color{(unsigned char)(9 * g2), (unsigned char)(8 * g2), (unsigned char)(4 * g2), 247});
        EndBlendMode();
    }
}
void DrawBench() {
    Color wood = F({140, 96, 60, 255}), iron = F({50, 52, 58, 255});
    DrawCube({0, 0.5f, 0}, 1.7f, 0.09f, 0.5f, wood);
    DrawCube({0, 0.85f, -0.22f}, 1.7f, 0.45f, 0.07f, wood);
    DrawCube({-0.75f, 0.25f, 0}, 0.08f, 0.5f, 0.45f, iron);
    DrawCube({0.75f, 0.25f, 0}, 0.08f, 0.5f, 0.45f, iron);
}
void DrawUmbrellaTable(Color canopy) {
    DrawCylinder({0, 0, 0}, 0.04f, 0.04f, 2.4f, 6, F({220, 220, 220, 255}));
    DrawCylinder({0, 2.4f, 0}, 0.0f, 1.3f, 0.5f, 10, F(canopy));
    DrawCylinder({0, 0.75f, 0}, 0.55f, 0.55f, 0.06f, 12, F({240, 240, 235, 255}));
    DrawCylinder({0, 0, 0}, 0.06f, 0.06f, 0.75f, 6, F({120, 120, 125, 255}));
}
void DrawPlanter(Color leaf) {
    DrawCube({0, 0.4f, 0}, 1.4f, 0.8f, 1.0f, F({150, 140, 128, 255}));
    DrawSphere({0, 1.0f, 0}, 0.62f, F(leaf));
    DrawSphere({0.35f, 1.15f, 0.1f}, 0.42f, F(ShadeC(leaf, 1.2f)));
}
void DrawDumpster() {
    DrawCube({0, 0.65f, 0}, 1.9f, 1.1f, 1.0f, F({46, 100, 66, 255}));
    DrawCube({0, 1.25f, 0.05f}, 2.0f, 0.1f, 1.1f, F({34, 70, 48, 255}));
    DrawCube({0, 0.3f, 0.52f}, 1.9f, 0.08f, 0.06f, F({30, 40, 34, 255}));
}
void DrawBillboard3D(Color board) {
    DrawCube({-2.4f, 3.0f, 0}, 0.3f, 6.0f, 0.3f, F({90, 92, 98, 255}));
    DrawCube({2.4f, 3.0f, 0}, 0.3f, 6.0f, 0.3f, F({90, 92, 98, 255}));
    DrawCube({0, 5.6f, 0.1f}, 7.0f, 3.2f, 0.25f, F({230, 230, 230, 255}));
    DrawCube({0, 5.6f, 0.25f}, 6.5f, 2.7f, 0.1f, F(board));
    DrawCube({-1.2f, 5.9f, 0.32f}, 3.2f, 0.7f, 0.05f, F({255, 255, 255, 255}));
    DrawCube({1.0f, 5.0f, 0.32f}, 4.0f, 0.4f, 0.05f, F({255, 220, 80, 255}));
}
void DrawUtilityPole() {
    Color wood = F({96, 72, 50, 255});
    DrawCylinder({0, 0, 0}, 0.12f, 0.16f, 7.5f, 8, wood);
    DrawCube({0, 7.0f, 0}, 0.12f, 0.12f, 2.6f, wood);
    DrawCube({0, 6.4f, 0}, 0.1f, 0.1f, 2.0f, wood);
}
void DrawHayBale(float r) {
    rlPushMatrix(); rlTranslatef(0.7f, r, 0); rlRotatef(90, 0, 0, 1);
    DrawCylinder({0, 0, 0}, r, r, 1.4f, 14, F({218, 188, 90, 255}));
    DrawCylinder({0, 0.3f, 0}, r * 1.02f, r * 1.02f, 0.08f, 14, F({160, 130, 60, 255}));
    DrawCylinder({0, 1.0f, 0}, r * 1.02f, r * 1.02f, 0.08f, 14, F({160, 130, 60, 255}));
    rlPopMatrix();
}

// ----------------------------------------------------------------- jungle props
void DrawFern(float s, Color c, float tilt) {                       // a rosette of arching leaves
    for (int i = 0; i < 9; i++) {
        rlPushMatrix(); rlRotatef(i * 40.0f + 7.0f, 0, 1, 0); rlRotatef(tilt, 0, 0, 1);
        DrawCube({0.6f * s, 0, 0}, 1.2f * s, 0.03f, 0.3f * s, F(i % 2 ? c : ShadeC(c, 0.82f)));
        rlPopMatrix();
    }
}
void DrawBush(float r0, float r1) {                                 // dark leafy bush, sometimes in flower
    Color leaf = {34, 96, 46, 255};
    DrawSphere({0, 0.55f, 0}, 0.8f, F(leaf));
    DrawSphere({0.6f, 0.4f, 0.25f}, 0.55f, F(ShadeC(leaf, 1.2f)));
    DrawSphere({-0.55f, 0.45f, -0.2f}, 0.6f, F(ShadeC(leaf, 0.85f)));
    if (r0 > 0.45f) {
        Color fl[3] = {{235, 70, 90, 255}, {250, 200, 60, 255}, {190, 90, 220, 255}};
        Color c = fl[(int)(r1 * 3) % 3];
        for (int k = 0; k < 6; k++) DrawSphere({-0.5f + k * 0.2f, 0.9f + 0.12f * (k % 3), 0.45f - 0.1f * (k % 2)}, 0.09f, F(c));
    }
}
void DrawBoulder(float sz, Color c) {                               // mossy rock
    rlPushMatrix(); rlScalef(1.0f, 0.75f, 0.9f);
    DrawSphere({0, 0.8f * sz, 0}, sz, F(c));
    DrawSphere({0.55f * sz, 0.45f * sz, 0.45f * sz}, 0.6f * sz, F(ShadeC(c, 0.88f)));
    rlPopMatrix();
    DrawSphere({0, 1.25f * sz, 0}, 0.55f * sz, F({62, 118, 54, 255}));                      // moss on top
}
void DrawJungleTree(float h, float r, bool limb, float rnd) {       // giant trunk, buttress roots, canopy, a limb over the trail
    Color bark = F({84, 62, 44, 255}), dark = {26, 84, 42, 255};
    DrawCylinder({0, 0, 0}, r * 0.78f, r, h, 10, bark);
    DrawCylinder({0, 0, 0}, r * 1.12f, r * 1.3f, 1.1f, 10, F({72, 54, 38, 255}));
    for (int k = 0; k < 4; k++) {
        rlPushMatrix(); rlRotatef(k * 90.0f + rnd * 40.0f, 0, 1, 0); rlTranslatef(r + 0.3f, 0.7f, 0); rlRotatef(24.0f, 0, 0, 1);
        DrawCube({0, 0, 0}, 0.6f, 1.9f, 0.2f, bark); rlPopMatrix();
    }
    DrawSphere({0, h, 0}, 3.4f, F(dark));
    DrawSphere({2.2f, h - 0.9f, 1.2f}, 2.5f, F(ShadeC(dark, 1.25f)));
    DrawSphere({-2.3f, h - 1.1f, -0.8f}, 2.7f, F(ShadeC(dark, 0.85f)));
    if (limb) {                                                      // low branch reaching toward the trail, with leaves and vines
        rlPushMatrix(); rlTranslatef(0, 7.5f + rnd * 2.0f, 0); rlRotatef(90, 1, 0, 0);
        DrawCylinder({0, 0, 0}, 0.2f, 0.3f, 6.0f, 8, bark);
        rlPopMatrix();
        DrawSphere({0, 7.5f + rnd * 2.0f, 6.0f}, 1.7f, F(ShadeC(dark, 1.15f)));
        DrawSphere({0.9f, 7.2f + rnd * 2.0f, 4.6f}, 1.3f, F(dark));
        Color vine = F({52, 118, 50, 255});
        for (int v = 0; v < 3; v++) DrawCylinder({-0.7f + v * 0.7f, 3.8f + rnd * 2.0f - v * 0.3f, 5.6f + 0.2f * v}, 0.03f, 0.03f, 4.0f + v * 0.6f, 5, vine);
    }
}
void DrawRuinPillars(float rnd) {                                   // a broken stone gateway beside the trail
    Color stone = F({126, 128, 118, 255}), moss = F({64, 120, 56, 255});
    for (int k = -1; k <= 1; k += 2) {
        DrawCylinder({k * 1.6f, 0, 0}, 0.5f, 0.56f, 3.4f - (k > 0 ? rnd * 1.2f : 0.0f), 10, stone);
        DrawCube({k * 1.6f, 0.05f, 0}, 1.3f, 0.25f, 1.3f, ShadeC(stone, 0.85f));
        DrawSphere({k * 1.6f, 1.2f, 0.42f}, 0.34f, moss);
    }
    DrawCube({-0.4f, 3.55f, 0}, 2.9f, 0.45f, 0.9f, stone);                                    // fallen lintel
    DrawSphere({-0.2f, 3.8f, 0}, 0.4f, moss);
}

// ----------------------------------------------------------------- scenery per street type
const Color HOUSE_WALLS[] = {{238,226,196,255},{190,214,232,255},{198,222,190,255},{236,190,170,255},{245,245,240,255},{226,206,150,255}};
const Color ROOFS[]       = {{140,60,50,255},{84,84,96,255},{112,82,62,255},{62,92,112,255}};
const Color SHOP_TINTS[]  = {{255,235,220,255},{225,240,255,255},{235,255,225,255},{255,225,235,255},{255,250,210,255}};
const Color GLASS_TINTS[] = {{255,255,255,255},{200,225,255,255},{230,235,215,255},{255,225,200,255}};
const Color AWNINGS[]     = {{200,50,50,255},{40,110,170,255},{40,140,90,255},{225,150,40,255},{130,60,150,255}};
const Color BOARDS[]      = {{40,100,190,255},{190,60,50,255},{40,150,90,255},{230,160,40,255}};
const Color TRUCKS[]      = {{240,240,240,255},{60,110,190,255},{230,190,50,255},{70,150,100,255},{200,70,60,255}};
const Color FOLIAGE_BASE[]= {{70,130,70,255},{86,150,72,255},{60,120,84,255},{110,150,60,255}};

// Shop row used by Commercial / Main / Pedestrian streets: shopfront ground floor + upper floors.
void DrawShopBlock(const Textures& tex, float r0, float r1, float r2, float r3, bool modernUpper, float depth) {
    DrawBoxTex(tex.shopfront, {0, 1.8f, 0}, {9.8f, 3.6f, depth}, 4.9f, 3.6f, F(Pick(SHOP_TINTS, 5, r0)), F({190, 186, 176, 255}));
    for (int k = 0; k < 2; k++)                                             // awnings over the two shops
        DrawCube({k == 0 ? -2.45f : 2.45f, 3.0f, depth / 2 + 0.6f}, 4.1f, 0.12f, 1.3f, F(Pick(AWNINGS, 5, k == 0 ? r1 : r3)));
    float uh = modernUpper ? 2.45f * (2 + (int)(r2 * 4)) : 4.9f * (r2 > 0.5f ? 2.0f : 1.0f);
    Color up = F(modernUpper ? Pick(GLASS_TINTS, 4, r1) : Color{235, 205, 190, 255});
    DrawBoxTex(modernUpper ? tex.facade : tex.brickwin, {0, 3.6f + uh / 2, 0}, {9.8f, uh, depth - 0.5f},
               modernUpper ? 9.8f : 4.9f, modernUpper ? 9.8f : 4.9f, up, F({150, 148, 142, 255}));
    DrawCube({0, 3.62f, 0}, 10.0f, 0.22f, depth + 0.1f, F({205, 200, 190, 255}));                      // ledge
    DrawCube({0, 3.6f + uh + 0.15f, 0}, 10.0f, 0.3f, depth - 0.1f, F({200, 196, 186, 255}));          // cornice
}

void DrawScenery(const Segment& s, float curve, Vector3 camPos, const Textures& tex) {
    const float roadHalf = LANE_W * 1.5f + 0.5f;
    const float gy0 = s.type == ST_PEDESTRIAN ? -0.02f : -0.5f;
    const Color glow = {255, 244, 190, 255};
    for (int i = 0; i < 2; i++) {
        float zc = s.z + (i == 0 ? 5.0f : -5.0f);
        gDist = fabsf(zc - camPos.z);
        for (int sd = 0; sd < 2; sd++) {
            float side = sd == 0 ? -1.0f : 1.0f;
            const float* r = &s.r[sd * 8 + i * 4];
            float face = -90.0f * side;                              // local +Z looks back at the road
            auto at = [&](float lateral, float gy) { PushFrame(FrameAt(curve, zc, side, lateral), gy, face); };
            Color leaf = F(Pick(FOLIAGE_BASE, 4, r[0]));
            switch (s.type) {

            case ST_RESIDENTIAL: case ST_CULDESAC: {
                bool cul = s.type == ST_CULDESAC;
                float w = 6.4f + r[2] * 2.4f, d = 5.4f + r[3] * 1.6f, h = 3.0f + r[1] * 1.0f, front = cul ? 8.0f : 9.4f;
                at(front + d / 2, gy0);
                DrawHouse(w, d, h, Pick(HOUSE_WALLS, 6, r[0]), Pick(ROOFS, 4, r[1]));
                if (cul) DrawCube({w * 0.18f, 0.55f, d / 2 + 1.7f}, 2.2f, 0.05f, 3.4f, F({150, 150, 148, 255}));    // driveway
                rlPopMatrix();
                if (!cul) { at(7.0f, gy0); DrawPicketFence(9.6f, F({245, 245, 240, 255})); rlPopMatrix(); }
                at(cul ? 6.5f : 8.2f, gy0); rlTranslatef((r[2] - 0.5f) * 6.0f, 0, 0); DrawTreeL(0.75f + r[3] * 0.45f, leaf); rlPopMatrix();
                if (i == 0) { at(roadHalf + 1.5f, 0.1f); DrawLamp(5.0f, false, glow); rlPopMatrix(); }
                if (cul && i == 1) {                                                                              // mailbox
                    at(roadHalf + 1.7f, 0.1f); rlTranslatef(1.5f, 0, 0);
                    DrawCube({0, 0.55f, 0}, 0.12f, 1.1f, 0.12f, F({90, 70, 50, 255})); DrawCube({0, 1.2f, 0}, 0.3f, 0.3f, 0.55f, F({50, 90, 170, 255}));
                    rlPopMatrix();
                }
                break; }

            case ST_COMMERCIAL: {
                at(roadHalf + 3.2f + 4.2f, gy0); DrawShopBlock(tex, r[0], r[1], r[2], r[3], true, 8.0f); rlPopMatrix();
                if (i == 0) { at(roadHalf + 0.9f, 0.1f); DrawLamp(5.2f, false, glow); rlPopMatrix(); }
                break; }

            case ST_MAIN: {
                at(roadHalf + 3.0f + 4.2f, gy0); DrawShopBlock(tex, r[0], r[1], r[2], r[3], false, 8.0f); rlPopMatrix();
                if (i == 0) {                                                                                     // flag pole
                    at(roadHalf + 0.7f, 0.1f);
                    DrawCylinder({0, 0, 0}, 0.05f, 0.07f, 4.6f, 6, F({140, 140, 146, 255}));
                    DrawCube({0, 4.1f, 0.65f}, 0.04f, 0.9f, 1.3f, F(Pick(AWNINGS, 5, r[1])));
                    rlPopMatrix();
                } else {
                    at(roadHalf + 0.9f, 0.1f); DrawLamp(5.2f, true, glow); rlPopMatrix();
                    if (r[0] < 0.6f) { at(roadHalf + 2.0f, 0.1f); rlTranslatef((r[1] - 0.5f) * 5.0f, 0, 0); DrawBench(); rlPopMatrix(); }
                }
                break; }

            case ST_ALLEY: {
                float h = r[0] > 0.5f ? 12.0f : 6.0f;
                Color dark = F({55, 58, 64, 255});
                at(5.0f + 1.6f, gy0);
                DrawBoxTex(tex.brickwin, {0, h / 2, 0}, {10.2f, h, 3.2f}, 5.1f, 6.0f, F({150, 132, 126, 255}), F({70, 70, 76, 255}));
                if (r[1] > 0.55f) DrawCube({-2.8f, 1.2f, 1.62f}, 1.3f, 2.4f, 0.08f, dark);                        // door
                DrawCylinder({3.6f, 0, 1.68f}, 0.1f, 0.1f, h, 6, F({92, 96, 104, 255}));                           // drain pipe
                if (r[3] > 0.55f) {                                                                               // fire escape
                    DrawCube({0, 4.0f, 2.2f}, 3.4f, 0.1f, 1.2f, dark);
                    DrawCube({0, 4.55f, 2.8f}, 3.4f, 0.9f, 0.05f, dark);
                    for (int k = -1; k <= 1; k++) DrawCube({k * 1.6f, 4.3f, 2.8f}, 0.06f, 0.8f, 0.06f, dark);
                }
                if (r[2] > 0.4f) { DrawCube({1.5f, 5.2f, 1.8f}, 0.04f, 0.5f, 0.04f, dark); DrawSphere({1.5f, 4.85f, 1.8f}, 0.2f, glow); }
                rlPopMatrix();
                if (r[1] < 0.4f) { at(4.55f, gy0 + 0.1f); rlTranslatef((r[2] - 0.5f) * 6.0f, 0, 0); DrawDumpster(); rlPopMatrix(); }
                else if (r[1] < 0.7f) {
                    at(4.5f, gy0); rlTranslatef((r[2] - 0.5f) * 6.0f, 0, 0);
                    DrawBoxTex(tex.crate, {0, 0.5f, 0}, {1.0f, 1.0f, 1.0f}, 1, 1, F(WHITE), F({150, 110, 70, 255}));
                    DrawBoxTex(tex.crate, {0.1f, 1.5f, 0}, {0.9f, 1.0f, 0.9f}, 1, 1, F(WHITE), F({150, 110, 70, 255}));
                    rlPopMatrix();
                }
                break; }

            case ST_AVENUE: {
                float hh = 2.45f * (4 + (int)(r[0] * 5.0f));
                at(roadHalf + 2.6f + 9.0f, gy0);
                DrawBoxTex(tex.facade, {0, hh / 2, 0}, {9.8f, hh, 10.0f}, 9.8f, 9.8f, F(Pick(GLASS_TINTS, 4, r[1])), F({120, 120, 125, 255}));
                rlPopMatrix();
                at(roadHalf + 1.3f, 0.1f);
                for (int k = -1; k <= 1; k += 2) { rlPushMatrix(); rlTranslatef(k * 2.6f, 0, 0); DrawTreeL(0.62f, leaf); rlPopMatrix(); }
                rlPopMatrix();
                if (i == 1) { at(roadHalf + 2.3f, 0.1f); DrawLamp(5.5f, false, glow); rlPopMatrix(); }
                break; }

            case ST_BOULEVARD: {
                float hh = 2.45f * (3 + (int)(r[0] * 3.0f));
                at(roadHalf + 3.4f, gy0);
                for (int k = -1; k <= 1; k += 2) { rlPushMatrix(); rlTranslatef(k * 2.7f, 0, 0); DrawPalm(4.4f + r[1] * 1.6f); rlPopMatrix(); }
                rlPopMatrix();
                at(roadHalf + 1.5f, gy0); DrawCube({0, 0.55f, 0}, 9.6f, 1.0f, 1.1f, F({58, 118, 62, 255})); rlPopMatrix();   // hedge
                if (i == 1) { at(roadHalf + 2.4f, gy0 + 0.0f); DrawLamp(5.8f, true, glow); rlPopMatrix(); }
                at(roadHalf + 17.0f, gy0);
                DrawBoxTex(tex.facade, {0, hh / 2, 0}, {9.8f, hh, 9.0f}, 9.8f, 9.8f, F({255, 225, 190, 255}), F({140, 130, 120, 255}));
                rlPopMatrix();
                break; }

            case ST_ARTERIAL: {
                at(roadHalf + 6.0f, gy0);
                DrawBoxTex(tex.sidewalk, {0, 1.9f, 0}, {10.2f, 3.8f, 0.6f}, 5.1f, 3.8f, F({150, 152, 156, 255}), F({170, 170, 172, 255}));
                rlPopMatrix();                                                                                    // sound wall
                if (i == 0 && r[0] > 0.5f) { at(roadHalf + 9.5f, gy0); DrawBillboard3D(Pick(BOARDS, 4, r[1])); rlPopMatrix(); }
                if (i == 1) { at(roadHalf + 1.2f, gy0); DrawLamp(9.0f, false, glow); rlPopMatrix(); }
                if (i == 0) {                                                                                     // distant warehouses
                    at(roadHalf + 22.0f + r[2] * 6.0f, gy0);
                    DrawCube({0, 3.0f, 0}, 14.0f, 6.5f, 10.0f, F({172, 176, 186, 255})); DrawCube({0, 6.6f, 0}, 14.3f, 0.4f, 10.3f, F({120, 125, 135, 255}));
                    rlPopMatrix();
                }
                break; }

            case ST_RURAL: {
                at(roadHalf + 1.3f, gy0); DrawRailFence(10.0f); rlPopMatrix();
                if (r[0] < 0.65f) { at(9.0f + r[1] * 14.0f, gy0); DrawTreeL(1.1f + r[2] * 0.8f, leaf); rlPopMatrix(); }
                if (r[2] > 0.55f) { at(7.0f + r[3] * 8.0f, gy0); rlTranslatef((r[0] - 0.5f) * 5.0f, 0, 0); DrawHayBale(0.7f); rlPopMatrix(); }
                if (i == 0 && r[3] > 0.8f) { at(roadHalf + 2.2f, gy0); DrawUtilityPole(); rlPopMatrix(); }
                if (i == 1 && r[0] > 0.88f) {                                                                     // barn
                    at(24.0f, gy0);
                    DrawCube({0, 2.5f, 0}, 9.0f, 5.0f, 7.0f, F({160, 52, 46, 255}));
                    DrawGable(10.0f, 8.0f, 5.0f, 2.6f, F({96, 98, 104, 255}), F({160, 52, 46, 255}));
                    DrawCube({0, 1.6f, 3.55f}, 3.0f, 3.2f, 0.1f, F({235, 232, 224, 255}));
                    rlPopMatrix();
                }
                break; }

            case ST_JUNGLE: {
                Color fern = Pick(FOLIAGE_BASE, 4, r[3]); fern = {(unsigned char)(fern.r * 0.6f), (unsigned char)(fern.g * 0.95f), (unsigned char)(fern.b * 0.7f), 255};
                at(8.5f + r[1] * 7.0f, gy0); DrawJungleTree(14.0f + r[2] * 5.0f, 0.55f + r[3] * 0.3f, r[0] > 0.35f, r[2]); rlPopMatrix();      // giant tree
                if (r[0] < 0.5f) { at(roadHalf + 3.0f + r[2] * 4.0f, gy0); DrawPalm(4.0f + r[1] * 2.5f); rlPopMatrix(); }                  // palm
                at(roadHalf + 0.9f + r[1] * 1.8f, gy0); rlTranslatef((r[2] - 0.5f) * 7.0f, 0, 0); DrawFern(1.0f + r[3] * 0.7f, F(fern), 38.0f); rlPopMatrix();
                at(roadHalf + 1.4f + r[0] * 1.4f, gy0); rlTranslatef((r[3] - 0.5f) * 6.0f, 0, 0); DrawFern(1.8f, F(fern), 58.0f); rlPopMatrix(); // big leaves
                if (r[1] > 0.4f) { at(roadHalf + 2.2f, gy0); rlTranslatef((r[0] - 0.5f) * 6.0f, 0, 0); DrawBush(r[2], r[3]); rlPopMatrix(); }
                if (r[1] > 0.72f) { at(roadHalf + 1.5f, gy0); rlTranslatef((r[3] - 0.5f) * 5.0f, 0, 0); DrawBoulder(0.55f + r[2] * 0.5f, {118, 124, 112, 255}); rlPopMatrix(); }
                if (i == 1 && r[0] > 0.86f) { at(roadHalf + 4.6f, gy0); DrawRuinPillars(r[2]); rlPopMatrix(); }
                break; }

            case ST_PEDESTRIAN: {
                at(roadHalf + 4.0f + 3.6f, gy0); DrawShopBlock(tex, r[0], r[1], r[2], r[3], false, 7.0f); rlPopMatrix();
                if (r[0] < 0.55f) { at(roadHalf + 1.9f, gy0); rlTranslatef((r[1] - 0.5f) * 4.0f, 0, 0); DrawUmbrellaTable(Pick(AWNINGS, 5, r[2])); rlPopMatrix(); }
                else              { at(roadHalf + 2.4f, gy0); rlTranslatef((r[1] - 0.5f) * 4.0f, 0, 0); DrawBench(); rlPopMatrix(); }
                at(roadHalf + 0.9f, gy0); rlTranslatef(-3.2f + r[3] * 1.5f, 0, 0); DrawPlanter(Pick(FOLIAGE_BASE, 4, r[2])); rlPopMatrix();
                if (i == 0) { at(roadHalf + 3.2f, gy0); DrawLamp(5.0f, true, glow); rlPopMatrix(); }
                break; }

            default: break;
            }
        }
    }
}

// Street-name gate across the road where a new street type begins.
void DrawGate(const Segment& s, float curve, Texture2D sign) {
    float zg = s.z + SEG_LEN / 2 - 0.8f;
    PushFrame(FrameAt(curve, zg, 0, 0), 0, 0);
    Color pole = {88, 90, 98, 255};
    float hw = LANE_W * 1.5f + 1.1f;
    DrawCube({-hw, 3.2f, 0}, 0.35f, 6.4f, 0.35f, pole);
    DrawCube({hw, 3.2f, 0}, 0.35f, 6.4f, 0.35f, pole);
    DrawCube({0, 6.3f, 0}, hw * 2 + 0.35f, 0.4f, 0.4f, pole);
    float bw = 7.2f, bh = 1.8f, cy = 5.1f;
    DrawCube({0, cy, -0.05f}, bw + 0.2f, bh + 0.2f, 0.12f, {38, 38, 42, 255});
    Face(sign, {-bw / 2, cy - bh / 2, 0.03f}, {bw / 2, cy - bh / 2, 0.03f}, {bw / 2, cy + bh / 2, 0.03f}, {-bw / 2, cy + bh / 2, 0.03f}, 1, 1, WHITE, true);
    rlPopMatrix();
}

// ----------------------------------------------------------------- obstacles
void DrawBlockObstacle(int style, int seed, const Textures& tex) {
    Color dark = {40, 42, 48, 255};
    switch (style) {
    case 0: {                                                    // delivery truck, seen from behind
        Color col = TRUCKS[seed % 5];
        DrawCube({0, 1.55f, 0}, 1.8f, 2.1f, 1.0f, col);
        DrawCube({0, 2.61f, 0}, 1.8f, 0.05f, 1.0f, ShadeC(col, 1.2f));
        DrawCube({-0.45f, 1.55f, 0.505f}, 0.8f, 1.9f, 0.02f, ShadeC(col, 1.12f));
        DrawCube({0.45f, 1.55f, 0.505f}, 0.8f, 1.9f, 0.02f, ShadeC(col, 1.12f));
        DrawCube({0, 1.55f, 0.51f}, 0.05f, 2.0f, 0.02f, dark);
        DrawCube({0, 0.45f, 0.5f}, 1.9f, 0.3f, 0.28f, {70, 72, 78, 255});
        DrawCube({-0.78f, 0.95f, 0.52f}, 0.2f, 0.32f, 0.05f, {220, 40, 40, 255});
        DrawCube({0.78f, 0.95f, 0.52f}, 0.2f, 0.32f, 0.05f, {220, 40, 40, 255});
        DrawCube({0, 0.7f, 0.52f}, 0.5f, 0.2f, 0.04f, {240, 240, 240, 255});
        DrawCube({0, 0.35f, -0.05f}, 1.7f, 0.3f, 0.9f, dark);
        for (int k = -1; k <= 1; k += 2) { rlPushMatrix(); rlTranslatef(k * 0.97f, 0.32f, -0.1f); rlRotatef(90, 0, 0, 1);
            DrawCylinder({0, -0.12f, 0}, 0.32f, 0.32f, 0.24f, 12, dark); rlPopMatrix(); }
        break; }
    case 1: {                                                    // crate stack
        DrawCube({0, 1.3f, 0}, 1.9f, 2.6f, 1.0f, WHITE);
        DrawWallQuad(tex.crate, {0, 1.3f, 0.51f}, 1.9f, 2.6f, 1.0f, 1.0f, WHITE);
        DrawCube({0, 2.59f, 0}, 1.9f, 0.06f, 1.0f, ShadeC(WHITE, 0.9f));
        DrawCubeWires({0, 1.3f, 0}, 1.9f, 2.6f, 1.0f, {60, 40, 20, 255});
        break; }
    case 2: {                                                    // stacked round hay bales (round faces to the camera)
        const float rad = 0.43f;
        for (int row = 0; row < 3; row++) for (int k = 0; k < 2; k++) {
            rlPushMatrix(); rlTranslatef((k == 0 ? -0.45f : 0.45f), rad + row * 0.86f, -0.5f); rlRotatef(90, 1, 0, 0);
            DrawCylinder({0, 0, 0}, rad, rad, 1.0f, 12, {218, 188, 90, 255});
            DrawCylinder({0, 0.0f, 0}, rad * 0.62f, rad * 0.62f, 0.995f, 10, {196, 164, 70, 255});
            DrawCylinder({0, 0.0f, 0}, rad * 0.30f, rad * 0.30f, 0.99f, 8, {170, 138, 56, 255});
            rlPopMatrix();
        }
        break; }
    case 4: {                                                    // stacked mossy boulders
        Color rock = {112, 118, 104, 255};
        rlPushMatrix(); rlScalef(0.95f, 0.9f, 0.5f); DrawSphere({0, 0.95f, 0}, 1.0f, rock); rlPopMatrix();
        rlPushMatrix(); rlScalef(0.75f, 0.62f, 0.46f); DrawSphere({0.1f, 3.25f, 0}, 1.0f, ShadeC(rock, 0.9f)); rlPopMatrix();
        rlPushMatrix(); rlScalef(0.95f, 0.5f, 0.52f); DrawSphere({0, 1.85f, 0}, 1.0f, ShadeC(rock, 1.08f)); rlPopMatrix();
        DrawSphere({0.1f, 2.45f, 0.1f}, 0.32f, {62, 120, 54, 255});                              // moss
        DrawSphere({-0.4f, 1.6f, 0.3f}, 0.22f, {70, 126, 58, 255});
        break; }
    default: {                                                   // market kiosk
        DrawCube({0, 0.7f, 0}, 1.8f, 1.4f, 1.0f, {170, 120, 70, 255});
        DrawCube({0, 1.95f, -0.4f}, 1.8f, 1.3f, 0.2f, {240, 236, 224, 255});
        for (int k = -1; k <= 1; k += 2) DrawCube({k * 0.88f, 1.7f, 0.1f}, 0.1f, 1.8f, 0.1f, {90, 92, 98, 255});
        for (int k = 0; k < 4; k++) DrawCube({-0.67f + k * 0.45f, 2.55f, 0.1f}, 0.45f, 0.1f, 1.3f, k % 2 ? Color{235, 235, 235, 255} : Color{200, 50, 50, 255});
        Color fruit[3] = {{220, 50, 40, 255}, {240, 150, 40, 255}, {110, 170, 60, 255}};
        for (int k = 0; k < 6; k++) DrawSphere({-0.65f + k * 0.26f, 1.5f, 0.15f}, 0.12f, fruit[k % 3]);
        break; }
    }
}

void DrawObstacle(const Obstacle& o, float curve, const Textures& tex, const Models& mdl) {
    const Theme& th = THEMES[o.street];
    PushFrame(FrameAt(curve, o.z, 1.0f, LaneX(o.lane)), 0.0f, 0.0f);
    DrawCylinder({0, 0.02f, 0}, 0.95f, 0.95f, 0.03f, 14, Fade(BLACK, 0.3f));
    switch (o.type) {
        case ObType::Low:
            DrawCapsuleX(mdl.capsule, {0, 0.5f, 0}, 1.9f, 1.0f, 0.8f, th.lowCol);
            DrawCapsuleX(mdl.capsule, {0, 0.62f, 0}, 1.7f, 0.5f, 0.55f, ShadeC(th.lowCol, 1.3f));   // top sheen
            break;
        case ObType::High:
            { Color post = o.street == ST_JUNGLE ? Color{92, 66, 42, 255} : Color{110, 110, 120, 255};
              DrawCube({-0.9f, 1.3f, 0}, 0.2f, 2.6f, 0.3f, post);
              DrawCube({0.9f, 1.3f, 0}, 0.2f, 2.6f, 0.3f, post); }
            DrawCapsuleX(mdl.capsule, {0, 1.8f, 0}, 2.0f, 1.6f, 0.6f, th.highCol);
            DrawCapsuleX(mdl.capsule, {0, 1.98f, 0}, 1.8f, 0.85f, 0.4f, ShadeC(th.highCol, 1.3f));
            break;
        case ObType::Block:
            DrawBlockObstacle(th.blockStyle, o.seed, tex);
            break;
    }
    rlPopMatrix();
}

// ----------------------------------------------------------------- the world
// ----------------------------------------------------------------- policeman props (bolted to bones)
// A bone's pose gives its position and how far it has turned since the bind pose; the prop is drawn
// in model-space axes (up = +Y, front = +Z) at that position, turned by the same amount.
template <class Fn> void AttachToBone(const Model& m, const ModelAnimation& an, int frame, int bone, Fn fn) {
    if (bone < 0 || frame < 0 || frame >= an.frameCount) return;
    Transform cur = an.framePoses[frame][bone], bind = m.bindPose[bone];
    Quaternion d = QuaternionMultiply(cur.rotation, QuaternionInvert(bind.rotation));
    Vector3 axis = {1, 0, 0}; float ang = 0;
    QuaternionToAxisAngle(d, &axis, &ang);
    rlPushMatrix();
    rlTranslatef(cur.translation.x, cur.translation.y, cur.translation.z);
    rlRotatef(ang * RAD2DEG, axis.x, axis.y, axis.z);
    fn();
    rlPopMatrix();
}
const Color COP_NAVY = {20, 28, 58, 255}, COP_BLACK = {16, 16, 20, 255}, COP_GOLD = {236, 190, 58, 255}, COP_SILVER = {200, 204, 212, 255};
void DrawCopHead() {                                  // offsets are from the head bone (the head spans +0.0 .. +0.18 above it)
    rlPushMatrix(); rlTranslatef(0, 0, 0.012f); rlScalef(1.0f, 1.0f, 1.08f);
    DrawCylinder({0, 0.138f, 0}, 0.082f, 0.089f, 0.062f, 20, COP_NAVY);                  // crown
    DrawCylinder({0, 0.198f, 0}, 0.083f, 0.083f, 0.006f, 20, ShadeC(COP_NAVY, 1.3f));    // flat top
    DrawCylinder({0, 0.138f, 0}, 0.0915f, 0.0915f, 0.016f, 20, COP_BLACK);               // band
    rlPopMatrix();
    DrawCube({0, 0.138f, 0.140f}, 0.14f, 0.006f, 0.06f, COP_BLACK);                      // visor
    DrawCube({0, 0.172f, 0.108f}, 0.024f, 0.026f, 0.006f, COP_GOLD);                     // cap badge
    DrawCube({-0.03f, 0.059f, 0.094f}, 0.05f, 0.02f, 0.013f, {10, 10, 14, 255});        // sunglasses: two lenses
    DrawCube({0.03f, 0.059f, 0.094f}, 0.05f, 0.02f, 0.013f, {10, 10, 14, 255});
    DrawCube({0, 0.064f, 0.094f}, 0.018f, 0.005f, 0.013f, {10, 10, 14, 255});            // bridge
    DrawCube({0, 0.020f, 0.096f}, 0.05f, 0.009f, 0.014f, {40, 24, 18, 255});            // moustache
}
void DrawCopChest() {                                 // from the Spine2 bone (about chest height)
    DrawCube({0.040f, 0.024f, 0.064f}, 0.028f, 0.034f, 0.006f, COP_GOLD);                // badge
    DrawCube({0.040f, 0.024f, 0.068f}, 0.015f, 0.019f, 0.004f, ShadeC(COP_GOLD, 1.25f));
    DrawCube({-0.045f, 0.040f, 0.064f}, 0.018f, 0.036f, 0.01f, COP_BLACK);               // chest radio
    DrawCylinder({-0.045f, 0.056f, 0.064f}, 0.003f, 0.003f, 0.04f, 6, COP_BLACK);
}
void DrawCopBelt() {                                  // from the Hips bone
    rlPushMatrix(); rlTranslatef(0, 0.030f, 0.0f); rlScalef(1.0f, 1.0f, 0.84f);
    DrawCylinder({0, 0, 0}, 0.109f, 0.109f, 0.026f, 20, COP_BLACK);
    rlPopMatrix();
    DrawCube({0, 0.043f, 0.093f}, 0.026f, 0.022f, 0.006f, COP_GOLD);                     // buckle
    DrawCube({-0.108f, 0.005f, 0.0f}, 0.026f, 0.075f, 0.045f, COP_BLACK);                // holster
    DrawCube({0.110f, 0.0f, 0.0f}, 0.016f, 0.10f, 0.016f, {34, 34, 38, 255});            // baton
}
void DrawCuffRing() { DrawSphere({0, 0, 0}, 0.017f, COP_SILVER); }

const Color FIELDS[] = {{232,210,110,255},{150,200,90,255},{176,136,92,255},{196,218,120,255}};

void DrawWorld(const Game& g, Vector3 camPos, const Textures& tex, const Models& mdl, const PlayerAnim& pa, const CopRig& cop, const Texture2D* signs) {
    const float c = g.curve;
    const float roadHalf = LANE_W * 1.5f + 0.5f, roadW = roadHalf * 2.0f;
    const Texture2D none{};
    for (int i = 0; i < SEG_COUNT; i++) {
        const Segment& s = g.segs[i];
        const Theme& th = THEMES[s.type];
        const float groundY = s.type == ST_PEDESTRIAN ? -0.02f : -0.5f;
        Color roadTint = (i % 2) ? ShadeC(th.roadTint, 0.93f) : th.roadTint;
        roadTint = ShadeC(roadTint, 1.0f - 0.30f * gRain);                  // rain darkens the road
        for (int sl = 0; sl < 4; sl++) {                    // each 20 m segment is drawn as four 5 m slices so it can bend
            float zN = s.z + SEG_LEN / 2 - sl * 5.0f, zF = zN - 5.0f, zM = (zN + zF) / 2;
            float oN = BendOff(c, zN), oF = BendOff(c, zF);
            float dN = sl * 5.0f, dF = dN + 5.0f;           // metres from the segment's near edge (keeps textures continuous)
            DrawStrip(TexById(tex, th.ground), 0, 400.0f, groundY, zF, zN, oF, oN, 100.0f, dF / 4.0f, dN / 4.0f, th.groundTint);
            if (s.type == ST_RURAL) for (int sd = 0; sd < 2; sd++) {          // patchwork fields
                float side = sd == 0 ? -1.0f : 1.0f; float stripe = (sl % 2) ? 0.93f : 1.0f;
                DrawStrip(none, side * 16.0f, 14.0f, -0.45f, zF, zN, oF, oN, 1, 0, 1, ShadeC(Pick(FIELDS, 4, s.r[sd]), stripe));
                DrawStrip(none, side * 34.0f, 22.0f, -0.45f, zF, zN, oF, oN, 1, 0, 1, ShadeC(Pick(FIELDS, 4, s.r[sd + 2]), stripe));
            }
            if (th.sidewalk > 0.0f) for (int sd = 0; sd < 2; sd++) {
                float side = sd == 0 ? -1.0f : 1.0f;
                DrawStrip(TexById(tex, th.sidewalkTex), side * (roadHalf + th.sidewalk / 2), th.sidewalk, 0.1f, zF, zN, oF, oN,
                          th.sidewalk / 2.0f, dF / 2.0f, dN / 2.0f, ShadeC(th.sidewalkTint, 1.0f - 0.22f * gRain));
            }
            DrawStrip(TexById(tex, th.road), 0, roadW, 0.0f, zF, zN, oF, oN, roadW / 2.5f, dF / 2.5f, dN / 2.5f, roadTint);
            if (th.lanes) for (int k = -1; k <= 1; k += 2)
                DrawStrip(tex.laneLine, k * LANE_W / 2, 0.12f, 0.02f, zF, zN, oF, oN, 1.0f, dF / 2.0f, dN / 2.0f, th.laneCol);
            if (th.edgeLine) for (int k = -1; k <= 1; k += 2)
                DrawStrip(none, k * (roadHalf - 0.3f), 0.12f, 0.02f, zF, zN, oF, oN, 1, 0, 1, th.edgeCol);
            for (int sd = 0; sd < 2; sd++) {                // whatever lines the road edge
                float side = sd == 0 ? -1.0f : 1.0f;
                if (th.edge == EDGE_CURB) {
                    PushFrame(FrameAt(c, zM, side, roadHalf + 0.15f), 0, 0);
                    DrawCube({0, 0.12f, 0}, 0.3f, 0.24f, 5.1f, {200, 200, 196, 255}); rlPopMatrix();
                } else if (th.edge == EDGE_BARRIER) {
                    PushFrame(FrameAt(c, zM, side, roadHalf + 0.3f), 0, 0);
                    DrawCube({0, 0.4f, 0}, 0.55f, 0.8f, 5.1f, {176, 176, 172, 255});
                    DrawCube({0, 0.82f, 0}, 0.3f, 0.06f, 5.1f, {205, 205, 200, 255}); rlPopMatrix();
                } else if (th.edge == EDGE_BOLLARDS && (sl % 2) == 0) {
                    PushFrame(FrameAt(c, zM, side, roadHalf + 0.2f), 0, 0);
                    DrawCylinder({0, 0, 0}, 0.13f, 0.13f, 0.95f, 8, {84, 88, 96, 255});
                    DrawSphere({0, 0.98f, 0}, 0.15f, {222, 222, 228, 255}); rlPopMatrix();
                }
            }
        }
        if (gRain > 0.25f) {                                                  // puddles reflecting the grey sky
            float pa = 0.42f * ClampF((gRain - 0.2f) * 2.0f, 0.0f, 1.0f);
            for (int k = 0; k < 2; k++) {
                float zp = s.z + (s.r[12 + k * 2] - 0.5f) * 16.0f, lat = (s.r[13 + k * 2] - 0.5f) * 6.4f, rad = 0.7f + 0.5f * s.r[(k * 3 + 5) % 16];
                PushFrame(FrameAt(c, zp, 1.0f, lat), 0.02f, 0);
                rlScalef(1.0f, 1.0f, 1.9f);
                DrawCylinder({0, 0, 0}, rad, rad, 0.004f, 14, Fade({118, 138, 166, 255}, pa));
                rlPopMatrix();
            }
        }
        DrawScenery(s, c, camPos, tex);
        if (s.gate) DrawGate(s, c, signs[s.type]);
    }

    for (const Obstacle& o : g.obs) DrawObstacle(o, c, tex, mdl);

    for (const Coin& cn : g.coins) {                       // coins follow the bend too
        if (cn.taken) continue;
        PushFrame(FrameAt(c, cn.z, 1.0f, LaneX(cn.lane)), 0.9f, 0.0f);
        rlRotatef(g.coinSpin, 0, 1, 0);
        rlRotatef(90, 1, 0, 0);
        DrawCylinder({0, -0.04f, 0}, 0.35f, 0.35f, 0.08f, 16, Glow(GOLD));
        DrawCylinder({0, 0.041f, 0}, 0.24f, 0.24f, 0.01f, 16, ShadeC(Glow(GOLD), 1.35f));
        rlPopMatrix();
    }
    for (const Particle& pt : g.particles) {              // dust puffs, coin sparkles
        float t = pt.life / pt.maxLife;
        DrawCube(pt.pos, pt.size * t, pt.size * t, pt.size * t, Fade(pt.color, t));
    }
    // ---- the runner (rigged, animated), then the policeman behind him
    const float PLAYER_SCALE = 1.78f;                  // the character mesh is 1 unit tall; scale it to ~1.78 game units
    const Player& p = g.p;
    DrawCylinder({p.x, 0.02f, 0}, 0.55f, 0.55f, 0.03f, 16, Fade(BLACK, 0.35f)); // contact shadow
    if (pa.model.meshCount > 0) {
        float lean = g.stumbleT > 0 ? 16.0f * ClampF(g.stumbleT / 1.4f, 0.0f, 1.0f) : 0.0f;     // stumbling: pitch forward + flash
        Color tint = (g.stumbleT > 0 && ((int)(g.stumbleT * 14.0f) % 2 == 0)) ? Color{255, 130, 130, 255} : WHITE;
        rlPushMatrix();
        rlTranslatef(p.x, p.y, 0.0f);
        rlRotatef(-lean, 1, 0, 0);
        rlRotatef(180.0f, 0, 1, 0);                    // model's forward faces +Z; player runs toward -Z
        rlScalef(PLAYER_SCALE, PLAYER_SCALE, PLAYER_SCALE);
        DrawModel(pa.model, {0, 0, 0}, 1.0f, tint);
        if (g.caught && g.arrestT > 1.0f && g.plClip >= 0 && g.plClip < pa.animCount) {         // handcuffs behind his back
            const ModelAnimation& an = pa.anims[g.plClip];
            int hl = cop.bHandL, hr = cop.bHandR;
            if (hl >= 0 && hr >= 0 && g.plFrame < an.frameCount) {
                Vector3 a2 = an.framePoses[g.plFrame][hl].translation, b2 = an.framePoses[g.plFrame][hr].translation;
                AttachToBone(pa.model, an, g.plFrame, hl, [] { DrawCuffRing(); });
                AttachToBone(pa.model, an, g.plFrame, hr, [] { DrawCuffRing(); });
                Vector3 mid = Vector3Scale(Vector3Add(a2, b2), 0.5f);
                DrawCube(mid, std::max(0.012f, fabsf(a2.x - b2.x)), 0.008f, 0.008f, COP_SILVER);   // the chain
            }
        }
        rlPopMatrix();
    }
    if (cop.model.meshCount > 0 && g.copVis < 9.0f && g.copClip >= 0 && g.copClip < pa.animCount) {
        DrawCylinder({g.copX, 0.02f, g.copVis}, 0.55f, 0.55f, 0.03f, 16, Fade(BLACK, 0.35f));
        const ModelAnimation& an = pa.anims[g.copClip];
        rlPushMatrix();
        rlTranslatef(g.copX, g.copY, g.copVis);
        rlRotatef(180.0f, 0, 1, 0);
        rlScalef(PLAYER_SCALE, PLAYER_SCALE, PLAYER_SCALE);
        DrawModel(cop.model, {0, 0, 0}, 1.0f, WHITE);
        AttachToBone(cop.model, an, g.copFrame, cop.bHead, [] { DrawCopHead(); });
        AttachToBone(cop.model, an, g.copFrame, cop.bSpine2, [] { DrawCopChest(); });
        AttachToBone(cop.model, an, g.copFrame, cop.bHips, [] { DrawCopBelt(); });
        rlPopMatrix();
    }
}

Rectangle TimeButton(float s)    { return {(float)GetScreenWidth() - 80.0f * s, 86.0f * s, 60.0f * s, 56.0f * s}; }
Rectangle WeatherButton(float s) { return {(float)GetScreenWidth() - 80.0f * s, 150.0f * s, 60.0f * s, 56.0f * s}; }
void DrawSunIcon(float cx, float cy, float r) {
    DrawCircle((int)cx, (int)cy, r * 0.55f, {255, 218, 90, 255});
    for (int k = 0; k < 8; k++) { float a = k * 0.7853982f; DrawLineEx({cx + cosf(a) * r * 0.75f, cy + sinf(a) * r * 0.75f}, {cx + cosf(a) * r * 1.0f, cy + sinf(a) * r * 1.0f}, 2.0f, {255, 218, 90, 255}); }
}
void DrawMoonIcon(float cx, float cy, float r, Color bg) {
    DrawCircle((int)cx, (int)cy, r * 0.72f, {236, 238, 250, 255});
    DrawCircle((int)(cx + r * 0.36f), (int)(cy - r * 0.18f), r * 0.6f, bg);
}
void DrawCloudIcon(float cx, float cy, float r, Color c) {
    DrawCircle((int)(cx - r * 0.45f), (int)(cy + r * 0.1f), r * 0.42f, c);
    DrawCircle((int)(cx + r * 0.1f), (int)(cy - r * 0.15f), r * 0.55f, c);
    DrawCircle((int)(cx + r * 0.6f), (int)(cy + r * 0.12f), r * 0.38f, c);
    DrawRectangle((int)(cx - r * 0.45f), (int)(cy + r * 0.1f), (int)(r * 1.45f), (int)(r * 0.4f), c);
}
void DrawRainIcon(float cx, float cy, float r) {
    DrawCloudIcon(cx, cy - r * 0.2f, r, {170, 182, 200, 255});
    for (int k = -1; k <= 1; k++) DrawLineEx({cx + k * r * 0.5f, cy + r * 0.45f}, {cx + k * r * 0.5f - r * 0.18f, cy + r * 0.95f}, 2.0f, {120, 170, 255, 255});
}

Rectangle PauseButton(float s) { return {(float)GetScreenWidth() - 80.0f * s, 15.0f * s, 60.0f * s, 60.0f * s}; }

void DrawHUD(const Game& g, float s) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawTextL(TextFormat("%d", g.Score()), 20 * s, 12 * s, 52 * s, WHITE);
    DrawTextL(TextFormat("Coins: %d", g.coinCount), 20 * s, 70 * s, 28 * s, GOLD);
    DrawTextL(TextFormat("Best: %d", g.best), 20 * s, 104 * s, 24 * s, LIGHTGRAY);
    DrawTextL(THEMES[g.curType].name, 20 * s, 134 * s, 22 * s, {190, 225, 255, 255});
    if (g.state == State::Playing) {                              // how close is the policeman?
        float danger = ClampF((COP_GAP_BASE - g.copGap) / (COP_GAP_BASE - COP_GAP_MIN), 0.0f, 1.0f);
        float bx = 20 * s, by = 190 * s, bw = 170 * s, bh = 12 * s;
        DrawTextL("POLICE", bx, by - 26 * s, 20 * s, {150, 190, 255, 255});
        DrawRectangle((int)bx, (int)by, (int)bw, (int)bh, Fade(BLACK, 0.5f));
        DrawRectangle((int)bx, (int)by, (int)(bw * (0.1f + 0.9f * danger)), (int)bh, MixC({80, 200, 120, 255}, {235, 50, 50, 255}, danger));
        if (danger > 0.3f) {                                      // flashing siren light when he is close
            bool red = ((int)(GetTime() * 5.0)) % 2 == 0;
            DrawCircle((int)(bx + bw + 20 * s), (int)(by + bh / 2), 8 * s, red ? Color{255, 60, 60, 255} : Color{70, 120, 255, 255});
        }
    }
    if (g.state == State::Playing && g.toast > 0.0f) {          // "now entering ..." banner
        float a = ClampF(g.toast / 0.5f, 0.0f, 1.0f) * ClampF((3.2f - g.toast) / 0.3f, 0.0f, 1.0f);
        const Theme& th = THEMES[g.toastType];
        float bw = 600 * s, bh = 96 * s, bx = (W - bw) / 2, by = H * 0.10f;
        DrawRectangleRounded({bx, by, bw, bh}, 0.2f, 8, Fade(th.sign, 0.88f * a));
        DrawRectangleRoundedLines({bx, by, bw, bh}, 0.2f, 8, Fade(WHITE, 0.8f * a));
        DrawTextC(th.name, W / 2, by + 10 * s, 46 * s, Fade(WHITE, a));
        DrawTextC(th.blurb, W / 2, by + 64 * s, 22 * s, Fade({235, 235, 235, 255}, a));
    }

    if (g.state != State::GameOver) {                              // time-of-day and weather buttons (tap, or keys T / C)
        Color bg = {20, 26, 44, 225};
        Rectangle tb = TimeButton(s), wb = WeatherButton(s);
        DrawRectangleRounded(tb, 0.25f, 6, bg); DrawRectangleRounded(wb, 0.25f, 6, bg);
        float cx = tb.x + tb.width / 2, r = 14.0f * s;
        if (g.night > 0.5f) DrawMoonIcon(cx, tb.y + 22 * s, r, {20, 26, 44, 255}); else DrawSunIcon(cx, tb.y + 22 * s, r);
        DrawTextC(g.todMode == 0 ? "AUTO" : (g.todMode == 1 ? "DAY" : "NIGHT"), cx, tb.y + 40 * s, 12 * s, {200, 215, 235, 255});
        if (g.rain > 0.5f) DrawRainIcon(cx, wb.y + 22 * s, r);
        else if (g.cloud > 0.5f) DrawCloudIcon(cx, wb.y + 22 * s, r, {225, 230, 238, 255});
        else DrawSunIcon(cx, wb.y + 22 * s, r);
        DrawTextC(g.wxMode == 0 ? "AUTO" : (g.wxMode == 1 ? "CLEAR" : (g.wxMode == 2 ? "CLOUDY" : "RAIN")), cx, wb.y + 40 * s, 12 * s, {200, 215, 235, 255});
        if (g.climateMsgT > 0.0f) {
            float a = ClampF(g.climateMsgT / 0.5f, 0.0f, 1.0f) * ClampF((3.0f - g.climateMsgT) / 0.3f, 0.0f, 1.0f);
            DrawTextC(g.climateMsg, W / 2, H * 0.255f, 26 * s, Fade({235, 240, 250, 255}, a));
        }
    }
    if (g.state == State::Playing) {
        Rectangle b = PauseButton(s);
        DrawRectangleRounded(b, 0.25f, 6, Fade(BLACK, 0.45f));
        DrawRectangle((int)(b.x + 18 * s), (int)(b.y + 15 * s), (int)(8 * s), (int)(30 * s), WHITE);
        DrawRectangle((int)(b.x + 34 * s), (int)(b.y + 15 * s), (int)(8 * s), (int)(30 * s), WHITE);
    }
    float ov = (g.state == State::GameOver && g.caught) ? 0.55f * ClampF((g.arrestT - 1.6f) / 0.6f, 0.0f, 1.0f) : 0.5f;
    if (g.state != State::Playing) DrawRectangle(0, 0, (int)W, (int)H, Fade(BLACK, ov));
    if (g.state == State::GameOver && g.caught) {                 // red / blue siren wash while he makes the arrest
        float a = 0.38f * ClampF(g.arrestT / 0.5f, 0.0f, 1.0f);
        bool red = ((int)(g.arrestT * 4.0f)) % 2 == 0;
        Color c1 = red ? Color{255, 40, 40, 255} : Color{50, 90, 255, 255}, c2 = red ? Color{50, 90, 255, 255} : Color{255, 40, 40, 255};
        DrawRectangleGradientH(0, 0, (int)(W * 0.45f), (int)H, Fade(c1, a), Fade(c1, 0.0f));
        DrawRectangleGradientH((int)(W * 0.55f), 0, (int)(W * 0.45f), (int)H, Fade(c2, 0.0f), Fade(c2, a));
    }

    float cx = W / 2;
    if (g.state == State::Menu) {
        DrawTextC("ENDLESS RUNNER", cx, H * 0.22f, 84 * s, GOLD);
        DrawTextC("Tap or press SPACE to start", cx, H * 0.45f, 36 * s, WHITE);
        DrawTextC("PC:  A/D or arrows = lane   W/Space = jump   S = slide   P = pause", cx, H * 0.60f, 24 * s, LIGHTGRAY);
        DrawTextC("Mobile:  swipe left / right = lane   swipe up = jump   swipe down = slide", cx, H * 0.66f, 24 * s, LIGHTGRAY);
        DrawTextC("Preview a street while playing: keys 1-9, 0 and -", cx, H * 0.74f, 22 * s, {170, 205, 235, 255});
        DrawTextC("T = time of day    C = weather    (or tap the buttons, top right)", cx, H * 0.80f, 22 * s, {170, 205, 235, 255});
    } else if (g.state == State::Paused) {
        DrawTextC("PAUSED", cx, H * 0.30f, 80 * s, WHITE);
        DrawTextC("Tap or press P to resume", cx, H * 0.50f, 34 * s, LIGHTGRAY);
    } else if (g.state == State::GameOver) {
        float a = ClampF((g.arrestT - 1.7f) / 0.5f, 0.0f, 1.0f);       // fades in once the arrest is made
        cx = W * 0.27f;                                                  // text on the left, the arrest on the right
        bool red = ((int)(g.arrestT * 3.0f)) % 2 == 0;
        DrawTextC("BUSTED!", cx, H * 0.14f, 100 * s, Fade(red ? Color{255, 70, 70, 255} : Color{100, 150, 255, 255}, a));
        DrawTextC("The police caught you", cx, H * 0.31f, 30 * s, Fade(WHITE, a));
        DrawTextC(TextFormat("Score: %d", g.Score()), cx, H * 0.40f, 48 * s, Fade(WHITE, a));
        DrawTextC(TextFormat("Coins: %d", g.coinCount), cx, H * 0.50f, 32 * s, Fade(GOLD, a));
        if (g.newBest) DrawTextC("NEW BEST!", cx, H * 0.58f, 36 * s, Fade(GREEN, a));
        else DrawTextC(TextFormat("Best: %d", g.best), cx, H * 0.58f, 32 * s, Fade(LIGHTGRAY, a));
        if (g.arrestT > 2.2f) DrawTextC("Tap or press SPACE to play again", cx, H * 0.74f, 32 * s, WHITE);
    }
}

} // namespace

// ----------------------------------------------------------------- main
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Endless Runner");
    SetExitKey(KEY_NULL);      // Esc pauses instead of quitting
    SetTargetFPS(60);

    Game g;
    g.best = LoadBest();
    g.Reset();
    Pointer ptr;
    Textures tex = LoadGameTextures();
    Models mdl = LoadGameModels();
    PlayerAnim pa = LoadPlayerAnim();
    CopRig cop = LoadCopRig();
    int copCurIdx = -1;
    float copAnimFrame = 0.0f;

    // scene shader: one tint for the whole 3D scene (time of day + weather); models use it too
    Shader sceneShader = LoadShaderFromMemory(SCENE_VS, SCENE_FS);
    bool shaderOK = sceneShader.id > 0 && sceneShader.id != rlGetShaderIdDefault();
    int locTint = GetShaderLocation(sceneShader, "uTint"), locGray = GetShaderLocation(sceneShader, "uGray");
    if (shaderOK) {
        auto apply = [&](Model& m) { for (int k = 0; k < m.materialCount; k++) m.materials[k].shader = sceneShader; };
        apply(mdl.head); apply(mdl.capsule); apply(pa.model); apply(cop.model);
    }
    int curAnimIdx = -1;
    float animFrame = 0.0f;

    // street-name boards for the gates, rendered once with raylib's own font
    RenderTexture2D signRT[ST_COUNT]; Texture2D signTex[ST_COUNT];
    for (int t = 0; t < ST_COUNT; t++) {
        signRT[t] = LoadRenderTexture(512, 128);
        BeginTextureMode(signRT[t]);
        ClearBackground(THEMES[t].sign);
        DrawRectangleLinesEx({6, 6, 500, 116}, 5, WHITE);
        int size = 60; while (size > 20 && MeasureText(THEMES[t].name, size) > 470) size -= 2;
        DrawText(THEMES[t].name, (512 - MeasureText(THEMES[t].name, size)) / 2, (128 - size) / 2, size, WHITE);
        EndTextureMode();
        SetTextureFilter(signRT[t].texture, TEXTURE_FILTER_BILINEAR);
        signTex[t] = signRT[t].texture;
    }
    Vector3 skyTop = {70, 130, 210}, skyBot = {175, 218, 242};     // eased towards each street's sky colours

    Camera3D cam{};
    cam.up = {0, 1, 0};
    cam.fovy = 60.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float s = (float)GetScreenHeight() / 720.0f;
        float dt = std::min(GetFrameTime(), 0.05f);
        Input in = PollInput(ptr, s);

#if !defined(PLATFORM_ANDROID)
        if (g.state == State::Playing && !IsWindowFocused()) g.state = State::Paused; // auto-pause
#endif

        for (int k = 0; k < ST_COUNT; k++)                             // keys 1-9, 0 and - jump to a street type
            if (IsKeyPressed(k < 9 ? KEY_ONE + k : (k == 9 ? KEY_ZERO : KEY_MINUS))) g.JumpToStreet(k);

        if (IsKeyPressed(KEY_T)) g.CycleTime();                          // T = time of day, C = weather
        if (IsKeyPressed(KEY_C)) g.CycleWeather();
        bool tapTime = in.tap && CheckCollisionPointRec(in.tapPos, TimeButton(s));
        bool tapWx   = in.tap && CheckCollisionPointRec(in.tapPos, WeatherButton(s));
        if (g.state != State::GameOver) { if (tapTime) g.CycleTime(); if (tapWx) g.CycleWeather(); }

        switch (g.state) {
            case State::Menu:
                if (in.confirm || (in.tap && !tapTime && !tapWx)) { g.Reset(); g.state = State::Playing; }
                break;
            case State::Playing:
                if (in.pause || (in.tap && CheckCollisionPointRec(in.tapPos, PauseButton(s)))) g.state = State::Paused;
                else g.Update(dt, in);
                break;
            case State::Paused:
                if (in.pause || in.confirm || (in.tap && !tapTime && !tapWx)) g.state = State::Playing;
                break;
            case State::GameOver:
                g.UpdateArrest(dt);
                if (g.arrestT > 2.2f && (in.confirm || in.tap)) { g.Reset(); g.state = State::Playing; }
                break;
        }

        g.UpdateClimate(dt, g.state == State::Playing || (g.state == State::GameOver && g.caught));

        // pick and advance the player's animation clip to match current game state
        if (pa.animCount > 0) {
            int idx = pa.idxIdle;
            if (g.state == State::Playing) {
                if (!g.p.onGround) idx = pa.idxJump;
                else if (g.p.slideTimer > 0) idx = pa.idxSlide;
                else idx = pa.idxRun;
            } else if (g.state == State::GameOver && g.caught && g.arrestT > 0.6f) idx = pa.idxArrested;
            if (idx != curAnimIdx) { animFrame = 0.0f; curAnimIdx = idx; }
            constexpr float CLIP_FPS = 58.8f;   // raylib bakes glTF clips at ~59 frames per second
            int fc = pa.anims[idx].frameCount;
            int fi;
            if (idx == pa.idxJump && g.state == State::Playing) {
                // Tie the jump clip to the real arc: frames 28..74 are takeoff -> apex -> touchdown,
                // so the pose follows the player's actual vertical velocity instead of a timer.
                float phase = (JUMP_V - g.p.vy) / (2.0f * JUMP_V);
                phase = phase < 0.0f ? 0.0f : (phase > 1.0f ? 1.0f : phase);
                fi = 28 + (int)(phase * 46.0f);
            } else {
                float rate = 1.0f;
                if (idx == pa.idxRun) {         // faster cadence as the game speeds up
                    float t = (g.speed - BASE_SPEED) / (MAX_SPEED - BASE_SPEED);
                    rate = 1.25f + 0.55f * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
                }
                animFrame += dt * CLIP_FPS * rate;
                fi = fc > 0 ? ((int)animFrame) % fc : 0;
            }
            if (fc > 0) { fi %= fc; if (fi < 0) fi += fc; }
            UpdateModelAnimation(pa.model, pa.anims[idx], fi);
            g.plClip = idx; g.plFrame = fi;
        }

        // the policeman's clip: runs behind you, hops/slides over what you left behind, grabs you at the end
        if (pa.animCount > 0) {
            constexpr float COP_FPS = 58.8f;
            int ci = pa.idxIdle;
            if (g.state == State::Playing) ci = g.copY > 0.01f ? pa.idxJump : (g.copSlide > 0 ? pa.idxSlide : pa.idxRun);
            else if (g.state == State::GameOver && g.caught) ci = g.arrestT < 0.9f ? pa.idxRun : pa.idxGrab;
            if (ci != copCurIdx) { copAnimFrame = ci == pa.idxRun ? 17.0f : 0.0f; copCurIdx = ci; }   // start out of step with the runner
            int cfc = pa.anims[ci].frameCount, cfi;
            if (ci == pa.idxJump && g.state == State::Playing) {
                float ph = ClampF((JUMP_V - g.copVy) / (2.0f * JUMP_V), 0.0f, 1.0f);
                cfi = 28 + (int)(ph * 46.0f);
            } else {
                float rate = 1.0f;
                if (ci == pa.idxRun) {
                    float t = ClampF((g.speed - BASE_SPEED) / (MAX_SPEED - BASE_SPEED), 0.0f, 1.0f);
                    rate = g.state == State::GameOver ? 2.0f : 1.25f + 0.55f * t;
                }
                copAnimFrame += dt * COP_FPS * rate;
                cfi = cfc > 0 ? ((int)copAnimFrame) % cfc : 0;
            }
            if (cfc > 0) { cfi %= cfc; if (cfi < 0) cfi += cfc; }
            UpdateModelAnimation(cop.model, pa.anims[ci], cfi);
            g.copClip = ci; g.copFrame = cfi;
        }

        cam.position = {g.p.x * 0.4f, 4.2f, 8.5f};
        cam.target   = {g.p.x * 0.4f + BendOff(g.curve, -20.0f) * 0.5f, 1.2f, -12.0f};   // look into the bend

        if (g.caught && g.state == State::GameOver) {            // arrest: the camera swings round to the side
            float t = ClampF(g.arrestT / 1.3f, 0.0f, 1.0f); t = t * t * (3.0f - 2.0f * t);
            cam.position = Vector3Lerp(cam.position, {g.p.x + 2.4f, 1.55f, 2.9f}, t);
            cam.target   = Vector3Lerp(cam.target,   {g.p.x - 1.0f, 0.95f, 0.95f}, t);   // pair sits right of centre; text goes left
        }
        if (g.shake > 0) {                                         // knocked off balance: shake the screen
            cam.position.x += sinf((float)GetTime() * 63.0f) * 0.10f * g.shake;
            cam.position.y += cosf((float)GetTime() * 71.0f) * 0.08f * g.shake;
        }

        // ---- climate: night amount, dusk glow, sky colours, scene tint and fog for this frame
        float nightS = g.night * g.night * (3.0f - 2.0f * g.night), dusk = 4.0f * g.night * (1.0f - g.night);
        gNight = nightS; gDusk = dusk; gCloud = g.cloud; gRain = g.rain;
        gFogNear = 50.0f - 24.0f * g.rain - 8.0f * g.cloud; gFogRange = 140.0f - 55.0f * g.rain - 15.0f * g.cloud;
        Vector3 tint; float grey; ComputeTint(g.night, dusk, g.cloud, g.rain, g.flash, tint, grey);
        {
            Vector3 tTop, tBot; SkyTarget(THEMES[g.curType], g.night, dusk, g.cloud, g.rain, tTop, tBot);
            float k = std::min(1.0f, dt * 1.6f);
            skyTop = Vector3Lerp(skyTop, tTop, k); skyBot = Vector3Lerp(skyBot, tBot, k);
        }
        auto toCol = [](Vector3 v) { return Color{(unsigned char)ClampF(v.x, 0, 255), (unsigned char)ClampF(v.y, 0, 255), (unsigned char)ClampF(v.z, 0, 255), 255}; };
        // distant scenery fades into skyBot / tint, so after the shader's tint it matches the sky exactly
        gHaze = toCol({skyBot.x / tint.x, skyBot.y / tint.y, skyBot.z / tint.z});
        if (shaderOK) {
            float t3[3] = {tint.x, tint.y, tint.z};
            SetShaderValue(sceneShader, locTint, t3, SHADER_UNIFORM_VEC3);
            SetShaderValue(sceneShader, locGray, &grey, SHADER_UNIFORM_FLOAT);
        }

        BeginDrawing();
        int W = GetScreenWidth(), H = GetScreenHeight();
        float lf = 0.6f * g.flash;                                      // lightning lights up the sky
        Color cTop = toCol(Vector3Lerp(skyTop, {235, 240, 255}, lf)), cBot = toCol(Vector3Lerp(skyBot, {235, 240, 255}, lf));
        ClearBackground(cBot);
        DrawRectangleGradientV(0, 0, W, H / 2, cTop, cBot);
        DrawStars(nightS * (1.0f - 0.95f * ClampF(g.cloud * 0.9f + g.rain, 0.0f, 1.0f)) * (1.0f - g.flash), W, H, (float)GetTime());
        BeginMode3D(cam);
        rlDisableBackfaceCulling();                    // props are drawn double-sided
        if (shaderOK) BeginShaderMode(sceneShader);
        DrawSky(cam.position, g.time, nightS, dusk, g.cloud, g.rain);
        DrawWorld(g, cam.position, tex, mdl, pa, cop, signTex);
        rlDrawRenderBatchActive();
        if (shaderOK) EndShaderMode();
        rlEnableBackfaceCulling();
        EndMode3D();
        if (!shaderOK) DrawRectangle(0, 0, W, H, Fade({12, 18, 50, 255}, 0.55f * nightS));    // fallback if the shader can't compile
        DrawRain(g.rain, W, H, (float)GetTime(), s);
        if (g.flash > 0.0f) DrawRectangle(0, 0, W, H, Fade(WHITE, 0.26f * g.flash));
        DrawHUD(g, s);
        EndDrawing();
    }
    for (int t = 0; t < ST_COUNT; t++) UnloadRenderTexture(signRT[t]);
    UnloadGameTextures(tex);
    {   // give the models back the default shader before unloading (so raylib doesn't free ours twice)
        Shader def = { rlGetShaderIdDefault(), rlGetShaderLocsDefault() };
        auto reset = [&](Model& m) { for (int k = 0; k < m.materialCount; k++) m.materials[k].shader = def; };
        reset(mdl.head); reset(mdl.capsule); reset(pa.model); reset(cop.model);
    }
    UnloadGameModels(mdl);
    UnloadCopRig(cop);
    UnloadPlayerAnim(pa);
    if (shaderOK) UnloadShader(sceneShader);
    CloseWindow();
    return 0;
}
