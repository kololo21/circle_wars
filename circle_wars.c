// CIRCLE WARS


#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define MAX_MONSTERS 1200
#define MAX_BULLETS 2500
#define MAX_DROPS 100
#define MAX_HEALS 80
#define MAX_PARTICLES 1200
#define MAX_ENV_OBJS 500
#define WORLD_SIZE 5000

#define WORLD_PAD 600.0f
#define WORLD_MIN (-WORLD_PAD)
#define WORLD_MAX (WORLD_SIZE + WORLD_PAD)


// Use fewer environment blocks
#define ENV_USED 320

typedef enum { GS_TITLE, GS_GAMEPLAY, GS_HELP, GS_GAMEOVER, GS_CLEAR } GameState;
typedef enum { M_NORMAL, M_TANK, M_GHOST, M_CHARGER, M_SHOOTER, M_WORM, M_RING, M_BOSS, M_MINION } MobType;
typedef enum { ATK_MELEE, ATK_RANGED, ATK_CONTINUOUS } AtkType;
typedef enum { ENV_ROCK, ENV_GRASS, ENV_LAVA } EnvType;
typedef enum { SKILL_ATK, SKILL_SPD, SKILL_REGEN, SKILL_RANGE, SKILL_SPECIAL, SKILL_MAXHP, SKILL_GACHA, SKILL_LIGHTNING, SKILL_FROST, SKILL_WEAPON_UP } SkillType;

typedef struct { char name[32]; int damage; float range; Color color; AtkType type; float fireRate; } Weapon;

// Added life to bullets 
typedef struct { Vector2 pos; Vector2 speed; bool active; bool isEnemy; float radius; float life; } Bullet;

typedef struct {
    Vector2 pos;
    Vector2 segments[5];
    int hp, maxHp;
    bool active;
    MobType type;
    float speed;
    float stateTimer;
    int state;
    Vector2 targetPos;
    Color color;
} Monster;

typedef struct { Vector2 pos; int weaponIdx; bool active; } WeaponDrop;
typedef struct { Vector2 pos; bool active; } HealItem;
typedef struct { Vector2 pos; EnvType type; float size; } EnvObj;
typedef struct { Vector2 pos; Vector2 vel; float life; Color col; bool active; } Particle;
typedef struct { Music mainBgm; Sound seSlash, seDamage, seHeal, seShoot; } GameAudio;

typedef struct { Vector2 a, b; float life; bool active; } ZapLine;
#define MAX_ZAPS 128

//GLOBAL VARIABLES
GameState gameState = GS_TITLE;

Monster monsters[MAX_MONSTERS] = { 0 };
Bullet bullets[MAX_BULLETS] = { 0 };
HealItem heals[MAX_HEALS] = { 0 };
WeaponDrop drops[MAX_DROPS] = { 0 };
EnvObj environment[MAX_ENV_OBJS] = { 0 };
Particle particles[MAX_PARTICLES] = { 0 };
ZapLine zaps[MAX_ZAPS] = { 0 };

Weapon weaponsList[] = {
    {"Rusty Dagger", 60, 200, GRAY, ATK_MELEE, 0},
    {"Katana", 100, 280, SKYBLUE, ATK_MELEE, 0},
    {"Holy Great Sword", 200, 420, GOLD, ATK_MELEE, 0},
    {"HANDGUN", 90, 700, LIME, ATK_RANGED, 0.5f},
    {"MAGIC STAFF", 28, 600, PURPLE, ATK_CONTINUOUS, 0.3f}
};

Weapon currentWeapon;
int weaponLevel = 1;

Vector2 playerPos = { WORLD_SIZE/2.0f, WORLD_SIZE/2.0f }, lastMove = { 0, -1 };

int playerHP = 100, playerMaxHP = 100, playerLevel = 1, playerExp = 0, playerMaxExp = 100;
int highScores[3] = { 0 };

bool gameOver = false, bossActive = false, paused = false, levelUpPending = false, gachaPending = false;

float attackVisualTimer = 0, pillarTimer = 0, specialGauge = 0, specialFlash = 0, damageSoundTimer = 0;
float fireTimer = 0, screenShake = 0, autoHealTimer = 0, chargeTimer = 0, bossIntroTimer = 0;

Vector2 pillarPos[1] = {0};

int skillLv[10] = {0};
int bonusDamage = 0; float bonusSpeed = 0; float bonusRange = 0;
SkillType choicePool[3];
int gachaResultIdx = 0, gachaChoiceKey = 0;
Monster *currentBossPtr = NULL;

//Added skill/runtime 
float regenTimer = 0.0f;
float lightningTimer = 0.0f;
float healSpawnTimer = 0.0f;
float healSpawnInterval = 3.0f;

float specialGainMul = 1.0f;
float rangeMul = 1.0f;
int regenEvolutionStacks = 0;

//Spawn pacing
float mobSpawnTimer = 0.0f;

//PROTOTYPES
void SpawnMonster(MobType type, bool isBoss, Vector2 spawnPos);
void ResetGame(void);
void UpdateRanking(int score);
void SpawnParticles(Vector2 pos, Color col, int count, float speedBase);
void Shoot(Vector2 start, Vector2 dir, bool isEnemy, float radius);
void Explode(Vector2 pos, float radius, int damage);

void AddZapLine(Vector2 a, Vector2 b);
int FindNearestMonsterIndex(Vector2 from, float maxDist, int excludeIdx);
void TriggerLightning(Vector2 fromPos, int chains, float maxJumpDist, int dmg);
void SpawnHealAtRandomMap(void);

static int CountActiveMobs(void);
static void CancelEnemyBulletsInRadius(Vector2 center, float r, int maxCancel);
static void BossShootAimedBurst(Monster *b, Vector2 targetDir);
static void BossShootSpiral(Monster *b, float t, int count, float speedMul);
static void BossDashExplode(Monster *b);

//IMPLEMENTATIONS
void AddZapLine(Vector2 a, Vector2 b) {
    for (int i = 0; i < MAX_ZAPS; i++) if (!zaps[i].active) {
        zaps[i].active = true;
        zaps[i].a = a; zaps[i].b = b;
        zaps[i].life = 0.12f;
        break;
    }
}

int FindNearestMonsterIndex(Vector2 from, float maxDist, int excludeIdx) {
    int best = -1;
    float bestD = maxDist;
    for (int i = 0; i < MAX_MONSTERS; i++) {
        if (!monsters[i].active) continue;
        if (i == excludeIdx) continue;
        float d = Vector2Distance(from, monsters[i].pos);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void TriggerLightning(Vector2 fromPos, int chains, float maxJumpDist, int dmg) {
    SpawnParticles(fromPos, Fade(SKYBLUE, 0.9f), 18, 5.0f);
    int prev = -1;
    Vector2 cur = fromPos;
    for (int c = 0; c < chains; c++) {
        int idx = FindNearestMonsterIndex(cur, maxJumpDist, prev);
        if (idx < 0) break;

        AddZapLine(cur, monsters[idx].pos);

        monsters[idx].hp -= dmg;
        SpawnParticles(monsters[idx].pos, Fade(SKYBLUE, 0.8f), 10, 4.0f);
        if (monsters[idx].hp <= 0) {
            monsters[idx].active = false;
            playerExp += 12;
        }
        prev = idx;
        cur = monsters[idx].pos;
    }
}

void SpawnHealAtRandomMap(void) {
    for (int i = 0; i < MAX_HEALS; i++) if (!heals[i].active) {
        Vector2 p;
        int tries = 0;
        do {
            p = (Vector2){(float)GetRandomValue(80, WORLD_SIZE-80), (float)GetRandomValue(80, WORLD_SIZE-80)};
            tries++;
        } while (Vector2Distance(p, playerPos) < 600 && tries < 30);

        heals[i].pos = p;
        heals[i].active = true;
        break;
    }
}

void Explode(Vector2 pos, float radius, int damage) {
    SpawnParticles(pos, ORANGE, 20, 3.0f);
    if (CheckCollisionCircles(playerPos, 20, pos, radius)) playerHP -= 8;
    for (int i = 0; i < MAX_MONSTERS; i++)
        if (monsters[i].active && Vector2Distance(monsters[i].pos, pos) < radius)
            monsters[i].hp -= damage;
}

void UpdateRanking(int score) {
    if (score > highScores[0]) { highScores[2] = highScores[1]; highScores[1] = highScores[0]; highScores[0] = score; }
    else if (score > highScores[1]) { highScores[2] = highScores[1]; highScores[1] = score; }
    else if (score > highScores[2]) { highScores[2] = score; }
}

static int CountActiveMobs(void) {
    int c = 0;
    for (int i = 0; i < MAX_MONSTERS; i++) if (monsters[i].active && monsters[i].type != M_BOSS) c++;
    return c;
}

// some ranged actions may clear bullets
static void CancelEnemyBulletsInRadius(Vector2 center, float r, int maxCancel) {
    int killed = 0;
    float r2 = r*r;
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active || !bullets[i].isEnemy) continue;
        Vector2 d = Vector2Subtract(bullets[i].pos, center);
        if ((d.x*d.x + d.y*d.y) <= r2) {
            bullets[i].active = false;
            killed++;
            if (killed >= maxCancel) break;
        }
    }
}

