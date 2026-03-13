#define _CRT_SECURE_NO_WARNINGS
#include <GL/glut.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#define PI            3.14159265358979323846f
#define DEG2RAD       (PI/180.0f)
#define MAX_PARTICLES 4000
#define MAX_STARS     1400
#define NUM_PLANETS   8
#define NUM_CLOUDS    22
#define NUM_MOUNTAINS 30

// ================================================================
// SOUND SYSTEM  (pure MCI, all 4 sounds, looping via update tick)
// ================================================================
static int snd_enginePlaying = 0;
static int snd_spacePlaying = 0;
static int snd_countdownPlayed = 0;
static int snd_srbSepPlayed = 0;
static char snd_exeDir[MAX_PATH] = { 0 };

void snd_initPath(void) {
    char* last;
    GetModuleFileNameA(NULL, snd_exeDir, MAX_PATH);
    last = strrchr(snd_exeDir, '\\');
    if (last) *(last + 1) = '\0';
}

/* Open + play an MCI alias. Closes any previous instance first. */
void snd_mci_play(const char* alias, const char* file) {
    char fullpath[MAX_PATH];
    char cmd[720];
    char errbuf[256];
    MCIERROR err;
    sprintf(fullpath, "%s%s", snd_exeDir, file);
    sprintf(cmd, "close %s", alias);
    mciSendStringA(cmd, NULL, 0, NULL);
    sprintf(cmd, "open \"%s\" type waveaudio alias %s", fullpath, alias);
    err = mciSendStringA(cmd, NULL, 0, NULL);
    if (err != 0) {
        mciGetErrorStringA(err, errbuf, sizeof(errbuf));
        /* Print to debug output visible in VS Output window */
        OutputDebugStringA("MCI open failed: ");
        OutputDebugStringA(errbuf);
        OutputDebugStringA(" file: ");
        OutputDebugStringA(fullpath);
        OutputDebugStringA("\n");
        return;
    }
    sprintf(cmd, "play %s", alias);
    err = mciSendStringA(cmd, NULL, 0, NULL);
    if (err != 0) {
        mciGetErrorStringA(err, errbuf, sizeof(errbuf));
        OutputDebugStringA("MCI play failed: ");
        OutputDebugStringA(errbuf);
        OutputDebugStringA("\n");
    }
}

/* Returns 1 if the alias has finished playing */
int snd_mci_done(const char* alias) {
    char status[64];
    char cmd[128];
    sprintf(cmd, "status %s mode", alias);
    if (mciSendStringA(cmd, status, sizeof(status), NULL) != 0) return 1;
    return (strncmp(status, "stopped", 7) == 0) ? 1 : 0;
}

void snd_mci_stop(const char* alias) {
    char cmd[128];
    sprintf(cmd, "stop %s", alias); mciSendStringA(cmd, NULL, 0, NULL);
    sprintf(cmd, "close %s", alias); mciSendStringA(cmd, NULL, 0, NULL);
}

/* Restart from position 0 */
void snd_mci_restart(const char* alias) {
    char cmd[128];
    sprintf(cmd, "seek %s to start", alias); mciSendStringA(cmd, NULL, 0, NULL);
    sprintf(cmd, "play %s", alias);          mciSendStringA(cmd, NULL, 0, NULL);
}

void snd_startEngine(void) {
    if (snd_enginePlaying) return;
    snd_enginePlaying = 1;
    snd_mci_play("engine", "engine.wav");
}
void snd_stopEngine(void) {
    snd_enginePlaying = 0;
    snd_mci_stop("engine");
}
void snd_startSpace(void) {
    if (snd_spacePlaying) return;
    snd_spacePlaying = 1;
    snd_mci_play("space", "space.wav");
}
void snd_stopSpace(void) {
    snd_spacePlaying = 0;
    snd_mci_stop("space");
}
/* Called every update tick to re-loop engine/space sounds */
void snd_tick(void) {
    if (snd_enginePlaying && snd_mci_done("engine"))
        snd_mci_restart("engine");
    if (snd_spacePlaying && snd_mci_done("space"))
        snd_mci_restart("space");
}
void snd_playOnce(const char* alias, const char* file) {
    snd_mci_play(alias, file);
}
void snd_stopOnce(const char* alias) {
    snd_mci_stop(alias);
}

int winW = 1280, winH = 720;

typedef enum { PHASE_LAUNCH, PHASE_ORBIT, PHASE_SPACE }Phase;
Phase phase = PHASE_LAUNCH;
typedef enum { ST_IDLE, ST_COUNTDOWN, ST_HOLD, ST_LIFTOFF, ST_ASCENT }LaunchState;
LaunchState lstate = ST_IDLE;

float rocketY = 0.0f, rocketSpeed = 0.0f;
float countTimer = 5.0f; int countInt = 5;
int   srbSep = 0;
float srbLX = -1.15f, srbRX = 1.15f;
float srbY = 0.0f;
float srbVelY = 0.0f;
float srbLRot = 0.0f, srbRRot = 0.0f;
float orbitTimer = 0.0f;
float skyR = 0.38f, skyG = 0.62f, skyB = 0.92f, starAlpha = 0.0f;
float vibX = 0.0f, vibZ = 0.0f, launchHoldTimer = 0.0f;

float rocketSX = 0.0f, rocketSY = 0.0f, rocketSZ = -30.0f;
float flySpeed = 0.0f, flyAccel = 0.55f, flyMaxSpeed = 12.0f;
float rocketTiltPitch = 0.0f, rocketTiltRoll = 0.0f;

float camYaw = 0.0f, camPitch = -8.0f, lastMX = 640.0f, lastMY = 360.0f;
int   mouseOK = 0; float mouseSens = 0.55f;
float CAM_DIST = 14.0f;
int keyW = 0, keyS = 0, keyA = 0, keyD = 0, keyQ = 0, keyE = 0;

typedef struct { float x, y, z, vx, vy, vz, life, maxLife, size; int type; }Particle;
Particle particles[MAX_PARTICLES];

typedef struct { float x, y, z, bright, twinkle, tspeed; }Star;
Star stars[MAX_STARS];

typedef struct {
    char name[32];
    float dist, radius, r, g, b, orbitSpeed, orbitAngle, axialTilt;
    int hasRing; float ringInner, ringOuter, ringR, ringG, ringB;
    float selfRotAngle, selfRotSpeed;
}Planet;

Planet planets[NUM_PLANETS] = {
    {"Mercury",  80.0f,  9.5f,0.72f,0.65f,0.58f,0.24f,0.0f,0.0f,  0,0,0,0,0,0,0.0f,0.9f},
    {"Venus",   140.0f, 16.0f,0.92f,0.82f,0.45f,0.18f,0.0f,2.6f,  0,0,0,0,0,0,0.0f,0.4f},
    {"Earth",   210.0f, 18.0f,0.20f,0.45f,0.85f,0.15f,0.0f,23.5f, 0,0,0,0,0,0,0.0f,1.0f},
    {"Mars",    290.0f, 13.0f,0.78f,0.32f,0.18f,0.12f,0.0f,25.2f, 0,0,0,0,0,0,0.0f,0.95f},
    {"Jupiter", 420.0f, 48.0f,0.82f,0.72f,0.58f,0.08f,0.0f,3.1f,  0,0,0,0,0,0,0.0f,2.4f},
    {"Saturn",  560.0f, 38.0f,0.92f,0.85f,0.65f,0.06f,0.0f,26.7f, 1,52.0f,82.0f,0.88f,0.80f,0.55f,0.0f,2.2f},
    {"Uranus",  700.0f, 27.0f,0.55f,0.88f,0.95f,0.04f,0.0f,97.8f, 1,36.0f,54.0f,0.55f,0.88f,0.95f,0.0f,1.4f},
    {"Neptune", 860.0f, 25.0f,0.22f,0.42f,0.95f,0.03f,0.0f,28.3f, 0,0,0,0,0,0,0.0f,1.5f}
};

typedef struct { float x, y, z, size, alpha, drift; }Cloud;
Cloud clouds[NUM_CLOUDS];
float mtX[NUM_MOUNTAINS], mtH[NUM_MOUNTAINS];

// Moon orbit
float moonAngle = 0.0f;
#define NUM_ASTEROIDS 280
typedef struct { float angle, dist, y, size, speed; }Asteroid;
Asteroid asteroids[NUM_ASTEROIDS];

float randf(float lo, float hi) { return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float lerpf(float a, float b, float t) { return a + (b - a) * clampf(t, 0, 1); }

void initStars(void) {
    for (int i = 0; i < MAX_STARS; i++) {
        float th = randf(0, 2 * PI), ph = randf(0, PI), r = randf(700, 1000);
        stars[i].x = r * sinf(ph) * cosf(th); stars[i].y = r * sinf(ph) * sinf(th); stars[i].z = r * cosf(ph);
        stars[i].bright = randf(0.3f, 1.0f); stars[i].twinkle = randf(0, 2 * PI); stars[i].tspeed = randf(0.5f, 4.0f);
    }
}
void initClouds(void) {
    for (int i = 0; i < NUM_CLOUDS; i++) {
        clouds[i].x = randf(-60, 60); clouds[i].y = randf(10, 28); clouds[i].z = randf(-70, -15);
        clouds[i].size = randf(3.0f, 8.0f); clouds[i].alpha = randf(0.5f, 0.9f);
        clouds[i].drift = randf(-0.002f, 0.002f);
    }
}
void initMountains(void) {
    for (int i = 0; i < NUM_MOUNTAINS; i++) {
        mtX[i] = -60.0f + (float)i * (120.0f / (float)NUM_MOUNTAINS) + randf(-2, 2);
        mtH[i] = randf(4, 14);
    }
    // Asteroid belt between Mars(290) and Jupiter(420)
    for (int i = 0; i < NUM_ASTEROIDS; i++) {
        asteroids[i].angle = randf(0, 2 * PI);
        asteroids[i].dist = randf(330.0f, 395.0f);
        asteroids[i].y = randf(-8.0f, 8.0f);
        asteroids[i].size = randf(0.4f, 2.2f);
        asteroids[i].speed = randf(0.008f, 0.022f);
    }
}
void resetParticles(void) { for (int i = 0; i < MAX_PARTICLES; i++)particles[i].life = 0.0f; }

void resetSim(void) {
    // Stop all sounds
    snd_stopOnce("countdown"); snd_stopOnce("srb_sep");
    snd_stopEngine(); snd_stopSpace();
    snd_enginePlaying = 0; snd_spacePlaying = 0;
    snd_countdownPlayed = 0; snd_srbSepPlayed = 0;

    phase = PHASE_LAUNCH; lstate = ST_IDLE;
    rocketY = 0.0f; rocketSpeed = 0.0f; countTimer = 5.0f; countInt = 5;
    srbSep = 0; srbLX = -1.15f; srbRX = 1.15f; srbY = 0.0f;
    srbVelY = 0.0f; srbLRot = 0.0f; srbRRot = 0.0f;
    orbitTimer = 0.0f;
    skyR = 0.38f; skyG = 0.62f; skyB = 0.92f; starAlpha = 0.0f;
    vibX = 0.0f; vibZ = 0.0f; launchHoldTimer = 0.0f;
    rocketSX = planets[2].dist * cosf(planets[2].orbitAngle) + 30.0f;
    rocketSY = 0.0f;
    rocketSZ = planets[2].dist * sinf(planets[2].orbitAngle) + 30.0f;
    flySpeed = 0.0f;
    rocketTiltPitch = 0.0f; rocketTiltRoll = 0.0f;
    camYaw = 0.0f; camPitch = -8.0f; mouseOK = 0;
    resetParticles();
}

// ================================================================
// PARTICLES
// ================================================================
void spawnParticle(int type, float bx, float by, float bz) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0.0f)continue;
        Particle* p = &particles[i]; p->type = type;
        switch (type) {
        case 0:
            p->x = bx + randf(-0.3f, 0.3f); p->y = by; p->z = bz + randf(-0.3f, 0.3f);
            p->vx = randf(-0.08f, 0.08f); p->vy = randf(-0.45f, -0.10f); p->vz = randf(-0.08f, 0.08f);
            p->maxLife = randf(0.2f, 0.85f); p->size = randf(0.20f, 0.65f); break;
        case 1:
            p->x = bx + randf(-0.6f, 0.6f); p->y = by - 0.3f; p->z = bz + randf(-0.6f, 0.6f);
            p->vx = randf(-0.10f, 0.10f); p->vy = randf(-0.12f, 0.02f); p->vz = randf(-0.10f, 0.10f);
            p->maxLife = randf(2.0f, 4.5f); p->size = randf(0.6f, 1.5f); break;
        case 2:
            p->x = bx + randf(-0.4f, 0.4f); p->y = by; p->z = bz + randf(-0.4f, 0.4f);
            p->vx = randf(-0.25f, 0.25f); p->vy = randf(0.05f, 0.25f); p->vz = randf(-0.25f, 0.25f);
            p->maxLife = randf(1.5f, 3.0f); p->size = randf(0.06f, 0.16f); break;
        case 3:
            p->x = bx + randf(-0.2f, 0.2f); p->y = by; p->z = bz + randf(-0.2f, 0.2f);
            p->vx = randf(-0.04f, 0.04f); p->vy = randf(-0.12f, 0.12f); p->vz = randf(-0.12f, 0.12f);
            p->maxLife = randf(0.3f, 0.9f); p->size = randf(0.04f, 0.16f); break;
        case 4:
            p->x = bx + randf(-0.12f, 0.12f); p->y = by; p->z = bz + randf(-0.12f, 0.12f);
            p->vx = randf(-0.05f, 0.05f); p->vy = randf(-0.35f, -0.08f); p->vz = randf(-0.05f, 0.05f);
            p->maxLife = randf(0.2f, 0.6f); p->size = randf(0.10f, 0.30f); break;
        }
        p->life = p->maxLife; return;
    }
}

