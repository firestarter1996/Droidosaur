// GLES BRIDGE IMPLEMENTATION
// Emulates OpenGL 1.x/2.x fixed-function pipeline on top of OpenGL ES 3.0
// for the Android port of Nanosaur.

#ifdef __ANDROID__

#include <GLES3/gl3.h>
#include <android/log.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

// GLdouble is not in GLES3; define it so that bridge_Ortho / bridge_Frustum
// (which mirror the desktop glOrtho/glFrustum signatures) compile cleanly.
typedef double GLdouble;

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  "Nanosaur", __VA_ARGS__)
#define LOGW(...)  __android_log_print(ANDROID_LOG_WARN,  "Nanosaur", __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "Nanosaur", __VA_ARGS__)

// Undefine our own macros so this .c file can call the real GLES functions
#undef glEnable
#undef glDisable
#undef glIsEnabled
#undef glEnableClientState
#undef glDisableClientState
#undef glVertexPointer
#undef glNormalPointer
#undef glTexCoordPointer
#undef glColorPointer
#undef glColor4f
#undef glColor4fv
#undef glDrawElements
#undef glDrawArrays
#undef glBegin
#undef glEnd
#undef glVertex3f
#undef glVertex2f
#undef glNormal3f
#undef glTexCoord2f
#undef glPolygonMode
#undef glMatrixMode
#undef glLoadIdentity
#undef glLoadMatrixf
#undef glMultMatrixf
#undef glPushMatrix
#undef glPopMatrix
#undef glTranslatef
#undef glRotatef
#undef glScalef
#undef glOrtho
#undef glFrustum
#undef glGetFloatv
#undef glLightfv
#undef glLightModelfv
#undef glMaterialfv
#undef glColorMaterial
#undef glFogf
#undef glFogfv
#undef glFogi
#undef glHint
#undef glAlphaFunc

// -------------------------------------------------------------------------
// Math helpers
// -------------------------------------------------------------------------

typedef struct { float m[16]; } Mat4;

static void Mat4_Identity(Mat4 *out)
{
    memset(out, 0, sizeof(*out));
    out->m[0] = out->m[5] = out->m[10] = out->m[15] = 1.0f;
}

static void Mat4_Multiply(const Mat4 *a, const Mat4 *b, Mat4 *out)
{
    Mat4 tmp;
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a->m[k * 4 + row] * b->m[col * 4 + k];
            tmp.m[col * 4 + row] = sum;
        }
    }
    *out = tmp;
}

// Normal matrix = upper-left 3x3 of modelview (assume uniform scale)
static void Mat4_NormalMatrix(const Mat4 *mv, float out[9])
{
    out[0] = mv->m[0]; out[1] = mv->m[1]; out[2] = mv->m[2];
    out[3] = mv->m[4]; out[4] = mv->m[5]; out[5] = mv->m[6];
    out[6] = mv->m[8]; out[7] = mv->m[9]; out[8] = mv->m[10];
}

// -------------------------------------------------------------------------
// Matrix stacks
// -------------------------------------------------------------------------

#define MATRIX_STACK_DEPTH  32

typedef struct
{
    Mat4    stack[MATRIX_STACK_DEPTH];
    int     top;
} MatrixStack;

static MatrixStack  gMatMV;          // GL_MODELVIEW
static MatrixStack  gMatProj;        // GL_PROJECTION
static MatrixStack  gMatTex;         // GL_TEXTURE
static MatrixStack *gCurrentStack;   // pointer to active stack
static int          gCurrentMode;    // GL_MODELVIEW / GL_PROJECTION / GL_TEXTURE

static void MatStack_Init(MatrixStack *s)
{
    Mat4_Identity(&s->stack[0]);
    s->top = 0;
}

static Mat4 *MatStack_Top(MatrixStack *s)
{
    return &s->stack[s->top];
}

static void MatStack_Push(MatrixStack *s)
{
    if (s->top + 1 >= MATRIX_STACK_DEPTH)
    {
        LOGE("Matrix stack overflow");
        return;
    }
    s->stack[s->top + 1] = s->stack[s->top];
    s->top++;
}

static void MatStack_Pop(MatrixStack *s)
{
    if (s->top <= 0)
    {
        LOGE("Matrix stack underflow");
        return;
    }
    s->top--;
}

// -------------------------------------------------------------------------
// Lighting state
// -------------------------------------------------------------------------

#define MAX_LIGHTS 8

typedef struct
{
    float position[4];
    float ambient[4];
    float diffuse[4];
    float specular[4];
    bool  enabled;
} LightState;

static LightState gLights[MAX_LIGHTS];
static float      gGlobalAmbient[4] = { 0.2f, 0.2f, 0.2f, 1.0f };

// -------------------------------------------------------------------------
// Fog state
// -------------------------------------------------------------------------

typedef struct
{
    float color[4];
    float start;
    float end;
    float density;
    int   mode;    // GL_LINEAR / GL_EXP / GL_EXP2
    bool  enabled;
} FogState;

static FogState gFog = {
    .color   = { 0, 0, 0, 1 },
    .start   = 0.0f,
    .end     = 1.0f,
    .density = 1.0f,
    .mode    = 0x2601, // GL_LINEAR
    .enabled = false
};

// -------------------------------------------------------------------------
// Alpha test state
// -------------------------------------------------------------------------

static struct
{
    GLenum func;
    float  ref;
    bool   enabled;
} gAlphaTest = { 0x0207 /* GL_ALWAYS */, 0.0f, false };

// -------------------------------------------------------------------------
// Misc render state
// -------------------------------------------------------------------------

