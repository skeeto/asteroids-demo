/* Asteroids Clone for Windows
 * This is free and unencumbered software released into the public domain.
 */
#include <math.h>
#include <stdint.h>

#include <windows.h>
#include <dsound.h>
#include <xinput.h>
#include <GL/gl.h>

/* Simplify building with Visual Studio (cl.exe) */
#ifdef _MSC_VER
#  pragma comment(lib, "gdi32.lib")
#  pragma comment(lib, "user32.lib")
#  pragma comment(lib, "opengl32.lib")
#  pragma comment(lib, "dsound.lib")
#  pragma comment(linker, "/subsystem:windows")
#endif

#define COUNTOF(a) (int)(sizeof(a) / sizeof(0[a]))
#define FRAMERATE 60
#define PI 0x1.921fb6p+1f

#define FATAL(msg)                                      \
    do {                                                \
        MessageBoxA(0, msg, "Fatal Error", MB_OK);      \
        ExitProcess(-1);                                \
    } while (0)

#define INIT_COUNT      8
#define LEVEL_DELAY     3
#define TIME_STEP_MIN   1.0f/15
#define SHIP_TURN_RATE  PI
#define SHIP_DAMPEN     0.995f
#define SHIP_ACCEL      0.5f
#define SHIP_SCALE      0.025f
#define SHOT_SPEED      0.5f
#define SHOT_TTL        1.0f
#define SHOT_COOLDOWN   0.2f
#define DEBRIS_TTL      1.2f

#define AUDIO_HZ        8000
#define AUDIO_LEN       8*AUDIO_HZ*2

#define ASTEROID0_MIN   0.025f
#define ASTEROID0_MAX   0.050f
#define ASTEROID0_SCORE 1
#define ASTEROID1_MIN   0.012f
#define ASTEROID1_MAX   0.025f
#define ASTEROID1_SCORE 2
#define ASTEROID2_MIN   0.008f
#define ASTEROID2_MAX   0.015f
#define ASTEROID2_SCORE 5

enum asteroid_size {A0, A1, A2};

/* format: ARGB */
#define C_SHIP      0xffffffff
#define C_ASTEROID  0xffffffff
#define C_SHOT      0xffffffff
#define C_THRUST    0xffffff00
#define C_FIRE      0x00df9f5f
#define C_SCORE     0xffffffff

struct v2 { float x, y; };

static const struct v2 ship[] = {
    {+1.00f * SHIP_SCALE, +0.00f * SHIP_SCALE},
    {-0.71f * SHIP_SCALE, +0.57f * SHIP_SCALE},
    {-0.43f * SHIP_SCALE, +0.29f * SHIP_SCALE},
    {-0.43f * SHIP_SCALE, -0.29f * SHIP_SCALE},
    {-0.71f * SHIP_SCALE, -0.57f * SHIP_SCALE},
};
static const struct v2 tail[] = {
    {-0.43f * SHIP_SCALE, -0.20f * SHIP_SCALE},
    {-0.70f * SHIP_SCALE, +0.00f * SHIP_SCALE},
    {-0.43f * SHIP_SCALE, +0.20f * SHIP_SCALE},
};

/* 7-segment font for digits */
#define FONT_SX 0.015f
#define FONT_SY 0.025f
static const struct v2 segv[] = {
    {0.0f*FONT_SX, 1.0f*FONT_SY}, {1.0f*FONT_SX, 1.0f*FONT_SY},
    {0.0f*FONT_SX, 0.5f*FONT_SY}, {0.0f*FONT_SX, 1.0f*FONT_SY},
    {1.0f*FONT_SX, 0.5f*FONT_SY}, {1.0f*FONT_SX, 1.0f*FONT_SY},
    {0.0f*FONT_SX, 0.5f*FONT_SY}, {1.0f*FONT_SX, 0.5f*FONT_SY},
    {0.0f*FONT_SX, 0.0f*FONT_SY}, {0.0f*FONT_SX, 0.5f*FONT_SY},
    {1.0f*FONT_SX, 0.0f*FONT_SY}, {1.0f*FONT_SX, 0.5f*FONT_SY},
    {0.0f*FONT_SX, 0.0f*FONT_SY}, {1.0f*FONT_SX, 0.0f*FONT_SY},
};
static const char seg7[] = {
    0x77, 0x24, 0x5d, 0x6d, 0x2e, 0x6b, 0x7b, 0x25, 0x7f, 0x6f
};

