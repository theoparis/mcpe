#define MCPE_GLES_INTERNAL 1
#include "gles.h"
#include "Tesselator.h"
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

static const float __glPi = 3.14159265358979323846f;
#if defined(SDL3)
static bool g_actualCallsEnabled = false;
#else
static bool g_actualCallsEnabled = true;
#endif
static float g_trackedColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static GLenum g_trackedBlendSrc = GL_SRC_ALPHA;
static GLenum g_trackedBlendDst = GL_ONE_MINUS_SRC_ALPHA;
static GLenum g_trackedMatrixMode = GL_MODELVIEW;
static bool g_trackedBlendEnabled = false;
static bool g_trackedAlphaTestEnabled = false;
static bool g_trackedTexture2dEnabled = false;
static bool g_trackedDepthTestEnabled = false;
static bool g_trackedCullFaceEnabled = false;
static bool g_trackedScissorTestEnabled = false;
static std::vector<std::array<GLfloat, 16>> g_modelViewStack;
static std::vector<std::array<GLfloat, 16>> g_projectionStack;

static void __gluMakeIdentityf(GLfloat m[16]);
void MultiplyMatrices4by4OpenGL_FLOAT(
    float *result, float *matrix1, float *matrix2);
static void glesEnsureMatrixTracker();
static auto glesCurrentTrackedMatrix() -> std::array<GLfloat, 16> &;
static void glesMultiplyCurrentMatrix(const GLfloat *rhs);
static auto glesTrackedCapability(GLenum cap) -> bool *;

void glesSetActualCallsEnabled(bool enabled) { g_actualCallsEnabled = enabled; }

bool glesActualCallsEnabled() { return g_actualCallsEnabled; }

GLenum glesGetError() { return 0; }

void glesTrackColor4f(float r, float g, float b, float a) {
  g_trackedColor[0] = r;
  g_trackedColor[1] = g;
  g_trackedColor[2] = b;
  g_trackedColor[3] = a;
}

void glesGetTrackedColor4f(float &r, float &g, float &b, float &a) {
  r = g_trackedColor[0];
  g = g_trackedColor[1];
  b = g_trackedColor[2];
  a = g_trackedColor[3];
}

void glesTrackBlendFunc(GLenum src, GLenum dst) {
  g_trackedBlendSrc = src;
  g_trackedBlendDst = dst;
}

void glesGetTrackedBlendFunc(GLenum &src, GLenum &dst) {
  src = g_trackedBlendSrc;
  dst = g_trackedBlendDst;
}

static auto glesTrackedCapability(GLenum cap) -> bool * {
  switch (cap) {
  case GL_BLEND:
    return &g_trackedBlendEnabled;
  case GL_ALPHA_TEST:
    return &g_trackedAlphaTestEnabled;
  case GL_TEXTURE_2D:
    return &g_trackedTexture2dEnabled;
  case GL_DEPTH_TEST:
    return &g_trackedDepthTestEnabled;
  case GL_CULL_FACE:
    return &g_trackedCullFaceEnabled;
  case GL_SCISSOR_TEST:
    return &g_trackedScissorTestEnabled;
  default:
    return nullptr;
  }
}

bool glesIsTrackedEnabled(GLenum cap) {
  const bool *state = glesTrackedCapability(cap);
  return state ? *state : false;
}

void glesColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  glesTrackColor4f(r, g, b, a);
}

void glesBlendFunc(GLenum src, GLenum dst) {
  glesTrackBlendFunc(src, dst);
}

void glesAlphaFunc(GLenum, GLclampf) {}

void glesBindBuffer(GLenum, GLuint) {}

void glesBindTexture(GLenum, GLuint) {}

void glesBufferData(GLenum, GLsizeiptr, const GLvoid *, GLenum) {}

void glesClear(GLbitfield) {}

void glesClearColor(GLclampf, GLclampf, GLclampf, GLclampf) {}