void updateParticles(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &particles[i]; if (p->life <= 0)continue;
        p->life -= dt;
        p->x += p->vx * dt * 60.0f; p->y += p->vy * dt * 60.0f; p->z += p->vz * dt * 60.0f;
        if (p->type == 1)p->vy += 0.0008f;
        if (p->type == 2)p->vy -= 0.006f;
    }
}

void drawParticles(void) {
    glDepthMask(GL_FALSE); glEnable(GL_BLEND);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &particles[i]; if (p->life <= 0)continue;
        float t = p->life / p->maxLife;
        switch (p->type) {
        case 0: case 4:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glColor4f(1.0f, t > 0.6f ? 1.0f : t * 1.66f, t > 0.8f ? t : 0.0f, t * 0.95f); break;
        case 1:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            { float g = 0.45f + t * 0.3f; glColor4f(g, g, g, t * 0.28f); }break;
        case 2:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glColor4f(1.0f, 0.55f * t, 0.0f, t); break;
        case 3:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glColor4f(0.4f, 0.75f, 1.0f, t * 0.65f); break;
        }
        float sz = p->type == 1 ? p->size * (1.8f - t * 0.8f) : p->size;
        glPushMatrix();
        if (phase == PHASE_LAUNCH)glTranslatef(p->x, p->y + rocketY, p->z);
        else glTranslatef(p->x + rocketSX, p->y + rocketSY, p->z + rocketSZ);
        glutSolidSphere((double)sz, 6, 6);
        glPopMatrix();
    }
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
}

// ================================================================
// HELPERS
// ================================================================
void cyl(float br, float tr, float h, int sl) {
    GLUquadric* q = gluNewQuadric(); gluQuadricNormals(q, GLU_SMOOTH);
    gluCylinder(q, (double)br, (double)tr, (double)h, sl, 4); gluDeleteQuadric(q);
}
void dsk(float ir, float or2, int sl) {
    GLUquadric* q = gluNewQuadric(); gluQuadricNormals(q, GLU_SMOOTH);
    gluDisk(q, (double)ir, (double)or2, sl, 2); gluDeleteQuadric(q);
}

// ================================================================
// SRB (one solid rocket booster — white, like SLS reference)
// ================================================================
void drawSRB(void) {
    // Long white body
    glColor3f(0.93f, 0.93f, 0.95f);
    glPushMatrix(); glRotatef(-90, 1, 0, 0); cyl(0.165f, 0.165f, 4.8f, 20); glPopMatrix();
    // Bottom cap
    glPushMatrix(); glRotatef(90, 1, 0, 0); dsk(0, 0.165f, 20); glPopMatrix();
    // Pointed nose cone
    glColor3f(0.90f, 0.90f, 0.92f);
    glPushMatrix(); glTranslatef(0, 4.8f, 0); glRotatef(-90, 1, 0, 0); cyl(0.165f, 0.01f, 0.85f, 20); glPopMatrix();
    // Nozzle bell
    glColor3f(0.22f, 0.22f, 0.26f);
    glPushMatrix(); glRotatef(90, 1, 0, 0); cyl(0.09f, 0.20f, 0.40f, 16); glPopMatrix();
    // Orange stripe near base
    glColor3f(0.90f, 0.46f, 0.05f);
    glPushMatrix(); glTranslatef(0, 0.55f, 0); glRotatef(-90, 1, 0, 0); cyl(0.170f, 0.170f, 0.42f, 20); glPopMatrix();
    // Thin mid stripe
    glColor3f(0.85f, 0.42f, 0.04f);
    glPushMatrix(); glTranslatef(0, 2.6f, 0); glRotatef(-90, 1, 0, 0); cyl(0.170f, 0.170f, 0.18f, 20); glPopMatrix();
    // Dark staging ring at top
    glColor3f(0.25f, 0.25f, 0.28f);
    glPushMatrix(); glTranslatef(0, 4.5f, 0); glRotatef(-90, 1, 0, 0); cyl(0.172f, 0.172f, 0.18f, 20); glPopMatrix();
}

// ================================================================
// ROCKET CORE (orange SLS body — no SRBs)
// ================================================================
void drawRocketCore(void) {
    // BIG ORANGE CORE STAGE
    glColor3f(0.90f, 0.46f, 0.06f);
    glPushMatrix(); glRotatef(-90, 1, 0, 0); cyl(0.50f, 0.50f, 5.2f, 36); glPopMatrix();
    glColor3f(0.80f, 0.38f, 0.04f);
    glPushMatrix(); glRotatef(90, 1, 0, 0); dsk(0, 0.50f, 36); glPopMatrix();

    // Dark intertank band
    glColor3f(0.15f, 0.15f, 0.17f);
    glPushMatrix(); glTranslatef(0, 2.2f, 0); glRotatef(-90, 1, 0, 0); cyl(0.515f, 0.515f, 0.28f, 36); glPopMatrix();

    // White USA flag panel on side
    glColor3f(0.95f, 0.95f, 0.97f);
    glPushMatrix(); glTranslatef(0.48f, 1.6f, 0.10f); glScalef(0.07f, 0.55f, 0.30f); glutSolidCube(1); glPopMatrix();

    // NASA blue stripe
    glColor3f(0.10f, 0.20f, 0.72f);
    glPushMatrix(); glTranslatef(0, 3.8f, 0); glRotatef(-90, 1, 0, 0); cyl(0.508f, 0.508f, 0.18f, 36); glPopMatrix();

    // 4 RS-25 engine bells at base
    glColor3f(0.28f, 0.28f, 0.32f);
    float eOff = 0.26f;
    float ePos[4][2] = { {eOff,eOff},{-eOff,eOff},{eOff,-eOff},{-eOff,-eOff} };
    for (int i = 0; i < 4; i++) {
        glPushMatrix(); glTranslatef(ePos[i][0], -0.08f, ePos[i][1]);
        glRotatef(90, 1, 0, 0); cyl(0.09f, 0.17f, 0.40f, 14); glPopMatrix();
    }

    // UPPER STAGE ADAPTER (white taper)
    glColor3f(0.90f, 0.90f, 0.92f);
    glPushMatrix(); glTranslatef(0, 5.2f, 0); glRotatef(-90, 1, 0, 0); cyl(0.50f, 0.36f, 0.75f, 32); glPopMatrix();

    // ICPS UPPER STAGE
    glColor3f(0.88f, 0.88f, 0.90f);
    glPushMatrix(); glTranslatef(0, 5.95f, 0); glRotatef(-90, 1, 0, 0); cyl(0.36f, 0.35f, 1.40f, 32); glPopMatrix();
    glPushMatrix(); glTranslatef(0, 5.95f, 0); glRotatef(90, 1, 0, 0); dsk(0, 0.36f, 32); glPopMatrix();

    // SM/CM separation ring
    glColor3f(0.22f, 0.22f, 0.25f);
    glPushMatrix(); glTranslatef(0, 7.35f, 0); glRotatef(-90, 1, 0, 0); cyl(0.356f, 0.356f, 0.10f, 32); glPopMatrix();

    // ORION CREW MODULE
    glColor3f(0.90f, 0.90f, 0.92f);
    glPushMatrix(); glTranslatef(0, 7.45f, 0); glRotatef(-90, 1, 0, 0); cyl(0.35f, 0.20f, 0.80f, 32); glPopMatrix();
    glPushMatrix(); glTranslatef(0, 8.25f, 0); glScalef(1, 0.55f, 1); glutSolidSphere(0.21f, 18, 12); glPopMatrix();

    // LAUNCH ABORT SYSTEM
    glColor3f(0.82f, 0.82f, 0.84f);
    glPushMatrix(); glTranslatef(0, 8.46f, 0); glRotatef(-90, 1, 0, 0); cyl(0.22f, 0.16f, 0.55f, 16); glPopMatrix();
    glColor3f(0.20f, 0.20f, 0.22f);
    glPushMatrix(); glTranslatef(0, 9.01f, 0); glRotatef(-90, 1, 0, 0); cyl(0.08f, 0.06f, 0.90f, 12); glPopMatrix();
    glColor3f(0.18f, 0.18f, 0.20f);
    glPushMatrix(); glTranslatef(0, 9.91f, 0); glRotatef(-90, 1, 0, 0); cyl(0.14f, 0.08f, 0.32f, 14); glPopMatrix();
    glColor3f(0.25f, 0.25f, 0.28f);
    glPushMatrix(); glTranslatef(0, 10.23f, 0); glRotatef(-90, 1, 0, 0); cyl(0.06f, 0.005f, 0.50f, 10); glPopMatrix();
    for (int i = 0; i < 3; i++) {
        glPushMatrix(); glRotatef((float)i * 120.0f, 0, 1, 0);
        glTranslatef(0.14f, 8.7f, 0); glRotatef(-65, 0, 0, 1);
        glColor3f(0.20f, 0.20f, 0.23f); cyl(0.018f, 0.018f, 0.62f, 6);
        glPopMatrix();
    }

    // Solar panels (space phase only)
    if (phase == PHASE_SPACE) {
        glColor3f(0.10f, 0.20f, 0.55f);
        for (int s = -1; s <= 1; s += 2) {
            glPushMatrix(); glTranslatef((float)s * 1.5f, 6.2f, 0); glScalef(1.2f, 0.05f, 0.55f); glutSolidCube(1); glPopMatrix();
        }
    }
}

// ================================================================
// FULL ROCKET = core + attached SRBs + connecting struts
// ================================================================
void drawRocket(void) {
    drawRocketCore();

    if (!srbSep) {
        // LEFT SRB
        glPushMatrix();
        glTranslatef(srbLX, srbY, 0);
        drawSRB();
        glPopMatrix();

        // RIGHT SRB
        glPushMatrix();
        glTranslatef(srbRX, srbY, 0);
        drawSRB();
        glPopMatrix();

        // Attachment struts (fore and aft)
        glColor3f(0.42f, 0.42f, 0.48f);
        float strutW = fabsf(srbLX) - 0.35f;
        // Left struts
        glPushMatrix(); glTranslatef(srbLX * 0.5f, 1.2f, 0); glScalef(strutW, 0.07f, 0.07f); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(srbLX * 0.5f, 3.6f, 0); glScalef(strutW, 0.07f, 0.07f); glutSolidCube(1); glPopMatrix();
        // Right struts
        glPushMatrix(); glTranslatef(srbRX * 0.5f, 1.2f, 0); glScalef(strutW, 0.07f, 0.07f); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(srbRX * 0.5f, 3.6f, 0); glScalef(strutW, 0.07f, 0.07f); glutSolidCube(1); glPopMatrix();
    }
}

// ================================================================
// FALLING SRBs — drawn in world space after separation
// ================================================================
void drawFallingSRBs(void) {
    if (!srbSep) return;

    // Left SRB falls and tumbles
    glPushMatrix();
    glTranslatef(srbLX, rocketY + srbY, 0.0f);
    glRotatef(srbLRot, 0, 0, 1);
    glRotatef(srbLRot * 0.5f, 1, 0, 0);
    drawSRB();
    glPopMatrix();

    // Right SRB falls and tumbles opposite direction
    glPushMatrix();
    glTranslatef(srbRX, rocketY + srbY, 0.0f);
    glRotatef(-srbRRot, 0, 0, 1);
    glRotatef(srbRRot * 0.5f, 1, 0, 0);
    drawSRB();
    glPopMatrix();
}