static bool gLightingEnabled      = false;
static bool gColorMaterialEnabled = false;
static bool gNormalizeEnabled     = false;
static bool gTexture2DEnabled     = false;

// Current color (glColor4f)
static float gCurrentColor[4] = { 1, 1, 1, 1 };

// -------------------------------------------------------------------------
// Client array state
// -------------------------------------------------------------------------

typedef struct
{
    bool         enabled;
    GLint        size;
    GLenum       type;
    GLsizei      stride;
    const void  *pointer;
} ClientArray;

static ClientArray gVertexArray   = { false, 4, GL_FLOAT, 0, NULL };
static ClientArray gNormalArray   = { false, 3, GL_FLOAT, 0, NULL };
static ClientArray gColorArray    = { false, 4, GL_FLOAT, 0, NULL };
static ClientArray gTexCoordArray = { false, 2, GL_FLOAT, 0, NULL };

// -------------------------------------------------------------------------
// Streaming VBOs
// -------------------------------------------------------------------------

static GLuint gStreamVBO   = 0;   // vertex data
static GLuint gStreamIBO   = 0;   // index data
static GLuint gVAO         = 0;   // vertex array object

// -------------------------------------------------------------------------
// Immediate mode buffers
// -------------------------------------------------------------------------

#define IMM_MAX_VERTS  8192

typedef struct
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
} ImmVertex;

static ImmVertex  gImmVerts[IMM_MAX_VERTS];
static int        gImmVertCount = 0;
static GLenum     gImmMode      = 0;
static float      gImmCurrentNormal[3]   = { 0, 0, 1 };
static float      gImmCurrentTexCoord[2] = { 0, 0 };

// -------------------------------------------------------------------------
// Shader
// -------------------------------------------------------------------------

static GLuint gShaderProgram = 0;

// Attribute locations
static GLint  gAttrPosition = -1;
static GLint  gAttrNormal   = -1;
static GLint  gAttrTexCoord = -1;
static GLint  gAttrColor    = -1;

// Uniform locations
static GLint  gUniMVMatrix      = -1;
static GLint  gUniProjMatrix    = -1;
static GLint  gUniNormalMatrix  = -1;
static GLint  gUniTexMatrix     = -1;

static GLint  gUniLightingEnabled    = -1;
static GLint  gUniColorMaterial      = -1;
static GLint  gUniGlobalAmbient      = -1;
static GLint  gUniLightEnabled[MAX_LIGHTS];
static GLint  gUniLightPos[MAX_LIGHTS];
static GLint  gUniLightAmbient[MAX_LIGHTS];
static GLint  gUniLightDiffuse[MAX_LIGHTS];

static GLint  gUniFogEnabled   = -1;
static GLint  gUniFogColor     = -1;
static GLint  gUniFogStart     = -1;
static GLint  gUniFogEnd       = -1;

static GLint  gUniAlphaTestEnabled = -1;
static GLint  gUniAlphaRef         = -1;
static GLint  gUniAlphaFunc        = -1;

static GLint  gUniTextureEnabled   = -1;
static GLint  gUniTexture          = -1;
static GLint  gUniCurrentColor     = -1;
static GLint  gUniHasVertexColors  = -1;

// -------------------------------------------------------------------------
// Vertex shader source
// -------------------------------------------------------------------------

static const char *kVertexShaderSrc =
    "#version 300 es\n"
    "in vec3 a_position;\n"
    "in vec3 a_normal;\n"
    "in vec2 a_texcoord;\n"
    "in vec4 a_color;\n"
    "\n"
    "uniform mat4  u_mvMatrix;\n"
    "uniform mat4  u_projMatrix;\n"
    "uniform mat3  u_normalMatrix;\n"
    "uniform mat4  u_texMatrix;\n"
    "\n"
    "uniform bool  u_lightingEnabled;\n"
    "uniform bool  u_colorMaterial;\n"
    "uniform vec4  u_globalAmbient;\n"
    "uniform bool  u_lightEnabled[8];\n"
    "uniform vec4  u_lightPos[8];\n"
    "uniform vec4  u_lightAmbient[8];\n"
    "uniform vec4  u_lightDiffuse[8];\n"
    "\n"
    "uniform bool  u_hasVertexColors;\n"
    "uniform vec4  u_currentColor;\n"
    "\n"
    "out vec4 v_color;\n"
    "out vec2 v_texcoord;\n"
    "out float v_eyeDepth;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec4 eyePos = u_mvMatrix * vec4(a_position, 1.0);\n"
    "    gl_Position = u_projMatrix * eyePos;\n"
    "    v_eyeDepth  = -eyePos.z;\n"
    "\n"
    "    vec4 baseColor = u_hasVertexColors ? a_color : u_currentColor;\n"
    "\n"
    "    if (u_lightingEnabled)\n"
    "    {\n"
    "        vec3 eyeNormal = normalize(u_normalMatrix * a_normal);\n"
    "        vec4 matColor  = u_colorMaterial ? baseColor : u_currentColor;\n"
    "\n"
    "        vec4 ambient  = u_globalAmbient;\n"
    "        vec4 diffuse  = vec4(0.0);\n"
    "        for (int i = 0; i < 8; i++)\n"
    "        {\n"
    "            if (u_lightEnabled[i])\n"
    "            {\n"
    "                vec3 lightDir = normalize(u_lightPos[i].xyz);\n"
    "                ambient  += u_lightAmbient[i];\n"
    "                diffuse  += max(dot(eyeNormal, lightDir), 0.0) * u_lightDiffuse[i];\n"
    "            }\n"
    "        }\n"
    "        v_color = clamp((ambient + diffuse) * matColor, 0.0, 1.0);\n"
    "        v_color.a = matColor.a;\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        v_color = baseColor;\n"
    "    }\n"
    "\n"
    "    v_texcoord = (u_texMatrix * vec4(a_texcoord, 0.0, 1.0)).xy;\n"
    "}\n";