static double
uepoch(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long hi = ft.dwHighDateTime;
    unsigned long long lo = ft.dwLowDateTime;
    return ((hi<<32 | lo)/10 - 11644473600000000) / 1e6;
}

static unsigned long long rng;

static unsigned long
rand32(void)
{
    rng = rng*0x7c3c3267d015ceb5 + 1;
    unsigned long r = rng>>32 & 0xffffffff;
    r ^= r>>16;
    r *= 0x60857ba9;
    r &= 0xffffffff;
    r ^= r>>16;
    return r;
}

static float
randu(void)
{
    return rand32() / 4294967296.0f;
}

struct tf { float c, s, tx, ty; };

static struct tf
tf(float a, float tx, float ty)
{
    struct tf tf = {
        .c = cosf(a),
        .s = sinf(a),
        .tx = tx, .ty = ty,
    };
    return tf;
}

static struct v2
tf_apply(struct tf tf, struct v2 v)
{
    struct v2 r = {
        tf.tx + v.x*tf.c - v.y*tf.s,
        tf.ty + v.x*tf.s + v.y*tf.c,
    };
    return r;
}

static int
lltostr(char *buf, long long n)
{
    int len = n ? 0 : 1;
    for (long long x = n; x; x /= 10) {
        len++;
    }
    buf[len] = 0;
    for (int i = len - 1; i >= 0; i--) {
        buf[i] = '0' + n%10;
        n /= 10;
    }
    return len;
}

static void win32_audio_mix(int16_t *buf, size_t len);
static void win32_audio_clear(size_t len);

static int g_nlines;
static struct {
    GLubyte r, g, b, a;
    GLfloat x, y;
} g_linebuf[1<<16];
static int g_npoints;
static struct {
    GLubyte r, g, b, a;
    GLfloat x, y;
} g_pointbuf[1<<10];

static const signed char toroid[][2] = {
    {+0, +0}, {-1, +0}, {+1, +0}, {+0, -1}, {+0, +1}
};

/* Push line segment onto rendering buffer. */
static void
g_line(struct v2 a, struct v2 b, uint32_t color)
{
    int i = g_nlines;
    g_nlines += 2;
    g_linebuf[i+0].r = color >> 16;
    g_linebuf[i+0].g = color >>  8;
    g_linebuf[i+0].b = color >>  0;
    g_linebuf[i+0].a = color >> 24;
    g_linebuf[i+0].x = 2*a.x - 1;
    g_linebuf[i+0].y = 2*a.y - 1;
    g_linebuf[i+1].r = color >> 16;
    g_linebuf[i+1].g = color >>  8;
    g_linebuf[i+1].b = color >>  0;
    g_linebuf[i+1].a = color >> 24;
    g_linebuf[i+1].x = 2*b.x - 1;
    g_linebuf[i+1].y = 2*b.y - 1;
}

/* Push toroid-wrapped line segment onto the rendering buffer. */
static void
g_wline(struct v2 a, struct v2 b, uint32_t color)
{
    for (int i = 0; i < 5; i++) {
        float tx = toroid[i][0];
        float ty = toroid[i][1];
        struct v2 ta = {a.x+tx, a.y+ty};
        struct v2 tb = {b.x+tx, b.y+ty};
        g_line(ta, tb, color);
    }
}

static void
g_wlinestrip(const struct v2 *v, int n, struct tf tf, uint32_t color)
{
    struct v2 a = tf_apply(tf, v[0]);
    for (int i = 1; i < n; i++) {
        struct v2 b = tf_apply(tf, v[i]);
        g_wline(a, b, color);
        a = b;
    }
}

static void
g_wlineloop(const struct v2 *v, int n, struct tf tf, uint32_t color)
{
    g_wlinestrip(v, n, tf, color);
    g_wline(tf_apply(tf, v[n-1]), tf_apply(tf, v[0]), color);
}