// ================================================================
// LAUNCHPAD (detailed NASA LC-39B style)
// ================================================================
void drawLaunchpad(void) {

    // CRAWLER-TRANSPORTER BASE (massive tracked platform)
    glColor3f(0.38f, 0.36f, 0.33f);
    glPushMatrix(); glTranslatef(0, -0.70f, 0); glScalef(9.0f, 1.40f, 9.0f); glutSolidCube(1); glPopMatrix();
    // Crawler track sections (dark)
    glColor3f(0.22f, 0.20f, 0.18f);
    for (int s = -1; s <= 1; s += 2) {
        glPushMatrix(); glTranslatef((float)s * 4.0f, -1.0f, 0); glScalef(0.9f, 1.0f, 8.5f); glutSolidCube(1); glPopMatrix();
    }
    glColor3f(0.18f, 0.17f, 0.15f);
    for (int s = -1; s <= 1; s += 2) {
        glPushMatrix(); glTranslatef(0, -1.35f, (float)s * 4.5f); glScalef(8.0f, 0.40f, 0.55f); glutSolidCube(1); glPopMatrix();
    }

    // MOBILE LAUNCHER PLATFORM (on top of crawler)
    glColor3f(0.48f, 0.46f, 0.42f);
    glPushMatrix(); glTranslatef(0, 0.06f, 0); glScalef(7.5f, 0.55f, 7.5f); glutSolidCube(1); glPopMatrix();
    // Corner pedestals
    glColor3f(0.35f, 0.33f, 0.30f);
    float pOff = 3.0f;
    float pPos[4][2] = { {pOff,pOff},{-pOff,pOff},{pOff,-pOff},{-pOff,-pOff} };
    for (int i = 0; i < 4; i++) {
        glPushMatrix(); glTranslatef(pPos[i][0], -0.50f, pPos[i][1]);
        glScalef(0.60f, 1.45f, 0.60f); glutSolidCube(1); glPopMatrix();
    }

    // FLAME TRENCH
    glColor3f(0.14f, 0.12f, 0.10f);
    glPushMatrix(); glTranslatef(0, -0.80f, 1.5f); glScalef(2.5f, 1.0f, 5.0f); glutSolidCube(1); glPopMatrix();
    // Flame deflector ramp
    glColor3f(0.42f, 0.40f, 0.37f);
    glPushMatrix(); glTranslatef(0, -0.50f, 4.2f); glRotatef(26, 1, 0, 0); glScalef(3.5f, 0.30f, 4.8f); glutSolidCube(1); glPopMatrix();
    // Water deluge nozzles along trench
    glColor3f(0.55f, 0.55f, 0.60f);
    for (int i = -2; i <= 2; i++) {
        glPushMatrix(); glTranslatef((float)i * 0.6f, 0.08f, 2.5f);
        glRotatef(-90, 1, 0, 0); cyl(0.04f, 0.04f, 0.30f, 8); glPopMatrix();
    }

    // WATER DELUGE TANKS (2 large white tanks)
    glColor3f(0.90f, 0.90f, 0.92f);
    float tkPos[2][2] = { {5.8f, 0.5f},{-5.5f, -1.2f} };
    for (int i = 0; i < 2; i++) {
        glPushMatrix(); glTranslatef(tkPos[i][0], 3.0f, tkPos[i][1]);
        glRotatef(-90, 1, 0, 0); cyl(0.72f, 0.72f, 5.5f, 18); glPopMatrix();
        glPushMatrix(); glTranslatef(tkPos[i][0], 8.5f, tkPos[i][1]);
        glutSolidSphere(0.73f, 12, 10); glPopMatrix();
        // Tank legs
        glColor3f(0.68f, 0.68f, 0.70f);
        for (int l = 0; l < 3; l++) {
            float la = (float)l * 120.0f * DEG2RAD;
            glPushMatrix(); glTranslatef(tkPos[i][0] + 0.68f * cosf(la), 1.3f, tkPos[i][1] + 0.68f * sinf(la));
            glScalef(0.10f, 3.5f, 0.10f); glutSolidCube(1); glPopMatrix();
        }
        glColor3f(0.90f, 0.90f, 0.92f);
    }

    // MOBILE LAUNCHER TOWER (tall lattice — left side, 4 columns)
    glColor3f(0.52f, 0.52f, 0.58f);
    float tcX[4] = { -3.8f,-3.8f,-5.1f,-5.1f };
    float tcZ[4] = { -1.1f, 1.1f,-1.1f, 1.1f };
    for (int i = 0; i < 4; i++) {
        glPushMatrix(); glTranslatef(tcX[i], 7.8f, tcZ[i]);
        glScalef(0.22f, 15.6f, 0.22f); glutSolidCube(1); glPopMatrix();
    }
    // Horizontal cross beams
    glColor3f(0.48f, 0.48f, 0.52f);
    for (int i = 0; i < 11; i++) {
        float hy = 0.4f + (float)i * 1.40f;
        glPushMatrix(); glTranslatef(-4.45f, hy, -1.1f); glScalef(1.5f, 0.08f, 0.08f); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(-4.45f, hy, 1.1f); glScalef(1.5f, 0.08f, 0.08f); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(-3.8f, hy, 0.0f); glScalef(0.08f, 0.08f, 2.3f); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(-5.1f, hy, 0.0f); glScalef(0.08f, 0.08f, 2.3f); glutSolidCube(1); glPopMatrix();
        // X diagonals
        glPushMatrix(); glTranslatef(-4.45f, hy + 0.7f, 0.0f);
        glRotatef(36, 0, 0, 1); glScalef(0.05f, 2.2f, 0.05f); glutSolidCube(1); glPopMatrix();
    }
    // Swing arms (2 levels)
    glColor3f(0.50f, 0.50f, 0.55f);
    glPushMatrix(); glTranslatef(-2.1f, 5.8f, 0); glScalef(2.8f, 0.12f, 0.18f); glutSolidCube(1); glPopMatrix();
    glPushMatrix(); glTranslatef(-2.1f, 4.0f, 0); glScalef(2.8f, 0.12f, 0.18f); glutSolidCube(1); glPopMatrix();
    glPushMatrix(); glTranslatef(-2.0f, 8.2f, 0); glScalef(2.6f, 0.10f, 0.10f); glutSolidCube(1); glPopMatrix();

    // 2 LIGHTNING MASTS (tall slender lattice towers)
    float lmX[2] = { -9.5f,  8.0f };
    float lmZ[2] = { 2.0f, -3.0f };
    for (int t = 0; t < 2; t++) {
        float tx = lmX[t], tz = lmZ[t];
        // Main mast column
        glColor3f(0.60f, 0.60f, 0.65f);
        glPushMatrix(); glTranslatef(tx, 12.0f, tz); glScalef(0.20f, 24.0f, 0.20f); glutSolidCube(1); glPopMatrix();
        // Base spread legs
        glColor3f(0.54f, 0.54f, 0.58f);
        for (int l = 0; l < 4; l++) {
            float la2 = (float)l * 90.0f * DEG2RAD;
            float bx2 = tx + 1.8f * cosf(la2), bz2 = tz + 1.8f * sinf(la2);
            glPushMatrix(); glTranslatef((tx + bx2) * 0.5f, 0.5f, (tz + bz2) * 0.5f);
            glScalef(fabsf(bx2 - tx) + 0.1f, 0.10f, fabsf(bz2 - tz) + 0.1f);
            glutSolidCube(1); glPopMatrix();
        }
        // Diagonal braces along mast
        for (int i = 0; i < 7; i++) {
            float by = 1.0f + (float)i * 3.2f;
            float sign = (i % 2 == 0) ? 1.0f : -1.0f;
            glPushMatrix(); glTranslatef(tx + sign * 0.65f, by + 1.6f, tz);
            glRotatef(sign * 27.0f, 0, 0, 1); glScalef(0.06f, 3.4f, 0.06f); glutSolidCube(1); glPopMatrix();
            glPushMatrix(); glTranslatef(tx, by + 1.6f, tz + sign * 0.65f);
            glRotatef(sign * 27.0f, 1, 0, 0); glScalef(0.06f, 3.4f, 0.06f); glutSolidCube(1); glPopMatrix();
        }
        // Horizontal ring braces
        glColor3f(0.56f, 0.56f, 0.60f);
        for (int i = 0; i < 5; i++) {
            float hy = 2.0f + (float)i * 4.5f;
            glPushMatrix(); glTranslatef(tx, hy, tz); glScalef(1.1f, 0.10f, 1.1f); glutSolidCube(1); glPopMatrix();
        }
        // Lightning rod tip
        glColor3f(0.72f, 0.72f, 0.76f);
        glPushMatrix(); glTranslatef(tx, 24.2f, tz); glRotatef(-90, 1, 0, 0);
        cyl(0.05f, 0.005f, 1.3f, 8); glPopMatrix();
    }

    // GROUND SUPPORT EQUIPMENT BOXES
    glColor3f(0.40f, 0.40f, 0.45f);
    glPushMatrix(); glTranslatef(-6.5f, 0.3f, 2.5f); glScalef(2.2f, 0.6f, 1.5f); glutSolidCube(1); glPopMatrix();
    glPushMatrix(); glTranslatef(5.0f, 0.3f, -2.0f); glScalef(1.8f, 0.6f, 1.2f); glutSolidCube(1); glPopMatrix();
    glPushMatrix(); glTranslatef(3.5f, 0.3f, 3.0f);  glScalef(1.4f, 0.5f, 1.0f); glutSolidCube(1); glPopMatrix();

    // Access road
    glColor3f(0.50f, 0.48f, 0.44f);
    glPushMatrix(); glTranslatef(12.0f, -0.39f, 0); glScalef(14.0f, 0.05f, 2.0f); glutSolidCube(1); glPopMatrix();
}

// ================================================================
// TERRAIN  —  Kennedy Space Center environment
// ================================================================

/* Draw a realistic tree: trunk + layered canopy */
void drawTree(float x, float y, float z, float h, float r, float g, float b) {
    /* Trunk */
    glColor3f(0.38f, 0.26f, 0.14f);
    glPushMatrix(); glTranslatef(x, y + h * 0.25f, z);
    glScalef(h * 0.08f, h * 0.55f, h * 0.08f); glutSolidCube(1); glPopMatrix();
    /* Three canopy layers */
    glColor3f(r, g, b);
    glPushMatrix(); glTranslatef(x, y + h * 0.65f, z); glScalef(1.0f, 0.55f, 1.0f); glutSolidSphere(h * 0.38f, 8, 6); glPopMatrix();
    glColor3f(r * 0.88f, g * 0.92f, b * 0.80f);
    glPushMatrix(); glTranslatef(x, y + h * 0.82f, z); glScalef(1.0f, 0.5f, 1.0f);  glutSolidSphere(h * 0.28f, 7, 5); glPopMatrix();
    glColor3f(r * 0.78f, g * 0.85f, b * 0.70f);
    glPushMatrix(); glTranslatef(x, y + h * 0.95f, z); glScalef(1.0f, 0.45f, 1.0f); glutSolidSphere(h * 0.18f, 6, 4); glPopMatrix();
}

/* Draw a palm tree */
void drawPalm(float x, float y, float z, float h) {
    /* Curved trunk — stack of tilted cylinders */
    int seg; float tx2 = x, ty = y, tz2 = z;
    glColor3f(0.55f, 0.42f, 0.22f);
    for (seg = 0; seg < 6; seg++) {
        float lean = (float)seg * 0.04f;
        glPushMatrix(); glTranslatef(tx2, ty + h * (float)seg / 6.0f, tz2);
        glRotatef(12.0f * lean * 57.3f, 0, 0, 1);
        glRotatef(-90, 1, 0, 0); cyl(h * 0.04f, h * 0.035f, h / 6.0f, 8);
        glPopMatrix();
        tx2 += lean * 0.3f;
    }
    /* Fronds */
    glColor3f(0.18f, 0.52f, 0.14f);
    for (seg = 0; seg < 7; seg++) {
        float fa = (float)seg * (360.0f / 7.0f) * DEG2RAD;
        glPushMatrix();
        glTranslatef(tx2, ty + h * 0.96f, tz2);
        glRotatef((float)seg * (360.0f / 7.0f), 0, 1, 0);
        glRotatef(-38, 0, 0, 1);
        glScalef(0.08f, 0.06f, h * 0.42f); glutSolidCube(1);
        glPopMatrix();
    }
}

/* Hangar / VAB-style building */
void drawBuilding(float x, float y, float z, float w, float h, float d,
    float r, float g, float b) {
    /* Main body */
    glColor3f(r, g, b);
    glPushMatrix(); glTranslatef(x, y + h * 0.5f, z); glScalef(w, h, d); glutSolidCube(1); glPopMatrix();
    /* Darker roof band */
    glColor3f(r * 0.75f, g * 0.75f, b * 0.75f);
    glPushMatrix(); glTranslatef(x, y + h, z); glScalef(w * 1.01f, h * 0.06f, d * 1.01f); glutSolidCube(1); glPopMatrix();
    /* Vertical window strips */
    glColor3f(0.55f, 0.70f, 0.82f);
    int wi; int nw = (int)(w / 1.2f); if (nw < 1)nw = 1;
    for (wi = 0; wi < nw; wi++) {
        float wx = x - w * 0.5f + (float)wi * (w / (float)nw) + w / (float)nw * 0.5f;
        glPushMatrix(); glTranslatef(wx, y + h * 0.5f, z + d * 0.501f);
        glScalef(w / (float)nw * 0.35f, h * 0.55f, 0.05f); glutSolidCube(1); glPopMatrix();
    }
}