// -------------------------------------------------------------------------
// Fragment shader source
// -------------------------------------------------------------------------

static const char *kFragmentShaderSrc =
    "#version 300 es\n"
    "precision mediump float;\n"
    "\n"
    "in vec4  v_color;\n"
    "in vec2  v_texcoord;\n"
    "in float v_eyeDepth;\n"
    "\n"
    "uniform bool      u_textureEnabled;\n"
    "uniform sampler2D u_texture;\n"
    "\n"
    "uniform bool  u_alphaTestEnabled;\n"
    "uniform float u_alphaRef;\n"
    "uniform int   u_alphaFunc;\n"
    "\n"
    "uniform bool  u_fogEnabled;\n"
    "uniform vec4  u_fogColor;\n"
    "uniform float u_fogStart;\n"
    "uniform float u_fogEnd;\n"
    "\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec4 color = v_color;\n"
    "\n"
    "    if (u_textureEnabled)\n"
    "        color *= texture(u_texture, v_texcoord);\n"
    "\n"
    "    if (u_alphaTestEnabled)\n"
    "    {\n"
    "        float a = color.a;\n"
    "        float r = u_alphaRef;\n"
    "        bool pass = true;\n"
    "        if      (u_alphaFunc == 0x0207) pass = true;\n"
    "        else if (u_alphaFunc == 0x0200) pass = false;\n"
    "        else if (u_alphaFunc == 0x0201) pass = (a <  r);\n"
    "        else if (u_alphaFunc == 0x0202) pass = (a == r);\n"
    "        else if (u_alphaFunc == 0x0203) pass = (a <= r);\n"
    "        else if (u_alphaFunc == 0x0204) pass = (a >  r);\n"
    "        else if (u_alphaFunc == 0x0205) pass = (a != r);\n"
    "        else if (u_alphaFunc == 0x0206) pass = (a >= r);\n"
    "        if (!pass) discard;\n"
    "    }\n"
    "\n"
    "    if (u_fogEnabled)\n"
    "    {\n"
    "        float fogFactor = clamp((u_fogEnd - v_eyeDepth) / (u_fogEnd - u_fogStart), 0.0, 1.0);\n"
    "        color.rgb = mix(u_fogColor.rgb, color.rgb, fogFactor);\n"
    "    }\n"
    "\n"
    "    fragColor = color;\n"
    "}\n";

// -------------------------------------------------------------------------
// Shader compilation helpers
// -------------------------------------------------------------------------