static void BossShootAimedBurst(Monster *b, Vector2 targetDir) {
    // 5-way aimed burst 
    float base = atan2f(targetDir.y, targetDir.x);
    float spreads[5] = { -0.28f, -0.14f, 0.0f, 0.14f, 0.28f };
    for (int k = 0; k < 5; k++) {
        float a = base + spreads[k];
        Shoot(b->pos, (Vector2){cosf(a), sinf(a)}, true, 6.5f);
    }
}

static void BossShootSpiral(Monster *b, float t, int count, float speedMul) {
    // Spiral bullets: 
    float a0 = t * 2.2f;
    for (int k = 0; k < count; k++) {
        float a = a0 + k*(6.2831853f/(float)count);
        Vector2 dir = (Vector2){cosf(a)*speedMul, sinf(a)*speedMul};
        Shoot(b->pos, dir, true, 6.0f);
    }
}

static void BossDashExplode(Monster *b) {
    // Dash near player 
    Vector2 dir = Vector2Normalize(Vector2Subtract(playerPos, b->pos));
    b->pos = Vector2Add(b->pos, Vector2Scale(dir, 26.0f)); 
}

// Environment placement + fewer blocks
void ResetGame(void) {
    UpdateRanking(playerLevel);

    playerPos = (Vector2){ WORLD_SIZE/2.0f, WORLD_SIZE/2.0f };
    playerHP = 100; playerMaxHP = 100;
    playerLevel = 1; playerExp = 0; playerMaxExp = 100;
    specialGauge = 0;

    bonusDamage = 0; bonusSpeed = 0; bonusRange = 0;
    weaponLevel = 1;
    bossActive = false; gameOver = false; paused = false;
    levelUpPending = false; gachaPending = false;

    for (int i = 0; i < 10; i++) skillLv[i] = 0;
    currentWeapon = weaponsList[0];

    // skill runtime  reset
    regenTimer = 0.0f;
    lightningTimer = 0.0f;
    healSpawnTimer = 0.0f;
    specialGainMul = 1.0f;
    rangeMul = 1.0f;
    regenEvolutionStacks = 0;
    mobSpawnTimer = 0.0f;

    for (int i = 0; i < MAX_ZAPS; i++) zaps[i].active = false;

    for (int i = 0; i < MAX_MONSTERS; i++) monsters[i].active = false;
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
    for (int i = 0; i < MAX_DROPS; i++) drops[i].active = false;
    for (int i = 0; i < MAX_HEALS; i++) heals[i].active = false;

    // environment placement 
    for (int i = 0; i < ENV_USED; i++) {
        Vector2 p;
        float minDist = 140.0f;            
        float safeDistFromPlayer = 520.0f;  
        int tries = 0;
        do {
            p = (Vector2){(float)GetRandomValue(80, WORLD_SIZE-80), (float)GetRandomValue(80, WORLD_SIZE-80)};
            tries++;
            if (Vector2Distance(p, playerPos) < safeDistFromPlayer) continue;
            bool ok = true;
            for (int j = 0; j < i; j++) {
                if (Vector2Distance(p, environment[j].pos) < minDist) { ok = false; break; }
            }
            if (ok) break;
        } while (tries < 220);

        environment[i].pos = p;
        int r = GetRandomValue(0, 99);
        if (r < 72) environment[i].type = ENV_ROCK;
        else if (r < 92) environment[i].type = ENV_GRASS;
        else environment[i].type = ENV_LAVA;
        environment[i].size = (float)GetRandomValue(60, 150); 
    }
    

    currentBossPtr = NULL;
}

void SpawnParticles(Vector2 pos, Color col, int count, float speedBase) {
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) if (!particles[i].active) {
        particles[i].active = true;
        particles[i].pos = pos;
        particles[i].vel = (Vector2){
            (float)GetRandomValue(-100, 100)/100.0f * speedBase,
            (float)GetRandomValue(-100, 100)/100.0f * speedBase
        };
        particles[i].life = 0.8f;
        particles[i].col = col;
        spawned++;
    }
}

void Shoot(Vector2 start, Vector2 dir, bool isEnemy, float radius) {
    for (int i = 0; i < MAX_BULLETS; i++) if (!bullets[i].active) {
        bullets[i].pos = start;

        // normalize dir 
        float len = Vector2Length(dir);
        Vector2 nd = (len > 0.0001f) ? Vector2Scale(dir, 1.0f/len) : (Vector2){1,0};

        // speed
        float spd = isEnemy ? 5.5f : 22.0f;
        // if dir length encodes speed multiplier, apply
        if (len > 1.05f) spd *= len;

        bullets[i].speed = Vector2Scale(nd, spd);
        bullets[i].active = true;
        bullets[i].isEnemy = isEnemy;
        bullets[i].radius = radius;

        //bullet lifetime  +player bullets life→lower
        bullets[i].life = isEnemy ? 1.25f : 1.35f;
        break;
    }
}

void SpawnMonster(MobType type, bool isBoss, Vector2 spawnPos) {
    for (int i = 0; i < MAX_MONSTERS; i++) if (!monsters[i].active) {
        monsters[i].pos = (spawnPos.x == 0 && spawnPos.y == 0)
            ? Vector2Add(playerPos, (Vector2){(float)GetRandomValue(-1000, 1000), (float)GetRandomValue(-1000, 1000)})
            : spawnPos;

        for (int s = 0; s < 5; s++) monsters[i].segments[s] = monsters[i].pos;

        monsters[i].active = true;
        monsters[i].type = isBoss ? M_BOSS : type;

        float scale = 1.0f + (playerLevel - 1) * 0.12f;
        monsters[i].maxHp = (int)((isBoss ? (800 + playerLevel * 250) : (type == M_MINION ? 20 : (50 + playerLevel * 6))) * scale);
        monsters[i].hp = monsters[i].maxHp;

        monsters[i].speed = ((type == M_WORM) ? 3.2f : (type == M_TANK) ? 0.8f : 2.5f) + fminf(2.0f, playerLevel * 0.02f);
        if (type == M_MINION) monsters[i].speed += 1.5f;

        Color palette[] = { SKYBLUE, GRAY, WHITE, ORANGE, PINK, LIME, MAGENTA, RED, MAROON };
        monsters[i].color = palette[monsters[i].type];

        monsters[i].stateTimer = 0;
        monsters[i].state = 0;
        monsters[i].targetPos = playerPos;

        if (isBoss) {
            currentBossPtr = &monsters[i];
            monsters[i].color = (playerLevel >= 60) ? GOLD : (playerLevel >= 40) ? PURPLE : RED;
        }
        break;
    }
}

