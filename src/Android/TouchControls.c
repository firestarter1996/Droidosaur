// TOUCH CONTROLS IMPLEMENTATION FOR ANDROID
// Virtual joystick + action buttons for Nanosaur.

#ifdef __ANDROID__

#include "TouchControls.h"
#include <GLES3/gl3.h>
#include <android/log.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO, "Nanosaur", __VA_ARGS__)

// -------------------------------------------------------------------------
// Layout constants (in normalised window coords 0..1, origin = top-left)
// -------------------------------------------------------------------------

// Joystick – left side
#define JOY_CX_NORM     0.12f
#define JOY_CY_NORM     0.65f
#define JOY_RADIUS_NORM 0.09f

// Action buttons – right side (diamond layout)
#define BTN_CX_NORM     0.85f
#define BTN_CY_NORM     0.68f
#define BTN_RADIUS_NORM 0.048f
#define BTN_SPACING     0.075f

// Jetpack buttons – below the action diamond
#define JET_BTN_CY_NORM        0.75f    // absolute normalised Y position
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

static void UpdateJoyAnalog(void)
{
    if (!gJoyActive) { gJoyAnalogX = gJoyAnalogY = 0; return; }

    float dx = (gJoyTouchX - gJoyCenterX) / JoyRadius();
    float dy = (gJoyTouchY - gJoyCenterY) / JoyRadius();
    float len = sqrtf(dx*dx + dy*dy);
    if (len > 1.0f) { dx /= len; dy /= len; len = 1.0f; }
    if (len < DEAD_ZONE) { gJoyAnalogX = gJoyAnalogY = 0; return; }
    float norm = (len - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    gJoyAnalogX =  dx * norm;
    gJoyAnalogY = -dy * norm;   // SDL y is down, game forward is +y
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

    // Draw joystick background
    float jcx = NormX(JOY_CX_NORM);
    float jcy = NormY(JOY_CY_NORM);
    float jr  = JoyRadius();

    DrawFilledCircle(jcx, jcy, jr, 32,   0.3f, 0.3f, 0.3f, 0.15f);
    DrawCircleOutline(jcx, jcy, jr, 32,  0.7f, 0.7f, 0.7f, 0.4f);

    // Draw thumb if active
    if (gJoyActive)
    {
        float tx = gJoyTouchX;
        float ty = gJoyTouchY;
        float dx = tx - jcx, dy = ty - jcy;
        float len = sqrtf(dx*dx + dy*dy);
        if (len > jr) { tx = jcx + dx/len*jr; ty = jcy + dy/len*jr; }
        DrawFilledCircle(tx, ty, jr * 0.3f, 16,  0.5f, 0.5f, 0.5f, 0.45f);
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

    // Restore state
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend)     glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    if (cullFace)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
}

#endif // __ANDROID__
