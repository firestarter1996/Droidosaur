// TOUCH CONTROLS IMPLEMENTATION FOR ANDROID
// Digital D-pad + action buttons for Nanosaur (Droidosaur).

#ifdef __ANDROID__

#include "TouchControls.h"
#include <GLES3/gl3.h>
#include <android/log.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO, "Nanosaur", __VA_ARGS__)

#include <jni.h>

// Haptics (owner 2026-09-19): a short tick when a button goes down and whenever the D-pad direction
// changes, through NanosaurActivity.vibrate(ms, amplitude) over JNI (SDL3 has no phone-vibrator API).
#define HAPTIC_BTN_MS       22
#define HAPTIC_BTN_AMP      170
#define HAPTIC_DPAD_MS      12
#define HAPTIC_DPAD_AMP     110

static jclass    gActivityClass = NULL;
static jmethodID gVibrateMethod = NULL;

static void Haptic(int ms, int amp)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return;
    if (!gActivityClass)
    {
        jobject activity = (jobject)SDL_GetAndroidActivity();
        if (!activity) return;
        jclass local = (*env)->GetObjectClass(env, activity);
        gActivityClass = (jclass)(*env)->NewGlobalRef(env, local);
        (*env)->DeleteLocalRef(env, local);
        (*env)->DeleteLocalRef(env, activity);
        gVibrateMethod = (*env)->GetStaticMethodID(env, gActivityClass, "vibrate", "(II)V");
        if (!gVibrateMethod) { (*env)->ExceptionClear(env); LOGI("Haptic: NanosaurActivity.vibrate not found"); }
    }
    if (gVibrateMethod)
    {
        (*env)->CallStaticVoidMethod(env, gActivityClass, gVibrateMethod, (jint)ms, (jint)amp);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }
}

// -------------------------------------------------------------------------
// Layout constants (in normalised window coords 0..1, origin = top-left)
// -------------------------------------------------------------------------

// D-pad – left side (owner 2026-09-19: the game was designed for keyboard, so the pad is DIGITAL 8-way,
// not an analog stick; and everything sits lower so the pad clears the Pixel's camera cutout, which is
// on the left edge at mid-height in landscape).
#define JOY_CX_NORM     0.13f
#define JOY_CY_NORM     0.76f
#define JOY_RADIUS_NORM 0.085f
#define DPAD_ARM_W      0.42f   // arm width as a fraction of the pad radius
#define DPAD_DIAG_ZONE  0.35f   // |dx| and |dy| both above this share -> diagonal (both directions)

// Action buttons – right side (diamond layout: top=Jump, right=Attack, left=Pickup)
// BTN_SPACING must satisfy: sqrt(spX^2 + spY^2) > 2*r_px for all screen ratios.
// With BTN_SPACING=0.115 and BTN_RADIUS_NORM=0.043, diagonal gap is ~250px on 16:9.
#define BTN_CX_NORM     0.83f
#define BTN_CY_NORM     0.70f
#define BTN_RADIUS_NORM 0.043f
#define BTN_SPACING     0.115f

// Jetpack buttons – clearly below the action diamond to avoid overlap
#define JET_BTN_CY_NORM        0.90f    // well below diamond (verified no-overlap for 16:9 and 20:9)
#define JET_BTN_X_OFFSET_SCALE 0.55f    // fraction of BTN_SPACING for X separation

// Weapon cycle buttons – top-center area (small)
#define WPN_BTN_Y_NORM         0.08f
#define WPN_BTN_LEFT_X_NORM    0.42f
#define WPN_BTN_RIGHT_X_NORM   0.54f
#define WPN_BTN_RADIUS_SCALE   0.75f

// Drawing
#define OUTLINE_BRIGHTNESS_SCALE  1.3f

// Pause button – top-right corner
#define PAUSE_CX_NORM     0.95f
#define PAUSE_CY_NORM     0.08f
#define PAUSE_RADIUS_NORM 0.04f

#define DEAD_ZONE           0.15f
#define BTN_HIT_MULTIPLIER  1.3f
#define JOY_HIT_MULTIPLIER  1.4f

// -------------------------------------------------------------------------
// State
// -------------------------------------------------------------------------

static int   gWindowW = 1;
static int   gWindowH = 1;

// Joystick
static bool  gJoyActive      = false;
static float gJoyTouchX      = 0;
static float gJoyTouchY      = 0;
static float gJoyCenterX     = 0;
static float gJoyCenterY     = 0;
static SDL_FingerID gJoyFinger = -1;