/* Water tower */
void drawWaterTower(float x, float z) {
    glColor3f(0.82f, 0.82f, 0.85f);
    /* Legs */
    int li;
    for (li = 0; li < 4; li++) {
        float la = (float)li * 90.0f * DEG2RAD;
        glPushMatrix(); glTranslatef(x + 1.2f * cosf(la), 3.0f, z + 1.2f * sinf(la));
        glScalef(0.12f, 6.0f, 0.12f); glutSolidCube(1); glPopMatrix();
    }
    /* Tank */
    glColor3f(0.88f, 0.88f, 0.90f);
    glPushMatrix(); glTranslatef(x, 7.5f, z);
    glRotatef(-90, 1, 0, 0); cyl(1.2f, 1.2f, 2.8f, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(x, 10.3f, z); glutSolidSphere(1.22f, 12, 10); glPopMatrix();
}

/* Antenna / comms dish */
void drawAntenna(float x, float z, float h) {
    glColor3f(0.65f, 0.65f, 0.68f);
    glPushMatrix(); glTranslatef(x, h * 0.5f, z); glScalef(0.08f, h, 0.08f); glutSolidCube(1); glPopMatrix();
    glColor3f(0.70f, 0.70f, 0.72f);
    glPushMatrix(); glTranslatef(x, h, z); glRotatef(40, 1, 0, 0);
    glScalef(1.0f, 0.3f, 1.0f); glutSolidSphere(0.7f, 8, 6); glPopMatrix();
}

/* Street / area lamp post */
void drawLampPost(float x, float z) {
    /* Pole */
    glColor3f(0.55f, 0.55f, 0.58f);
    glPushMatrix(); glTranslatef(x, 2.5f, z); glScalef(0.09f, 5.0f, 0.09f); glutSolidCube(1); glPopMatrix();
    /* Arm */
    glPushMatrix(); glTranslatef(x + 0.4f, 5.2f, z); glScalef(0.8f, 0.07f, 0.07f); glutSolidCube(1); glPopMatrix();
    /* Lamp head */
    glColor3f(0.25f, 0.25f, 0.22f);
    glPushMatrix(); glTranslatef(x + 0.8f, 5.1f, z); glScalef(0.28f, 0.14f, 0.18f); glutSolidCube(1); glPopMatrix();
    /* Warm glow point — bright yellow-white */
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0f, 0.95f, 0.60f, 0.55f);
    glPushMatrix(); glTranslatef(x + 0.8f, 5.0f, z); glutSolidSphere(0.22f, 6, 4); glPopMatrix();
    glColor4f(1.0f, 0.90f, 0.40f, 0.18f);
    glPushMatrix(); glTranslatef(x + 0.8f, 4.8f, z); glutSolidSphere(0.55f, 6, 4); glPopMatrix();
    glDisable(GL_BLEND); glEnable(GL_LIGHTING);
}

/* Draw a ground-level haze quad for atmosphere depth */
void drawHazeBand(void) {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* Thin milky haze at horizon level */
    glBegin(GL_QUADS);
    glColor4f(0.75f, 0.82f, 0.90f, 0.0f);  glVertex3f(-500, 8.0f, -300); glVertex3f(500, 8.0f, -300);
    glColor4f(0.75f, 0.82f, 0.90f, 0.38f); glVertex3f(500, -0.3f, -300); glVertex3f(-500, -0.3f, -300);
    glEnd();
    glBegin(GL_QUADS);
    glColor4f(0.75f, 0.82f, 0.90f, 0.0f);  glVertex3f(-500, 8.0f, -300); glVertex3f(500, 8.0f, -300);
    glColor4f(0.75f, 0.82f, 0.90f, 0.22f); glVertex3f(500, -0.3f, -60);   glVertex3f(-500, -0.3f, -60);
    glEnd();
    glDisable(GL_BLEND); glEnable(GL_LIGHTING);
}