static void
g_render(void)
{
    glClear(GL_COLOR_BUFFER_BIT);

    glInterleavedArrays(GL_C4UB_V2F, 0, g_linebuf);
    glDrawArrays(GL_LINES, 0, g_nlines);
    g_nlines = 0;

    glInterleavedArrays(GL_C4UB_V2F, 0, g_pointbuf);
    glDrawArrays(GL_POINTS, 0, g_npoints);
    g_npoints = 0;
}

static void
g_point(float x, float y, uint32_t color)
{
    int i = g_npoints++;
    g_pointbuf[i].r = color >> 16;
    g_pointbuf[i].g = color >>  8;
    g_pointbuf[i].b = color >>  0;
    g_pointbuf[i].a = color >> 24;
    g_pointbuf[i].x = 2*x - 1;
    g_pointbuf[i].y = 2*y - 1;
}

static void
g_wpoint(float x, float y, uint32_t color)
{
    for (int i = 0; i < 5; i++) {
        g_point(toroid[i][0] + x, toroid[i][1] + y, color);
    }
}

struct {
    double last;
    long level;
    float transition;
    long long score;
    int lives;

    #define I_TURNL  (1<<0)
    #define I_TURNR  (1<<1)
    #define I_THRUST (1<<2)
    #define I_FIRE   (1<<3)
    int controls;

    float  px,  py,  pa;
    float pdx, pdy, pda;

    struct asteroid {
        float  x,  y,  a;
        float dx, dy, da;
        struct v2 v[16];
        short n;
        short kind;
        float shot_r2; // shot hit radius^2
        float ship_r2; // ship hit radius^2
    } asteroids[1024];
    int nasteroids;

    struct shot {
        float  x,  y;
        float dx, dy;
        float ttl;
    } shots[64];
    int nshots;
    float cooldown;

    struct debris {
        float  x,  y;
        float dx, dy, da;
        float age;
        uint32_t color;
        struct v2 v[2];
    } debris[1024];
    int ndebris;
} game;

static struct {
    double deadline;
    int16_t pcm_fire[AUDIO_HZ/5];
    int16_t pcm_destroy[AUDIO_HZ/4];
} audio;

static int
game_asteroid(enum asteroid_size kind)
{
    if (game.nasteroids == COUNTOF(game.asteroids)) return -1;

    struct asteroid *a = game.asteroids + game.nasteroids;
    float dx, dy;
    do {
        a->x  = randu();
        a->y  = randu();
        dx = a->x - 0.5f;
        dy = a->y - 0.5f;
    } while (dx*dx + dy*dy < 0.1f);
    a->dx = 0.1f * (2*randu() - 1);
    a->dy = 0.1f * (2*randu() - 1);
    a->a  = 2 * PI * randu();
    a->da = PI*(2*randu() - 1);

    int n = 0;
    float min = 0;
    float max = 0;
    switch (kind) {
    case A0: n = 16; max = ASTEROID0_MAX; min = ASTEROID0_MIN; break;
    case A1: n = 12; max = ASTEROID1_MAX; min = ASTEROID1_MIN; break;
    case A2: n =  8; max = ASTEROID2_MAX; min = ASTEROID2_MIN; break;
    }
    for (int i = 0; i < n; i++) {
        float t = 2*PI * (i - 1) / (float)n;
        float r = randu()*(max - min) + min;
        a->v[i].x = r * cosf(t);
        a->v[i].y = r * sinf(t);
    }
    a->n = n;
    a->kind = kind;

    // Make hit radius favor player since it's imprecise
    a->shot_r2 = max*max;
    a->ship_r2 = (max+min)*(max+min)/4;

    return game.nasteroids++;
}

static void
game_new_level(void)
{
    game.px = game.py = 0.5f;
    game.pdx = game.pdy = 0.0f;

    game.nshots = 0;
    game.ndebris = 0;
    game.cooldown = 0;
    game.transition = 0;
    game.lives = 1;

    game.nasteroids = 0;
    for (int i = 0; i < game.level; i++) {
        game_asteroid(A0);
    }
}