void glesColorMask(GLboolean, GLboolean, GLboolean, GLboolean) {}

void glesColorPointer(GLint, GLenum, GLsizei, const GLvoid *) {}

void glesCullFace(GLenum) {}

void glesDeleteBuffers(GLsizei, const GLuint *) {}

void glesDeleteLists(GLuint, GLsizei) {}

void glesDeleteTextures(GLsizei, const GLuint *) {}

void glesDepthFunc(GLenum) {}

void glesDepthMask(GLboolean) {}

void glesDepthRange(GLclampd, GLclampd) {}

void glesDisable(GLenum cap) {
  if (bool *state = glesTrackedCapability(cap)) {
    *state = false;
  }
}

void glesDisableClientState(GLenum) {}

void glesDrawArrays(GLenum, GLint, GLsizei) {}

void glesEnable(GLenum cap) {
  if (bool *state = glesTrackedCapability(cap)) {
    *state = true;
  }
}

void glesEnableClientState(GLenum) {}

void glesEndList() {}

void glesFogf(GLenum, GLfloat) {}

void glesFogfv(GLenum, const GLfloat *) {}

void glesFogi(GLenum, GLint) {}

GLuint glesGenLists(GLsizei range) {
  static GLuint nextList = 1;
  const GLuint first = nextList;
  nextList += range > 0 ? (GLuint)range : 1U;
  return first;
}

void glesHint(GLenum, GLenum) {}

static void glesEnsureMatrixTracker() {
  if (!g_modelViewStack.empty() && !g_projectionStack.empty()) {
    return;
  }

  std::array<GLfloat, 16> identity{};
  __gluMakeIdentityf(identity.data());
  g_modelViewStack.push_back(identity);
  g_projectionStack.push_back(identity);
}

static auto glesCurrentTrackedMatrix() -> std::array<GLfloat, 16> & {
  glesEnsureMatrixTracker();
  if (g_trackedMatrixMode == GL_PROJECTION) {
    return g_projectionStack.back();
  }
  return g_modelViewStack.back();
}

static void glesMultiplyCurrentMatrix(const GLfloat *rhs) {
  GLfloat result[16];
  std::array<GLfloat, 16> &current = glesCurrentTrackedMatrix();
  MultiplyMatrices4by4OpenGL_FLOAT(result, current.data(),
      const_cast<GLfloat *>(rhs));
  std::memcpy(current.data(), result, sizeof(result));
}

void glesMatrixMode(GLenum mode) {
  if (Options::debugGl) {
    LOGI("glMatrixMode @ %s:%d : %d\n", __FILE__, __LINE__, mode);
  }
  g_trackedMatrixMode = mode;
  GLERR(26);
}

void glesTranslatef(GLfloat x, GLfloat y, GLfloat z) {
  if (Options::debugGl) {
    LOGI("glTrans @ %s:%d: %f,%f,%f\n", __FILE__, __LINE__, x, y, z);
  }
  GLfloat matrix[16];
  __gluMakeIdentityf(matrix);
  matrix[12] = x;
  matrix[13] = y;
  matrix[14] = z;
  glesMultiplyCurrentMatrix(matrix);
  GLERR(0);
}

void glesRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
  if (Options::debugGl) {
    LOGI("glRotat @ %s:%d: %f,%f,%f,%f\n", __FILE__, __LINE__, angle, x, y, z);
  }

  const GLfloat magnitude = std::sqrt(x * x + y * y + z * z);
  if (magnitude != 0.0f) {
    x /= magnitude;
    y /= magnitude;
    z /= magnitude;
  }

  const GLfloat radians = angle * (__glPi / 180.0f);
  const GLfloat c = std::cos(radians);
  const GLfloat s = std::sin(radians);
  const GLfloat t = 1.0f - c;

  const GLfloat matrix[16] = {
      t * x * x + c,
      t * x * y + s * z,
      t * x * z - s * y,
      0.0f,
      t * x * y - s * z,
      t * y * y + c,
      t * y * z + s * x,
      0.0f,
      t * x * z + s * y,
      t * y * z - s * x,
      t * z * z + c,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      1.0f,
  };
  glesMultiplyCurrentMatrix(matrix);
  GLERR(1);
}