static GLuint CompileShader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        LOGE("Shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint LinkProgram(GLuint vs, GLuint fs)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);

    // Bind attribute locations before linking
    glBindAttribLocation(prog, 0, "a_position");
    glBindAttribLocation(prog, 1, "a_normal");
    glBindAttribLocation(prog, 2, "a_texcoord");
    glBindAttribLocation(prog, 3, "a_color");

    glLinkProgram(prog);

    GLint status = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status)
    {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        LOGE("Program link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// -------------------------------------------------------------------------
// Init / Shutdown
// -------------------------------------------------------------------------

void GLESBridge_Init(void)
{
    // Guard against multiple initialization
    if (gShaderProgram)
    {
        // Already initialized; just reset the matrix stacks and light state
        MatStack_Init(&gMatMV);
        MatStack_Init(&gMatProj);
        MatStack_Init(&gMatTex);
        gCurrentStack = &gMatMV;
        gCurrentMode  = 0x1700;
        return;
    }

    // Initialize matrix stacks
    MatStack_Init(&gMatMV);
    MatStack_Init(&gMatProj);
    MatStack_Init(&gMatTex);
    gCurrentStack = &gMatMV;
    gCurrentMode  = 0x1700; // GL_MODELVIEW

    // Initialize lights (all off)
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        gLights[i].enabled     = false;
        gLights[i].ambient[0]  = 0; gLights[i].ambient[1]  = 0;
        gLights[i].ambient[2]  = 0; gLights[i].ambient[3]  = 1;
        gLights[i].diffuse[0]  = (i == 0) ? 1.0f : 0;
        gLights[i].diffuse[1]  = (i == 0) ? 1.0f : 0;
        gLights[i].diffuse[2]  = (i == 0) ? 1.0f : 0;
        gLights[i].diffuse[3]  = 1.0f;
        gLights[i].position[0] = 0; gLights[i].position[1] = 0;
        gLights[i].position[2] = 1; gLights[i].position[3] = 0;
    }

    // Compile and link shaders
    GLuint vs = CompileShader(GL_VERTEX_SHADER,   kVertexShaderSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
    gShaderProgram = LinkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!gShaderProgram)
    {
        LOGE("GLESBridge_Init: shader compilation failed");
        return;
    }

    glUseProgram(gShaderProgram);

    // Cache attribute locations
    gAttrPosition = glGetAttribLocation(gShaderProgram, "a_position");
    gAttrNormal   = glGetAttribLocation(gShaderProgram, "a_normal");
    gAttrTexCoord = glGetAttribLocation(gShaderProgram, "a_texcoord");
    gAttrColor    = glGetAttribLocation(gShaderProgram, "a_color");

    // Cache uniform locations
    gUniMVMatrix     = glGetUniformLocation(gShaderProgram, "u_mvMatrix");
    gUniProjMatrix   = glGetUniformLocation(gShaderProgram, "u_projMatrix");
    gUniNormalMatrix = glGetUniformLocation(gShaderProgram, "u_normalMatrix");
    gUniTexMatrix    = glGetUniformLocation(gShaderProgram, "u_texMatrix");

    gUniLightingEnabled  = glGetUniformLocation(gShaderProgram, "u_lightingEnabled");
    gUniColorMaterial    = glGetUniformLocation(gShaderProgram, "u_colorMaterial");
    gUniGlobalAmbient    = glGetUniformLocation(gShaderProgram, "u_globalAmbient");
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        char name[64];
        snprintf(name, sizeof(name), "u_lightEnabled[%d]", i);
        gUniLightEnabled[i] = glGetUniformLocation(gShaderProgram, name);
        snprintf(name, sizeof(name), "u_lightPos[%d]", i);
        gUniLightPos[i]     = glGetUniformLocation(gShaderProgram, name);
        snprintf(name, sizeof(name), "u_lightAmbient[%d]", i);
        gUniLightAmbient[i] = glGetUniformLocation(gShaderProgram, name);
        snprintf(name, sizeof(name), "u_lightDiffuse[%d]", i);
        gUniLightDiffuse[i] = glGetUniformLocation(gShaderProgram, name);
    }

    gUniFogEnabled   = glGetUniformLocation(gShaderProgram, "u_fogEnabled");
    gUniFogColor     = glGetUniformLocation(gShaderProgram, "u_fogColor");
    gUniFogStart     = glGetUniformLocation(gShaderProgram, "u_fogStart");
    gUniFogEnd       = glGetUniformLocation(gShaderProgram, "u_fogEnd");

    gUniAlphaTestEnabled = glGetUniformLocation(gShaderProgram, "u_alphaTestEnabled");
    gUniAlphaRef         = glGetUniformLocation(gShaderProgram, "u_alphaRef");
    gUniAlphaFunc        = glGetUniformLocation(gShaderProgram, "u_alphaFunc");

    gUniTextureEnabled  = glGetUniformLocation(gShaderProgram, "u_textureEnabled");
    gUniTexture         = glGetUniformLocation(gShaderProgram, "u_texture");
    gUniCurrentColor    = glGetUniformLocation(gShaderProgram, "u_currentColor");
    gUniHasVertexColors = glGetUniformLocation(gShaderProgram, "u_hasVertexColors");

    // Set texture unit 0
    glUniform1i(gUniTexture, 0);

    // Create streaming VBO and IBO
    glGenBuffers(1, &gStreamVBO);
    glGenBuffers(1, &gStreamIBO);

    // Create VAO
    glGenVertexArrays(1, &gVAO);
    glBindVertexArray(gVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gStreamVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gStreamIBO);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);

    // Upload identity matrices as defaults
    float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(gUniMVMatrix,   1, GL_FALSE, identity);
    glUniformMatrix4fv(gUniProjMatrix, 1, GL_FALSE, identity);
    glUniformMatrix4fv(gUniTexMatrix,  1, GL_FALSE, identity);
    float identity3[9] = {1,0,0, 0,1,0, 0,0,1};
    glUniformMatrix3fv(gUniNormalMatrix, 1, GL_FALSE, identity3);

    (void)gAttrPosition;
    (void)gAttrNormal;
    (void)gAttrTexCoord;
    (void)gAttrColor;

    LOGI("GLESBridge_Init: OK");
}

void GLESBridge_Shutdown(void)
{
    if (gShaderProgram) { glDeleteProgram(gShaderProgram); gShaderProgram = 0; }
    if (gStreamVBO)     { glDeleteBuffers(1, &gStreamVBO); gStreamVBO = 0; }
    if (gStreamIBO)     { glDeleteBuffers(1, &gStreamIBO); gStreamIBO = 0; }
    if (gVAO)           { glDeleteVertexArrays(1, &gVAO); gVAO = 0; }
}

// -------------------------------------------------------------------------
// Matrix stack
// -------------------------------------------------------------------------

void bridge_MatrixMode(GLenum mode)
{
    gCurrentMode = (int)mode;
    switch (mode)
    {
        case 0x1700: gCurrentStack = &gMatMV;   break;
        case 0x1701: gCurrentStack = &gMatProj; break;
        case 0x1702: gCurrentStack = &gMatTex;  break;
        default:
            LOGW("bridge_MatrixMode: unknown mode %u", mode);
            gCurrentStack = &gMatMV;
            break;
    }
}

void bridge_LoadIdentity(void)
{
    Mat4_Identity(MatStack_Top(gCurrentStack));
}

void bridge_LoadMatrixf(const GLfloat *m)
{
    memcpy(MatStack_Top(gCurrentStack)->m, m, 16 * sizeof(float));
}

void bridge_MultMatrixf(const GLfloat *m)
{
    Mat4 b;
    memcpy(b.m, m, 16 * sizeof(float));
    Mat4 result;
    Mat4_Multiply(MatStack_Top(gCurrentStack), &b, &result);
    *MatStack_Top(gCurrentStack) = result;
}

void bridge_PushMatrix(void)
{
    MatStack_Push(gCurrentStack);
}

void bridge_PopMatrix(void)
{
    MatStack_Pop(gCurrentStack);
}

void bridge_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
    Mat4 t;
    Mat4_Identity(&t);
    t.m[12] = x; t.m[13] = y; t.m[14] = z;
    bridge_MultMatrixf(t.m);
}