static void
game_init(int size)
{
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_POINT_SMOOTH);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glLineWidth(2e-3f * size);
    glPointSize(4e-3f * size);

    rng += uepoch() * 1e6;

    game.level = INIT_COUNT;
    game.last = uepoch();
    game.pa = PI/2;
    game.pda = 0.0f;
    game.controls = 0;
    game.score = 0;

    game_new_level();

    /* Synthesize sound effects */

    for (int i = 0; i < COUNTOF(audio.pcm_fire); i++) {
        float t = (float)i / AUDIO_HZ;
        float f = 440 - t*300;
        float v = (float)i/COUNTOF(audio.pcm_fire);
        audio.pcm_fire[i] = 0x7fff * sinf(2*PI*t*f)*(1 - v*v);
    }

    for (int i = 0; i < COUNTOF(audio.pcm_destroy); i++) {
        switch (i % 12) {
        case  0: audio.pcm_destroy[i] = 0x7fff * randu(); break;
        default: audio.pcm_destroy[i] = audio.pcm_destroy[i-1];
        }
    }
}

static void
game_down(int control)
{
    if (game.controls & control) return;
    game.controls |= control;
    switch (control) {
    case I_TURNL:  game.pda += SHIP_TURN_RATE; break;
    case I_TURNR:  game.pda -= SHIP_TURN_RATE; break;
    case I_THRUST: break;
    case I_FIRE:   break;
    }
}

static void
game_up(int control)
{
    if (!(game.controls & control)) return;
    game.controls ^= control;
    switch (control) {
    case I_TURNL:  game.pda -= SHIP_TURN_RATE; break;
    case I_TURNR:  game.pda += SHIP_TURN_RATE; break;
    case I_THRUST: break;
    case I_FIRE:   break;
    }
}

static void
game_debris(struct v2 *v, float x, float y, float dx, float dy, uint32_t c)
{
    if (game.ndebris < COUNTOF(game.debris)) {
        int i = game.ndebris++;
        game.debris[i].x     = x;
        game.debris[i].y     = y;
        game.debris[i].dx    = dx;
        game.debris[i].dy    = dy;
        game.debris[i].da    = 2*PI*(2*randu() - 1);
        game.debris[i].age   = 0;
        game.debris[i].color = c & 0xffffff;
        game.debris[i].v[0]  = v[0];
        game.debris[i].v[1]  = v[1];
    }
}

static void
game_destroy_asteroid(int n)
{
    struct asteroid *a = game.asteroids + n;
    struct tf t = tf(a->a, 0, 0);
    for (int i = 0; i < a->n; i++) {
        int j = (i + 1)%a->n;
        struct v2 v[] = {
            tf_apply(t, a->v[i]),
            tf_apply(t, a->v[j]),
        };
        float mx = (v[0].x + v[1].x) / 2;
        float my = (v[0].y + v[1].y) / 2;
        v[0].x -= mx; v[1].x -= mx;
        v[0].y -= my; v[1].y -= my;
        float dx = a->dx + mx*randu();
        float dy = a->dy + my*randu();
        game_debris(v, a->x+mx, a->y+my, dx, dy, C_ASTEROID);
    }

    float x = a->x;
    float y = a->y;
    enum asteroid_size kind = a->kind;
    game.asteroids[n] = game.asteroids[--game.nasteroids];

    switch (kind) {
    case A0: game.score += ASTEROID0_SCORE; break;
    case A1: game.score += ASTEROID1_SCORE; break;
    case A2: game.score += ASTEROID2_SCORE; break;
    }

    if (kind != A2) {
        int c = 1 + rand32()%2;
        for (int i = 0; i < c; i++) {
            int n = game_asteroid(kind + 1);
            if (n >= 0) {
                game.asteroids[n].x = x;
                game.asteroids[n].y = y;
            }
        }
    }
}