void drawTerrain(void) {
    float t = (float)glutGet(GLUT_ELAPSED_TIME) * 0.001f;
    int i;
    for (i = 0; i < NUM_CLOUDS; i++) clouds[i].x += clouds[i].drift;
    float curveAmt = clampf(rocketY / 40.0f, 0.0f, 1.0f);

    /* ============================================================
       SKY — rich layered gradient: warm horizon → vivid blue zenith
       matching KSC reference photo atmosphere
    ============================================================ */
    glDisable(GL_LIGHTING); glDepthMask(GL_FALSE);
    {
        /* Layer 1: sandy/warm horizon glow */
        glBegin(GL_QUADS);
        glColor3f(skyR * 1.08f * 0.95f, skyG * 0.82f * 0.95f, skyB * 0.55f * 0.95f);
        glVertex3f(-800, -10, -400); glVertex3f(800, -10, -400);
        glColor3f(skyR * 1.04f * 0.90f, skyG * 0.90f * 0.90f, skyB * 0.72f * 0.90f);
        glVertex3f(800, 18, -400); glVertex3f(-800, 18, -400);
        glEnd();
        /* Layer 2: mid sky — soft peachy blue */
        glBegin(GL_QUADS);
        glColor3f(skyR * 1.04f * 0.90f, skyG * 0.90f * 0.90f, skyB * 0.72f * 0.90f);
        glVertex3f(-800, 18, -400); glVertex3f(800, 18, -400);
        glColor3f(skyR * 0.82f, skyG * 0.90f, skyB * 1.05f);
        glVertex3f(800, 65, -400); glVertex3f(-800, 65, -400);
        glEnd();
        /* Layer 3: upper deep blue */
        glBegin(GL_QUADS);
        glColor3f(skyR * 0.82f, skyG * 0.90f, skyB * 1.05f);
        glVertex3f(-800, 65, -400); glVertex3f(800, 65, -400);
        glColor3f(skyR * 0.28f, skyG * 0.38f, skyB * 0.95f);
        glVertex3f(800, 300, -400); glVertex3f(-800, 300, -400);
        glEnd();
    }
    glDepthMask(GL_TRUE); glEnable(GL_LIGHTING);

    if (curveAmt < 0.95f) {

        /* ---- GROUND BASE LAYERS ---- */
        /* Sandy scrubland base (KSC sits on Florida barrier island) */
        glColor3f(0.62f, 0.59f, 0.48f);
        glPushMatrix(); glTranslatef(0, -0.55f, 0); glScalef(700, 0.6f, 500); glutSolidCube(1); glPopMatrix();

        /* Large lush grass areas */
        glColor3f(0.20f, 0.46f, 0.14f);
        glPushMatrix(); glTranslatef(-45, -0.19f, 8);  glScalef(60, 0.24f, 100); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(42, -0.19f, 4);  glScalef(50, 0.24f, 90); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(0, -0.19f, 50);  glScalef(220, 0.24f, 55); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(0, -0.19f, -105); glScalef(320, 0.24f, 90); glutSolidCube(1); glPopMatrix();
        /* Lighter bright-green patches (well-watered lawn) */
        glColor3f(0.25f, 0.54f, 0.16f);
        glPushMatrix(); glTranslatef(-10, -0.18f, 18); glScalef(12, 0.20f, 42); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(24, -0.18f, 12); glScalef(14, 0.20f, 36); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(-60, -0.18f, -18); glScalef(16, 0.20f, 30); glutSolidCube(1); glPopMatrix();

        /* Concrete pad around launch structure */
        glColor3f(0.64f, 0.62f, 0.58f);
        glPushMatrix(); glTranslatef(0, -0.18f, 0); glScalef(24, 0.20f, 24); glutSolidCube(1); glPopMatrix();
        /* Slightly darker secondary apron */
        glColor3f(0.56f, 0.54f, 0.50f);
        glPushMatrix(); glTranslatef(0, -0.17f, 0); glScalef(38, 0.16f, 38); glutSolidCube(1); glPopMatrix();
        /* Concrete stain / marking lines */
        glColor3f(0.48f, 0.46f, 0.43f);
        for (i = 0; i < 4; i++) {
            glPushMatrix(); glTranslatef(-14 + i * 9, -0.16f, 0); glScalef(0.18f, 0.12f, 38); glutSolidCube(1); glPopMatrix();
        }

        /* ---- CRAWLERWAY — wide track from VAB to pad ---- */
        /* Gravel crawlerway surface */
        glColor3f(0.52f, 0.50f, 0.45f);
        glPushMatrix(); glTranslatef(35, -0.38f, 0);  glScalef(62, 0.06f, 8.0f); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(-32, -0.38f, 0); glScalef(50, 0.06f, 7.5f); glutSolidCube(1); glPopMatrix();
        /* Crawlerway grass median */
        glColor3f(0.22f, 0.50f, 0.14f);
        glPushMatrix(); glTranslatef(35, -0.35f, 0);  glScalef(60, 0.08f, 2.5f); glutSolidCube(1); glPopMatrix();

        /* ---- ROAD NETWORK ---- */
        glColor3f(0.28f, 0.27f, 0.25f);
        /* Main access road toward pad */
        glPushMatrix(); glTranslatef(0, -0.39f, 32); glScalef(5.5f, 0.06f, 60); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(0, -0.39f, -38); glScalef(4.5f, 0.06f, 42); glutSolidCube(1); glPopMatrix();
        /* Perimeter road */
        glPushMatrix(); glTranslatef(-28, -0.39f, 0); glScalef(40, 0.06f, 4.5f); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(55, -0.39f, -5); glScalef(30, 0.06f, 4.0f); glutSolidCube(1); glPopMatrix();
        /* Road edge white lines */
        glColor3f(0.88f, 0.88f, 0.88f);
        for (i = 0; i < 7; i++) {
            float rx = -22.0f + (float)i * 7.5f;
            glPushMatrix(); glTranslatef(rx, -0.37f, -38); glScalef(3.5f, 0.03f, 0.20f); glutSolidCube(1); glPopMatrix();
        }
        /* Yellow centerline */
        glColor3f(0.90f, 0.82f, 0.10f);
        glPushMatrix(); glTranslatef(0, -0.37f, 32); glScalef(0.18f, 0.03f, 58); glutSolidCube(1); glPopMatrix();

        /* ---- VAB (Vehicle Assembly Building) — huge landmark ---- */
        /* Main body */
        glColor3f(0.76f, 0.76f, 0.78f);
        glPushMatrix(); glTranslatef(-85, -0.5f, -55); glScalef(30, 34, 24); glutSolidCube(1); glPopMatrix();
        /* Dark horizontal band */
        glColor3f(0.30f, 0.32f, 0.38f);
        glPushMatrix(); glTranslatef(-85, 7.5f, -55); glScalef(30.2f, 2.5f, 24.2f); glutSolidCube(1); glPopMatrix();
        /* US flag blue field hint */
        glColor3f(0.15f, 0.22f, 0.60f);
        glPushMatrix(); glTranslatef(-85, 22.0f, -42.8f); glScalef(10, 14, 0.15f); glutSolidCube(1); glPopMatrix();
        /* VAB vertical stripe panels */
        glColor3f(0.60f, 0.60f, 0.62f);
        for (i = 0; i < 5; i++) {
            float vx = -85 - 12.0f + (float)i * 6.0f;
            glPushMatrix(); glTranslatef(vx, 17.0f, -42.7f); glScalef(0.35f, 34, 0.12f); glutSolidCube(1); glPopMatrix();
        }
        /* Low annex building attached to VAB */
        glColor3f(0.68f, 0.67f, 0.65f);
        glPushMatrix(); glTranslatef(-85, -0.5f, -38); glScalef(22, 10, 8); glutSolidCube(1); glPopMatrix();

        /* ---- LAUNCH CONTROL CENTER ---- */
        drawBuilding(-58, -0.5f, -28, 20, 7, 13, 0.70f, 0.69f, 0.67f);
        /* LCC glass front */
        glColor3f(0.52f, 0.65f, 0.78f);
        glPushMatrix(); glTranslatef(-58, 3.2f, -21.3f); glScalef(18, 5.5f, 0.12f); glutSolidCube(1); glPopMatrix();

        /* ---- SUPPORT BUILDINGS ---- */
        drawBuilding(58, -0.5f, -42, 15, 5.5f, 11, 0.68f, 0.67f, 0.65f);
        drawBuilding(-40, -0.5f, 32, 11, 5.0f, 9, 0.65f, 0.64f, 0.62f);
        drawBuilding(48, -0.5f, 28, 9, 4.5f, 8, 0.66f, 0.65f, 0.63f);
        drawBuilding(62, -0.5f, 12, 10, 4.0f, 8, 0.64f, 0.63f, 0.61f);
        /* A-frame maintenance shed */
        glColor3f(0.60f, 0.59f, 0.58f);
        glPushMatrix(); glTranslatef(28, -0.5f, -32); glScalef(8, 5, 6); glutSolidCube(1); glPopMatrix();
        /* Slanted roof hint */
        glColor3f(0.50f, 0.49f, 0.47f);
        glPushMatrix(); glTranslatef(28, 4.5f, -32); glRotatef(22, 0, 0, 1); glScalef(9, 0.5f, 6.2f); glutSolidCube(1); glPopMatrix();

        /* ---- FUEL / LOX TANKS ---- */
        /* Big spherical tanks */
        glColor3f(0.84f, 0.84f, 0.86f);
        glPushMatrix(); glTranslatef(36, -0.5f + 4.0f, -22); glutSolidSphere(4.0f, 16, 12); glPopMatrix();
        glPushMatrix(); glTranslatef(43, -0.5f + 3.2f, -20); glutSolidSphere(3.2f, 14, 10); glPopMatrix();
        glPushMatrix(); glTranslatef(30, -0.5f + 2.8f, -26); glutSolidSphere(2.8f, 12, 9); glPopMatrix();
        /* Tank support columns */
        glColor3f(0.58f, 0.58f, 0.60f);
        glPushMatrix(); glTranslatef(36, -0.5f, -22); glScalef(0.45f, 8.0f, 0.45f); glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(43, -0.5f, -20); glScalef(0.40f, 6.4f, 0.40f); glutSolidCube(1); glPopMatrix();
        /* Pipe connections */
        glColor3f(0.55f, 0.55f, 0.57f);
        glPushMatrix(); glTranslatef(39.5f, 4.0f, -21); glScalef(7, 0.25f, 0.25f); glutSolidCube(1); glPopMatrix();

        /* ---- WATER TOWERS ---- */
        drawWaterTower(-50, -20);
        drawWaterTower(52, 20);

        /* ---- COMMS ANTENNAS ---- */
        drawAntenna(-28, -48, 9);
        drawAntenna(32, -52, 7);
        drawAntenna(66, -22, 8);
        drawAntenna(-70, 10, 6);

        /* ---- LAMP POSTS along roads and pad ---- */
        /* Along main access road */
        for (i = 0; i < 6; i++) {
            drawLampPost(-3.5f, 12.0f + (float)i * 10.0f);
            drawLampPost(3.5f, 12.0f + (float)i * 10.0f);
        }
        /* Around pad perimeter */
        drawLampPost(-14, -14); drawLampPost(14, -14);
        drawLampPost(-14, 14); drawLampPost(14, 14);
        /* Along crawlerway */
        for (i = 0; i < 5; i++) {
            drawLampPost(12.0f + (float)i * 10.0f, 5.5f);
            drawLampPost(12.0f + (float)i * 10.0f, -5.5f);
        }
        /* LCC parking area lights */
        drawLampPost(-62, -18); drawLampPost(-58, -18); drawLampPost(-54, -18);

        /* ---- PARKING LOTS ---- */
        glColor3f(0.30f, 0.29f, 0.28f);
        glPushMatrix(); glTranslatef(-58, -0.37f, -14); glScalef(14, 0.04f, 9);  glutSolidCube(1); glPopMatrix();
        glPushMatrix(); glTranslatef(55, -0.37f, -30); glScalef(10, 0.04f, 7);  glutSolidCube(1); glPopMatrix();
        /* Parked cars (colored boxes) */
        float carX[6] = { -61,-58,-55,-52,-62,-59 };
        float carZ[6] = { -12,-12,-12,-12,-16,-16 };
        float carR[6] = { 0.65f,0.85f,0.15f,0.55f,0.88f,0.22f };
        float carG[6] = { 0.12f,0.82f,0.28f,0.52f,0.12f,0.20f };
        float carB2[6] = { 0.12f,0.80f,0.62f,0.55f,0.14f,0.62f };
        for (i = 0; i < 6; i++) {
            glColor3f(carR[i], carG[i], carB2[i]);
            glPushMatrix(); glTranslatef(carX[i], -0.18f, carZ[i]); glScalef(0.75f, 0.38f, 1.4f); glutSolidCube(1); glPopMatrix();
        }

        /* ---- FLAGPOLES ---- */
        glColor3f(0.72f, 0.72f, 0.75f);
        float fpX[3] = { -60,-57.5f,-55 }; float fpH[3] = { 6,5.5f,5 };
        for (i = 0; i < 3; i++) {
            glPushMatrix(); glTranslatef(fpX[i], -0.5f, -8); glScalef(0.09f, fpH[i] * 2, 0.09f); glutSolidCube(1); glPopMatrix();
        }
        /* Flags */
        glColor3f(0.82f, 0.12f, 0.12f);
        glPushMatrix(); glTranslatef(fpX[0] + 0.7f, fpH[0], -8); glScalef(1.5f, 0.06f, 0.04f); glutSolidCube(1); glPopMatrix();
        glColor3f(0.12f, 0.20f, 0.72f);
        glPushMatrix(); glTranslatef(fpX[1] + 0.7f, fpH[1], -8); glScalef(1.5f, 0.06f, 0.04f); glutSolidCube(1); glPopMatrix();
        glColor3f(0.88f, 0.88f, 0.88f);
        glPushMatrix(); glTranslatef(fpX[2] + 0.7f, fpH[2], -8); glScalef(1.5f, 0.06f, 0.04f); glutSolidCube(1); glPopMatrix();

        /* ---- BEACH / OCEAN ---- */
        /* Beach sand strip */
        glColor3f(0.80f, 0.76f, 0.60f);
        glPushMatrix(); glTranslatef(0, -0.42f, -85); glScalef(600, 0.14f, 18); glutSolidCube(1); glPopMatrix();
        /* Shallow turquoise coastal water */
        glDisable(GL_LIGHTING); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.14f, 0.52f, 0.72f, 0.90f);
        glBegin(GL_QUADS);
        glVertex3f(-600, -0.40f, -300); glVertex3f(600, -0.40f, -300);
        glVertex3f(600, -0.40f, -100);  glVertex3f(-600, -0.40f, -100);
        glEnd();
        /* Deeper ocean tint */
        glColor4f(0.06f, 0.22f, 0.58f, 0.92f);
        glBegin(GL_QUADS);
        glVertex3f(-600, -0.40f, -300); glVertex3f(600, -0.40f, -300);
        glVertex3f(600, -0.40f, -160);  glVertex3f(-600, -0.40f, -160);
        glEnd();
        /* Animated sun glint on water */
        float wt = sinf(t * 1.6f) * 0.5f + 0.5f;
        glColor4f(0.80f, 0.90f, 1.00f, 0.22f * wt);
        glBegin(GL_QUADS);
        glVertex3f(-600, -0.35f, -300); glVertex3f(600, -0.35f, -300);
        glVertex3f(600, -0.35f, -100);  glVertex3f(-600, -0.35f, -100);
        glEnd();
        glDisable(GL_BLEND); glEnable(GL_LIGHTING);

        /* Banana River intracoastal waterway */
        glDisable(GL_LIGHTING); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.10f, 0.35f, 0.60f, 0.78f);
        glBegin(GL_QUADS);
        glVertex3f(70, -0.36f, -85); glVertex3f(130, -0.36f, -85);
        glVertex3f(130, -0.36f, 55); glVertex3f(70, -0.36f, 55);
        glEnd();
        glDisable(GL_BLEND); glEnable(GL_LIGHTING);

        /* ---- DENSE TREELINES ---- */
        glEnable(GL_LIGHTING);
        /* Back horizon treeline — wide dense belt */
        {
            int ti;
            for (ti = 0; ti < 70; ti++) {
                float tx3 = -150.0f + (float)ti * 4.5f;
                float tz3 = -93.0f - (float)((ti * 5) % 12) * 1.2f;
                float th2 = 4.0f + (float)((ti * 7 + 2) % 6) * 0.9f;
                float grn = 0.28f + (float)((ti * 3) % 5) * 0.04f;
                drawTree(tx3, -0.5f, tz3, th2, 0.10f + ((ti * 2) % 3) * 0.02f, grn, 0.07f + (ti % 3) * 0.01f);
            }
            /* Second thicker row */
            for (ti = 0; ti < 50; ti++) {
                float tx3 = -140.0f + (float)ti * 6.0f;
                float tz3 = -82.0f - (float)((ti * 3) % 9) * 1.5f;
                float th2 = 3.5f + (float)((ti * 5) % 5) * 1.0f;
                drawTree(tx3, -0.5f, tz3, th2, 0.12f, 0.32f + (ti % 4) * 0.03f, 0.08f);
            }
        }
        /* Left treeline */
        {
            int ti;
            for (ti = 0; ti < 38; ti++) {
                float tz3 = -85.0f + (float)ti * 5.0f;
                float tx3 = -95.0f - (float)((ti * 4) % 10) * 1.5f;
                float th2 = 3.8f + (float)((ti * 3) % 5) * 0.8f;
                drawTree(tx3, -0.5f, tz3, th2, 0.11f, 0.31f + (ti % 3) * 0.03f, 0.08f);
            }
        }
        /* Right treeline + river bank vegetation */
        {
            int ti;
            for (ti = 0; ti < 38; ti++) {
                float tz3 = -80.0f + (float)ti * 5.5f;
                float tx3 = 90.0f + (float)((ti * 3) % 10) * 1.5f;
                float th2 = 3.6f + (float)((ti * 4) % 6) * 0.7f;
                drawTree(tx3, -0.5f, tz3, th2, 0.12f, 0.30f + (ti % 4) * 0.03f, 0.09f);
            }
            /* River bank mangroves / palms */
            for (ti = 0; ti < 10; ti++) {
                drawPalm(68.0f + (float)((ti * 3) % 5) * 1.0f, -0.5f, -60.0f + (float)ti * 12.0f, 4.0f + (ti % 3) * 0.5f);
            }
        }
        /* Palm rows lining access roads */
        {
            int ti;
            for (ti = 0; ti < 14; ti++) {
                float pz3 = 10.0f + (float)ti * 8.0f;
                drawPalm(-4.5f, -0.5f, pz3, 4.2f + (ti % 3) * 0.5f);
                drawPalm(4.5f, -0.5f, pz3, 4.0f + (ti % 3) * 0.6f);
            }
        }
        /* Scattered ornamental trees near buildings */
        drawTree(-68, -0.5f, -26, 5.2f, 0.12f, 0.38f, 0.10f);
        drawTree(-65, -0.5f, -32, 4.6f, 0.14f, 0.34f, 0.09f);
        drawTree(-72, -0.5f, -30, 5.0f, 0.11f, 0.36f, 0.10f);
        drawTree(50, -0.5f, -28, 5.2f, 0.13f, 0.36f, 0.10f);
        drawTree(55, -0.5f, -34, 4.8f, 0.12f, 0.34f, 0.09f);
        drawTree(-38, -0.5f, 36, 4.5f, 0.14f, 0.37f, 0.11f);
        drawTree(-34, -0.5f, 28, 5.0f, 0.13f, 0.35f, 0.10f);
        drawTree(40, -0.5f, 34, 4.2f, 0.12f, 0.36f, 0.10f);
        /* Palms near LCC entrance */
        drawPalm(-50, -0.5f, -20, 5.0f); drawPalm(-48, -0.5f, -25, 4.5f);
        drawPalm(-45, -0.5f, -18, 4.8f);

        /* ---- FLAT SCRUB HORIZON (Florida is flat!) ---- */
        glDisable(GL_LIGHTING);
        /* Dark tree mass silhouette — no mountains */
        glBegin(GL_QUADS);
        glColor3f(0.09f, 0.20f, 0.07f);
        glVertex3f(-500, -0.5f, -97); glVertex3f(500, -0.5f, -97);
        glVertex3f(500, 5.5f, -97); glVertex3f(-500, 5.5f, -97);
        glEnd();
        glBegin(GL_QUADS);
        glColor3f(0.12f, 0.27f, 0.09f);
        glVertex3f(-500, 3.5f, -97); glVertex3f(500, 3.5f, -97);
        glVertex3f(500, 7.5f, -97); glVertex3f(-500, 7.5f, -97);
        glEnd();
        /* Lighter canopy tops catching light */
        glBegin(GL_QUADS);
        glColor3f(0.17f, 0.36f, 0.11f);
        glVertex3f(-500, 6.0f, -97); glVertex3f(500, 6.0f, -97);
        glVertex3f(500, 8.5f, -97); glVertex3f(-500, 8.5f, -97);
        glEnd();
        glEnable(GL_LIGHTING);

        /* ---- ATMOSPHERIC HAZE ---- */
        drawHazeBand();
    }

    /* ---- ROUND EARTH (ascent phase) ---- */
    if (curveAmt > 0.05f) {
        float earthR = 380.0f;
        glPushMatrix();
        glTranslatef(0.0f, -earthR * 0.94f - rocketY * 2.8f, 0.0f);
        glColor3f(0.08f, 0.28f, 0.68f); glutSolidSphere((double)earthR, 64, 64);
        glColor3f(0.18f, 0.52f, 0.18f);
        for (i = 0; i < 7; i++) {
            glPushMatrix();
            glRotatef((float)i * 51.4f, 0.35f, 1.0f, 0.22f);
            glRotatef(28.0f * sinf((float)i * 1.4f), 1, 0, 0);
            glTranslatef(0, 0, earthR * 0.97f);
            glScalef(1.0f, 0.55f, 0.14f);
            glutSolidSphere(earthR * 0.30f + 35.0f * sinf((float)i * 2.0f), 12, 12);
            glPopMatrix();
        }
        glColor3f(0.92f, 0.95f, 0.99f);
        glPushMatrix(); glTranslatef(0, earthR * 0.91f, 0); glScalef(1, 0.28f, 1); glutSolidSphere(earthR * 0.34f, 20, 20); glPopMatrix();
        glPushMatrix(); glTranslatef(0, -earthR * 0.91f, 0); glScalef(1, 0.28f, 1); glutSolidSphere(earthR * 0.30f, 20, 20); glPopMatrix();
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 1, 0.16f * curveAmt);    glutSolidSphere((double)(earthR + 7.0f), 48, 48);
        glColor4f(0.3f, 0.6f, 1.0f, 0.12f * curveAmt); glutSolidSphere((double)(earthR + 22.0f), 40, 40);
        glDisable(GL_BLEND);
        glPopMatrix();
    }

    /* ---- CLOUDS — big puffy Florida cumulus ---- */
    float cloudVis = clampf(1.0f - rocketY / 25.0f, 0, 1);
    if (cloudVis > 0.01f) {
        glDisable(GL_LIGHTING); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (i = 0; i < NUM_CLOUDS; i++) {
            float a = clouds[i].alpha * cloudVis;
            float cs = clouds[i].size;
            glPushMatrix(); glTranslatef(clouds[i].x, clouds[i].y, clouds[i].z);
            /* Dark grey underside */
            glColor4f(0.76f, 0.76f, 0.78f, a * 0.55f);
            glPushMatrix(); glScalef(1.0f, 0.35f, 1.0f); glutSolidSphere(cs, 10, 6); glPopMatrix();
            /* Bright white main body */
            glColor4f(0.98f, 0.98f, 1.00f, a * 0.82f);
            glutSolidSphere(cs * 0.88f, 10, 7);
            /* Extra puffs */
            glColor4f(1.0f, 1.0f, 1.0f, a * 0.75f);
            glPushMatrix(); glTranslatef(cs * 0.75f, cs * 0.18f, 0);   glutSolidSphere(cs * 0.72f, 8, 5); glPopMatrix();
            glPushMatrix(); glTranslatef(-cs * 1.25f, cs * 0.22f, 0);   glutSolidSphere(cs * 0.62f, 8, 5); glPopMatrix();
            glPushMatrix(); glTranslatef(cs * 0.30f, cs * 0.40f, cs * 0.38f); glutSolidSphere(cs * 0.50f, 6, 4); glPopMatrix();
            glPushMatrix(); glTranslatef(-cs * 0.15f, cs * 0.52f, cs * 0.18f); glutSolidSphere(cs * 0.38f, 6, 4); glPopMatrix();
            glPopMatrix();
        }
        glDisable(GL_BLEND); glEnable(GL_LIGHTING);
    }
}