void bridge_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float rad = angle * (float)(3.14159265358979323846 / 180.0);
    float c = cosf(rad), s = sinf(rad);
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 1e-6f) { x /= len; y /= len; z /= len; }
    Mat4 r;
    r.m[0]  = x*x*(1-c)+c;   r.m[1]  = y*x*(1-c)+z*s; r.m[2]  = z*x*(1-c)-y*s; r.m[3]  = 0;
    r.m[4]  = x*y*(1-c)-z*s; r.m[5]  = y*y*(1-c)+c;   r.m[6]  = z*y*(1-c)+x*s; r.m[7]  = 0;
    r.m[8]  = x*z*(1-c)+y*s; r.m[9]  = y*z*(1-c)-x*s; r.m[10] = z*z*(1-c)+c;   r.m[11] = 0;
    r.m[12] = 0;              r.m[13] = 0;              r.m[14] = 0;              r.m[15] = 1;
    bridge_MultMatrixf(r.m);
}

void bridge_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
    Mat4 s;
    Mat4_Identity(&s);
    s.m[0] = x; s.m[5] = y; s.m[10] = z;
    bridge_MultMatrixf(s.m);
}

void bridge_Ortho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                  GLdouble nearVal, GLdouble farVal)
{
    float l = (float)left, r = (float)right, b = (float)bottom, t = (float)top;
    float n = (float)nearVal, f = (float)farVal;
    Mat4 m;
    Mat4_Identity(&m);
    m.m[0]  =  2.0f / (r - l);
    m.m[5]  =  2.0f / (t - b);
    m.m[10] = -2.0f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    bridge_MultMatrixf(m.m);
}

void bridge_Frustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                    GLdouble nearVal, GLdouble farVal)
{
    float l = (float)left, r = (float)right, b = (float)bottom, t = (float)top;
    float n = (float)nearVal, f = (float)farVal;
    Mat4 m;
    memset(&m, 0, sizeof(m));
    m.m[0]  =  2.0f * n / (r - l);
    m.m[5]  =  2.0f * n / (t - b);
    m.m[8]  =  (r + l) / (r - l);
    m.m[9]  =  (t + b) / (t - b);
    m.m[10] = -(f + n) / (f - n);
    m.m[11] = -1.0f;
    m.m[14] = -2.0f * f * n / (f - n);
    bridge_MultMatrixf(m.m);
}

void bridge_GetFloatv(GLenum pname, GLfloat *params)
{
    switch (pname)
    {
        case 0x0BA6: // GL_MODELVIEW_MATRIX
            memcpy(params, MatStack_Top(&gMatMV)->m, 16 * sizeof(float));
            break;
        case 0x0BA7: // GL_PROJECTION_MATRIX
            memcpy(params, MatStack_Top(&gMatProj)->m, 16 * sizeof(float));
            break;
        default:
            glGetFloatv(pname, params);
            break;
    }
}

// -------------------------------------------------------------------------
// Lighting
// -------------------------------------------------------------------------

void bridge_Lightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    int i = (int)(light - 0x4000); // GL_LIGHT0
    if (i < 0 || i >= MAX_LIGHTS) return;

    switch (pname)
    {
        case 0x1200: // GL_AMBIENT
            memcpy(gLights[i].ambient,  params, 4 * sizeof(float)); break;
        case 0x1201: // GL_DIFFUSE
            memcpy(gLights[i].diffuse,  params, 4 * sizeof(float)); break;
        case 0x1202: // GL_SPECULAR
            memcpy(gLights[i].specular, params, 4 * sizeof(float)); break;
        case 0x1203: // GL_POSITION
            memcpy(gLights[i].position, params, 4 * sizeof(float));
            break;
        default:
            break;
    }
}

void bridge_LightModelfv(GLenum pname, const GLfloat *params)
{
    if (pname == 0x0B53 /* GL_LIGHT_MODEL_AMBIENT */)
        memcpy(gGlobalAmbient, params, 4 * sizeof(float));
}

void bridge_Materialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    (void)face; (void)pname; (void)params;
}

void bridge_ColorMaterial(GLenum face, GLenum mode)
{
    (void)face; (void)mode;
}

// -------------------------------------------------------------------------
// Fog
// -------------------------------------------------------------------------

void bridge_Fogf(GLenum pname, GLfloat param)
{
    switch (pname)
    {
        case 0x0B63: gFog.start   = param; break; // GL_FOG_START
        case 0x0B64: gFog.end     = param; break; // GL_FOG_END
        case 0x0B62: gFog.density = param; break; // GL_FOG_DENSITY
        default: break;
    }
}

void bridge_Fogfv(GLenum pname, const GLfloat *params)
{
    if (pname == 0x0B66) // GL_FOG_COLOR
        memcpy(gFog.color, params, 4 * sizeof(float));
    else
        bridge_Fogf(pname, params[0]);
}

void bridge_Fogi(GLenum pname, GLint param)
{
    if (pname == 0x0B65) // GL_FOG_MODE
        gFog.mode = param;
    else
        bridge_Fogf(pname, (float)param);
}

// -------------------------------------------------------------------------
// Alpha test
// -------------------------------------------------------------------------

void bridge_AlphaFunc(GLenum func, GLfloat ref)
{
    gAlphaTest.func = func;
    gAlphaTest.ref  = ref;
}

// -------------------------------------------------------------------------
// State (Enable/Disable)
// -------------------------------------------------------------------------