void glesScalef(GLfloat x, GLfloat y, GLfloat z) {
  if (Options::debugGl) {
    LOGI("glScale @ %s:%d: %f,%f,%f\n", __FILE__, __LINE__, x, y, z);
  }
  GLfloat matrix[16];
  __gluMakeIdentityf(matrix);
  matrix[0] = x;
  matrix[5] = y;
  matrix[10] = z;
  glesMultiplyCurrentMatrix(matrix);
  GLERR(2);
}

void glesPushMatrix() {
  if (Options::debugGl) {
    LOGI("glPushM @ %s:%d\n", __FILE__, __LINE__);
  }
  glesEnsureMatrixTracker();
  if (g_trackedMatrixMode == GL_PROJECTION) {
    g_projectionStack.push_back(g_projectionStack.back());
  } else {
    g_modelViewStack.push_back(g_modelViewStack.back());
  }
  GLERR(3);
}

void glesPopMatrix() {
  if (Options::debugGl) {
    LOGI("glPopM  @ %s:%d\n", __FILE__, __LINE__);
  }
  glesEnsureMatrixTracker();
  if (g_trackedMatrixMode == GL_PROJECTION) {
    if (g_projectionStack.size() > 1) {
      g_projectionStack.pop_back();
    }
  } else {
    if (g_modelViewStack.size() > 1) {
      g_modelViewStack.pop_back();
    }
  }
  GLERR(4);
}

void glesLoadIdentity() {
  if (Options::debugGl) {
    LOGI("glLoadI @ %s:%d\n", __FILE__, __LINE__);
  }
  __gluMakeIdentityf(glesCurrentTrackedMatrix().data());
  GLERR(5);
}

void glesGetTrackedMatrix(GLenum matrixName, GLfloat *out) {
  glesEnsureMatrixTracker();
  const std::array<GLfloat, 16> *source = nullptr;
  if (matrixName == GL_PROJECTION_MATRIX) {
    source = &g_projectionStack.back();
  } else if (matrixName == GL_MODELVIEW_MATRIX) {
    source = &g_modelViewStack.back();
  }

  if (source != nullptr && out != nullptr) {
    std::memcpy(out, source->data(), sizeof(GLfloat) * 16);
  }
}

void glesGetFloatv(GLenum pname, GLfloat *params) {
  if (params == nullptr) {
    return;
  }

  if (pname == GL_PROJECTION_MATRIX || pname == GL_MODELVIEW_MATRIX) {
    glesGetTrackedMatrix(pname, params);
    return;
  }
}

void glesLineWidth(GLfloat) {}

void glesMultMatrixf(const GLfloat *m) {
  if (m != nullptr) {
    glesMultiplyCurrentMatrix(m);
  }
}

void glesNewList(GLuint, GLenum) {}

void glesNormal3f(GLfloat, GLfloat, GLfloat) {}

void glesNormalPointer(GLenum, GLsizei, const GLvoid *) {}

void glesOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
    GLdouble zNear, GLdouble zFar) {
  const GLdouble width = right - left;
  const GLdouble height = top - bottom;
  const GLdouble depth = zFar - zNear;
  if (width == 0.0 || height == 0.0 || depth == 0.0) {
    return;
  }

  GLfloat matrix[16];
  __gluMakeIdentityf(matrix);
  matrix[0] = (GLfloat)(2.0 / width);
  matrix[5] = (GLfloat)(2.0 / height);
  matrix[10] = (GLfloat)(-2.0 / depth);
  matrix[12] = (GLfloat)(-(right + left) / width);
  matrix[13] = (GLfloat)(-(top + bottom) / height);
  matrix[14] = (GLfloat)(-(zFar + zNear) / depth);
  glesMultiplyCurrentMatrix(matrix);
}

