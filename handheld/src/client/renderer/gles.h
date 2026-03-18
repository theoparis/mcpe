#ifndef NET_MINECRAFT_CLIENT_RENDERER__gles_H__
#define NET_MINECRAFT_CLIENT_RENDERER__gles_H__

#include "../../platform/log.h"
#include "../Options.h"
#include <cstddef>

#define USE_VBO
#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLbyte = signed char;
using GLint = int;
using GLsizei = int;
using GLuint = unsigned int;
using GLfloat = float;
using GLclampf = float;
using GLdouble = double;
using GLclampd = double;
using GLsizeiptr = std::ptrdiff_t;
using GLubyte = unsigned char;
using GLvoid = void;

#ifndef GL_FALSE
#define GL_FALSE 0
#endif
#ifndef GL_TRUE
#define GL_TRUE 1
#endif
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif
#ifndef GL_ZERO
#define GL_ZERO 0
#endif
#ifndef GL_ONE
#define GL_ONE 1
#endif
#ifndef GL_BYTE
#define GL_BYTE 0x1400
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif
#ifndef GL_UNSIGNED_SHORT_4_4_4_4
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#endif
#ifndef GL_UNSIGNED_SHORT_5_5_5_1
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#endif
#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif
#ifndef GL_FLOAT
#define GL_FLOAT 0x1406
#endif
#ifndef GL_DEPTH_BUFFER_BIT
#define GL_DEPTH_BUFFER_BIT 0x00000100
#endif
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif
#ifndef GL_LINES
#define GL_LINES 0x0001
#endif
#ifndef GL_LINE_STRIP
#define GL_LINE_STRIP 0x0003
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP 0x0005
#endif
#ifndef GL_TRIANGLE_FAN
#define GL_TRIANGLE_FAN 0x0006
#endif
#ifndef GL_LEQUAL
#define GL_LEQUAL 0x0203
#endif
#ifndef GL_EQUAL
#define GL_EQUAL 0x0202
#endif
#ifndef GL_GREATER
#define GL_GREATER 0x0204
#endif
#ifndef GL_SRC_COLOR
#define GL_SRC_COLOR 0x0300
#endif
#ifndef GL_ONE_MINUS_SRC_COLOR
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#endif
#ifndef GL_SRC_ALPHA
#define GL_SRC_ALPHA 0x0302
#endif
#ifndef GL_ONE_MINUS_SRC_ALPHA
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#endif
#ifndef GL_DST_ALPHA
#define GL_DST_ALPHA 0x0304
#endif
#ifndef GL_DST_COLOR
#define GL_DST_COLOR 0x0306
#endif
#ifndef GL_ONE_MINUS_DST_COLOR
#define GL_ONE_MINUS_DST_COLOR 0x0307
#endif
#ifndef GL_FRONT
#define GL_FRONT 0x0404
#endif
#ifndef GL_BACK
#define GL_BACK 0x0405
#endif
#ifndef GL_EXP
#define GL_EXP 0x0800
#endif
#ifndef GL_FOG
#define GL_FOG 0x0B60
#endif
#ifndef GL_FOG_DENSITY
#define GL_FOG_DENSITY 0x0B62
#endif
#ifndef GL_FOG_START
#define GL_FOG_START 0x0B63
#endif
#ifndef GL_FOG_END
#define GL_FOG_END 0x0B64
#endif
#ifndef GL_FOG_MODE
#define GL_FOG_MODE 0x0B65
#endif
#ifndef GL_FOG_COLOR
#define GL_FOG_COLOR 0x0B66
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING 0x0B50
#endif
#ifndef GL_COLOR_MATERIAL
#define GL_COLOR_MATERIAL 0x0B57
#endif
#ifndef GL_CULL_FACE
#define GL_CULL_FACE 0x0B44
#endif
#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST 0x0B71
#endif
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST 0x0BC0
#endif
#ifndef GL_BLEND
#define GL_BLEND 0x0BE2
#endif
#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif
#ifndef GL_PERSPECTIVE_CORRECTION_HINT
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#endif
#ifndef GL_FASTEST
#define GL_FASTEST 0x1101
#endif
#ifndef GL_MODELVIEW
#define GL_MODELVIEW 0x1700
#endif
#ifndef GL_PROJECTION
#define GL_PROJECTION 0x1701
#endif
#ifndef GL_TEXTURE
#define GL_TEXTURE 0x1702
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_SMOOTH
#define GL_SMOOTH 0x1D01
#endif
#ifndef GL_FLAT
#define GL_FLAT 0x1D00
#endif
#ifndef GL_RENDERER
#define GL_RENDERER 0x1F01
#endif
#ifndef GL_NEAREST
#define GL_NEAREST 0x2600
#endif
#ifndef GL_LINEAR
#define GL_LINEAR 0x2601
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER 0x2800
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER 0x2801
#endif
#ifndef GL_TEXTURE_WRAP_S
#define GL_TEXTURE_WRAP_S 0x2802
#endif
#ifndef GL_TEXTURE_WRAP_T
#define GL_TEXTURE_WRAP_T 0x2803
#endif
#ifndef GL_REPEAT
#define GL_REPEAT 0x2901
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MODELVIEW_MATRIX
#define GL_MODELVIEW_MATRIX 0x0BA6
#endif
#ifndef GL_PROJECTION_MATRIX
#define GL_PROJECTION_MATRIX 0x0BA7
#endif
#ifndef GL_VERTEX_ARRAY
#define GL_VERTEX_ARRAY 0x8074
#endif
#ifndef GL_NORMAL_ARRAY
#define GL_NORMAL_ARRAY 0x8075
#endif
#ifndef GL_COLOR_ARRAY
#define GL_COLOR_ARRAY 0x8076
#endif
#ifndef GL_TEXTURE_COORD_ARRAY
#define GL_TEXTURE_COORD_ARRAY 0x8078
#endif
#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL 0x8037
#endif
#ifndef GL_RESCALE_NORMAL
#define GL_RESCALE_NORMAL 0x803A
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_COMPILE
#define GL_COMPILE 0x1300
#endif
#ifndef GL_LINE
#define GL_LINE 0x1B01
#endif
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif
#ifndef GL_RGB
#define GL_RGB 0x1907
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif

// Uglyness to fix redeclaration issues
#ifdef WIN32
#include <WinSock2.h>
#include <Windows.h>
#endif

#ifndef glFogx
#define glFogx(a, b) glFogi(a, b)
#endif
#ifndef glOrthof
#define glOrthof(a, b, c, d, e, f)                                           \
  glOrtho((GLdouble)(a), (GLdouble)(b), (GLdouble)(c), (GLdouble)(d),        \
      (GLdouble)(e), (GLdouble)(f))
#endif
#ifndef glDepthRangef
#define glDepthRangef(n, f) glDepthRange((GLclampd)(n), (GLclampd)(f))
#endif

#define GLERRDEBUG 1
#if GLERRDEBUG
#define GLERR(x)                                                               \
  do {                                                                         \
    if (glesActualCallsEnabled()) {                                            \
      const int errCode = (int)glesGetError();                                 \
      if (errCode != 0)                                                        \
        LOGE("OpenGL ERROR @%d: #%d @ (%s : %d)\n", x, errCode, __FILE__,      \
            __LINE__);                                                         \
    }                                                                          \
  } while (0)
#else
#define GLERR(x) x
#endif

void anGenBuffers(GLsizei n, GLuint *buffer);
void glesSetActualCallsEnabled(bool enabled);
bool glesActualCallsEnabled();
GLenum glesGetError();
void glesTrackColor4f(float r, float g, float b, float a);
void glesGetTrackedColor4f(float &r, float &g, float &b, float &a);
void glesTrackBlendFunc(GLenum src, GLenum dst);
void glesGetTrackedBlendFunc(GLenum &src, GLenum &dst);
bool glesIsTrackedEnabled(GLenum cap);
void glesAlphaFunc(GLenum func, GLclampf ref);
void glesBindBuffer(GLenum target, GLuint buffer);
void glesBindTexture(GLenum target, GLuint texture);
void glesColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glesColorMask(
    GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void glesBufferData(
    GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
void glesBlendFunc(GLenum src, GLenum dst);
void glesClear(GLbitfield mask);
void glesClearColor(
    GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glesCullFace(GLenum mode);
void glesColorPointer(GLint size, GLenum type, GLsizei stride,
    const GLvoid *ptr);
void glesDeleteBuffers(GLsizei n, const GLuint *buffers);
void glesDeleteLists(GLuint list, GLsizei range);
void glesDeleteTextures(GLsizei n, const GLuint *textures);
void glesDepthFunc(GLenum func);
void glesDepthMask(GLboolean flag);
void glesDepthRange(GLclampd n, GLclampd f);
void glesDisable(GLenum cap);
void glesDisableClientState(GLenum array);
void glesDrawArrays(GLenum mode, GLint first, GLsizei count);
void glesEnable(GLenum cap);
void glesEnableClientState(GLenum array);
void glesEndList();
void glesFogf(GLenum pname, GLfloat param);
void glesFogfv(GLenum pname, const GLfloat *params);
void glesFogi(GLenum pname, GLint param);
GLuint glesGenLists(GLsizei range);
void glesHint(GLenum target, GLenum mode);
void glesMatrixMode(GLenum mode);
void glesMultMatrixf(const GLfloat *m);
void glesNewList(GLuint list, GLenum mode);
void glesNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
void glesNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr);
void glesOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
    GLdouble zNear, GLdouble zFar);
void glesCallList(GLuint list);
void glesCallLists(GLsizei n, GLenum type, const GLvoid *lists);
void glesPolygonMode(GLenum face, GLenum mode);
void glesPolygonOffset(GLfloat factor, GLfloat units);
void glesScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glesShadeModel(GLenum mode);
void glesTexCoordPointer(GLint size, GLenum type, GLsizei stride,
    const GLvoid *ptr);
void glesTexImage2D(GLenum target, GLint level, GLint internalFormat,
    GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type,
    const GLvoid *pixels);
void glesTexParameteri(GLenum target, GLenum pname, GLint param);
void glesTexSubImage2D(GLenum target, GLint level, GLint xoffset,
    GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type,
    const GLvoid *pixels);
void glesTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glesRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glesScalef(GLfloat x, GLfloat y, GLfloat z);
void glesPushMatrix();
void glesPopMatrix();
void glesLoadIdentity();
void glesGetTrackedMatrix(GLenum matrixName, GLfloat *out);
void glesGetFloatv(GLenum pname, GLfloat *params);
void glesVertexPointer(
    GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void glesViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glesLineWidth(GLfloat width);

#ifndef MCPE_GLES_INTERNAL
#define glAlphaFunc glesAlphaFunc
#define glBindBuffer glesBindBuffer
#define glBindTexture glesBindTexture
#define glBlendFunc glesBlendFunc
#define glBufferData glesBufferData
#define glCallList glesCallList
#define glCallLists glesCallLists
#define glClear glesClear
#define glClearColor glesClearColor
#define glColor4f glesColor4f
#define glColorMask glesColorMask
#define glColorPointer glesColorPointer
#define glCullFace glesCullFace
#define glDeleteBuffers glesDeleteBuffers
#define glDeleteLists glesDeleteLists
#define glDeleteTextures glesDeleteTextures
#define glDepthFunc glesDepthFunc
#define glDepthMask glesDepthMask
#define glDepthRange glesDepthRange
#define glDisable glesDisable
#define glDisableClientState glesDisableClientState
#define glDrawArrays glesDrawArrays
#define glEnable glesEnable
#define glEnableClientState glesEnableClientState
#define glEndList glesEndList
#define glFogf glesFogf
#define glFogfv glesFogfv
#define glFogi glesFogi
#define glGenLists glesGenLists
#define glGetError glesGetError
#define glGetFloatv glesGetFloatv
#define glHint glesHint
#define glLineWidth glesLineWidth
#define glLoadIdentity glesLoadIdentity
#define glMatrixMode glesMatrixMode
#define glMultMatrixf glesMultMatrixf
#define glNewList glesNewList
#define glNormal3f glesNormal3f
#define glNormalPointer glesNormalPointer
#define glOrtho glesOrtho
#define glPolygonMode glesPolygonMode
#define glPolygonOffset glesPolygonOffset
#define glPopMatrix glesPopMatrix
#define glPushMatrix glesPushMatrix
#define glRotatef glesRotatef
#define glScalef glesScalef
#define glScissor glesScissor
#define glShadeModel glesShadeModel
#define glTexCoordPointer glesTexCoordPointer
#define glTexImage2D glesTexImage2D
#define glTexParameteri glesTexParameteri
#define glTexSubImage2D glesTexSubImage2D
#define glTranslatef glesTranslatef
#define glVertexPointer glesVertexPointer
#define glViewport glesViewport
#endif

#ifdef USE_VBO
#define drawArrayVT_NoState drawArrayVT
#define drawArrayVTC_NoState drawArrayVTC
void drawArrayVT(int bufferId, int vertices, int vertexSize, unsigned int mode);
#ifndef drawArrayVT_NoState
// void drawArrayVT_NoState(int bufferId, int vertices, int vertexSize = 24);
#endif
void drawArrayVTC(int bufferId, int vertices, int vertexSize);
#ifndef drawArrayVTC_NoState
void drawArrayVTC_NoState(int bufferId, int vertices, int vertexSize4);
#endif
#endif

void glInit();
void gluPerspective(GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar);
int glhUnProjectf(float winx, float winy, float winz, float *modelview,
    float *projection, int *viewport, float *objectCoordinate);

// Used for "debugging" (...). Obviously stupid dependency on Options (and ugly
// gl*2 calls).
#ifdef GLDEBUG
#define glTranslatef2 glesTranslatef
#define glRotatef2 glesRotatef
#define glScalef2 glesScalef
#define glPushMatrix2 glesPushMatrix
#define glPopMatrix2 glesPopMatrix
#define glLoadIdentity2 glesLoadIdentity

#define glVertexPointer2(a, b, c, d)                                           \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glVertexPtr @ %s:%d : %d\n", __FILE__, __LINE__, 0);               \
    glesVertexPointer(a, b, c, d);                                             \
    GLERR(6);                                                                  \
  } while (0)
#define glColorPointer2(a, b, c, d)                                            \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glColorPtr @ %s:%d : %d\n", __FILE__, __LINE__, 0);                \
    glesColorPointer(a, b, c, d);                                              \
    GLERR(7);                                                                  \
  } while (0)