void bridge_Enable(GLenum cap)
{
    // GL_LIGHT0..GL_LIGHT7 (0x4000..0x4007)
    if (cap >= 0x4000 && cap <= 0x4007)
    {
        gLights[cap - 0x4000].enabled = true;
        return;
    }

    switch (cap)
    {
        case 0x0B50: gLightingEnabled      = true;  break; // GL_LIGHTING
        case 0x0B57: gColorMaterialEnabled = true;  break; // GL_COLOR_MATERIAL
        case 0x0BA1: gNormalizeEnabled     = true;  break; // GL_NORMALIZE
        case 0x0BC0: gAlphaTest.enabled    = true;  break; // GL_ALPHA_TEST
        case 0x0B60: gFog.enabled          = true;  break; // GL_FOG
        case 0x0DE1: gTexture2DEnabled     = true;  break; // GL_TEXTURE_2D – track manually

        // Pass through everything GLES supports natively
        default:
            glEnable(cap);
            break;
    }
}

void bridge_Disable(GLenum cap)
{
    // GL_LIGHT0..GL_LIGHT7
    if (cap >= 0x4000 && cap <= 0x4007)
    {
        gLights[cap - 0x4000].enabled = false;
        return;
    }

    switch (cap)
    {
        case 0x0B50: gLightingEnabled      = false; break;
        case 0x0B57: gColorMaterialEnabled = false; break;
        case 0x0BA1: gNormalizeEnabled     = false; break;
        case 0x0BC0: gAlphaTest.enabled    = false; break;
        case 0x0B60: gFog.enabled          = false; break;
        case 0x0DE1: gTexture2DEnabled     = false; break; // GL_TEXTURE_2D

        default:
            glDisable(cap);
            break;
    }
}

bool bridge_IsEnabled(GLenum cap)
{
    if (cap >= 0x4000 && cap <= 0x4007)
        return gLights[cap - 0x4000].enabled;

    switch (cap)
    {
        case 0x0B50: return gLightingEnabled;
        case 0x0B57: return gColorMaterialEnabled;
        case 0x0BC0: return gAlphaTest.enabled;
        case 0x0B60: return gFog.enabled;
        case 0x0DE1: return gTexture2DEnabled;
        default:     return (bool)glIsEnabled(cap);
    }
}

// -------------------------------------------------------------------------
// Client state
// -------------------------------------------------------------------------

void bridge_EnableClientState(GLenum array)
{
    switch (array)
    {
        case 0x8074: gVertexArray.enabled   = true; break;
        case 0x8075: gNormalArray.enabled   = true; break;
        case 0x8076: gColorArray.enabled    = true; break;
        case 0x8078: gTexCoordArray.enabled = true; break;
        default: break;
    }
}

void bridge_DisableClientState(GLenum array)
{
    switch (array)
    {
        case 0x8074: gVertexArray.enabled   = false; break;
        case 0x8075: gNormalArray.enabled   = false; break;
        case 0x8076: gColorArray.enabled    = false; break;
        case 0x8078: gTexCoordArray.enabled = false; break;
        default: break;
    }
}

void bridge_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    gVertexArray.size    = size;
    gVertexArray.type    = type;
    gVertexArray.stride  = stride;
    gVertexArray.pointer = pointer;
}

void bridge_NormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    gNormalArray.size    = 3;
    gNormalArray.type    = type;
    gNormalArray.stride  = stride;
    gNormalArray.pointer = pointer;
}

void bridge_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    gTexCoordArray.size    = size;
    gTexCoordArray.type    = type;
    gTexCoordArray.stride  = stride;
    gTexCoordArray.pointer = pointer;
}

void bridge_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    gColorArray.size    = size;
    gColorArray.type    = type;
    gColorArray.stride  = stride;
    gColorArray.pointer = pointer;
}

// -------------------------------------------------------------------------
// Current color
// -------------------------------------------------------------------------

void bridge_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    gCurrentColor[0] = r;
    gCurrentColor[1] = g;
    gCurrentColor[2] = b;
    gCurrentColor[3] = a;
}

void bridge_Color4fv(const GLfloat *v)
{
    bridge_Color4f(v[0], v[1], v[2], v[3]);
}

// -------------------------------------------------------------------------
// Flush state to shader uniforms
// -------------------------------------------------------------------------

void bridge_FlushState(void)
{
    if (!gShaderProgram) return;

    glUseProgram(gShaderProgram);

    // Matrices
    glUniformMatrix4fv(gUniMVMatrix,   1, GL_FALSE, MatStack_Top(&gMatMV)->m);
    glUniformMatrix4fv(gUniProjMatrix, 1, GL_FALSE, MatStack_Top(&gMatProj)->m);
    glUniformMatrix4fv(gUniTexMatrix,  1, GL_FALSE, MatStack_Top(&gMatTex)->m);

    float nm[9];
    Mat4_NormalMatrix(MatStack_Top(&gMatMV), nm);
    glUniformMatrix3fv(gUniNormalMatrix, 1, GL_FALSE, nm);

    // Lighting
    glUniform1i(gUniLightingEnabled, (GLint)gLightingEnabled);
    glUniform1i(gUniColorMaterial,   (GLint)gColorMaterialEnabled);
    glUniform4fv(gUniGlobalAmbient, 1, gGlobalAmbient);
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        glUniform1i(gUniLightEnabled[i], (GLint)gLights[i].enabled);
        glUniform4fv(gUniLightPos[i],     1, gLights[i].position);
        glUniform4fv(gUniLightAmbient[i], 1, gLights[i].ambient);
        glUniform4fv(gUniLightDiffuse[i], 1, gLights[i].diffuse);
    }

    // Fog
    glUniform1i(gUniFogEnabled, (GLint)gFog.enabled);
    if (gFog.enabled)
    {
        glUniform4fv(gUniFogColor, 1, gFog.color);
        glUniform1f(gUniFogStart,  gFog.start);
        glUniform1f(gUniFogEnd,    gFog.end);
    }

    // Alpha test
    glUniform1i(gUniAlphaTestEnabled, (GLint)gAlphaTest.enabled);
    if (gAlphaTest.enabled)
    {
        glUniform1f(gUniAlphaRef,  gAlphaTest.ref);
        glUniform1i(gUniAlphaFunc, (GLint)gAlphaTest.func);
    }

    // Texture
    glUniform1i(gUniTextureEnabled, (GLint)gTexture2DEnabled);

    // Current color
    glUniform4fv(gUniCurrentColor, 1, gCurrentColor);
}