// ================================================================
// STARS
// ================================================================
void drawStars(void) {
    float a = (phase == PHASE_SPACE) ? 1.0f : starAlpha; if (a < 0.01f)return;
    glDisable(GL_LIGHTING); glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    float t2 = (float)glutGet(GLUT_ELAPSED_TIME) * 0.001f;
    glPointSize(2.8f); glBegin(GL_POINTS);
    for (int i = 0; i < MAX_STARS; i++) {
        float tw = 0.65f + 0.35f * sinf(t2 * stars[i].tspeed + stars[i].twinkle);
        glColor4f(0.95f, 0.97f, 1.0f, stars[i].bright * tw * a);
        if (phase == PHASE_SPACE)
            glVertex3f(stars[i].x + rocketSX * 0.015f, stars[i].y + rocketSY * 0.015f, stars[i].z + rocketSZ * 0.015f);
        else
            glVertex3f(stars[i].x, stars[i].y + rocketY * 0.04f, stars[i].z);
    }
    glEnd(); glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_LIGHTING);
}

// ================================================================
// SUN
// ================================================================
void drawSun(void) {
    float t2 = (float)glutGet(GLUT_ELAPSED_TIME) * 0.001f;
    float pulse = 1.0f + 0.03f * sinf(t2 * 0.7f);
    glDisable(GL_LIGHTING); glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0f, 0.60f, 0.10f, 0.025f); glutSolidSphere(60.0f * pulse, 24, 24);
    glColor4f(1.0f, 0.65f, 0.12f, 0.05f);  glutSolidSphere(48.0f * pulse, 24, 24);
    glColor4f(1.0f, 0.70f, 0.15f, 0.08f);  glutSolidSphere(38.0f, 24, 24);
    glColor4f(1.0f, 0.78f, 0.20f, 0.12f);  glutSolidSphere(29.0f, 24, 24);
    glColor4f(1.0f, 0.85f, 0.25f, 0.16f);  glutSolidSphere(22.0f, 24, 24);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glColor3f(1.0f, 0.97f, 0.82f); glutSolidSphere(12.8f, 48, 48);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 0.78f, 0.12f, 0.30f); glutSolidSphere(13.6f, 48, 48);
    glDisable(GL_BLEND);
    glPushMatrix(); glRotatef(t2 * 7.0f, 0, 1, 0);
    glColor3f(0.82f, 0.52f, 0.04f);
    glPushMatrix(); glTranslatef(10.5f, 5.0f, 0); glScalef(1, 0.45f, 1); glutSolidSphere(1.9f, 10, 10); glPopMatrix();
    glPushMatrix(); glTranslatef(-8.0f, -4.5f, 7.0f); glScalef(0.7f, 0.35f, 0.7f); glutSolidSphere(1.5f, 10, 10); glPopMatrix();
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

// ================================================================
// SOLAR SYSTEM
// ================================================================
void drawRing(float inner, float outer, float r, float g, float b) {
    glDisable(GL_LIGHTING); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    int steps = 100; float dA = 2.0f * PI / (float)steps;
    glColor4f(r, g, b, 0.22f);
    glBegin(GL_TRIANGLE_STRIP);
    for (int s = 0; s <= steps; s++) { float a2 = (float)s * dA; glVertex3f(inner * cosf(a2), 0, inner * sinf(a2)); glVertex3f(outer * cosf(a2), 0, outer * sinf(a2)); }
    glEnd();
    for (int layer = 0; layer < 5; layer++) {
        float frac = (float)layer / 4.0f, rad = inner + (outer - inner) * frac;
        glColor4f(r * 1.1f, g * 1.1f, b * 1.1f, 0.4f * (1.0f - fabsf(frac - 0.5f) * 1.5f));
        glBegin(GL_LINE_LOOP);
        for (int s = 0; s < steps; s++) { float a2 = (float)s * dA; glVertex3f(rad * cosf(a2), 0, rad * sinf(a2)); }
        glEnd();
    }
    glDisable(GL_BLEND); glEnable(GL_LIGHTING);
}

void drawOrbitTrail(float dist) {
    glDisable(GL_LIGHTING); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.45f, 0.45f, 0.70f, 0.09f); int steps = 140;
    glBegin(GL_LINE_LOOP);
    for (int s = 0; s < steps; s++) { float a2 = 2.0f * PI * (float)s / (float)steps; glVertex3f(dist * cosf(a2), 0, dist * sinf(a2)); }
    glEnd(); glDisable(GL_BLEND); glEnable(GL_LIGHTING);
}