enum sound {SOUND_SILENCE, SOUND_FIRE, SOUND_DESTROY};
static void
game_sound(double now, enum sound what)
{
    int len = 0;
    switch (what) {
    case SOUND_SILENCE:
        if (now >= audio.deadline) {
            len = AUDIO_HZ;
            win32_audio_clear(len);
        }
        break;
    case SOUND_FIRE:
        len = COUNTOF(audio.pcm_fire);
        win32_audio_mix(audio.pcm_fire, len);
        break;
    case SOUND_DESTROY:
        len = COUNTOF(audio.pcm_destroy);
        win32_audio_mix(audio.pcm_destroy, len);
        break;
    }
    if (len) audio.deadline = now + len/(double)AUDIO_HZ - 0.015;
}

static void
game_step(void)
{
    double now = uepoch();
    float dt = now - game.last;
    if (dt > TIME_STEP_MIN) dt = TIME_STEP_MIN;
    game.last = now;

    if (!game.lives || !game.nasteroids) {
        game.transition += dt;
    }

    if (!game.lives && game.transition > LEVEL_DELAY) {
        game.score /= 2;
        game_new_level();
    } else if (!game.nasteroids && game.transition > LEVEL_DELAY) {
        game.score += game.level * 100;
        game.level++;
        game_new_level();
    }

    game.pa   = fmodf(game.pa + dt*game.pda + 2*PI, 2*PI);
    if (game.controls & I_THRUST) {
        float c = cosf(game.pa);
        float s = sinf(game.pa);
        game.pdx += dt*c*SHIP_ACCEL;
        game.pdy += dt*s*SHIP_ACCEL;

        /* thruster fire trail */
        if (randu() < 0.75f) {
            float f = SHIP_SCALE*0.15f;
            struct v2 v[] = {
                {(2*randu() - 1)*f, (2*randu() - 1)*f},
                {(2*randu() - 1)*f, (2*randu() - 1)*f},
            };
            float x = game.px + c*ship[3].x;
            float y = game.py + s*ship[3].x;
            game_debris(v, x, y, -c*0.1f, -s*0.1f, C_FIRE);
        }
    }
    game.px   = fmodf(game.px + dt*game.pdx + 1, 1);
    game.py   = fmodf(game.py + dt*game.pdy + 1, 1);
    game.pdx *= SHIP_DAMPEN;
    game.pdy *= SHIP_DAMPEN;

    if ((game.controls & I_FIRE) &&
        game.nshots < COUNTOF(game.shots) &&
        game.cooldown <= 0) {

        int i = game.nshots++;
        float c = cosf(game.pa);
        float s = sinf(game.pa);
        game.shots[i].x = SHIP_SCALE*c+game.px;
        game.shots[i].y = SHIP_SCALE*s+game.py;
        game.shots[i].dx = game.pdx + c*SHOT_SPEED;
        game.shots[i].dy = game.pdy + s*SHOT_SPEED;
        game.shots[i].ttl = SHOT_TTL;
        game.cooldown = SHOT_COOLDOWN;
        game_sound(now, SOUND_FIRE);
    } else if (game.cooldown > 0) {
        game.cooldown -= dt;
    }

    for (int i = 0; i < game.nshots; i++) {
        struct shot *s = game.shots + i;
        if ((s->ttl -= dt) < 0) {
            // TODO: hit detection for final partial step
            game.shots[i--] = game.shots[--game.nshots];
        } else {
            s->x = fmodf(s->x + dt*s->dx + 1, 1);
            s->y = fmodf(s->y + dt*s->dy + 1, 1);
        }
    }

    for (int i = 0; i < game.nasteroids; i++) {
        struct asteroid *a = game.asteroids + i;
        a->x = fmodf(a->x + dt*a->dx + 1, 1);
        a->y = fmodf(a->y + dt*a->dy + 1, 1);
        a->a = fmodf(a->a + dt*a->da, 360);
    }

    // TODO: precise hit detection
    for (int i = 0; i < game.nshots; i++) {
        struct shot *s = game.shots + i;
        for (int j = 0; j < game.nasteroids; j++) {
            struct asteroid *a = game.asteroids + j;
            float dx = s->x - a->x;
            float dy = s->y - a->y;
            if (dx*dx + dy*dy < a->shot_r2) {
                game.shots[i--] = game.shots[--game.nshots];
                game_destroy_asteroid(j--);
                game_sound(now, SOUND_DESTROY);
                break;
            }
        }
    }

    for (int i = 0; i < game.ndebris; i++) {
        if ((game.debris[i].age += dt) > DEBRIS_TTL) {
            game.debris[i--] = game.debris[--game.ndebris];
        }
    }

    // TODO: precise hit detection
    struct tf t = tf(game.pa, game.px, game.py);
    for (int i = 0; game.lives && i < COUNTOF(ship); i++) {
        struct v2 p = tf_apply(t, ship[i]);
        for (int j = 0; j < game.nasteroids; j++) {
            struct asteroid *a = game.asteroids + j;
            float dx = p.x - a->x;
            float dy = p.y - a->y;
            if (dx*dx + dy*dy < a->ship_r2) {
                game.lives = 0;
                for (int i = 0; i < 256; i++) {
                    float s = 0.01f;
                    struct v2 v[] = {
                        {s*(randu()*2 - 1), s*(randu()*2 - 1)},
                        {s*(randu()*2 - 1), s*(randu()*2 - 1)},
                    };
                    float r = 0.25f*randu();
                    float a = 2*PI*randu();
                    float dx = game.pdx/2 + r*cosf(a);
                    float dy = game.pdy/2 + r*sinf(a);
                    uint32_t color = randu() < 0.7f ? C_SHIP : C_FIRE;
                    game_debris(v, game.px, game.py, dx, dy, color);
                }
                game_sound(now, SOUND_DESTROY);
                break;
            }
        }
    }

    game_sound(now, SOUND_SILENCE);
}