// -------------------------------------------------------------------------
// Draw helpers: compute element size from GL type
// -------------------------------------------------------------------------

static int GLTypeSize(GLenum type)
{
    switch (type)
    {
        case GL_FLOAT:          return 4;
        case GL_UNSIGNED_SHORT: return 2;
        case GL_UNSIGNED_INT:   return 4;
        case GL_UNSIGNED_BYTE:  return 1;
        case GL_SHORT:          return 2;
        case GL_INT:            return 4;
        default:                return 4;
    }
}

// Build an interleaved vertex buffer from client arrays and upload it.
static int UploadClientArrays(int numVerts)
{
    // Layout: float3 pos | float3 normal | float2 tc | float4 color  = 12 floats = 48 bytes
    const int kStride = (3 + 3 + 2 + 4) * 4;

    static float *buf = NULL;
    static int    bufCapacity = 0;
    int needed = numVerts * kStride;
    if (needed > bufCapacity)
    {
        free(buf);
        buf = (float *)malloc((size_t)needed);
        bufCapacity = needed;
    }
    if (!buf) return 0;

    const uint8_t *vp = (const uint8_t *)gVertexArray.pointer;
    const uint8_t *np = (const uint8_t *)gNormalArray.pointer;
    const uint8_t *tp = (const uint8_t *)gTexCoordArray.pointer;
    const uint8_t *cp = (const uint8_t *)gColorArray.pointer;

    int vs = gVertexArray.stride   ? gVertexArray.stride   : gVertexArray.size   * GLTypeSize(gVertexArray.type);
    int ns = gNormalArray.stride   ? gNormalArray.stride   : 3 * GLTypeSize(gNormalArray.type);
    int ts = gTexCoordArray.stride ? gTexCoordArray.stride : gTexCoordArray.size * GLTypeSize(gTexCoordArray.type);
    int cs = gColorArray.stride    ? gColorArray.stride    : gColorArray.size    * GLTypeSize(gColorArray.type);

    for (int i = 0; i < numVerts; i++)
    {
        float *out = buf + i * (kStride / 4);

        // Position (always float3)
        if (vp)
        {
            const float *v = (const float *)(vp + i * vs);
            out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
        }
        else { out[0] = out[1] = out[2] = 0; }

        // Normal (float3)
        if (np && gNormalArray.enabled)
        {
            const float *n = (const float *)(np + i * ns);
            out[3] = n[0]; out[4] = n[1]; out[5] = n[2];
        }
        else { out[3] = 0; out[4] = 0; out[5] = 1; }

        // TexCoord (float2)
        if (tp && gTexCoordArray.enabled)
        {
            const float *t = (const float *)(tp + i * ts);
            out[6] = t[0]; out[7] = t[1];
        }
        else { out[6] = out[7] = 0; }

        // Color (float4)
        if (cp && gColorArray.enabled)
        {
            const float *c = (const float *)(cp + i * cs);
            out[8] = c[0]; out[9] = c[1]; out[10] = c[2]; out[11] = c[3];
        }
        else
        {
            out[8]  = gCurrentColor[0]; out[9]  = gCurrentColor[1];
            out[10] = gCurrentColor[2]; out[11] = gCurrentColor[3];
        }
    }

    // Upload to streaming VBO
    glBindBuffer(GL_ARRAY_BUFFER, gStreamVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(numVerts * kStride), buf, GL_STREAM_DRAW);

    return numVerts;
}

static void SetupVAOAttribs(void)
{
    const int kStride = (3 + 3 + 2 + 4) * 4;

    glBindVertexArray(gVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gStreamVBO);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void *)(0 * 4));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride, (void *)(3 * 4));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride, (void *)(6 * 4));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, kStride, (void *)(8 * 4));
    glEnableVertexAttribArray(3);
}

// We need to know numVerts to interleave data; compute from max index + 1.
static int MaxIndex(const void *indices, GLsizei count, GLenum type)
{
    int maxIdx = 0;
    if (type == GL_UNSIGNED_SHORT)
    {
        const uint16_t *idx = (const uint16_t *)indices;
        for (int i = 0; i < count; i++)
            if ((int)idx[i] > maxIdx) maxIdx = (int)idx[i];
    }
    else if (type == GL_UNSIGNED_INT)
    {
        const uint32_t *idx = (const uint32_t *)indices;
        for (int i = 0; i < count; i++)
            if ((int)idx[i] > maxIdx) maxIdx = (int)idx[i];
    }
    else if (type == GL_UNSIGNED_BYTE)
    {
        const uint8_t *idx = (const uint8_t *)indices;
        for (int i = 0; i < count; i++)
            if ((int)idx[i] > maxIdx) maxIdx = (int)idx[i];
    }
    return maxIdx + 1;
}