void glesCallList(GLuint) {}

void glesCallLists(GLsizei, GLenum, const GLvoid *) {}

void glesPolygonMode(GLenum, GLenum) {}

void glesPolygonOffset(GLfloat, GLfloat) {}

void glesScissor(GLint, GLint, GLsizei, GLsizei) {}

void glesShadeModel(GLenum) {}

void glesTexCoordPointer(GLint, GLenum, GLsizei, const GLvoid *) {}

void glesTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
    GLenum, const GLvoid *) {}

void glesTexParameteri(GLenum, GLenum, GLint) {}

void glesTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
    GLenum, const GLvoid *) {}

void glesVertexPointer(GLint, GLenum, GLsizei, const GLvoid *) {}

void glesViewport(GLint, GLint, GLsizei, GLsizei) {}

void gluPerspective(GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar) {
  GLfloat m[4][4];
  GLfloat sine, cotangent, deltaZ;
  GLfloat radians = (GLfloat)(fovy / 2.0f * __glPi / 180.0f);

  deltaZ = zFar - zNear;
  sine = (GLfloat)sin(radians);
  if ((deltaZ == 0.0f) || (sine == 0.0f) || (aspect == 0.0f)) {
    return;
  }
  cotangent = (GLfloat)(cos(radians) / sine);

  __gluMakeIdentityf(&m[0][0]);
  m[0][0] = cotangent / aspect;
  m[1][1] = cotangent;
  m[2][2] = -(zFar + zNear) / deltaZ;
  m[2][3] = -1.0f;
  m[3][2] = -2.0f * zNear * zFar / deltaZ;
  m[3][3] = 0;
  glesMultiplyCurrentMatrix(&m[0][0]);
}

void __gluMakeIdentityf(GLfloat m[16]) {
  m[0] = 1;
  m[4] = 0;
  m[8] = 0;
  m[12] = 0;
  m[1] = 0;
  m[5] = 1;
  m[9] = 0;
  m[13] = 0;
  m[2] = 0;
  m[6] = 0;
  m[10] = 1;
  m[14] = 0;
  m[3] = 0;
  m[7] = 0;
  m[11] = 0;
  m[15] = 1;
}

void glInit() {}

void anGenBuffers(GLsizei n, GLuint *buffers) {
  static GLuint k = 1;
  for (int i = 0; i < n; ++i)
    buffers[i] = ++k;
}

#ifdef USE_VBO
void drawArrayVT(
    int bufferId, int vertices, int vertexSize, unsigned int mode) {
  if (Tesselator::instance.drawBuffer((GLuint)bufferId)) {
    return;
  }

  glBindBuffer2(GL_ARRAY_BUFFER, bufferId);
  glTexCoordPointer2(2, GL_FLOAT, vertexSize, (GLvoid *)(3 * 4));
  glEnableClientState2(GL_TEXTURE_COORD_ARRAY);
  glVertexPointer2(3, GL_FLOAT, vertexSize, 0);
  glEnableClientState2(GL_VERTEX_ARRAY);
  glDrawArrays2(mode, 0, vertices);
  glDisableClientState2(GL_VERTEX_ARRAY);
  glDisableClientState2(GL_TEXTURE_COORD_ARRAY);
}

#ifndef drawArrayVT_NoState
void drawArrayVT_NoState(
    int bufferId, int vertices, int vertexSize /* = 24 */) {
  // if (Options::debugGl) LOGI("drawArray\n");
  if (Tesselator::instance.drawBuffer((GLuint)bufferId)) {
    return;
  }

  glBindBuffer2(GL_ARRAY_BUFFER, bufferId);
  glTexCoordPointer2(2, GL_FLOAT, vertexSize, (GLvoid *)(3 * 4));
  // glEnableClientState2(GL_TEXTURE_COORD_ARRAY);
  glVertexPointer2(3, GL_FLOAT, vertexSize, 0);
  // glEnableClientState2(GL_VERTEX_ARRAY);
  glDrawArrays2(GL_TRIANGLES, 0, vertices);
  // glDisableClientState2(GL_VERTEX_ARRAY);
  // glDisableClientState2(GL_TEXTURE_COORD_ARRAY);
}
#endif