#define glTexCoordPointer2(a, b, c, d)                                         \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glTexPtr @ %s:%d : %d\n", __FILE__, __LINE__, 0);                  \
    glesTexCoordPointer(a, b, c, d);                                           \
    GLERR(8);                                                                  \
  } while (0)
#define glEnableClientState2(s)                                                \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glEnableClient @ %s:%d : %d\n", __FILE__, __LINE__, 0);            \
    glesEnableClientState(s);                                                  \
    GLERR(9);                                                                  \
  } while (0)
#define glDisableClientState2(s)                                               \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glDisableClient @ %s:%d : %d\n", __FILE__, __LINE__, 0);           \
    glesDisableClientState(s);                                                 \
    GLERR(10);                                                                 \
  } while (0)
#define glDrawArrays2(m, o, v)                                                 \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glDrawA @ %s:%d : %d\n", __FILE__, __LINE__, 0);                   \
    glesDrawArrays(m, o, v);                                                   \
    GLERR(11);                                                                 \
  } while (0)

#define glTexParameteri2(m, o, v)                                              \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glTexParameteri @ %s:%d : %d\n", __FILE__, __LINE__, v);           \
    glesTexParameteri(m, o, v);                                                \
    GLERR(12);                                                                 \
  } while (0)
#define glTexImage2D2(a, b, c, d, e, f, g, height, i)                          \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glTexImage2D @ %s:%d : %d\n", __FILE__, __LINE__, 0);              \
    glesTexImage2D(a, b, c, d, e, f, g, height, i);                            \
    GLERR(13);                                                                 \
  } while (0)