static float gJoyAnalogX     = 0;
static float gJoyAnalogY     = 0;

// Buttons
static bool         gBtnDown[kTouchBtn_COUNT];
static float        gBtnCX[kTouchBtn_COUNT];
static float        gBtnCY[kTouchBtn_COUNT];
static SDL_FingerID gBtnFinger[kTouchBtn_COUNT];

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

static float NormX(float nx) { return nx * (float)gWindowW; }
static float NormY(float ny) { return ny * (float)gWindowH; }

static void UpdateButtonPositions(void)
{
    float cx = NormX(BTN_CX_NORM);
    float cy = NormY(BTN_CY_NORM);
    float sp = NormX(BTN_SPACING);
    float spY = NormY(BTN_SPACING);

    // Main action diamond: Jump (top), Attack (right), Pickup (left)
    gBtnCX[kTouchBtn_Jump]   = cx;         gBtnCY[kTouchBtn_Jump]   = cy - spY;
    gBtnCX[kTouchBtn_Attack] = cx + sp;    gBtnCY[kTouchBtn_Attack] = cy;
    gBtnCX[kTouchBtn_Pickup] = cx - sp;    gBtnCY[kTouchBtn_Pickup] = cy;

    // Jetpack buttons – flanking below the diamond
    float jetY = NormY(JET_BTN_CY_NORM);
    gBtnCX[kTouchBtn_JetUp]   = cx + sp * JET_BTN_X_OFFSET_SCALE;  gBtnCY[kTouchBtn_JetUp]   = jetY;
    gBtnCX[kTouchBtn_JetDown] = cx - sp * JET_BTN_X_OFFSET_SCALE;  gBtnCY[kTouchBtn_JetDown] = jetY;

    // Weapon cycle buttons – top-centre strip
    gBtnCX[kTouchBtn_PrevWeapon] = NormX(WPN_BTN_LEFT_X_NORM);
    gBtnCY[kTouchBtn_PrevWeapon] = NormY(WPN_BTN_Y_NORM);
    gBtnCX[kTouchBtn_NextWeapon] = NormX(WPN_BTN_RIGHT_X_NORM);
    gBtnCY[kTouchBtn_NextWeapon] = NormY(WPN_BTN_Y_NORM);

    // Pause button
    gBtnCX[kTouchBtn_Pause]  = NormX(PAUSE_CX_NORM);
    gBtnCY[kTouchBtn_Pause]  = NormY(PAUSE_CY_NORM);
}

static float BtnRadius(TouchButtonID btn)
{
    if (btn == kTouchBtn_Pause)
        return NormX(PAUSE_RADIUS_NORM);
    if (btn == kTouchBtn_PrevWeapon || btn == kTouchBtn_NextWeapon)
        return NormX(BTN_RADIUS_NORM * WPN_BTN_RADIUS_SCALE);
    return NormX(BTN_RADIUS_NORM);
}

static float JoyRadius(void) { return NormX(JOY_RADIUS_NORM); }

static int HitButton(float x, float y)
{
    for (int i = 0; i < kTouchBtn_COUNT; i++)
    {
        float dx = x - gBtnCX[i];
        float dy = y - gBtnCY[i];
        float r  = BtnRadius(i) * BTN_HIT_MULTIPLIER;
        if (dx*dx + dy*dy <= r*r)
            return i;
    }
    return -1;
}

static bool HitJoystick(float x, float y)
{
    float jcx = NormX(JOY_CX_NORM);
    float jcy = NormY(JOY_CY_NORM);
    float r   = JoyRadius() * JOY_HIT_MULTIPLIER;
    float dx  = x - jcx;
    float dy  = y - jcy;
    return dx*dx + dy*dy <= r*r;
}