void drawSolarSystem(void) {
    float t2 = (float)glutGet(GLUT_ELAPSED_TIME) * 0.001f;
    drawSun();

    // ---- ASTEROID BELT (between Mars and Jupiter) ----
    glDisable(GL_LIGHTING);
    for (int i = 0; i < NUM_ASTEROIDS; i++) {
        float ax = asteroids[i].dist * cosf(asteroids[i].angle);
        float ay = asteroids[i].y;
        float az = asteroids[i].dist * sinf(asteroids[i].angle);
        float brt = 0.55f + 0.35f * sinf(asteroids[i].angle * 7.3f);
        glColor3f(brt * 0.62f, brt * 0.58f, brt * 0.52f);
        glPushMatrix();
        glTranslatef(ax, ay, az);
        glRotatef(asteroids[i].angle * 57.3f, 0.5f, 1.0f, 0.3f);
        // irregular rocky shape via scaled sphere
        glScalef(1.0f, 0.65f + 0.35f * sinf((float)i * 1.7f), 0.8f + 0.4f * cosf((float)i * 2.3f));
        glutSolidSphere((double)asteroids[i].size, 5, 4);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);

    for (int i = 0; i < NUM_PLANETS; i++) {
        Planet* pl = &planets[i];
        drawOrbitTrail(pl->dist);
        glPushMatrix();
        glRotatef(pl->orbitAngle * (180.0f / PI), 0, 1, 0);
        glTranslatef(pl->dist, 0, 0);
        glRotatef(pl->axialTilt, 0, 0, 1);
        glRotatef(pl->selfRotAngle, 0, 1, 0);

        // ---- MERCURY ----
        if (i == 0) {
            // Base grey-brown rocky surface
            glColor3f(0.68f, 0.62f, 0.55f);
            glutSolidSphere((double)pl->radius, 48, 48);
            // Darker lowland regions
            glColor3f(0.50f, 0.46f, 0.40f);
            for (int c = 0; c < 8; c++) {
                glPushMatrix();
                glRotatef((float)c * 45.0f + 15.0f, 0.5f, 1.0f, 0.3f);
                glTranslatef(pl->radius * 0.92f, 0, 0);
                glScalef(0.8f, 0.5f, 0.8f);
                glutSolidSphere(pl->radius * 0.18f, 8, 8);
                glPopMatrix();
            }
            // Impact craters (light rims)
            glColor3f(0.78f, 0.74f, 0.68f);
            for (int c = 0; c < 12; c++) {
                glPushMatrix();
                glRotatef((float)c * 30.0f, 0.3f + sinf((float)c), 1.0f, 0.2f + cosf((float)c));
                glTranslatef(pl->radius * 0.99f, 0, 0);
                glScalef(1.0f, 0.15f, 1.0f);
                glutSolidSphere(pl->radius * 0.12f, 7, 5);
                glPopMatrix();
            }
        }

        // ---- VENUS ----
        else if (i == 1) {
            // Yellowish-white base
            glColor3f(0.90f, 0.80f, 0.42f);
            glutSolidSphere((double)pl->radius, 48, 48);
            // Swirling thick cloud layers
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            float vt = t2 * 0.08f;
            for (int layer = 0; layer < 4; layer++) {
                float la = (float)layer * 0.25f;
                glColor4f(0.95f, 0.88f, 0.55f + la * 0.1f, 0.38f - la * 0.07f);
                glPushMatrix();
                glRotatef(vt * (1.0f + la) * 15.0f, 0, 1, 0);
                glScalef(1.0f, 0.18f + la * 0.05f, 1.0f);
                glutSolidSphere((double)(pl->radius * (1.03f + la * 0.03f)), 32, 8);
                glPopMatrix();
            }
            // Thick outer atmosphere glow
            glColor4f(0.98f, 0.90f, 0.40f, 0.22f);
            glutSolidSphere((double)(pl->radius * 1.12f), 32, 32);
            glDisable(GL_BLEND);
        }

        // ---- EARTH ----
        else if (i == 2) {
            // Deep ocean blue base
            glColor3f(0.10f, 0.35f, 0.80f);
            glutSolidSphere((double)pl->radius, 64, 64);
            // Continents - multiple varied patches
            float landColors[3][3] = { {0.20f,0.55f,0.18f},{0.55f,0.50f,0.30f},{0.25f,0.60f,0.22f} };
            // Americas
            glColor3f(landColors[0][0], landColors[0][1], landColors[0][2]);
            glPushMatrix(); glRotatef(20, 0, 1, 0); glRotatef(-15, 1, 0, 0);
            glTranslatef(0, 0, pl->radius * 0.97f);
            glScalef(0.7f, 1.4f, 0.12f);
            glutSolidSphere(pl->radius * 0.32f, 14, 14); glPopMatrix();
            // Europe+Africa
            glColor3f(landColors[2][0], landColors[2][1], landColors[2][2]);
            glPushMatrix(); glRotatef(180, 0, 1, 0); glRotatef(10, 1, 0, 0);
            glTranslatef(0, 0, pl->radius * 0.97f);
            glScalef(0.6f, 1.5f, 0.12f);
            glutSolidSphere(pl->radius * 0.30f, 14, 14); glPopMatrix();
            // Asia
            glColor3f(landColors[1][0], landColors[1][1], landColors[1][2]);
            glPushMatrix(); glRotatef(120, 0, 1, 0); glRotatef(20, 1, 0, 0);
            glTranslatef(0, 0, pl->radius * 0.97f);
            glScalef(1.1f, 0.9f, 0.12f);
            glutSolidSphere(pl->radius * 0.34f, 14, 14); glPopMatrix();
            // Australia
            glColor3f(0.60f, 0.52f, 0.30f);
            glPushMatrix(); glRotatef(150, 0, 1, 0); glRotatef(-35, 1, 0, 0);
            glTranslatef(0, 0, pl->radius * 0.97f);
            glScalef(0.6f, 0.5f, 0.12f);
            glutSolidSphere(pl->radius * 0.18f, 10, 10); glPopMatrix();
            // Ice caps
            glColor3f(0.92f, 0.96f, 1.00f);
            glPushMatrix(); glTranslatef(0, pl->radius * 0.94f, 0); glScalef(1, 0.25f, 1);
            glutSolidSphere(pl->radius * 0.38f, 20, 20); glPopMatrix();
            glPushMatrix(); glTranslatef(0, -pl->radius * 0.94f, 0); glScalef(1, 0.25f, 1);
            glutSolidSphere(pl->radius * 0.32f, 20, 20); glPopMatrix();
            // Cloud layer
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1, 1, 1, 0.20f);
            glPushMatrix(); glRotatef(t2 * 3.0f, 0, 1, 0);
            glutSolidSphere((double)(pl->radius * 1.025f), 48, 48); glPopMatrix();
            // Atmosphere blue glow
            glColor4f(0.30f, 0.58f, 1.0f, 0.18f);
            glutSolidSphere((double)(pl->radius * 1.06f), 40, 40);
            glColor4f(0.25f, 0.50f, 0.95f, 0.08f);
            glutSolidSphere((double)(pl->radius * 1.10f), 32, 32);
            glDisable(GL_BLEND);

            // MOON — orbits Earth in world space (drawn inside Earth's transform)
            glPushMatrix();
            // Undo Earth's axial tilt and self-rotation to orbit flat
            glRotatef(-pl->selfRotAngle, 0, 1, 0);
            glRotatef(-pl->axialTilt, 0, 0, 1);
            float moonDist = pl->radius * 2.8f;
            float mx2 = moonDist * cosf(moonAngle);
            float mz2 = moonDist * sinf(moonAngle);
            glTranslatef(mx2, 0, mz2);
            // Moon surface
            glColor3f(0.78f, 0.76f, 0.72f);
            glutSolidSphere((double)(pl->radius * 0.27f), 24, 24);
            // Moon dark maria patches
            glColor3f(0.50f, 0.48f, 0.44f);
            for (int mc = 0; mc < 5; mc++) {
                glPushMatrix();
                glRotatef((float)mc * 72.0f, 0.4f, 1.0f, 0.2f);
                glTranslatef(pl->radius * 0.25f, 0, 0);
                glScalef(0.8f, 0.4f, 0.8f);
                glutSolidSphere(pl->radius * 0.08f, 8, 8);
                glPopMatrix();
            }
            glPopMatrix();
        }

        // ---- MARS ----
        else if (i == 3) {
            // Rust red base
            glColor3f(0.76f, 0.30f, 0.15f);
            glutSolidSphere((double)pl->radius, 56, 56);
            // Lighter highland regions
            glColor3f(0.82f, 0.45f, 0.25f);
            for (int c = 0; c < 5; c++) {
                glPushMatrix();
                glRotatef((float)c * 72.0f, 0.3f, 1.0f, 0.4f);
                glTranslatef(0, 0, pl->radius * 0.96f);
                glScalef(1.0f, 0.6f, 0.12f);
                glutSolidSphere(pl->radius * 0.30f, 12, 12);
                glPopMatrix();
            }
            // Olympus Mons (big shield volcano)
            glColor3f(0.68f, 0.28f, 0.12f);
            glPushMatrix(); glRotatef(45, 0, 1, 0); glTranslatef(0, 0, pl->radius * 0.99f);
            glScalef(1.0f, 0.25f, 1.0f);
            glutSolidSphere(pl->radius * 0.22f, 12, 12); glPopMatrix();
            // Valles Marineris (dark canyon stripe)
            glColor3f(0.45f, 0.18f, 0.08f);
            glPushMatrix(); glRotatef(15, 0, 0, 1); glScalef(1, 0.06f, 1);
            glutSolidSphere((double)(pl->radius * 1.001f), 32, 8); glPopMatrix();
            // Polar ice caps
            glColor3f(0.95f, 0.95f, 0.98f);
            glPushMatrix(); glTranslatef(0, pl->radius * 0.92f, 0); glScalef(1, 0.22f, 1);
            glutSolidSphere(pl->radius * 0.30f, 16, 16); glPopMatrix();
            glPushMatrix(); glTranslatef(0, -pl->radius * 0.92f, 0); glScalef(1, 0.18f, 1);
            glutSolidSphere(pl->radius * 0.22f, 16, 16); glPopMatrix();
            // Dust haze
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.82f, 0.40f, 0.18f, 0.10f);
            glutSolidSphere((double)(pl->radius * 1.04f), 32, 32);
            glDisable(GL_BLEND);
        }

        // ---- JUPITER ----
        else if (i == 4) {
            // Base tan
            glColor3f(0.80f, 0.68f, 0.50f);
            glutSolidSphere((double)pl->radius, 64, 64);
            // Cloud bands — alternating lighter/darker strips
            float jbands[8][3] = {
                {0.88f,0.78f,0.62f},{0.70f,0.54f,0.36f},
                {0.92f,0.82f,0.68f},{0.65f,0.50f,0.32f},
                {0.86f,0.75f,0.58f},{0.72f,0.56f,0.38f},
                {0.90f,0.80f,0.65f},{0.68f,0.52f,0.34f}
            };
            for (int b2 = 0; b2 < 8; b2++) {
                glPushMatrix();
                glRotatef((float)b2 * 22.5f - 78.0f, 1, 0, 0);
                glScalef(1.0f, 0.12f, 1.0f);
                glColor3f(jbands[b2][0], jbands[b2][1], jbands[b2][2]);
                glutSolidSphere((double)(pl->radius * 1.005f), 40, 6);
                glPopMatrix();
            }
            // Great Red Spot
            glColor3f(0.72f, 0.24f, 0.18f);
            glPushMatrix();
            glRotatef(pl->selfRotAngle * 0.25f, 0, 1, 0);
            glTranslatef(pl->radius * 0.95f, -pl->radius * 0.18f, 0);
            glScalef(0.18f, 0.10f, 0.14f);
            glutSolidSphere(pl->radius, 14, 10);
            glPopMatrix();
            // Subtle atmosphere
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.85f, 0.75f, 0.55f, 0.08f);
            glutSolidSphere((double)(pl->radius * 1.03f), 40, 40);
            glDisable(GL_BLEND);
        }

        // ---- SATURN ----
        else if (i == 5) {
            // Pale golden base
            glColor3f(0.90f, 0.82f, 0.60f);
            glutSolidSphere((double)pl->radius, 64, 64);
            // Cloud bands
            float sbands[5][3] = {
                {0.94f,0.86f,0.66f},{0.82f,0.74f,0.52f},
                {0.90f,0.82f,0.62f},{0.86f,0.78f,0.56f},{0.78f,0.70f,0.48f}
            };
            for (int b2 = 0; b2 < 5; b2++) {
                glPushMatrix();
                glRotatef((float)b2 * 36.0f - 72.0f, 1, 0, 0);
                glScalef(1.0f, 0.15f, 1.0f);
                glColor3f(sbands[b2][0], sbands[b2][1], sbands[b2][2]);
                glutSolidSphere((double)(pl->radius * 1.004f), 36, 6);
                glPopMatrix();
            }
            // Polar hexagon hint
            glColor3f(0.75f, 0.65f, 0.42f);
            glPushMatrix(); glTranslatef(0, pl->radius * 0.96f, 0); glScalef(1, 0.10f, 1);
            glutSolidSphere(pl->radius * 0.28f, 6, 6); glPopMatrix();
        }

        // ---- URANUS ----
        else if (i == 6) {
            // Teal-cyan
            glColor3f(0.52f, 0.86f, 0.92f);
            glutSolidSphere((double)pl->radius, 48, 48);
            // Subtle banding
            for (int b2 = 0; b2 < 3; b2++) {
                glPushMatrix();
                glRotatef((float)b2 * 60.0f, 1, 0, 0);
                glScalef(1.0f, 0.10f, 1.0f);
                glColor3f(0.60f, 0.90f, 0.95f);
                glutSolidSphere((double)(pl->radius * 1.004f), 32, 6);
                glPopMatrix();
            }
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.55f, 0.90f, 0.96f, 0.18f);
            glutSolidSphere((double)(pl->radius * 1.05f), 32, 32);
            glDisable(GL_BLEND);
        }

        // ---- NEPTUNE ----
        else if (i == 7) {
            // Deep blue
            glColor3f(0.18f, 0.36f, 0.90f);
            glutSolidSphere((double)pl->radius, 48, 48);
            // Lighter streaks / storm bands
            glColor3f(0.28f, 0.50f, 0.98f);
            for (int b2 = 0; b2 < 3; b2++) {
                glPushMatrix();
                glRotatef((float)b2 * 60.0f + 20.0f, 1, 0, 0);
                glScalef(1.0f, 0.08f, 1.0f);
                glutSolidSphere((double)(pl->radius * 1.003f), 32, 6);
                glPopMatrix();
            }
            // Great Dark Spot
            glColor3f(0.08f, 0.18f, 0.55f);
            glPushMatrix();
            glRotatef(pl->selfRotAngle * 0.4f, 0, 1, 0);
            glTranslatef(pl->radius * 0.94f, pl->radius * 0.12f, 0);
            glScalef(0.15f, 0.09f, 0.12f);
            glutSolidSphere(pl->radius, 12, 10);
            glPopMatrix();
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.20f, 0.40f, 0.95f, 0.16f);
            glutSolidSphere((double)(pl->radius * 1.05f), 32, 32);
            glDisable(GL_BLEND);
        }

        // Rings (Saturn / Uranus)
        if (pl->hasRing) {
            glPushMatrix(); glRotatef(-pl->axialTilt, 0, 0, 1);
            drawRing(pl->ringInner, pl->ringOuter, pl->ringR, pl->ringG, pl->ringB);
            glPopMatrix();
        }

        // Planet name label — use surface distance so large planets don't lose to small ones
        float px2 = pl->dist * cosf(pl->orbitAngle), pz2 = pl->dist * sinf(pl->orbitAngle);
        float dx2 = rocketSX - px2, dy2 = rocketSY, dz2 = rocketSZ - pz2;
        float dp2 = sqrtf(dx2 * dx2 + dy2 * dy2 + dz2 * dz2) - pl->radius;
        if (dp2 < 300.0f) {
            glDisable(GL_LIGHTING); glColor3f(1.0f, 0.9f, 0.3f);
            glRasterPos3f(0, (float)(pl->radius + 2.0f), 0);
            for (const char* c = pl->name; *c; c++)glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
            glEnable(GL_LIGHTING);
        }
        glPopMatrix();
    }
}

// ================================================================
// HUD
// ================================================================
void hud2D(float x, float y, const char* txt, float r, float g, float b) {
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0, (double)winW, 0, (double)winH);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glColor3f(r, g, b); glRasterPos2f(x, y);
    for (const char* c = txt; *c; c++)glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glEnable(GL_LIGHTING);
}
void hud2DBig(float x, float y, const char* txt, float r, float g, float b) {
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0, (double)winW, 0, (double)winH);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glColor3f(r, g, b); glRasterPos2f(x, y);
    for (const char* c = txt; *c; c++)glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, *c);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawHUD(void) {
    char buf[128];
    if (phase == PHASE_LAUNCH) {
        sprintf(buf, "ALTITUDE: %.1f km", rocketY * 12.5f); hud2D(22, (float)(winH - 40), buf, 0.3f, 1.0f, 0.5f);
        sprintf(buf, "VELOCITY: %.2f km/s", rocketSpeed * 180.0f); hud2D(22, (float)(winH - 66), buf, 0.3f, 1.0f, 0.5f);
        if (lstate == ST_IDLE)
            hud2DBig((float)(winW / 2 - 160), (float)(winH / 2 + 10), "PRESS  SPACE  TO  LAUNCH", 1.0f, 0.92f, 0.1f);
        if (lstate == ST_COUNTDOWN) {
            sprintf(buf, "Launching in  %d", countInt);
            hud2DBig((float)(winW / 2 - 130), (float)(winH / 2 + 10), buf, 1.0f, 0.85f, 0.15f);
        }
        if (lstate == ST_HOLD)
            hud2DBig((float)(winW / 2 - 110), (float)(winH / 2 + 10), "IGNITION  ...", 1.0f, 0.5f, 0.1f);
        // LIFTOFF text removed
        if (srbSep) hud2D(22, (float)(winH - 94), "SRB SEPARATION", 1.0f, 0.55f, 0.1f);
        hud2D(22, 22, "SPACE: Launch   R: Reset   Mouse: Look Around", 0.55f, 0.55f, 0.55f);
    }
    else if (phase == PHASE_ORBIT) {
        hud2DBig((float)(winW / 2 - 150), (float)(winH / 2 + 10), "ACHIEVING  ORBIT ...", 0.4f, 0.9f, 1.0f);
    }
    else {
        sprintf(buf, "POS  X:%.0f  Y:%.0f  Z:%.0f", (double)rocketSX, (double)rocketSY, (double)rocketSZ);
        hud2D(22, (float)(winH - 40), buf, 0.3f, 1.0f, 0.5f);
        sprintf(buf, "SPEED: %.2f", (double)flySpeed); hud2D(22, (float)(winH - 66), buf, 0.3f, 1.0f, 0.5f);
        hud2D(22, 22, "W/S=Fwd/Back  A/D=Left/Right  Q/E=Up/Down  Mouse=Look  R=Reset", 0.52f, 0.52f, 0.52f);
        /* Approaching planet: find the planet whose orbit radius is closest
           to the rocket's own distance from the sun — this is robust to
           orbit angle randomness and always picks the right planet         */
        {
            int closestIdx = -1; int i;
            float rocketOrbitDist = sqrtf(rocketSX * rocketSX + rocketSZ * rocketSZ);
            float closestOrbitDiff = 999999.0f;
            for (i = 0; i < NUM_PLANETS; i++) {
                /* How close is the rocket's orbital distance to this planet's orbital distance */
                float orbitDiff = fabsf(rocketOrbitDist - planets[i].dist);
                /* Also check actual XZ distance to the planet's current position */
                float px2 = planets[i].dist * cosf(planets[i].orbitAngle);
                float pz2 = planets[i].dist * sinf(planets[i].orbitAngle);
                float dx2 = rocketSX - px2;
                float dz2 = rocketSZ - pz2;
                float actualSurf = sqrtf(dx2 * dx2 + dz2 * dz2) - planets[i].radius;
                /* Use the better of the two measures */
                float score = (actualSurf < orbitDiff) ? actualSurf : orbitDiff;
                if (score < closestOrbitDiff) {
                    closestOrbitDiff = score;
                    closestIdx = i;
                }
            }
            if (closestIdx >= 0 && closestOrbitDiff < 300.0f) {
                char lbl[64];
                sprintf(lbl, ">> Approaching: %s <<", planets[closestIdx].name);
                hud2DBig((float)(winW / 2 - 160), (float)(winH - 60), lbl, 1.0f, 0.88f, 0.25f);
            }
        }
    }
    hud2D((float)(winW - 100), 22, "R = Reset", 0.48f, 0.48f, 0.48f);
}