#define glTexSubImage2D2(a, b, c, d, e, f, g, height, i)                       \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glTexSubImage2D @ %s:%d : %d\n", __FILE__, __LINE__, 0);           \
    glesTexSubImage2D(a, b, c, d, e, f, g, height, i);                         \
    GLERR(14);                                                                 \
  } while (0)
#define glGenBuffers2(s, id)                                                   \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glGenBuffers @ %s:%d : %d\n", __FILE__, __LINE__, id);             \
    anGenBuffers(s, id);                                                       \
    GLERR(15);                                                                 \
  } while (0)
#define glBindBuffer2(s, id)                                                   \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glBindBuffer @ %s:%d : %d\n", __FILE__, __LINE__, id);             \
    glesBindBuffer(s, id);                                                     \
    GLERR(16);                                                                 \
  } while (0)
#define glBufferData2(a, b, c, d)                                              \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glBufferData @ %s:%d : %d\n", __FILE__, __LINE__, d);              \
    glesBufferData(a, b, c, d);                                                \
    GLERR(17);                                                                 \
  } while (0)
#define glBindTexture2(m, z)                                                   \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glBindTexture @ %s:%d : %d\n", __FILE__, __LINE__, z);             \
    glesBindTexture(m, z);                                                     \
    GLERR(18);                                                                 \
  } while (0)

#define glEnable2(s)                                                           \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glEnable @ %s:%d : %d\n", __FILE__, __LINE__, s);                  \
    glesEnable(s);                                                             \
    GLERR(19);                                                                 \
  } while (0)