static void
game_render(void)
{
    for (int i = 0; i < game.nasteroids; i++) {
        struct asteroid *a = game.asteroids + i;
        g_wlineloop(a->v, a->n, tf(a->a, a->x, a->y), C_ASTEROID);
    }

    for (int i = 0; i < game.nshots; i++) {
        g_wpoint(game.shots[i].x, game.shots[i].y, C_SHOT);
    }

    if (game.lives) {
        struct tf ship_tf = tf(game.pa, game.px, game.py);
        g_wlineloop(ship, COUNTOF(ship), ship_tf, C_SHIP);
        if ((game.controls & I_THRUST) && randu() < 0.5f) {
            g_wlinestrip(tail, COUNTOF(tail), ship_tf, C_THRUST);
        }
    } else {
    }

    for (int i = 0; i < game.ndebris; i++) {
        struct debris *d = game.debris + i;
        float dt = d->age;
        struct tf t = tf(dt*d->da, d->x + dt*d->dx, d->y + dt*d->dy);
        uint32_t alpha = 255 * (1 - d->age/DEBRIS_TTL);
        g_wlinestrip(d->v, COUNTOF(d->v), t, d->color | alpha<<24);
    }

    char score[32];
    int scorelen = lltostr(score, game.score);
    for (int i = 0; i < scorelen; i++) {
        float pad = 0.01f;
        struct tf t = tf(0, pad + i*FONT_SY, 1 - pad - FONT_SY);
        int segs = seg7[score[i] - '0'];
        for (int s = 0; s < 7; s++) {
            if (segs & 1<<s) {
                struct v2 a = tf_apply(t, segv[s*2+0]);
                struct v2 b = tf_apply(t, segv[s*2+1]);
                g_line(a, b, C_SCORE);
            }
        }
    }

    g_render();
}

static BOOL win32_opengl_initialized;
static int win32_opengl_size;

static void
win32_opengl_init(HDC hdc)
{
    PIXELFORMATDESCRIPTOR pdf = {
        .nSize = sizeof(pdf),
        .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        .iPixelType = PFD_TYPE_RGBA,
        .cColorBits = 32,
        .cDepthBits = 24,
        .cStencilBits = 8,
        .iLayerType = PFD_MAIN_PLANE,
    };
    SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pdf), &pdf);
    HGLRC old = wglCreateContext(hdc);
    wglMakeCurrent(hdc, old);
    win32_opengl_initialized = TRUE;
}