void DrawMonsterVisual(Monster m) {
    if (!m.active) return;
    float t = (float)GetTime();
    switch (m.type) {
        case M_NORMAL: // Spider
            DrawCircleV(m.pos, 22, m.color);
            for (int i = 0; i < 8; i++) {
                Vector2 legEnd = { m.pos.x + cosf(t*4 + i*0.78f)*35, m.pos.y + sinf(t*4 + i*0.78f)*35 };
                DrawLineEx(m.pos, legEnd, 3, m.color);
            }
            break;
        case M_TANK: // Golem
            DrawRectanglePro((Rectangle){m.pos.x, m.pos.y, 60, 60}, (Vector2){30, 30}, 0, m.color);
            DrawRectangleV((Vector2){m.pos.x-45, m.pos.y-15}, (Vector2){15, 45}, m.color);
            DrawRectangleV((Vector2){m.pos.x+30, m.pos.y-15}, (Vector2){15, 45}, m.color);
            DrawCircleV(m.pos, 10, RED);
            break;
        case M_GHOST:
            DrawCircleV(m.pos, 22 + sinf(t*4)*3, Fade(m.color, 0.5f));
            for (int i = 0; i < 3; i++)
                DrawCircleV(Vector2Add(m.pos, (Vector2){sinf(t*2+i)*10, 20+i*10}), 10-i*3, Fade(m.color, 0.4f-i*0.1f));
            break;
        case M_CHARGER:
            DrawPoly(m.pos, 3, 30, Vector2Angle((Vector2){0,0}, Vector2Subtract(playerPos, m.pos))*RAD2DEG, m.color);
            if (m.state == 1) DrawCircleLinesV(m.pos, 40, GOLD);
            DrawCircleV(Vector2Add(m.pos, Vector2Scale(Vector2Normalize(Vector2Subtract(playerPos, m.pos)), 20)), 8, BLACK);
            break;
        case M_SHOOTER: {
            float p = 22 + sinf(t*12)*6;
            DrawEllipse(m.pos.x, m.pos.y, p, p+8, m.color);
            DrawCircleV(m.pos, p*0.5f, BLACK);
        } break;
        case M_WORM:
            for (int i = 4; i >= 0; i--) DrawCircleV(m.segments[i], 20-i*3, m.color);
            break;
        case M_RING:
            DrawCircleLinesV(m.pos, 28, m.color);
            DrawCircleLinesV(m.pos, 18, Fade(WHITE, 0.6f));
            break;
        case M_BOSS:
            DrawPoly(m.pos, (playerLevel/10)+3, 120 + sinf(t*5)*10, t*40, m.color);
            DrawCircleV(m.pos, 90, BLACK);
            DrawCircleV(m.pos, 70, m.color);
            // telegraph for dash
            if (m.state == 2) DrawCircleLinesV(m.pos, 150, Fade(WHITE, 0.25f));
            break;
        default:
            DrawCircleV(m.pos, 22, m.color);
            break;
    }
}

void DrawDetailedHelp() {
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.98f));
    DrawText("SURVIVAL PROTOCOL: HELP MENU", GetScreenWidth()/2 - 260, 25, 35, GOLD);
    int y = 75; int sp = 18;

    DrawText("1. OBJECTIVE & CONTROLS", 40, y, 22, GOLD); y+=30;
    DrawText("Goal: Defeat Bosses every 20 levels. Slay the Level 100 Boss to clear.", 60, y, 16, WHITE); y+=sp;
    DrawText("Movement: [WASD]. Combat: [SPACE] fire. HOLD [SPACE] for melee charge spin.", 60, y, 16, WHITE); y+=sp;
    DrawText("Menu: [P] Pause/Help. Swap: [TAB]. Special: [Left CTRL + W] at full gauge.", 60, y, 16, WHITE); y+=35;

    DrawText("2. WEAPON SYSTEMS & LEVELING", 40, y, 22, SKYBLUE); y+=30;
    DrawText("Levels: Picking up the SAME weapon (TAB) increases its level (Power/Bullets).", 60, y, 16, WHITE); y+=sp;
    DrawText("- Ranged bullets can CANCEL enemy bullets now.", 60, y, 16, GRAY); y+=sp;
    DrawText("- Mob bullets vanish faster to reduce screen clutter.", 60, y, 16, GRAY); y+=35;

    DrawText("3. SKILL CARDS & EVOLUTION", 40, y, 22, LIME); y+=30;
    DrawText("Every 5 levels, pick a card. Pick the same skill 3 times for EVOLUTION.", 60, y, 16, WHITE); y+=sp;
    DrawText("- REGEN: Heals HP. Lv3 Evolution grants +1 Max HP every heal tick.", 60, y, 16, GRAY); y+=sp;
    DrawText("- FROST: Freezing aura. Lv3 Evolution STOPS nearby enemies completely.", 60, y, 16, GRAY); y+=sp;
    DrawText("- LTNG: Triggers lightning chain between multiple enemies.", 60, y, 16, GRAY); y+=35;

    DrawText("4. ENEMY BESTIARY", 40, y, 22, RED); y+=30;
    DrawText("- Shooter/Ring bullet patterns remain, but bullets do not linger.", 60, y, 16, GRAY); y+=sp;
    DrawText("- BOSS has more phases: radial, pillar+summon, aimed+spiral+dashes.", 60, y, 16, GOLD); y+=35;

    DrawText("5. STAGE MECHANICS", 40, y, 22, ORANGE); y+=30;
    DrawText("Periodic Heal: Heal items also spawn on whole map every few seconds.", 60, y, 16, WHITE); y+=sp;
    DrawText("Environment blocks reduced a bit for smoother navigation.", 60, y, 16, WHITE);

    DrawText("PRESS [P] OR [ESC] TO RESUME", GetScreenWidth()/2 - 180, GetScreenHeight() - 40, 18, GRAY);
}