#define glDisable2(s)                                                          \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glDisable @ %s:%d : %d\n", __FILE__, __LINE__, s);                 \
    glesDisable(s);                                                            \
    GLERR(20);                                                                 \
  } while (0)

#define glColor4f2(r, g, b, a)                                                 \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glColor4f2 @ %s:%d : (%f,%f,%f,%f)\n", __FILE__, __LINE__, r, g,   \
          b, a);                                                               \
    glesColor4f(r, g, b, a);                                                   \
    GLERR(21);                                                                 \
  } while (0)

// #define glBlendMode2(s) do{ if (Options::debugGl) LOGI("glEnable @ %s:%d :
// %d\n", __FILE__, __LINE__, s); glEnable(s); GLERR(19); } while(0)
#define glBlendFunc2(src, dst)                                                 \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glBlendFunc @ %s:%d : %d - %d\n", __FILE__, __LINE__, src, dst);   \
    glesBlendFunc(src, dst);                                                   \
    GLERR(23);                                                                 \
  } while (0)
#define glShadeModel2(s)                                                       \
  do {                                                                         \
    if (Options::debugGl)                                                      \
      LOGI("glShadeModel @ %s:%d : %d\n", __FILE__, __LINE__, s);              \
    glesShadeModel(s);                                                         \
    GLERR(25);                                                                 \
  } while (0)
#else
#define glTranslatef2 glesTranslatef
#define glRotatef2 glesRotatef
#define glScalef2 glesScalef
#define glPushMatrix2 glesPushMatrix
#define glPopMatrix2 glesPopMatrix
#define glLoadIdentity2 glesLoadIdentity

#define glVertexPointer2(a, b, c, d)                                           \
  do {                                                                         \
    glesVertexPointer(a, b, c, d);                                             \
  } while (0)
#define glColorPointer2(a, b, c, d)                                            \
  do {                                                                         \
    glesColorPointer(a, b, c, d);                                              \
  } while (0)
#define glTexCoordPointer2(a, b, c, d)                                         \
  do {                                                                         \
    glesTexCoordPointer(a, b, c, d);                                           \
  } while (0)
#define glEnableClientState2(s)                                                \
  do {                                                                         \
    glesEnableClientState(s);                                                  \
  } while (0)
#define glDisableClientState2(s)                                               \
  do {                                                                         \
    glesDisableClientState(s);                                                 \
  } while (0)
#define glDrawArrays2(m, o, v)                                                 \
  do {                                                                         \
    glesDrawArrays(m, o, v);                                                   \
  } while (0)

#define glTexParameteri2(m, o, v)                                              \
  do {                                                                         \
    glesTexParameteri(m, o, v);                                                \
  } while (0)
#define glTexImage2D2(a, b, c, d, e, f, g, height, i)                          \
  do {                                                                         \
    glesTexImage2D(a, b, c, d, e, f, g, height, i);                            \
  } while (0)
#define glTexSubImage2D2(a, b, c, d, e, f, g, height, i)                       \
  do {                                                                         \
    glesTexSubImage2D(a, b, c, d, e, f, g, height, i);                         \
  } while (0)
#define glGenBuffers2 anGenBuffers
#define glBindBuffer2(s, id)                                                   \
  do {                                                                         \
    glesBindBuffer(s, id);                                                     \
  } while (0)
#define glBufferData2(a, b, c, d)                                              \
  do {                                                                         \
    glesBufferData(a, b, c, d);                                                \
  } while (0)
#define glBindTexture2(m, z)                                                   \
  do {                                                                         \
    glesBindTexture(m, z);                                                     \
  } while (0)

#define glEnable2(s)                                                           \
  do {                                                                         \
    glesEnable(s);                                                             \
  } while (0)
#define glDisable2(s)                                                          \
  do {                                                                         \
    glesDisable(s);                                                            \
  } while (0)

#define glColor4f2(r, g, b, a)                                                 \
  do {                                                                         \
    glesColor4f(r, g, b, a);                                                   \
  } while (0)
#define glBlendFunc2(src, dst)                                                 \
  do {                                                                         \
    glesBlendFunc(src, dst);                                                   \
  } while (0)
#define glShadeModel2(s)                                                       \
  do {                                                                         \
    glesShadeModel(s);                                                         \
  } while (0)
#endif

//
// Extensions
//
#ifdef WIN32
#define glGetProcAddress(a) wglGetProcAddress(a)
#else
#define glGetProcAddress(a) (void *(0))
#endif

#endif /*NET_MINECRAFT_CLIENT_RENDERER__gles_H__ */