// ================================================================
// DISPLAY
// ================================================================
void display(void) {
    if (phase == PHASE_LAUNCH || phase == PHASE_ORBIT)
        glClearColor(skyR, skyG, skyB, 1.0f);
    else
        glClearColor(0.0f, 0.0f, 0.018f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(65.0, (double)winW / (double)winH, 0.05, 3000.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    float yr = camYaw * DEG2RAD, pr = camPitch * DEG2RAD;
    float cd = (phase == PHASE_SPACE) ? CAM_DIST : 13.0f;
    float cx, cy, cz, lx, ly, lz;
    if (phase != PHASE_SPACE) {
        cx = cd * sinf(yr) * cosf(pr); cz = cd * cosf(yr) * cosf(pr); cy = cd * sinf(pr) + rocketY + 3.5f;
        lx = 0; ly = rocketY + 3.0f; lz = 0;
    }
    else {
        cx = rocketSX - cd * sinf(yr) * cosf(pr); cy = rocketSY + cd * sinf(pr) + 2.5f; cz = rocketSZ - cd * cosf(yr) * cosf(pr);
        lx = rocketSX; ly = rocketSY + 1.2f; lz = rocketSZ;
    }
    gluLookAt((double)cx, (double)cy, (double)cz, (double)lx, (double)ly, (double)lz, 0, 1, 0);

    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE); glShadeModel(GL_SMOOTH);
    GLfloat lp[] = { 60,90,50,1 }, ld[] = { 1.0f,0.96f,0.88f,1 }, la[] = { 0.15f,0.15f,0.18f,1 };
    glLightfv(GL_LIGHT0, GL_POSITION, lp); glLightfv(GL_LIGHT0, GL_DIFFUSE, ld); glLightfv(GL_LIGHT0, GL_AMBIENT, la);
    glEnable(GL_DEPTH_TEST);

    drawStars();
    if (phase == PHASE_LAUNCH || phase == PHASE_ORBIT) {
        drawTerrain();
        drawLaunchpad();
        drawFallingSRBs();
        glPushMatrix();
        glTranslatef(vibX, rocketY, vibZ);
        drawRocket();
        glPopMatrix();
        drawParticles();
    }
    else {
        drawSolarSystem();
        glPushMatrix(); glTranslatef(rocketSX, rocketSY, rocketSZ);
        glRotatef(camYaw, 0, 1, 0);
        glRotatef(rocketTiltPitch, 1, 0, 0);
        glRotatef(rocketTiltRoll, 0, 0, 1);
        drawRocketCore();
        glPopMatrix();
        drawParticles();
    }
    drawHUD();
    glutSwapBuffers();
}

// ================================================================
// UPDATE
// ================================================================
void update(int v) {
    float dt = 0.016f;
    snd_tick(); /* re-loop engine/space sounds if they finished */
    if (phase == PHASE_LAUNCH) {
        if (lstate == ST_COUNTDOWN) {
            countTimer -= dt;
            countInt = (int)(countTimer)+1;
            if (countInt > 5)countInt = 5;
            // Play countdown voice once when countdown starts
            if (!snd_countdownPlayed) {
                snd_countdownPlayed = 1;
                snd_playOnce("countdown", "countdown.wav");
            }
            if (countTimer <= 0.0f) { lstate = ST_HOLD; countTimer = 0; launchHoldTimer = 0; }
        }
        if (lstate == ST_HOLD) {
            launchHoldTimer += dt;
            vibX = randf(-0.10f, 0.10f); vibZ = randf(-0.10f, 0.10f);
            for (int i = 0; i < 38; i++)spawnParticle(0, vibX, -0.4f, vibZ);
            for (int i = 0; i < 8; i++) spawnParticle(1, 0, -1.1f, 0);
            for (int i = 0; i < 10; i++)spawnParticle(4, srbLX + vibX, -0.35f, vibZ);
            for (int i = 0; i < 10; i++)spawnParticle(4, srbRX + vibX, -0.35f, vibZ);
            // Start engine sound when ignition begins
            snd_startEngine();
            if (launchHoldTimer > 2.0f) { lstate = ST_LIFTOFF; }
        }
        if (lstate == ST_LIFTOFF || lstate == ST_ASCENT) {
            float accel = (rocketY < 3.0f) ? 0.0003f :
                (rocketY < 12.0f) ? 0.0008f :
                (rocketY < 35.0f) ? 0.0020f : 0.0035f;
            rocketSpeed += accel * dt * 60.0f;
            if (rocketSpeed > 0.50f)rocketSpeed = 0.50f;
            rocketY += rocketSpeed;

            float vibAmt = clampf(0.08f - rocketSpeed * 0.12f, 0.0f, 0.08f);
            vibX = randf(-vibAmt, vibAmt); vibZ = randf(-vibAmt, vibAmt);

            int fc = (rocketY < 8.0f) ? 32 : (rocketY < 25.0f) ? 26 : 18;
            for (int i = 0; i < fc; i++)spawnParticle(0, vibX, -0.4f, vibZ);
            for (int i = 0; i < 6; i++) spawnParticle(1, 0, -1.1f, 0);
            if (!srbSep) {
                for (int i = 0; i < 12; i++)spawnParticle(4, srbLX + vibX, -0.35f, vibZ);
                for (int i = 0; i < 12; i++)spawnParticle(4, srbRX + vibX, -0.35f, vibZ);
            }

            // SRB SEPARATION
            if (!srbSep && rocketY > 38.0f) {
                srbSep = 1;
                srbVelY = 0.0f;
                srbLRot = 0.0f; srbRRot = 0.0f;
                for (int i = 0; i < 80; i++)spawnParticle(2, 0, rocketY + 0.5f, 0);
                // SRB separation boom
                if (!snd_srbSepPlayed) {
                    snd_srbSepPlayed = 1;
                    snd_playOnce("srb_sep", "srb_sep.wav");
                }
            }
            // SRBs tumble and fall away
            if (srbSep) {
                srbVelY -= 0.010f;
                srbY += srbVelY;
                srbLX -= 0.022f;
                srbRX += 0.022f;
                srbLRot += 1.8f;
                srbRRot += 1.6f;
            }

            float tt = clampf(rocketY / 100.0f, 0, 1);
            skyR = lerpf(0.38f, 0.01f, tt); skyG = lerpf(0.62f, 0.01f, tt); skyB = lerpf(0.92f, 0.02f, tt);
            starAlpha = tt;
            if (rocketY > 110.0f) {
                phase = PHASE_ORBIT; orbitTimer = 0.0f;
                snd_stopEngine(); snd_enginePlaying = 0;
                snd_stopOnce("countdown");
            }
        }
    }
    else if (phase == PHASE_ORBIT) {
        orbitTimer += dt;
        if (orbitTimer > 4.0f) {
            phase = PHASE_SPACE;
            // Start eerie space ambient
            snd_startSpace();
        }
    }
    else {
        for (int i = 0; i < NUM_PLANETS; i++) {
            planets[i].orbitAngle += planets[i].orbitSpeed * dt * 0.15f;
            planets[i].selfRotAngle += planets[i].selfRotSpeed * dt * 20.0f;
        }
        moonAngle += 0.35f * dt;
        for (int i = 0; i < NUM_ASTEROIDS; i++)
            asteroids[i].angle += asteroids[i].speed * dt;
        float yr2 = camYaw * DEG2RAD;
        float fwdX = sinf(yr2), fwdZ = cosf(yr2);
        float rgtX = cosf(yr2), rgtZ = -sinf(yr2);
        float mx = 0, my = 0, mz = 0;
        if (keyW) { mx += fwdX; mz += fwdZ; }
        if (keyS) { mx -= fwdX; mz -= fwdZ; }
        if (keyA) { mx += rgtX; mz += rgtZ; }
        if (keyD) { mx -= rgtX; mz -= rgtZ; }
        if (keyQ)my += 1.0f;
        if (keyE)my -= 1.0f;
        float targetPitch = keyW ? 45.0f : (keyS ? -45.0f : 0.0f);
        float targetRoll = keyA ? -28.0f : (keyD ? 28.0f : 0.0f);
        rocketTiltPitch += (targetPitch - rocketTiltPitch) * 0.12f;
        rocketTiltRoll += (targetRoll - rocketTiltRoll) * 0.12f;
        float mlen = sqrtf(mx * mx + my * my + mz * mz);
        if (mlen > 0.01f) {
            flySpeed += flyAccel * dt * 60.0f; if (flySpeed > flyMaxSpeed)flySpeed = flyMaxSpeed;
            float sc = flySpeed * dt * 60.0f * 0.04f / mlen;
            rocketSX += mx * sc; rocketSY += my * sc; rocketSZ += mz * sc;
            for (int i = 0; i < 4; i++)spawnParticle(3, 0, -0.5f, 0.3f);
        }
        else { flySpeed *= 0.88f; }
    }
    updateParticles(dt);
    glutPostRedisplay();
    glutTimerFunc(16, update, 0);
}

// ================================================================
// INPUT
// ================================================================
void keyDown(unsigned char k, int x, int y) {
    if (k == ' ' && phase == PHASE_LAUNCH && lstate == ST_IDLE) { lstate = ST_COUNTDOWN; countTimer = 5.0f; countInt = 5; }
    if (k == 'r' || k == 'R')resetSim();
    if (k == 27) {
        snd_stopOnce("countdown"); snd_stopOnce("srb_sep"); snd_stopEngine(); snd_stopSpace();
        exit(0);
    }
    if (k == 'w' || k == 'W')keyW = 1; if (k == 's' || k == 'S')keyS = 1;
    if (k == 'a' || k == 'A')keyA = 1; if (k == 'd' || k == 'D')keyD = 1;
    if (k == 'q' || k == 'Q')keyQ = 1; if (k == 'e' || k == 'E')keyE = 1;
}
void keyUp(unsigned char k, int x, int y) {
    if (k == 'w' || k == 'W')keyW = 0; if (k == 's' || k == 'S')keyS = 0;
    if (k == 'a' || k == 'A')keyA = 0; if (k == 'd' || k == 'D')keyD = 0;
    if (k == 'q' || k == 'Q')keyQ = 0; if (k == 'e' || k == 'E')keyE = 0;
}
void mouseMove(int x, int y) {
    if (!mouseOK) { lastMX = (float)x; lastMY = (float)y; mouseOK = 1; return; }
    float dx = (float)x - lastMX, dy = (float)y - lastMY;
    lastMX = (float)x; lastMY = (float)y;
    camYaw += dx * mouseSens; camPitch += dy * mouseSens;
    if (camPitch > 88.0f)camPitch = 88.0f; if (camPitch < -88.0f)camPitch = -88.0f;
}
void reshape(int w, int h) { winW = w; winH = h; glViewport(0, 0, w, h); }

// ================================================================
// MAIN
// ================================================================
int main(int argc, char** argv) {
    snd_initPath(); // find exe folder so WAV files load correctly
    srand((unsigned int)time(NULL));
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(winW, winH);
    glutInitWindowPosition(60, 40);
    glutCreateWindow("Space Exploration Simulation");
    glEnable(GL_DEPTH_TEST); glEnable(GL_NORMALIZE);
    initStars(); initClouds(); initMountains();
    srbLX = -1.15f; srbRX = 1.15f;
    for (int i = 0; i < NUM_PLANETS; i++) planets[i].orbitAngle = randf(0, 2 * PI);
    /* Start rocket near Earth so the player is oriented correctly */
    rocketSX = planets[2].dist * cosf(planets[2].orbitAngle) + 30.0f;
    rocketSZ = planets[2].dist * sinf(planets[2].orbitAngle) + 30.0f;
    rocketSY = 0.0f;
    resetParticles();
    glutDisplayFunc(display); glutReshapeFunc(reshape);
    glutKeyboardFunc(keyDown); glutKeyboardUpFunc(keyUp);
    glutPassiveMotionFunc(mouseMove); glutMotionFunc(mouseMove);
    glutTimerFunc(16, update, 0);
    glutMainLoop();
    return 0;
}