// Digital 8-way D-pad: the finger's offset from the pad centre picks a direction, full strength
// (-1/0/+1 per axis, exactly like holding the arrow keys). Diagonals when both components are big.
// The finger may slide anywhere while held - the direction follows it, so you never "fall off".
static void UpdateJoyAnalog(void)
{
    if (!gJoyActive) { gJoyAnalogX = gJoyAnalogY = 0; return; }

    float dx = (gJoyTouchX - gJoyCenterX) / JoyRadius();
    float dy = (gJoyTouchY - gJoyCenterY) / JoyRadius();
    float len = sqrtf(dx*dx + dy*dy);
    if (len < DEAD_ZONE) { gJoyAnalogX = gJoyAnalogY = 0; return; }
    float ax = fabsf(dx), ay = fabsf(dy);
    float share = (ax < ay ? ax : ay) / (ax > ay ? ax : ay);   // 0 = pure axis, 1 = exact diagonal
    bool diag = share > DPAD_DIAG_ZONE;
    float nx = (diag || ax >= ay) ? (dx > 0 ? 1.0f : -1.0f) : 0.0f;
    float ny = (diag || ay >  ax) ? (dy > 0 ? -1.0f : 1.0f) : 0.0f;   // SDL y is down, game forward is +y
    if (nx != gJoyAnalogX || ny != gJoyAnalogY)
        Haptic(HAPTIC_DPAD_MS, HAPTIC_DPAD_AMP);
    gJoyAnalogX = nx;
    gJoyAnalogY = ny;
}

// -------------------------------------------------------------------------
// Init / Shutdown
// -------------------------------------------------------------------------

void TouchControls_Init(void)
{
    memset(gBtnDown,   0, sizeof(gBtnDown));
    for (int i = 0; i < kTouchBtn_COUNT; i++)
        gBtnFinger[i] = (SDL_FingerID)-1;
    gJoyFinger  = (SDL_FingerID)-1;
    gJoyActive  = false;
    gJoyAnalogX = gJoyAnalogY = 0;
    LOGI("TouchControls_Init: OK");
}

void TouchControls_Shutdown(void)
{
    // nothing to clean up
}

// -------------------------------------------------------------------------
// Event processing
// -------------------------------------------------------------------------

bool TouchControls_ProcessEvent(const SDL_Event *event)
{
    // Update window dimensions each time (cheap)
    {
        int count = 0;
        SDL_Window **wins = SDL_GetWindows(&count);
        if (wins && count > 0)
            SDL_GetWindowSizeInPixels(wins[0], &gWindowW, &gWindowH);
        SDL_free(wins);
    }
    UpdateButtonPositions();

    if (event->type != SDL_EVENT_FINGER_DOWN &&
        event->type != SDL_EVENT_FINGER_UP   &&
        event->type != SDL_EVENT_FINGER_MOTION)
        return false;

    float tx = event->tfinger.x * (float)gWindowW;
    float ty = event->tfinger.y * (float)gWindowH;
    SDL_FingerID fid = event->tfinger.fingerID;

    if (event->type == SDL_EVENT_FINGER_DOWN)
    {
        // Check buttons first
        int btn = HitButton(tx, ty);
        if (btn >= 0)
        {
            gBtnDown[btn]   = true;
            gBtnFinger[btn] = fid;
            Haptic(HAPTIC_BTN_MS, HAPTIC_BTN_AMP);
            return true;
        }

        // Check joystick
        if (HitJoystick(tx, ty) && !gJoyActive)
        {
            gJoyActive  = true;
            gJoyFinger  = fid;
            gJoyCenterX = NormX(JOY_CX_NORM);
            gJoyCenterY = NormY(JOY_CY_NORM);
            gJoyTouchX  = tx;
            gJoyTouchY  = ty;
            UpdateJoyAnalog();
            return true;
        }
    }
    else if (event->type == SDL_EVENT_FINGER_UP)
    {
        // Release button
        for (int i = 0; i < kTouchBtn_COUNT; i++)
        {
            if (gBtnFinger[i] == fid)
            {
                gBtnDown[i]   = false;
                gBtnFinger[i] = (SDL_FingerID)-1;
                return true;
            }
        }

        // Release joystick
        if (gJoyFinger == fid)
        {
            gJoyActive  = false;
            gJoyFinger  = (SDL_FingerID)-1;
            gJoyAnalogX = gJoyAnalogY = 0;
            return true;
        }
    }
    else if (event->type == SDL_EVENT_FINGER_MOTION)
    {
        // Update joystick position
        if (gJoyFinger == fid && gJoyActive)
        {
            gJoyTouchX = tx;
            gJoyTouchY = ty;
            UpdateJoyAnalog();
            return true;
        }
    }

    return false;
}

// -------------------------------------------------------------------------
// Query
// -------------------------------------------------------------------------

float TouchControls_GetJoystickX(void) { return gJoyAnalogX; }
float TouchControls_GetJoystickY(void) { return gJoyAnalogY; }

bool TouchControls_IsButtonDown(TouchButtonID btn)
{
    if (btn < 0 || btn >= kTouchBtn_COUNT) return false;
    return gBtnDown[btn];
}