void drawArrayVTC(int bufferId, int vertices, int vertexSize /* = 24 */) {
  // if (Options::debugGl) LOGI("drawArray\n");
  // LOGI("draw-vtc: %d, %d, %d\n", bufferId, vertices, vertexSize);
  if (Tesselator::instance.drawBuffer((GLuint)bufferId)) {
    return;
  }

  glEnableClientState2(GL_VERTEX_ARRAY);
  glEnableClientState2(GL_TEXTURE_COORD_ARRAY);
  glEnableClientState2(GL_COLOR_ARRAY);

  glBindBuffer2(GL_ARRAY_BUFFER, bufferId);

  glVertexPointer2(3, GL_FLOAT, vertexSize, 0);
  glTexCoordPointer2(2, GL_FLOAT, vertexSize, (GLvoid *)(3 * 4));
  glColorPointer2(4, GL_UNSIGNED_BYTE, vertexSize, (GLvoid *)(5 * 4));

  glDrawArrays2(GL_TRIANGLES, 0, vertices);

  glDisableClientState2(GL_VERTEX_ARRAY);
  glDisableClientState2(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState2(GL_COLOR_ARRAY);
}

#ifndef drawArrayVTC_NoState
void drawArrayVTC_NoState(
    int bufferId, int vertices, int vertexSize /* = 24 */) {
  if (Tesselator::instance.drawBuffer((GLuint)bufferId)) {
    return;
  }

  glBindBuffer2(GL_ARRAY_BUFFER, bufferId);

  glVertexPointer2(3, GL_FLOAT, vertexSize, 0);
  glTexCoordPointer2(2, GL_FLOAT, vertexSize, (GLvoid *)(3 * 4));
  glColorPointer2(4, GL_UNSIGNED_BYTE, vertexSize, (GLvoid *)(5 * 4));

  glDrawArrays2(GL_TRIANGLES, 0, vertices);
}
#endif

#endif

//
// Code borrowed from OpenGL.org
// http://www.opengl.org/wiki/GluProject_and_gluUnProject_code
// The gluUnProject code in Android seems to be broken
//

void MultiplyMatrices4by4OpenGL_FLOAT(
    float *result, float *matrix1, float *matrix2) {
  result[0] = matrix1[0] * matrix2[0] + matrix1[4] * matrix2[1] +
      matrix1[8] * matrix2[2] + matrix1[12] * matrix2[3];
  result[4] = matrix1[0] * matrix2[4] + matrix1[4] * matrix2[5] +
      matrix1[8] * matrix2[6] + matrix1[12] * matrix2[7];
  result[8] = matrix1[0] * matrix2[8] + matrix1[4] * matrix2[9] +
      matrix1[8] * matrix2[10] + matrix1[12] * matrix2[11];
  result[12] = matrix1[0] * matrix2[12] + matrix1[4] * matrix2[13] +
      matrix1[8] * matrix2[14] + matrix1[12] * matrix2[15];
  result[1] = matrix1[1] * matrix2[0] + matrix1[5] * matrix2[1] +
      matrix1[9] * matrix2[2] + matrix1[13] * matrix2[3];
  result[5] = matrix1[1] * matrix2[4] + matrix1[5] * matrix2[5] +
      matrix1[9] * matrix2[6] + matrix1[13] * matrix2[7];
  result[9] = matrix1[1] * matrix2[8] + matrix1[5] * matrix2[9] +
      matrix1[9] * matrix2[10] + matrix1[13] * matrix2[11];
  result[13] = matrix1[1] * matrix2[12] + matrix1[5] * matrix2[13] +
      matrix1[9] * matrix2[14] + matrix1[13] * matrix2[15];
  result[2] = matrix1[2] * matrix2[0] + matrix1[6] * matrix2[1] +
      matrix1[10] * matrix2[2] + matrix1[14] * matrix2[3];
  result[6] = matrix1[2] * matrix2[4] + matrix1[6] * matrix2[5] +
      matrix1[10] * matrix2[6] + matrix1[14] * matrix2[7];
  result[10] = matrix1[2] * matrix2[8] + matrix1[6] * matrix2[9] +
      matrix1[10] * matrix2[10] + matrix1[14] * matrix2[11];
  result[14] = matrix1[2] * matrix2[12] + matrix1[6] * matrix2[13] +
      matrix1[10] * matrix2[14] + matrix1[14] * matrix2[15];
  result[3] = matrix1[3] * matrix2[0] + matrix1[7] * matrix2[1] +
      matrix1[11] * matrix2[2] + matrix1[15] * matrix2[3];
  result[7] = matrix1[3] * matrix2[4] + matrix1[7] * matrix2[5] +
      matrix1[11] * matrix2[6] + matrix1[15] * matrix2[7];
  result[11] = matrix1[3] * matrix2[8] + matrix1[7] * matrix2[9] +
      matrix1[11] * matrix2[10] + matrix1[15] * matrix2[11];
  result[15] = matrix1[3] * matrix2[12] + matrix1[7] * matrix2[13] +
      matrix1[11] * matrix2[14] + matrix1[15] * matrix2[15];
}

void MultiplyMatrixByVector4by4OpenGL_FLOAT(
    float *resultvector, const float *matrix, const float *pvector) {
  resultvector[0] = matrix[0] * pvector[0] + matrix[4] * pvector[1] +
      matrix[8] * pvector[2] + matrix[12] * pvector[3];
  resultvector[1] = matrix[1] * pvector[0] + matrix[5] * pvector[1] +
      matrix[9] * pvector[2] + matrix[13] * pvector[3];
  resultvector[2] = matrix[2] * pvector[0] + matrix[6] * pvector[1] +
      matrix[10] * pvector[2] + matrix[14] * pvector[3];
  resultvector[3] = matrix[3] * pvector[0] + matrix[7] * pvector[1] +
      matrix[11] * pvector[2] + matrix[15] * pvector[3];
}

#define SWAP_ROWS_DOUBLE(a, b)                                                 \
  {                                                                            \
    double *_tmp = a;                                                          \
    (a) = (b);                                                                 \
    (b) = _tmp;                                                                \
  }
#define SWAP_ROWS_FLOAT(a, b)                                                  \
  {                                                                            \
    float *_tmp = a;                                                           \
    (a) = (b);                                                                 \
    (b) = _tmp;                                                                \
  }
#define MAT(m, r, c) (m)[(c) * 4 + (r)]

// This code comes directly from GLU except that it is for float
int glhInvertMatrixf2(float *m, float *out) {
  float wtmp[4][8];
  float m0, m1, m2, m3, s;
  float *r0, *r1, *r2, *r3;
  r0 = wtmp[0], r1 = wtmp[1], r2 = wtmp[2], r3 = wtmp[3];
  r0[0] = MAT(m, 0, 0), r0[1] = MAT(m, 0, 1), r0[2] = MAT(m, 0, 2),
  r0[3] = MAT(m, 0, 3), r0[4] = 1.0f, r0[5] = r0[6] = r0[7] = 0.0f,
  r1[0] = MAT(m, 1, 0), r1[1] = MAT(m, 1, 1), r1[2] = MAT(m, 1, 2),
  r1[3] = MAT(m, 1, 3), r1[5] = 1.0f, r1[4] = r1[6] = r1[7] = 0.0f,
  r2[0] = MAT(m, 2, 0), r2[1] = MAT(m, 2, 1), r2[2] = MAT(m, 2, 2),
  r2[3] = MAT(m, 2, 3), r2[6] = 1.0f, r2[4] = r2[5] = r2[7] = 0.0f,
  r3[0] = MAT(m, 3, 0), r3[1] = MAT(m, 3, 1), r3[2] = MAT(m, 3, 2),
  r3[3] = MAT(m, 3, 3), r3[7] = 1.0f, r3[4] = r3[5] = r3[6] = 0.0f;
  /* choose pivot - or die */
  if (fabsf(r3[0]) > fabsf(r2[0]))
    SWAP_ROWS_FLOAT(r3, r2);
  if (fabsf(r2[0]) > fabsf(r1[0]))
    SWAP_ROWS_FLOAT(r2, r1);
  if (fabsf(r1[0]) > fabsf(r0[0]))
    SWAP_ROWS_FLOAT(r1, r0);
  if (0.0f == r0[0])
    return 0;
  /* eliminate first variable     */
  m1 = r1[0] / r0[0];
  m2 = r2[0] / r0[0];
  m3 = r3[0] / r0[0];
  s = r0[1];
  r1[1] -= m1 * s;
  r2[1] -= m2 * s;
  r3[1] -= m3 * s;
  s = r0[2];
  r1[2] -= m1 * s;
  r2[2] -= m2 * s;
  r3[2] -= m3 * s;
  s = r0[3];
  r1[3] -= m1 * s;
  r2[3] -= m2 * s;
  r3[3] -= m3 * s;
  s = r0[4];
  if (s != 0.0f) {
    r1[4] -= m1 * s;
    r2[4] -= m2 * s;
    r3[4] -= m3 * s;
  }
  s = r0[5];
  if (s != 0.0f) {
    r1[5] -= m1 * s;
    r2[5] -= m2 * s;
    r3[5] -= m3 * s;
  }
  s = r0[6];
  if (s != 0.0f) {
    r1[6] -= m1 * s;
    r2[6] -= m2 * s;
    r3[6] -= m3 * s;
  }
  s = r0[7];
  if (s != 0.0f) {
    r1[7] -= m1 * s;
    r2[7] -= m2 * s;
    r3[7] -= m3 * s;
  }
  /* choose pivot - or die */
  if (fabsf(r3[1]) > fabsf(r2[1]))
    SWAP_ROWS_FLOAT(r3, r2);
  if (fabsf(r2[1]) > fabsf(r1[1]))
    SWAP_ROWS_FLOAT(r2, r1);
  if (0.0f == r1[1])
    return 0;
  /* eliminate second variable */
  m2 = r2[1] / r1[1];
  m3 = r3[1] / r1[1];
  r2[2] -= m2 * r1[2];
  r3[2] -= m3 * r1[2];
  r2[3] -= m2 * r1[3];
  r3[3] -= m3 * r1[3];
  s = r1[4];
  if (0.0f != s) {
    r2[4] -= m2 * s;
    r3[4] -= m3 * s;
  }
  s = r1[5];
  if (0.0f != s) {
    r2[5] -= m2 * s;
    r3[5] -= m3 * s;
  }
  s = r1[6];
  if (0.0f != s) {
    r2[6] -= m2 * s;
    r3[6] -= m3 * s;
  }
  s = r1[7];
  if (0.0f != s) {
    r2[7] -= m2 * s;
    r3[7] -= m3 * s;
  }
  /* choose pivot - or die */
  if (fabsf(r3[2]) > fabsf(r2[2]))
    SWAP_ROWS_FLOAT(r3, r2);
  if (0.0f == r2[2])
    return 0;
  /* eliminate third variable */
  m3 = r3[2] / r2[2];
  r3[3] -= m3 * r2[3], r3[4] -= m3 * r2[4], r3[5] -= m3 * r2[5],
      r3[6] -= m3 * r2[6], r3[7] -= m3 * r2[7];
  /* last check */
  if (0.0f == r3[3])
    return 0;
  s = 1.0f / r3[3]; /* now back substitute row 3 */
  r3[4] *= s;
  r3[5] *= s;
  r3[6] *= s;
  r3[7] *= s;
  m2 = r2[3]; /* now back substitute row 2 */
  s = 1.0f / r2[2];
  r2[4] = s * (r2[4] - r3[4] * m2), r2[5] = s * (r2[5] - r3[5] * m2),
  r2[6] = s * (r2[6] - r3[6] * m2), r2[7] = s * (r2[7] - r3[7] * m2);
  m1 = r1[3];
  r1[4] -= r3[4] * m1, r1[5] -= r3[5] * m1, r1[6] -= r3[6] * m1,
      r1[7] -= r3[7] * m1;
  m0 = r0[3];
  r0[4] -= r3[4] * m0, r0[5] -= r3[5] * m0, r0[6] -= r3[6] * m0,
      r0[7] -= r3[7] * m0;
  m1 = r1[2]; /* now back substitute row 1 */
  s = 1.0f / r1[1];
  r1[4] = s * (r1[4] - r2[4] * m1), r1[5] = s * (r1[5] - r2[5] * m1),
  r1[6] = s * (r1[6] - r2[6] * m1), r1[7] = s * (r1[7] - r2[7] * m1);
  m0 = r0[2];
  r0[4] -= r2[4] * m0, r0[5] -= r2[5] * m0, r0[6] -= r2[6] * m0,
      r0[7] -= r2[7] * m0;
  m0 = r0[1]; /* now back substitute row 0 */
  s = 1.0f / r0[0];
  r0[4] = s * (r0[4] - r1[4] * m0), r0[5] = s * (r0[5] - r1[5] * m0),
  r0[6] = s * (r0[6] - r1[6] * m0), r0[7] = s * (r0[7] - r1[7] * m0);
  MAT(out, 0, 0) = r0[4];
  MAT(out, 0, 1) = r0[5], MAT(out, 0, 2) = r0[6];
  MAT(out, 0, 3) = r0[7], MAT(out, 1, 0) = r1[4];
  MAT(out, 1, 1) = r1[5], MAT(out, 1, 2) = r1[6];
  MAT(out, 1, 3) = r1[7], MAT(out, 2, 0) = r2[4];
  MAT(out, 2, 1) = r2[5], MAT(out, 2, 2) = r2[6];
  MAT(out, 2, 3) = r2[7], MAT(out, 3, 0) = r3[4];
  MAT(out, 3, 1) = r3[5], MAT(out, 3, 2) = r3[6];
  MAT(out, 3, 3) = r3[7];
  return 1;
}

int glhUnProjectf(float winx, float winy, float winz, float *modelview,
    float *projection, int *viewport, float *objectCoordinate) {
  // Transformation matrices
  float m[16], A[16];
  float in[4], out[4];
  // Calculation for inverting a matrix, compute projection x modelview
  // and store in A[16]
  MultiplyMatrices4by4OpenGL_FLOAT(A, projection, modelview);
  // Now compute the inverse of matrix A
  if (glhInvertMatrixf2(A, m) == 0)
    return 0;
  // Transformation of normalized coordinates between -1 and 1
  in[0] = (winx - (float)viewport[0]) / (float)viewport[2] * 2.0f - 1.0f;
  in[1] = (winy - (float)viewport[1]) / (float)viewport[3] * 2.0f - 1.0f;
  in[2] = 2.0f * winz - 1.0f;
  in[3] = 1.0f;
  // Objects coordinates
  MultiplyMatrixByVector4by4OpenGL_FLOAT(out, m, in);
  if (out[3] == 0.0f)
    return 0;
  out[3] = 1.0f / out[3];
  objectCoordinate[0] = out[0] * out[3];
  objectCoordinate[1] = out[1] * out[3];
  objectCoordinate[2] = out[2] * out[3];
  return 1;
}