static LRESULT CALLBACK
win32_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
        case WM_CREATE:
            win32_opengl_init(GetDC(hwnd));
            game_init(win32_opengl_size);
            break;
        case WM_KEYUP:
            switch (wparam) {
            case VK_LEFT:  game_up(I_TURNL);  break;
            case VK_RIGHT: game_up(I_TURNR);  break;
            case VK_UP:    game_up(I_THRUST); break;
            case VK_SPACE: game_up(I_FIRE);   break;
            }
            break;
        case WM_KEYDOWN:
            if (lparam & 0x40000000) break;
            switch (wparam) {
            case VK_LEFT:  game_down(I_TURNL);  break;
            case VK_RIGHT: game_down(I_TURNR);  break;
            case VK_UP:    game_down(I_THRUST); break;
            case VK_SPACE: game_down(I_FIRE);   break;
            }
            break;
        case WM_CLOSE:
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wparam, lparam);
    }
    return 0;
}

static HWND
win32_window_init(void)
{
    const char *title = "Asteroids";
    WNDCLASS wndclass = {
        .style = CS_OWNDC,
        .lpfnWndProc = win32_wndproc,
        .lpszClassName = "gl",
        .hCursor = LoadCursor(0, IDC_ARROW),
        .hIcon = LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(1)),
    };
    RegisterClass(&wndclass);
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    int size = win32_opengl_size = (h < w ? h : w) - 100;
    int x = (w - size) / 2;
    int y = (h - size) / 2;
    DWORD style = WS_OVERLAPPED | WS_VISIBLE | WS_MINIMIZEBOX | WS_SYSMENU;
    return CreateWindow("gl", title, style, x, y, size, size, 0, 0, 0, 0);
}

static void  (*XInputEnable_p)(BOOL);
static DWORD (*XInputGetState_p)(DWORD, XINPUT_STATE *);
static DWORD (*XInputGetKeystroke_p)(DWORD, DWORD, PXINPUT_KEYSTROKE);

static int
joystick_discovery(void)
{
    static char dll[][16] = {
        "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"
    };
    HINSTANCE h = 0;
    for (int i = 0; !h && i < COUNTOF(dll); i++) {
        h = LoadLibraryA(dll[i]);
    }
    if (!h) {
        return 0;
    }
    XInputEnable_p = (void *)GetProcAddress(h, "XInputEnable");
    XInputGetState_p = (void *)GetProcAddress(h, "XInputGetState");
    XInputGetKeystroke_p = (void *)GetProcAddress(h, "XInputGetKeystroke");
    if (!XInputEnable_p || !XInputGetState_p || !XInputGetKeystroke_p) {
        return 0;  // xinput9_1_0.dll is often broken
    }

    int bits = 0;
    XInputEnable_p(TRUE);
    for (int i = 0; i < 4; i++) {
        XINPUT_STATE state;
        if (!XInputGetState_p(i, &state)) {
            bits |= 1 << i;
        }
    }
    return bits;
}

static void
joystick_read(int bits)
{
    for (int i = 0; i < 4; i++) {
        if (!(bits & 1<<i)) continue;
        int limit = 32;
        XINPUT_KEYSTROKE k;
        while (--limit && XInputGetKeystroke_p(i, 0, &k) == ERROR_SUCCESS) {
            int control = 0;
            switch (k.VirtualKey) {
            case VK_PAD_A:
            case VK_PAD_B:
                control = I_FIRE;
                break;
            case VK_PAD_X:
            case VK_PAD_Y:
                control = I_THRUST;
                break;
            case VK_PAD_RSHOULDER:
            case VK_PAD_DPAD_RIGHT:
            case VK_PAD_LTHUMB_RIGHT:
                control = I_TURNR;
                break;
            case VK_PAD_LSHOULDER:
            case VK_PAD_DPAD_LEFT:
            case VK_PAD_LTHUMB_LEFT:
                control = I_TURNL;
                break;
            }
            switch (k.Flags) {
            case XINPUT_KEYSTROKE_KEYDOWN: game_down(control); break;
            case XINPUT_KEYSTROKE_KEYUP:   game_up(control);   break;
            }
        }
    }
}