void bridge_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (!gShaderProgram) return;

    bridge_FlushState();

    int numVerts = MaxIndex(indices, count, type);
    UploadClientArrays(numVerts);
    SetupVAOAttribs();

    // Upload indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gStreamIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(count * GLTypeSize(type)), indices, GL_STREAM_DRAW);

    glUniform1i(gUniHasVertexColors, (GLint)(gColorArray.enabled && gColorArray.pointer != NULL));

    glDrawElements(mode, count, type, 0);
    glBindVertexArray(0);
}

void bridge_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (!gShaderProgram) return;

    bridge_FlushState();

    const uint8_t *savedVP = (const uint8_t *)gVertexArray.pointer;
    const uint8_t *savedNP = (const uint8_t *)gNormalArray.pointer;
    const uint8_t *savedTP = (const uint8_t *)gTexCoordArray.pointer;
    const uint8_t *savedCP = (const uint8_t *)gColorArray.pointer;

    if (first > 0)
    {
        int vs = gVertexArray.stride   ? gVertexArray.stride   : gVertexArray.size   * GLTypeSize(gVertexArray.type);
        int ns = gNormalArray.stride   ? gNormalArray.stride   : 3 * GLTypeSize(gNormalArray.type);
        int ts = gTexCoordArray.stride ? gTexCoordArray.stride : gTexCoordArray.size * GLTypeSize(gTexCoordArray.type);
        int cs = gColorArray.stride    ? gColorArray.stride    : gColorArray.size    * GLTypeSize(gColorArray.type);
        if (savedVP) gVertexArray.pointer   = savedVP   + (size_t)first * (size_t)vs;
        if (savedNP) gNormalArray.pointer   = savedNP   + (size_t)first * (size_t)ns;
        if (savedTP) gTexCoordArray.pointer = savedTP   + (size_t)first * (size_t)ts;
        if (savedCP) gColorArray.pointer    = savedCP   + (size_t)first * (size_t)cs;
    }

    UploadClientArrays(count);
    SetupVAOAttribs();

    glUniform1i(gUniHasVertexColors, (GLint)(gColorArray.enabled && gColorArray.pointer != NULL));
    glDrawArrays(mode, 0, count);
    glBindVertexArray(0);

    gVertexArray.pointer   = savedVP;
    gNormalArray.pointer   = savedNP;
    gTexCoordArray.pointer = savedTP;
    gColorArray.pointer    = savedCP;
}

// -------------------------------------------------------------------------
// Immediate mode (glBegin/glEnd)
// -------------------------------------------------------------------------

void bridge_Begin(GLenum mode)
{
    gImmMode      = mode;
    gImmVertCount = 0;
}

void bridge_End(void)
{
    if (gImmVertCount == 0 || !gShaderProgram) return;

    bridge_FlushState();

    const int kStride = (3 + 3 + 2 + 4) * 4;
    static float *buf = NULL;
    static int    bufCap = 0;
    int needed = gImmVertCount * kStride;
    if (needed > bufCap)
    {
        free(buf);
        buf    = (float *)malloc((size_t)needed);
        bufCap = needed;
    }
    if (!buf) { gImmVertCount = 0; return; }

    for (int i = 0; i < gImmVertCount; i++)
    {
        float *out = buf + i * (kStride / 4);
        out[0]  = gImmVerts[i].x;  out[1]  = gImmVerts[i].y;  out[2]  = gImmVerts[i].z;
        out[3]  = gImmVerts[i].nx; out[4]  = gImmVerts[i].ny; out[5]  = gImmVerts[i].nz;
        out[6]  = gImmVerts[i].u;  out[7]  = gImmVerts[i].v;
        out[8]  = gImmVerts[i].r;  out[9]  = gImmVerts[i].g;
        out[10] = gImmVerts[i].b;  out[11] = gImmVerts[i].a;
    }

    glBindBuffer(GL_ARRAY_BUFFER, gStreamVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(gImmVertCount * kStride), buf, GL_STREAM_DRAW);
    SetupVAOAttribs();

    glUniform1i(gUniHasVertexColors, GL_TRUE);
    glDrawArrays(gImmMode, 0, gImmVertCount);
    glBindVertexArray(0);

    gImmVertCount = 0;
}

void bridge_Vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (gImmVertCount >= IMM_MAX_VERTS) return;
    ImmVertex *v = &gImmVerts[gImmVertCount++];
    v->x  = x; v->y = y; v->z = z;
    v->nx = gImmCurrentNormal[0];
    v->ny = gImmCurrentNormal[1];
    v->nz = gImmCurrentNormal[2];
    v->u  = gImmCurrentTexCoord[0];
    v->v  = gImmCurrentTexCoord[1];
    v->r  = gCurrentColor[0]; v->g = gCurrentColor[1];
    v->b  = gCurrentColor[2]; v->a = gCurrentColor[3];
}

void bridge_Vertex2f(GLfloat x, GLfloat y)
{
    bridge_Vertex3f(x, y, 0.0f);
}

void bridge_Normal3f(GLfloat x, GLfloat y, GLfloat z)
{
    gImmCurrentNormal[0] = x;
    gImmCurrentNormal[1] = y;
    gImmCurrentNormal[2] = z;
}

void bridge_TexCoord2f(GLfloat s, GLfloat t)
{
    gImmCurrentTexCoord[0] = s;
    gImmCurrentTexCoord[1] = t;
}

// -------------------------------------------------------------------------
// Hint (no-op)
// -------------------------------------------------------------------------

void bridge_Hint(GLenum target, GLenum mode)
{
    (void)target; (void)mode;
}

// -------------------------------------------------------------------------
// Polygon mode (stub – no wireframe on GLES)
// -------------------------------------------------------------------------

void bridge_PolygonMode(GLenum face, GLenum mode)
{
    (void)face; (void)mode;
}

#endif // __ANDROID__