// -------------------------------------------------------------------------
// Drawing helpers (simple GLES3 shapes – no bridge dependency)
// -------------------------------------------------------------------------

static GLuint gOvlShader = 0;
static GLuint gOvlVBO    = 0;
static GLuint gOvlVAO    = 0;
static GLint  gOvlUniColor  = -1;
static GLint  gOvlUniMatrix = -1;
static int    gOvlW = 1, gOvlH = 1;

static const char *kOvlVS =
    "#version 300 es\n"
    "in vec2 a_pos;\n"
    "uniform mat4 u_matrix;\n"
    "void main() { gl_Position = u_matrix * vec4(a_pos, 0.0, 1.0); }\n";

static const char *kOvlFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = u_color; }\n";

static GLuint OvlCompileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static void EnsureOvlShader(void)
{
    if (gOvlShader) return;

    GLuint vs = OvlCompileShader(GL_VERTEX_SHADER,   kOvlVS);
    GLuint fs = OvlCompileShader(GL_FRAGMENT_SHADER, kOvlFS);
    gOvlShader = glCreateProgram();
    glAttachShader(gOvlShader, vs);
    glAttachShader(gOvlShader, fs);
    glBindAttribLocation(gOvlShader, 0, "a_pos");
    glLinkProgram(gOvlShader);
    glDeleteShader(vs);
    glDeleteShader(fs);

    gOvlUniColor  = glGetUniformLocation(gOvlShader, "u_color");
    gOvlUniMatrix = glGetUniformLocation(gOvlShader, "u_matrix");

    glGenBuffers(1, &gOvlVBO);
    glGenVertexArrays(1, &gOvlVAO);
}

// Build orthographic matrix (pixel coords, origin top-left)
static void MakeOrtho2D(float *m, float w, float h)
{
    memset(m, 0, 64);
    m[0]  =  2.0f / w;
    m[5]  = -2.0f / h;
    m[10] = -1.0f;
    m[12] = -1.0f;
    m[13] =  1.0f;
    m[15] =  1.0f;
}

// Draw filled circle
static void DrawFilledCircle(float cx, float cy, float radius, int segs,
                              float r, float g, float b, float a)
{
    EnsureOvlShader();

    float verts[2 + 2 * 65];
    verts[0] = cx; verts[1] = cy;
    for (int i = 0; i <= segs; i++)
    {
        float angle = (float)i / (float)segs * 6.28318f;
        verts[2 + i*2 + 0] = cx + cosf(angle) * radius;
        verts[2 + i*2 + 1] = cy + sinf(angle) * radius;
    }

    float m[16];
    MakeOrtho2D(m, (float)gOvlW, (float)gOvlH);

    glUseProgram(gOvlShader);
    glUniform4f(gOvlUniColor, r, g, b, a);
    glUniformMatrix4fv(gOvlUniMatrix, 1, GL_FALSE, m);

    glBindVertexArray(gOvlVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gOvlVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((segs + 2) * 2 * sizeof(float)), verts, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, segs + 2);
    glBindVertexArray(0);
}

static void DrawCircleOutline(float cx, float cy, float radius, int segs,
                               float r, float g, float b, float a)
{
    EnsureOvlShader();

    float verts[2 * 65];
    for (int i = 0; i < segs; i++)
    {
        float angle = (float)i / (float)segs * 6.28318f;
        verts[i*2 + 0] = cx + cosf(angle) * radius;
        verts[i*2 + 1] = cy + sinf(angle) * radius;
    }

    float m[16];
    MakeOrtho2D(m, (float)gOvlW, (float)gOvlH);

    glUseProgram(gOvlShader);
    glUniform4f(gOvlUniColor, r, g, b, a);
    glUniformMatrix4fv(gOvlUniMatrix, 1, GL_FALSE, m);

    glBindVertexArray(gOvlVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gOvlVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(segs * 2 * sizeof(float)), verts, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_LINE_LOOP, 0, segs);
    glBindVertexArray(0);
}