static IDirectSoundBuffer *win32_dsb;

static void *
sound_init(HWND wnd)
{
    IDirectSound *ds;
    if (DirectSoundCreate(0, &ds, 0) != DS_OK) {
        return 0;
    }
    if (IDirectSound8_SetCooperativeLevel(ds, wnd, DSSCL_NORMAL) != DS_OK) {
        return 0;
    }

    DSBUFFERDESC desc = {
        .dwSize = sizeof(desc),
        .dwBufferBytes = AUDIO_LEN,
        .lpwfxFormat = &(WAVEFORMATEX){
            .wFormatTag = WAVE_FORMAT_PCM,
            .nChannels = 1,
            .nSamplesPerSec = AUDIO_HZ,
            .nAvgBytesPerSec = AUDIO_HZ * 2,
            .nBlockAlign = 2,
            .wBitsPerSample = 16,
        },
        .guid3DAlgorithm = {0},
    };
    if (IDirectSound8_CreateSoundBuffer(ds, &desc, &win32_dsb, 0) != DS_OK) {
        return 0;
    }
    IDirectSoundBuffer_Play(win32_dsb, 0, 0, DSBPLAY_LOOPING);

    return ds;
}

/* Silence the next LEN samples of the audio buffer. */
static void
win32_audio_clear(size_t len)
{
    if (!win32_dsb) return;

    void *p0, *p1;
    DWORD z0, z1;
    DWORD r = IDirectSoundBuffer_Lock(
        win32_dsb, 0, len*2, &p0, &z0, &p1, &z1, DSBLOCK_FROMWRITECURSOR
    );
    if (r != DS_OK) return;
    memset(p0, 0, z0);
    memset(p1, 0, z1);
    IDirectSoundBuffer_Unlock(win32_dsb, p0, z1, p1, z1);
}

/* Mix a buffer of samples into the audio buffer. */
static void
win32_audio_mix(int16_t *buf, size_t len)
{
    if (!win32_dsb) return;

    void *p0, *p1;
    DWORD z0, z1;
    DWORD r = IDirectSoundBuffer_Lock(
        win32_dsb, 0, len*2, &p0, &z0, &p1, &z1, DSBLOCK_FROMWRITECURSOR
    );
    if (r != DS_OK) return;

    int16_t *s0 = p0;
    int16_t *s1 = p1;
    int16_t *b0 = buf;
    int16_t *b1 = buf + z0/2;
    for (DWORD i = 0; i < z0/2; i++) {
        float s = s0[i]/(float)0x7fff;
        float b = b0[i]/(float)0x7fff;
        s0[i] = (s + b) / 2 * 0x7fff;
    }
    for (DWORD i = 0; i < z1/2; i++) {
        float s = s1[i]/(float)0x7fff;
        float b = b1[i]/(float)0x7fff;
        s1[i] = (s + b) / 2 * 0x7fff;
    }
    IDirectSoundBuffer_Unlock(win32_dsb, p0, z1, p1, z1);
}

static double
counter_freq(void)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return freq.QuadPart;
}

static double
counter_now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

int WINAPI
WinMain(HINSTANCE h, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)h; (void)prev; (void)cmd; (void)show;

    HWND wnd = win32_window_init();

    sound_init(wnd);

    int joysticks = joystick_discovery();

    double freq = counter_freq();
    HDC hdc = GetDC(wnd);
    for (;;) {
        double start = counter_now();

        MSG msg;
        while (PeekMessage(&msg, 0, 0, 0, TRUE)) {
            if (msg.message == WM_QUIT) {
                TerminateProcess(GetCurrentProcess(), 0);
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (win32_opengl_initialized) {
            joystick_read(joysticks);
            game_step();
            game_render();
            SwapBuffers(hdc);

            // Some systems have a broken swap interval (virtual machines,
            // certain Wine configurations), so sleep rest of the frame if
            // necessary.
            double rem = 1.0/FRAMERATE - (counter_now() - start)/freq;
            if (rem > 0.001) {
                Sleep(1000 * rem);
            }
        }
    }
    return 0;
}