int main(void) {
    InitWindow(1280, 720, "CIRCLE WARS - ULTIMATE");
    InitAudioDevice();

    GameAudio audio;
    audio.mainBgm = LoadMusicStream("resources/audio/Phantom_Protocol.mp3");
    audio.seSlash = LoadSound("resources/audio/se_slash.mp3");
    audio.seDamage = LoadSound("resources/audio/se_damage.mp3");
    audio.seHeal = LoadSound("resources/audio/se_heal.mp3");
    audio.seShoot = LoadSound("resources/audio/se_shoot.mp3");

    PlayMusicStream(audio.mainBgm);
    SetMusicVolume(audio.mainBgm, 0.35f);

    SetTargetFPS(60);
    srand((unsigned)time(NULL));

    ResetGame();

    Camera2D camera = { 0 };
    camera.offset = (Vector2){640, 360};
    camera.target = playerPos;
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    while (!WindowShouldClose()) {
        UpdateMusicStream(audio.mainBgm);
        float dt = GetFrameTime();

        // TITLE / HELP state transitions
        if (gameState == GS_TITLE) {
            // title buttons
            int sw = GetScreenWidth();
            int sh = GetScreenHeight();

            Rectangle btnStart = { sw/2.0f - 120, sh*0.56f, 240, 44 };
            Rectangle btnHelp  = { sw/2.0f - 120, sh*0.64f, 240, 44 };
            Rectangle btnQuit  = { sw/2.0f - 120, sh*0.72f, 240, 44 };

            if (IsKeyPressed(KEY_ENTER)) gameState = GS_GAMEPLAY;

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 mp = GetMousePosition();
                if (CheckCollisionPointRec(mp, btnStart)) gameState = GS_GAMEPLAY;
                else if (CheckCollisionPointRec(mp, btnHelp)) gameState = GS_HELP;
                else if (CheckCollisionPointRec(mp, btnQuit)) CloseWindow();
            }
        } else if (gameState == GS_HELP) {
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) gameState = GS_GAMEPLAY;
        } else {
            // gameplay
            if (IsKeyPressed(KEY_P)) paused = !paused;

            if (!gameOver && !paused && !levelUpPending && !gachaPending) {

                if (damageSoundTimer > 0) damageSoundTimer -= dt;
                if (screenShake > 0) screenShake -= dt * 40.0f; else screenShake = 0;
                if (bossIntroTimer > 0) bossIntroTimer -= dt;

                //Periodic heal spawn on whole map
                healSpawnTimer += dt;
                if (healSpawnTimer >= healSpawnInterval) {
                    SpawnHealAtRandomMap();
                    healSpawnTimer = 0.0f;
                }

                //REGEN (Lv3 evolution)
                if (skillLv[SKILL_REGEN] > 0) {
                    regenTimer += dt;
                    float tick = 1.25f - 0.12f * (skillLv[SKILL_REGEN]-1);
                    if (tick < 0.65f) tick = 0.65f;

                    if (regenTimer >= tick) {
                        int healAmt = 2 + skillLv[SKILL_REGEN];
                        playerHP = (int)fminf((float)playerMaxHP, (float)playerHP + healAmt);

                        if (skillLv[SKILL_REGEN] >= 3) {
                            playerMaxHP += 1;
                            regenEvolutionStacks++;
                        }
                        regenTimer = 0.0f;
                    }
                }

                // LIGHTNING
                if (skillLv[SKILL_LIGHTNING] > 0) {
                    lightningTimer += dt;
                    float lTick = 2.2f - 0.25f * (skillLv[SKILL_LIGHTNING]-1);
                    if (lTick < 0.9f) lTick = 0.9f;

                    if (lightningTimer >= lTick) {
                        int chains = 2 + skillLv[SKILL_LIGHTNING];
                        float jump = 280.0f + 40.0f * skillLv[SKILL_LIGHTNING];
                        int dmg = 80 + 50 * skillLv[SKILL_LIGHTNING] + bonusDamage/2;
                        TriggerLightning(playerPos, chains, jump, dmg);
                        lightningTimer = 0.0f;
                    }
                }

                // Move logic
                Vector2 moveDir = {
                    (float)(IsKeyDown(KEY_D) - IsKeyDown(KEY_A)),
                    (float)(IsKeyDown(KEY_S) - IsKeyDown(KEY_W))
                };

                if (Vector2Length(moveDir) > 0) {
                    SpawnParticles(playerPos, Fade(SKYBLUE, 0.5f), 1, 0.2f);

                    Vector2 next = Vector2Add(playerPos,
                        Vector2Scale(Vector2Normalize(moveDir), (6.0f + playerLevel*0.12f + bonusSpeed)));
                    // clamp movement within world bounds
                    next.x = fminf(WORLD_MAX, fmaxf(WORLD_MIN, next.x));
                    next.y = fminf(WORLD_MAX, fmaxf(WORLD_MIN, next.y));
                    bool hit = false;
                    for (int i = 0; i < ENV_USED; i++) {
                        if (environment[i].type == ENV_ROCK &&
                            CheckCollisionCircles(next, 24, environment[i].pos, environment[i].size*0.42f)) hit = true;

                        if (environment[i].type == ENV_LAVA &&
                            CheckCollisionCircles(playerPos, 15, environment[i].pos, environment[i].size*0.5f)) {
                            playerHP -= 1;
                            if (damageSoundTimer <= 0) { PlaySound(audio.seDamage); damageSoundTimer = 1.0f; }
                            if (playerHP <= 0) gameOver = true;
                        }
                    }
                    if (!hit) playerPos = next;
                    lastMove = Vector2Normalize(moveDir);
                }

                // Camera follow
                camera.target = playerPos;
                camera.offset = (Vector2){ GetScreenWidth()/2.0f, GetScreenHeight()/2.0f };

                // Dynamic Items
                int ni = 0;
                for (int i = 0; i < MAX_DROPS; i++)
                    if (drops[i].active && Vector2Distance(playerPos, drops[i].pos) < 1400) ni++;

                if (ni < 15) {
                    for (int i = 0; i < MAX_DROPS; i++) if (!drops[i].active) {
                        float a = (float)GetRandomValue(0, 360) * DEG2RAD;
                        drops[i].pos = Vector2Add(playerPos, (Vector2){cosf(a)*950, sinf(a)*950});
                        drops[i].weaponIdx = GetRandomValue(0, 4);
                        drops[i].active = true;
                        break;
                    }
                    for (int i = 0; i < MAX_HEALS; i++) if (!heals[i].active) {
                        float a = (float)GetRandomValue(0, 360) * DEG2RAD;
                        heals[i].pos = Vector2Add(playerPos, (Vector2){cosf(a)*950, sinf(a)*950});
                        heals[i].active = true;
                        break;
                    }
                }

                // Heal pickup
                for (int i = 0; i < MAX_HEALS; i++)
                    if (heals[i].active && CheckCollisionCircles(playerPos, 30, heals[i].pos, 22)) {
                        playerHP = (int)fminf((float)playerMaxHP, (float)playerHP + 35);
                        heals[i].active = false;
                        PlaySound(audio.seHeal);
                    }

                // Weapon TAB swap
                for (int i = 0; i < MAX_DROPS; i++)
                    if (drops[i].active && Vector2Distance(playerPos, drops[i].pos) < 80) {
                        if (IsKeyPressed(KEY_TAB)) {
                            if (strcmp(currentWeapon.name, weaponsList[drops[i].weaponIdx].name) == 0) weaponLevel++;
                            else { currentWeapon = weaponsList[drops[i].weaponIdx]; weaponLevel = 1; }
                            drops[i].active = false;
                            PlaySound(audio.seHeal);
                        }
                    }

                // ATTACK 
                if (currentWeapon.type != ATK_MELEE && IsKeyDown(KEY_SPACE)) {
                    fireTimer += dt;
                    if (fireTimer >= currentWeapon.fireRate) {
                        PlaySound(audio.seShoot);

                        //  bullet cutting
                        CancelEnemyBulletsInRadius(playerPos, 110.0f + weaponLevel*6.0f, 8 + weaponLevel);

                        int bC = (currentWeapon.type == ATK_CONTINUOUS) ? weaponLevel : 8 + (weaponLevel * 2);
                        for (int i = 0; i < bC; i++) {
                            float ang = (currentWeapon.type == ATK_CONTINUOUS)
                                ? (GetTime()*15.0f + i)
                                : (i * (360.0f/bC) * DEG2RAD);
                            Shoot(playerPos, (Vector2){cosf(ang), sinf(ang)}, false, 14.0f + weaponLevel * 2);
                        }
                        fireTimer = 0;
                    }
                } else if (currentWeapon.type == ATK_MELEE) {
                    if (IsKeyDown(KEY_SPACE)) chargeTimer += dt;
                    if (IsKeyReleased(KEY_SPACE)) {
                        float mult = (chargeTimer > 1.0f && currentWeapon.range < 350) ? 2.5f : 1.0f;
                        attackVisualTimer = 0.4f;
                        PlaySound(audio.seSlash);
                        screenShake = 20.0f * mult;

                        float range = (currentWeapon.range + bonusRange) * rangeMul * (mult > 1.0f ? 1.8f : 1.0f);

                        // melee clears enemy bullets
                        for (int i = 0; i < MAX_BULLETS; i++)
                            if (bullets[i].active && bullets[i].isEnemy && Vector2Distance(playerPos, bullets[i].pos) < range)
                                bullets[i].active = false;

                        // melee hits monsters
                        for (int i = 0; i < MAX_MONSTERS; i++)
                            if (monsters[i].active && Vector2Distance(playerPos, monsters[i].pos) < range) {
                                monsters[i].hp -= (int)((currentWeapon.damage + bonusDamage) * weaponLevel * mult);
                                if (monsters[i].hp <= 0) {
                                    monsters[i].active = false;
                                    playerExp += 55;
                                    if (monsters[i].type == M_BOSS) {
                                        bossActive = false;
                                        if (playerLevel >= 100) gameState = GS_CLEAR;
                                        playerLevel++;
                                        for (int j = 0; j < 60; j++) SpawnMonster(M_NORMAL, false, (Vector2){0,0});
                                    }
                                }
                            }
                        chargeTimer = 0;
                    }
                }

                //Bullet hit logic
                for (int i = 0; i < MAX_BULLETS; i++) if (bullets[i].active) {

                    // lifetime update
                    bullets[i].life -= dt;
                    if (bullets[i].life <= 0.0f) {
                        bullets[i].active = false;
                        continue;
                    }

                    bullets[i].pos = Vector2Add(bullets[i].pos, bullets[i].speed);

                    if (!bullets[i].isEnemy) {
                        // player bullet cancels enemy bullet
                        for (int k = 0; k < MAX_BULLETS; k++) {
                            if (!bullets[k].active || !bullets[k].isEnemy) continue;
                            float rr = bullets[i].radius + bullets[k].radius;
                            if (CheckCollisionCircles(bullets[i].pos, bullets[i].radius, bullets[k].pos, bullets[k].radius)) {
                                Vector2 mid = Vector2Lerp(bullets[i].pos, bullets[k].pos, 0.5f);
                                SpawnParticles(mid, Fade(WHITE, 0.7f), 6, 2.4f);
                                bullets[i].active = false;
                                bullets[k].active = false;
                                break;
                            }
                        }
                        if (!bullets[i].active) continue;

                        // then hit monsters
                        for (int j = 0; j < MAX_MONSTERS; j++) {
                            if (monsters[j].active && CheckCollisionCircles(bullets[i].pos, bullets[i].radius, monsters[j].pos, 24)) {
                                monsters[j].hp -= (currentWeapon.damage + bonusDamage) * weaponLevel;
                                bullets[i].active = false;
                                if (monsters[j].hp <= 0) {
                                    monsters[j].hp = 0;
                                    monsters[j].active = false;
                                    playerExp += 30;     //10→20or30
                                    SpawnParticles(monsters[j].pos, RED, 5, 1.0f);

                                    if (monsters[j].type == M_BOSS) {
                                        bossActive = false;
                                        currentBossPtr = NULL;

                                        if (playerLevel >= 100) gameState = GS_CLEAR;
                                        else {
                                            for (int k = 0; k < 60; k++) SpawnMonster(M_NORMAL, false, (Vector2){0,0});
                                        }
                                        playerLevel++;
                                    }
                                }

                                break;
                            }
                        }
                    } else {
                        // enemy bullet hit player
                        if (CheckCollisionCircles(bullets[i].pos, bullets[i].radius, playerPos, 18)) {
                            playerHP -= 6;
                            bullets[i].active = false;
                            if (playerHP <= 0) gameOver = true;
                        }
                    }

                    // distance cull 
                    float distCull = bullets[i].isEnemy ? (900.0f * rangeMul) : (1500.0f * rangeMul);
                    if (Vector2Distance(playerPos, bullets[i].pos) > distCull) bullets[i].active = false;
                }

                // Boss and Spawning
                if (playerLevel % 20 == 0 && !bossActive) {
                    bossActive = true;
                    bossIntroTimer = 3.5f;
                    SpawnMonster(M_BOSS, true, (Vector2){0,0});
                }

                // early levels spawn fewer mobs
                int mobCap;
                if (playerLevel <= 5) mobCap = 10;
                else if (playerLevel <= 10) mobCap = 18;
                else if (playerLevel <= 20) mobCap = 30;
                else mobCap = 45 + (playerLevel/2);

                float spawnInterval;
                if (playerLevel <= 5) spawnInterval = 0.35f;
                else if (playerLevel <= 10) spawnInterval = 0.22f;
                else if (playerLevel <= 20) spawnInterval = 0.14f;
                else spawnInterval = 0.09f;

                mobSpawnTimer += dt;
                if (mobSpawnTimer >= spawnInterval) {
                    mobSpawnTimer = 0.0f;
                    int active = CountActiveMobs();
                    if (active < mobCap) {
                        // weighted mob types
                        int roll = GetRandomValue(0, 99);
                        MobType t = M_NORMAL;
                        if (playerLevel < 8) {
                            t = (roll < 85) ? M_NORMAL : M_SHOOTER;
                        } else if (playerLevel < 20) {
                            if (roll < 55) t = M_NORMAL;
                            else if (roll < 68) t = M_GHOST;
                            else if (roll < 80) t = M_CHARGER;
                            else if (roll < 92) t = M_SHOOTER;
                            else t = M_RING;
                        } else {
                            if (roll < 35) t = M_NORMAL;
                            else if (roll < 48) t = M_GHOST;
                            else if (roll < 60) t = M_CHARGER;
                            else if (roll < 72) t = M_SHOOTER;
                            else if (roll < 84) t = M_RING;
                            else if (roll < 92) t = M_WORM;
                            else t = M_TANK;
                        }
                        SpawnMonster(t, false, (Vector2){0,0});
                    }
                }

                // Monsters AI update 
                for (int i = 0; i < MAX_MONSTERS; i++) {
                    if (!monsters[i].active) continue;
                    Monster *mob = &monsters[i];
                    float mS = mob->speed;

                    // FROST aura slows
                    if (skillLv[SKILL_FROST] >= 1 && Vector2Distance(playerPos, mob->pos) < 280) {
                        mS = (skillLv[SKILL_FROST] >= 3) ? 0.0f : mS * 0.3f;
                    }

                    switch (mob->type) {
                        case M_WORM:
                            for (int j = 4; j > 0; j--) mob->segments[j] = Vector2Lerp(mob->segments[j], mob->segments[j-1], 0.25f);
                            mob->segments[0] = mob->pos;
                            mob->pos = Vector2MoveTowards(mob->pos, playerPos, mS);
                            break;

                        case M_TANK:
                            mob->stateTimer += dt;
                            if (mob->state == 0 && mob->stateTimer > 3.0f) {
                                mob->state = 1;
                                mob->stateTimer = 0;
                                mob->targetPos = playerPos;
                            }
                            if (mob->state == 1 && mob->stateTimer > 1.0f) {
                                mob->pos = mob->targetPos;
                                Explode(mob->pos, 100, 20);
                                mob->state = 0;
                                mob->stateTimer = 0;
                            }
                            if (mob->state == 0) mob->pos = Vector2MoveTowards(mob->pos, playerPos, mS);
                            break;

                        case M_GHOST: {
                            mob->stateTimer += dt;
                            Vector2 toP = Vector2Subtract(playerPos, mob->pos);
                            Vector2 dir = Vector2Normalize(toP);
                            Vector2 side = (Vector2){-dir.y, dir.x};
                            float wob = sinf(GetTime()*2.5f) * 0.8f;
                            Vector2 move = Vector2Add(dir, Vector2Scale(side, wob));
                            mob->pos = Vector2Add(mob->pos, Vector2Scale(Vector2Normalize(move), mS));

                            if (mob->stateTimer > 3.8f) {
                                mob->stateTimer = 0;
                                Vector2 back = Vector2Scale(Vector2Normalize(Vector2Subtract(mob->pos, playerPos)), 220);
                                mob->pos = Vector2Add(playerPos, back);
                                SpawnParticles(mob->pos, Fade(WHITE, 0.6f), 25, 3.5f);
                            }
                        } break;

                        case M_CHARGER: {
                            mob->stateTimer += dt;
                            float dist = Vector2Distance(mob->pos, playerPos);

                            if (mob->state == 0) {
                                if (dist < 260) mob->pos = Vector2MoveTowards(mob->pos, playerPos, -mS);
                                else if (mob->stateTimer > 1.2f) { mob->state = 1; mob->stateTimer = 0; mob->targetPos = playerPos; }
                            } else {
                                Vector2 dir = Vector2Normalize(Vector2Subtract(mob->targetPos, mob->pos));
                                mob->pos = Vector2Add(mob->pos, Vector2Scale(dir, mS * 4.0f));
                                if (mob->stateTimer > 0.45f) { mob->state = 0; mob->stateTimer = 0; }
                            }
                        } break;

                        case M_SHOOTER: {
                            mob->stateTimer += dt;
                            float dist = Vector2Distance(mob->pos, playerPos);

                            if (dist < 360) mob->pos = Vector2MoveTowards(mob->pos, playerPos, -mS);
                            else if (dist > 650) mob->pos = Vector2MoveTowards(mob->pos, playerPos, mS);

                            // reduced frequency + bullets die faster → much less clutter 
                            float shootInt = (playerLevel < 12) ? 1.15f : 0.95f;
                            if (fmodf(mob->stateTimer, shootInt) < 0.05f) {
                                Vector2 dir = Vector2Normalize(Vector2Subtract(playerPos, mob->pos));
                                Shoot(mob->pos, dir, true, 7.0f);
                                Vector2 dirL = Vector2Rotate(dir, -0.18f);
                                Vector2 dirR = Vector2Rotate(dir,  0.18f);
                                Shoot(mob->pos, dirL, true, 6.0f);
                                Shoot(mob->pos, dirR, true, 6.0f);
                            }
                        } break;

                        case M_RING: {
                            Vector2 toP = Vector2Subtract(playerPos, mob->pos);
                            float dist = Vector2Length(toP);
                            Vector2 dir = Vector2Normalize(toP);
                            Vector2 tangent = (Vector2){-dir.y, dir.x};

                            float targetR = 450.0f;
                            float radial = (dist - targetR) * 0.02f;
                            Vector2 move = Vector2Add(Vector2Scale(tangent, 1.2f), Vector2Scale(dir, radial));
                            mob->pos = Vector2Add(mob->pos, Vector2Scale(Vector2Normalize(move), mS * 1.4f));

                            mob->stateTimer += dt;
                            float shootInt = (playerLevel < 12) ? 1.35f : 1.10f;
                            if (fmodf(mob->stateTimer, shootInt) < 0.05f) {
                                Shoot(mob->pos, Vector2Normalize(Vector2Subtract(playerPos, mob->pos)), true, 6.0f);
                            }
                        } break;

                        case M_BOSS: {
                            // more boss variety (3-state cycle)
                            mob->stateTimer += dt;

                            // State 0: radial ring 
                            if (mob->state == 0) {
                                mob->pos = Vector2MoveTowards(mob->pos, playerPos, 1.25f);

                                float sInt = (playerLevel <= 20) ? 0.85f : 0.50f;
                                if (fmodf(mob->stateTimer, sInt) < 0.05f) {
                                    float a = GetTime()*4.0f;
                                    int bL = 10; // reduced from 12
                                    for (int j = 0; j < bL; j++) {
                                        Shoot(mob->pos, (Vector2){cosf(a + j*(6.28f/bL)), sinf(a + j*(6.28f/bL))}, true, 6.0f);
                                    }
                                    // boss bullets can live a bit longer than mobs for pattern 
                                }

                                if (mob->stateTimer > 7.2f) { mob->state = 1; mob->stateTimer = 0; }
                            }
                            // State 1: pillars + minion summon
                            else if (mob->state == 1) {
                                if (fmodf(mob->stateTimer, 0.75f) < 0.05f) {
                                    pillarPos[0] = Vector2Add(playerPos, (Vector2){(float)GetRandomValue(-200,200),(float)GetRandomValue(-200,200)});
                                    pillarTimer = 1.2f;
                                    screenShake = 20.0f;
                                }
                                if (fmodf(mob->stateTimer, 1.25f) < 0.05f) {
                                    for (int k = 0; k < 3; k++) {
                                        float a = (float)GetRandomValue(0, 360) * DEG2RAD;
                                        Vector2 sp = Vector2Add(playerPos, (Vector2){cosf(a)*600, sinf(a)*600});
                                        SpawnMonster(M_MINION, false, sp);
                                    }
                                }
                                if (mob->stateTimer > 4.5f) { mob->state = 2; mob->stateTimer = 0; mob->targetPos = playerPos; }
                            }
                            // State 2: aimed burst + spiral + dash explode
                            else {
                                float d = Vector2Distance(mob->pos, playerPos);

                                if (d < 420) {
                                    // 後退をやめて、横にずれる
                                    Vector2 dir = Vector2Normalize(Vector2Subtract(playerPos, mob->pos));
                                    Vector2 tan = (Vector2){ -dir.y, dir.x };
                                    mob->pos = Vector2Add(mob->pos, Vector2Scale(tan, 1.1f));
                                } else if (d > 820) {
                                    mob->pos = Vector2MoveTowards(mob->pos, playerPos, 1.0f);
                                }

                                // aimed burst every ~0.9s
                                if (fmodf(mob->stateTimer, 0.95f) < 0.05f) {
                                    Vector2 dir = Vector2Normalize(Vector2Subtract(playerPos, mob->pos));
                                    BossShootAimedBurst(mob, dir);
                                }

                                // spiral ticks 
                                if (fmodf(mob->stateTimer, 0.12f) < 0.03f) {
                                    BossShootSpiral(mob, GetTime(), 2, 1.20f);
                                }

                                // dash telegraph then execute
                                if (mob->stateTimer > 2.2f && mob->stateTimer < 3.2f) {
                                    // telegraph by staying and flashing
                                    if (fmodf(mob->stateTimer, 0.18f) < 0.04f) {
                                        SpawnParticles(mob->pos, Fade(WHITE, 0.35f), 6, 1.2f);
                                    }
                                }
                                if (mob->stateTimer >= 3.2f && mob->stateTimer < 3.9f) {
                                    // dash
                                    BossDashExplode(mob);
                                }
                                if (mob->stateTimer >= 3.9f && mob->stateTimer < 4.0f) {
                                    Explode(mob->pos, 120, 25);
                                    //  clear some bullets to reset pattern
                                    CancelEnemyBulletsInRadius(mob->pos, 220.0f, 180);
                                }

                                if (mob->stateTimer > 5.2f) { mob->state = 0; mob->stateTimer = 0; }
                            }
                        } break;

                        default:
                            mob->pos = Vector2MoveTowards(mob->pos, playerPos, mS);
                            break;
                    }

                    // Touch damage
                    float mobRad = (mob->type == M_BOSS) ? 85.0f : 22.0f;
                    if (CheckCollisionCircles(playerPos, 18, mob->pos, mobRad)) {
                        playerHP -= 1;
                        if (damageSoundTimer <= 0) { PlaySound(audio.seDamage); damageSoundTimer = 0.8f; }
                        if (playerHP <= 0) { playerHP = 0; gameOver = true; }
                    }
                }

                // Particles update
                for (int i = 0; i < MAX_PARTICLES; i++) if (particles[i].active) {
                    particles[i].pos = Vector2Add(particles[i].pos, particles[i].vel);
                    particles[i].life -= dt;
                    if (particles[i].life <= 0) particles[i].active = false;
                }

                // Zap update
                for (int i = 0; i < MAX_ZAPS; i++) if (zaps[i].active) {
                    zaps[i].life -= dt;
                    if (zaps[i].life <= 0) zaps[i].active = false;
                }

                // Level up
                if (playerExp >= playerMaxExp) {
                    playerLevel++;
                    playerExp = 0;
                    playerMaxExp += 45;
                    if (playerLevel % 5 == 0) {
                        levelUpPending = true;
                        for (int i = 0; i < 3; i++) choicePool[i] = (SkillType)GetRandomValue(0, 9);
                    }
                }

                // Special gauge 
                if (specialGauge < 100.0f) specialGauge += dt * 2.5f * specialGainMul;

            } else if (gameOver && IsKeyPressed(KEY_ENTER)) {
                ResetGame();
            } else if (levelUpPending) {
                int ch = -1;
                if (IsKeyPressed(KEY_ONE)) ch = 0;
                if (IsKeyPressed(KEY_TWO)) ch = 1;
                if (IsKeyPressed(KEY_THREE)) ch = 2;

                if (ch != -1) {
                    SkillType s = choicePool[ch];
                    skillLv[s]++;

                    if (s == SKILL_ATK) bonusDamage += 35;
                    else if (s == SKILL_SPD) bonusSpeed += 1.3f;
                    else if (s == SKILL_RANGE) { rangeMul += 0.12f; bonusRange += 25; }
                    else if (s == SKILL_SPECIAL) { specialGainMul += 0.25f; }
                    else if (s == SKILL_MAXHP) { playerMaxHP += 25; playerHP += 25; }
                    else if (s == SKILL_GACHA) { gachaPending = true; gachaResultIdx = GetRandomValue(1, 4); }
                    else if (s == SKILL_WEAPON_UP) { weaponLevel++; }

                    levelUpPending = false;
                }
            } else if (gachaPending) {
                if (IsKeyPressed(KEY_Y)) {
                    if (strcmp(currentWeapon.name, weaponsList[gachaResultIdx].name) == 0) weaponLevel++;
                    else { currentWeapon = weaponsList[gachaResultIdx]; weaponLevel = 1; }
                    gachaPending = false;
                }
                if (IsKeyPressed(KEY_N)) gachaPending = false;
            }

            // Special activation (CTRL+W)
            if (specialGauge >= 100.0f && IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_W)) {
                specialGauge = 0;
                specialFlash = 0.5f;
                PlaySound(audio.seSlash);
                screenShake = 40.0f;

                // clears bullets
                CancelEnemyBulletsInRadius(playerPos, 1200.0f, 9999);

                for (int i = 0; i < MAX_MONSTERS; i++) if (monsters[i].active) {
                    monsters[i].hp -= 2000;
                    if (monsters[i].hp <= 0) monsters[i].active = false;
                }
            }
        } 

        // DRAW
    
        BeginDrawing();
        ClearBackground(GetColor(0x050508FF));

        // Screen shake 
        Vector2 shakeOff = {0};
        if (screenShake > 0) {
            shakeOff.x = (float)GetRandomValue(-100, 100)/100.0f * screenShake;
            shakeOff.y = (float)GetRandomValue(-100, 100)/100.0f * screenShake;
        }
        camera.offset = Vector2Add((Vector2){ GetScreenWidth()/2.0f, GetScreenHeight()/2.0f }, shakeOff);

        BeginMode2D(camera);

            // Environment draw
            for (int i = 0; i < ENV_USED; i++) {
                if (environment[i].type == ENV_ROCK) {
                    DrawPoly(environment[i].pos, 6, environment[i].size, 0, DARKGRAY);
                } else if (environment[i].type == ENV_LAVA) {
                    DrawCircleV(environment[i].pos, environment[i].size, Fade(ORANGE, 0.4f));
                    DrawCircleV(environment[i].pos, environment[i].size*0.7f, Fade(RED, 0.5f));
                } else if (environment[i].type == ENV_GRASS) {
                    DrawCircleV(environment[i].pos, environment[i].size*0.8f, Fade(GREEN, 0.15f));
                    DrawCircleLinesV(environment[i].pos, environment[i].size*0.8f, Fade(GREEN, 0.25f));
                }
            }

            // Lightning zap lines
            for (int i = 0; i < MAX_ZAPS; i++) if (zaps[i].active) {
                float a = zaps[i].life / 0.12f;
                DrawLineEx(zaps[i].a, zaps[i].b, 3, Fade(SKYBLUE, a));
            }

            // Pillar attack
            if (pillarTimer > 0) {
                pillarTimer -= dt;
                DrawCircleLinesV(pillarPos[0], 150 * (1.2f - pillarTimer), RED);
                if (pillarTimer < 0.2f) Explode(pillarPos[0], 120, 20);
            }

            // Heals draw
            for (int i = 0; i < MAX_HEALS; i++) if (heals[i].active) {
                DrawCircleV(heals[i].pos, 22, WHITE);
                DrawRectangle((int)(heals[i].pos.x-12), (int)(heals[i].pos.y-4), 24, 8, RED);
                DrawRectangle((int)(heals[i].pos.x-4), (int)(heals[i].pos.y-12), 8, 24, RED);
            }

            // Drops draw
            for (int i = 0; i < MAX_DROPS; i++) if (drops[i].active) {
                Color dc = weaponsList[drops[i].weaponIdx].color;
                DrawRectangleV(Vector2Subtract(drops[i].pos, (Vector2){22,18}), (Vector2){44,28}, dc);
                DrawRectangleLinesEx((Rectangle){drops[i].pos.x-22, drops[i].pos.y-18, 44, 28}, 3, GOLD);
                DrawRectangleV((Vector2){drops[i].pos.x-22, drops[i].pos.y-18}, (Vector2){44,10}, BLACK);
                DrawCircleV(drops[i].pos, 5, GOLD);
                if (Vector2Distance(playerPos, drops[i].pos) < 100) DrawText("[TAB]", (int)(drops[i].pos.x-15), (int)(drops[i].pos.y-40), 15, WHITE);
            }

            // Monsters draw
            for (int i = 0; i < MAX_MONSTERS; i++) DrawMonsterVisual(monsters[i]);

            // Bullets draw 
            for (int i = 0; i < MAX_BULLETS; i++) if (bullets[i].active) {
                if (bullets[i].isEnemy) {
                    float a = bullets[i].life / 1.25f; if (a > 1) a = 1;
                    DrawCircleV(bullets[i].pos, bullets[i].radius, Fade(RED, 0.35f + 0.65f*a));
                } else {
                    DrawCircleV(bullets[i].pos, bullets[i].radius, LIME);
                }
            }

            // Melee visual
            if (attackVisualTimer > 0 && currentWeapon.type == ATK_MELEE) {
                attackVisualTimer -= dt;
                float rot = GetTime()*3000.0f;
                float fR = (currentWeapon.range + bonusRange) * rangeMul * ((chargeTimer == 0 && attackVisualTimer > 0.3f) ? 1.6f : 1.0f);
                for (int i = 0; i < weaponLevel; i++) {
                    Vector2 tip = {
                        playerPos.x + cosf((rot + i*1.2f)*DEG2RAD)*fR,
                        playerPos.y + sinf((rot + i*1.2f)*DEG2RAD)*fR
                    };
                    DrawLineEx(playerPos, tip, 15, currentWeapon.color);
                    DrawCircleV(tip, 10, WHITE);
                }
            }

            if (chargeTimer > 0.1f) DrawCircleLinesV(playerPos, 45 + chargeTimer*45, (chargeTimer > 1.0f ? GOLD : WHITE));

            // Player draw
            if (!gameOver) {
                DrawCircleV(playerPos, 28, BLACK);
                DrawCircleV(playerPos, 24, SKYBLUE);
                DrawPoly(playerPos, 3, 15, GetTime()*100, WHITE);
            }

            // Particles draw
            for (int i = 0; i < MAX_PARTICLES; i++) if (particles[i].active)
                DrawCircleV(particles[i].pos, 2, Fade(particles[i].col, particles[i].life));

        EndMode2D();

        // UI LAYERS 
        if (gameState == GS_TITLE) {
            int sw = GetScreenWidth();
            int sh = GetScreenHeight();

            Rectangle btnStart = { sw/2.0f - 120, sh*0.56f, 240, 44 };
            Rectangle btnHelp  = { sw/2.0f - 120, sh*0.64f, 240, 44 };
            Rectangle btnQuit  = { sw/2.0f - 120, sh*0.72f, 240, 44 };

            Vector2 mp = GetMousePosition();

            DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.7f));
            DrawText("CIRCLE WARS", sw/2 - 250, (int)(sh*0.20f), 80, GOLD);

            // hover effect 
            Color cStart = CheckCollisionPointRec(mp, btnStart) ? Fade(WHITE, 0.25f) : Fade(DARKGRAY, 0.85f);
            Color cHelp  = CheckCollisionPointRec(mp, btnHelp)  ? Fade(WHITE, 0.25f) : Fade(DARKGRAY, 0.85f);
            Color cQuit  = CheckCollisionPointRec(mp, btnQuit)  ? Fade(WHITE, 0.25f) : Fade(DARKGRAY, 0.85f);

            // START
            DrawRectangleRec(btnStart, cStart);
            DrawRectangleLinesEx(btnStart, 2, GOLD);
            DrawText("START", (int)(btnStart.x + 75), (int)(btnStart.y + 8), 30, WHITE);

            // HELP
            DrawRectangleRec(btnHelp, cHelp);
            DrawRectangleLinesEx(btnHelp, 2, GOLD);
            DrawText("HELP", (int)(btnHelp.x + 85), (int)(btnHelp.y + 8), 30, WHITE);

            // QUIT
            DrawRectangleRec(btnQuit, cQuit);
            DrawRectangleLinesEx(btnQuit, 2, GOLD);
            DrawText("QUIT", (int)(btnQuit.x + 85), (int)(btnQuit.y + 8), 30, WHITE);         
        } else if (gameState == GS_HELP) {
            DrawDetailedHelp();
        } else {
            if (bossIntroTimer > 0) DrawText(TextFormat("LEVEL %d BOSS APPEARS!!", (playerLevel/20)*20),
                GetScreenWidth()/2 - 300, GetScreenHeight()/2, 50, RED);

            DrawRectangle(10, 10, 350, 180, Fade(BLACK, 0.8f));
            DrawText(TextFormat("LV: %d  HP: %d/%d", playerLevel, playerHP, playerMaxHP), 20, 30, 22, GREEN);
            DrawRectangle(20, 60, 260, 15, DARKGRAY);
            DrawRectangle(20, 60, (int)((float)playerHP/playerMaxHP*260), 15, RED);

            DrawText(TextFormat("%s Lv.%d", currentWeapon.name, weaponLevel), 20, 90, 20, currentWeapon.color);
            DrawRectangle(20, 120, 260, 12, DARKGRAY);
            DrawRectangle(20, 120, (int)((float)playerExp/playerMaxExp*260), 12, SKYBLUE);

            int rX = GetScreenWidth()-220;
            DrawRectangle(rX, 10, 210, 150, Fade(BLACK, 0.8f));
            DrawRectangleLines(rX, 10, 210, 150, ORANGE);
            DrawText("MISSION", rX+50, 20, 18, RED);
            DrawText("Defeat Lv.100 Boss", rX+15, 45, 16, WHITE);
            for (int i = 0; i < 3; i++) DrawText(TextFormat("%d. LV %d", i+1, highScores[i]), rX+20, 80+(i*20), 18, WHITE);

            if (bossActive && currentBossPtr) {
                DrawRectangle(GetScreenWidth()/2-300, 60, 600, 25, BLACK);
                DrawRectangle(GetScreenWidth()/2-300, 60, (int)((float)currentBossPtr->hp/currentBossPtr->maxHp * 600), 25, RED);
            }

            int kX = GetScreenWidth()-200, kY = GetScreenHeight()-150;
            DrawRectangle(kX-10, kY-10, 190, 140, Fade(BLACK, 0.8f));
            DrawRectangle(kX+55, kY, 35, 35, IsKeyDown(KEY_W)?SKYBLUE:GRAY); DrawText("W", kX+65, kY+10, 15, BLACK);
            DrawRectangle(kX+10, kY+40, 35, 35, IsKeyDown(KEY_A)?SKYBLUE:GRAY); DrawText("A", kX+20, kY+50, 15, BLACK);
            DrawRectangle(kX+55, kY+40, 35, 35, IsKeyDown(KEY_S)?SKYBLUE:GRAY); DrawText("S", kX+65, kY+50, 15, BLACK);
            DrawRectangle(kX+100, kY+40, 35, 35, IsKeyDown(KEY_D)?SKYBLUE:GRAY); DrawText("D", kX+110, kY+50, 15, BLACK);
            DrawText("SPACE: ATTACK / TAB: SWAP", kX, kY+85, 11, WHITE);
            Color spC = (specialGauge >= 100.0f && ((int)(GetTime()*15)%2==0)) ? WHITE : GOLD;
            DrawText("P: PAUSE / CTRL+W: SPECIAL", kX, kY+105, 11, spC);

            if (paused) {
                DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(), Fade(BLACK, 0.8f));
                DrawText("PAUSED", GetScreenWidth()/2 - 100, 250, 60, WHITE);
                DrawText("RESUME (P) / HELP (H) / QUIT (Q)", GetScreenWidth()/2 - 180, 350, 20, LIGHTGRAY);
                if (IsKeyPressed(KEY_H)) gameState = GS_HELP;
                if (IsKeyPressed(KEY_Q)) CloseWindow();
            }

            if (levelUpPending) {
                DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(), Fade(BLACK, 0.9f));
                const char* sN[] = {"ATK UP","SPD UP","REGEN","RANGE","SPECIAL","MAXHP","GACHA","LTNG","FRZ","WPN UP"};
                for (int i = 0; i < 3; i++) {
                    int cX = GetScreenWidth()/2 - 480 + i*320;
                    DrawRectangle(cX, 250, 280, 400, DARKGRAY);
                    DrawRectangleLines(cX, 250, 280, 400, WHITE);
                    DrawText(sN[choicePool[i]], cX+50, 400, 30, WHITE);
                    DrawText(TextFormat("Press [%d]", i+1), cX+90, 600, 20, GRAY);
                }
            }

            if (gachaPending) {
                DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(), Fade(BLACK, 0.9f));
                DrawText("GACHA RESULT", GetScreenWidth()/2-120, 300, 40, GOLD);
                DrawText(weaponsList[gachaResultIdx].name, GetScreenWidth()/2-100, 400, 30, weaponsList[gachaResultIdx].color);
                DrawText("Equip? (Y/N)", GetScreenWidth()/2-80, 500, 25, WHITE);
            }

            if (gameOver) {
                DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(), Fade(MAROON, 0.8f));
                DrawText("YOU DIED", GetScreenWidth()/2-150, GetScreenHeight()/2, 80, WHITE);
                DrawText("PRESS ENTER TO RESPAWN", GetScreenWidth()/2 - 140, GetScreenHeight()/2 + 80, 20, LIGHTGRAY);
            }

            if (gameState == GS_CLEAR) {
                DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(), Fade(GOLD, 0.8f));
                DrawText("GAME CLEAR!!", GetScreenWidth()/2 - 250, GetScreenHeight()/2, 80, WHITE);
                if ((int)(GetTime()*10)%2==0) PlaySound(audio.seShoot);
            }
        }

        EndDrawing();
    }

    UnloadMusicStream(audio.mainBgm);
    UnloadSound(audio.seSlash);
    UnloadSound(audio.seDamage);
    UnloadSound(audio.seHeal);
    UnloadSound(audio.seShoot);

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