// Draw a filled triangle
static void DrawTriangle(float x1, float y1, float x2, float y2, float x3, float y3,
                         float r, float g, float b, float a)
{
    EnsureOvlShader();
    float verts[6] = { x1, y1, x2, y2, x3, y3 };
    float m[16];
    MakeOrtho2D(m, (float)gOvlW, (float)gOvlH);
    glUseProgram(gOvlShader);
    glUniform4f(gOvlUniColor, r, g, b, a);
    glUniformMatrix4fv(gOvlUniMatrix, 1, GL_FALSE, m);
    glBindVertexArray(gOvlVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gOvlVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

// Draw a filled axis-aligned rectangle (lx=left, ty=top, w=width, h=height)
static void DrawQuad(float lx, float ty, float w, float h,
                     float r, float g, float b, float a)
{
    EnsureOvlShader();
    float rx = lx + w, by = ty + h;
    float verts[8] = { lx, ty,  rx, ty,  lx, by,  rx, by };
    float m[16];
    MakeOrtho2D(m, (float)gOvlW, (float)gOvlH);
    glUseProgram(gOvlShader);
    glUniform4f(gOvlUniColor, r, g, b, a);
    glUniformMatrix4fv(gOvlUniMatrix, 1, GL_FALSE, m);
    glBindVertexArray(gOvlVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gOvlVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

// Draw a recognisable icon inside each button circle.
// Icon size is ~55% of the button radius so it fits comfortably inside.
static void DrawButtonIcon(int btn)
{
    float cx = gBtnCX[btn];
    float cy = gBtnCY[btn];
    float s  = BtnRadius(btn) * 0.55f;  // icon half-size in pixels
    const float wr = 1.0f, wg = 1.0f, wb = 1.0f, wa = 0.85f;

    switch (btn)
    {
        case kTouchBtn_Jump:
            // Up-pointing arrow: head + shaft
            DrawTriangle(cx,       cy - s,
                         cx - s*0.85f, cy + s*0.25f,
                         cx + s*0.85f, cy + s*0.25f,  wr, wg, wb, wa);
            DrawQuad(cx - s*0.22f, cy + s*0.1f, s*0.44f, s*0.7f, wr, wg, wb, wa);
            break;

        case kTouchBtn_Attack:
            // Cross (+): two overlapping thin quads
            DrawQuad(cx - s,       cy - s*0.18f, s*2.0f, s*0.36f, wr, wg, wb, wa);
            DrawQuad(cx - s*0.18f, cy - s,       s*0.36f, s*2.0f, wr, wg, wb, wa);
            break;

        case kTouchBtn_Pickup:
            // Down-pointing arrow: head + shaft
            DrawTriangle(cx,       cy + s,
                         cx - s*0.85f, cy - s*0.25f,
                         cx + s*0.85f, cy - s*0.25f,  wr, wg, wb, wa);
            DrawQuad(cx - s*0.22f, cy - s*0.8f, s*0.44f, s*0.7f, wr, wg, wb, wa);
            break;

        case kTouchBtn_JetUp:
            // Flame / rocket: filled upward triangle
            DrawTriangle(cx,        cy - s*0.9f,
                         cx - s*0.75f, cy + s*0.6f,
                         cx + s*0.75f, cy + s*0.6f,  wr, wg, wb, wa);
            break;

        case kTouchBtn_JetDown:
            // Downward triangle
            DrawTriangle(cx,        cy + s*0.9f,
                         cx - s*0.75f, cy - s*0.6f,
                         cx + s*0.75f, cy - s*0.6f,  wr, wg, wb, wa);
            break;

        case kTouchBtn_PrevWeapon:
            // Left-pointing arrow (◄)
            DrawTriangle(cx - s*0.8f, cy,
                         cx + s*0.5f,  cy - s*0.75f,
                         cx + s*0.5f,  cy + s*0.75f, wr, wg, wb, wa);
            break;

        case kTouchBtn_NextWeapon:
            // Right-pointing arrow (►)
            DrawTriangle(cx + s*0.8f, cy,
                         cx - s*0.5f,  cy - s*0.75f,
                         cx - s*0.5f,  cy + s*0.75f, wr, wg, wb, wa);
            break;

        case kTouchBtn_Pause:
            // Two vertical bars (||)
            DrawQuad(cx - s*0.6f, cy - s*0.7f, s*0.35f, s*1.4f, wr, wg, wb, wa);
            DrawQuad(cx + s*0.25f, cy - s*0.7f, s*0.35f, s*1.4f, wr, wg, wb, wa);
            break;

        default:
            break;
    }
}

// -------------------------------------------------------------------------
// Draw
// -------------------------------------------------------------------------

void TouchControls_Draw(void)
{
    {
        int count = 0;
        SDL_Window **wins = SDL_GetWindows(&count);
        if (!wins || count == 0) { SDL_free(wins); return; }
        SDL_GetWindowSizeInPixels(wins[0], &gOvlW, &gOvlH);
        SDL_free(wins);
    }
    UpdateButtonPositions();

    // Force full-window viewport so controls are not clipped to the 3D pane.
    // The 3D scene sets a restricted glViewport (paneClip) and possibly
    // glScissor; reset both so the overlay covers the entire screen.
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, gOvlW, gOvlH);

    // Save/restore some GL state
    GLboolean depthTest, blend, cullFace;
    glGetBooleanv(GL_DEPTH_TEST, &depthTest);
    glGetBooleanv(GL_BLEND,      &blend);
    glGetBooleanv(GL_CULL_FACE,  &cullFace);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw the D-pad: a cross of four arms, the pressed arm(s) lit
    float jcx = NormX(JOY_CX_NORM);
    float jcy = NormY(JOY_CY_NORM);
    float jr  = JoyRadius();
    float aw  = jr * DPAD_ARM_W;           // half arm width
    float ah  = jr;                        // arm length from centre
    {
        bool up = gJoyAnalogY > 0, down = gJoyAnalogY < 0, left = gJoyAnalogX < 0, right = gJoyAnalogX > 0;
        float base = 0.18f, lit = 0.55f;
        // arms (top, bottom, left, right)
        DrawQuad(jcx - aw, jcy - ah, aw*2, ah - aw,  0.4f, 0.4f, 0.4f, up    ? lit : base);
        DrawQuad(jcx - aw, jcy + aw, aw*2, ah - aw,  0.4f, 0.4f, 0.4f, down  ? lit : base);
        DrawQuad(jcx - ah, jcy - aw, ah - aw, aw*2,  0.4f, 0.4f, 0.4f, left  ? lit : base);
        DrawQuad(jcx + aw, jcy - aw, ah - aw, aw*2,  0.4f, 0.4f, 0.4f, right ? lit : base);
        DrawQuad(jcx - aw, jcy - aw, aw*2, aw*2,     0.4f, 0.4f, 0.4f, base);            // hub
        // arrow heads on each arm
        float s = aw * 0.7f, wa = 0.85f;
        DrawTriangle(jcx, jcy - ah + s*0.3f, jcx - s, jcy - ah + s*1.6f, jcx + s, jcy - ah + s*1.6f, 1,1,1, wa);
        DrawTriangle(jcx, jcy + ah - s*0.3f, jcx - s, jcy + ah - s*1.6f, jcx + s, jcy + ah - s*1.6f, 1,1,1, wa);
        DrawTriangle(jcx - ah + s*0.3f, jcy, jcx - ah + s*1.6f, jcy - s, jcx - ah + s*1.6f, jcy + s, 1,1,1, wa);
        DrawTriangle(jcx + ah - s*0.3f, jcy, jcx + ah - s*1.6f, jcy - s, jcx + ah - s*1.6f, jcy + s, 1,1,1, wa);
        DrawCircleOutline(jcx, jcy, jr * 1.15f, 32,  0.7f, 0.7f, 0.7f, 0.25f);   // touch area hint
    }

    // Draw action buttons
    for (int i = 0; i < kTouchBtn_COUNT; i++)
    {
        float r = BtnRadius(i);
        float alpha = gBtnDown[i] ? 0.55f : 0.22f;
        // Color-code buttons: action=blue, jet=green, weapon=orange, pause=red
        float br, bg, bb;
        if (i == kTouchBtn_Pause)
        {
            br = 0.8f; bg = 0.2f; bb = 0.2f;
        }
        else if (i == kTouchBtn_JetUp || i == kTouchBtn_JetDown)
        {
            br = 0.2f; bg = 0.7f; bb = 0.3f;
        }
        else if (i == kTouchBtn_PrevWeapon || i == kTouchBtn_NextWeapon)
        {
            br = 0.8f; bg = 0.5f; bb = 0.1f;
        }
        else
        {
            br = 0.4f; bg = 0.4f; bb = 0.8f;
        }
        DrawFilledCircle(gBtnCX[i], gBtnCY[i], r, 20,   br, bg, bb, alpha);
        DrawCircleOutline(gBtnCX[i], gBtnCY[i], r, 20,
                          br * OUTLINE_BRIGHTNESS_SCALE,
                          bg * OUTLINE_BRIGHTNESS_SCALE,
                          bb * OUTLINE_BRIGHTNESS_SCALE, 0.55f);
    }

    // Draw icons on top of each button
    for (int i = 0; i < kTouchBtn_COUNT; i++)
        DrawButtonIcon(i);

    // Restore state
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend)     glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    if (cullFace)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
}

#endif // __ANDROID__
