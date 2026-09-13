#ifndef VULKANTRON_FIXED_FUNCTION_H
#define VULKANTRON_FIXED_FUNCTION_H
/* Compile-time GL 1.x types/tokens only. Every production call below is recorded
 * by VulkanTron's own adapter; this target never opens or links an OpenGL context. */
#include <SDL3/SDL_opengl.h>
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Callback fills tightly packed bottom-up RGB; nonzero means success. */
typedef int (*VTReadbackCallback)(int, int, int, int, unsigned, unsigned, void *);
void VT_SetReadbackCallback(VTReadbackCallback callback);
void VT_SetFramebufferInfo(int depth_bits, int stencil_bits, int max_texture_size, const char *renderer);
void vt_glBegin(GLenum mode);
void vt_glBindTexture(GLenum target, GLuint texture);
void vt_glBlendFunc(GLenum sfactor, GLenum dfactor);
void vt_glClear(GLbitfield mask);
void vt_glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void vt_glClearDepth(GLclampd depth);
void vt_glClearStencil(GLint s);
void vt_glColor3d(GLdouble red, GLdouble green, GLdouble blue);
void vt_glColor3f(GLfloat red, GLfloat green, GLfloat blue);
void vt_glColor3fv(const GLfloat *v);
void vt_glColor3ubv(const GLubyte *v);
void vt_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void vt_glColor4fv(const GLfloat *v);
void vt_glColorMaterial(GLenum face, GLenum mode);
void vt_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void vt_glCullFace(GLenum mode);
void vt_glDeleteTextures(GLsizei n, const GLuint *textures);
void vt_glDepthFunc(GLenum func);
void vt_glDepthMask(GLboolean flag);
void vt_glDisable(GLenum cap);
void vt_glDisableClientState(GLenum cap);
void vt_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void vt_glEnable(GLenum cap);
void vt_glEnableClientState(GLenum cap);
void vt_glEnd(void);
void vt_glFogfv(GLenum pname, const GLfloat *params);
void vt_glFogi(GLenum pname, GLint param);
void vt_glFrontFace(GLenum mode);
void vt_glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val,
                  GLdouble far_val);
void vt_glGenTextures(GLsizei n, GLuint *textures);
void vt_glGetBooleanv(GLenum pname, GLboolean *params);
void vt_glGetDoublev(GLenum pname, GLdouble *params);
GLenum vt_glGetError(void);
void vt_glGetFloatv(GLenum pname, GLfloat *params);
void vt_glGetIntegerv(GLenum pname, GLint *params);
const GLubyte *vt_glGetString(GLenum name);
void vt_glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params);
GLboolean vt_glIsEnabled(GLenum cap);
GLboolean vt_glIsTexture(GLuint texture);
void vt_glLightModelfv(GLenum pname, const GLfloat *params);
void vt_glLightModeli(GLenum pname, GLint param);
void vt_glLightfv(GLenum light, GLenum pname, const GLfloat *params);
void vt_glLineWidth(GLfloat width);
void vt_glLoadIdentity(void);
void vt_glLoadMatrixf(const GLfloat *m);
void vt_glMaterialf(GLenum face, GLenum pname, GLfloat param);
void vt_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
void vt_glMatrixMode(GLenum mode);
void vt_glMultMatrixf(const GLfloat *m);
void vt_glNormal3fv(const GLfloat *v);
void vt_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr);
void vt_glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val,
                GLdouble far_val);
void vt_glPixelStorei(GLenum pname, GLint param);
void vt_glPointSize(GLfloat size);
void vt_glPolygonMode(GLenum face, GLenum mode);
void vt_glPolygonOffset(GLfloat factor, GLfloat units);
void vt_glPopClientAttrib(void);
void vt_glPopMatrix(void);
void vt_glPushClientAttrib(GLbitfield mask);
void vt_glPushMatrix(void);
void vt_glRasterPos2i(GLint x, GLint y);
void vt_glReadBuffer(GLenum mode);
void vt_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                     GLvoid *pixels);
void vt_glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void vt_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void vt_glScalef(GLfloat x, GLfloat y, GLfloat z);
void vt_glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void vt_glShadeModel(GLenum mode);
void vt_glStencilFunc(GLenum func, GLint ref, GLuint mask);
void vt_glStencilMask(GLuint mask);
void vt_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void vt_glTexCoord2f(GLfloat s, GLfloat t);
void vt_glTexCoord2fv(const GLfloat *v);
void vt_glTexCoord2i(GLint s, GLint t);
void vt_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void vt_glTexEnvi(GLenum target, GLenum pname, GLint param);
void vt_glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
                     GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void vt_glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void vt_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void vt_glTexParameteri(GLenum target, GLenum pname, GLint param);
void vt_glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void vt_glVertex2d(GLdouble x, GLdouble y);
void vt_glVertex2f(GLfloat x, GLfloat y);
void vt_glVertex2i(GLint x, GLint y);
void vt_glVertex3d(GLdouble x, GLdouble y, GLdouble z);
void vt_glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void vt_glVertex3fv(const GLfloat *v);
void vt_glVertex3i(GLint x, GLint y, GLint z);
void vt_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void vt_glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
#ifdef __cplusplus
}
#endif
#define glBegin vt_glBegin
#define glBindTexture vt_glBindTexture
#define glBlendFunc vt_glBlendFunc
#define glClear vt_glClear
#define glClearColor vt_glClearColor
#define glClearDepth vt_glClearDepth
#define glClearStencil vt_glClearStencil
#define glColor3d vt_glColor3d
#define glColor3f vt_glColor3f
#define glColor3fv vt_glColor3fv
#define glColor3ubv vt_glColor3ubv
#define glColor4f vt_glColor4f
#define glColor4fv vt_glColor4fv
#define glColorMaterial vt_glColorMaterial
#define glColorPointer vt_glColorPointer
#define glCullFace vt_glCullFace
#define glDeleteTextures vt_glDeleteTextures
#define glDepthFunc vt_glDepthFunc
#define glDepthMask vt_glDepthMask
#define glDisable vt_glDisable
#define glDisableClientState vt_glDisableClientState
#define glDrawElements vt_glDrawElements
#define glEnable vt_glEnable
#define glEnableClientState vt_glEnableClientState
#define glEnd vt_glEnd
#define glFogfv vt_glFogfv
#define glFogi vt_glFogi
#define glFrontFace vt_glFrontFace
#define glFrustum vt_glFrustum
#define glGenTextures vt_glGenTextures
#define glGetBooleanv vt_glGetBooleanv
#define glGetDoublev vt_glGetDoublev
#define glGetError vt_glGetError
#define glGetFloatv vt_glGetFloatv
#define glGetIntegerv vt_glGetIntegerv
#define glGetString vt_glGetString
#define glGetTexLevelParameteriv vt_glGetTexLevelParameteriv
#define glIsEnabled vt_glIsEnabled
#define glIsTexture vt_glIsTexture
#define glLightModelfv vt_glLightModelfv
#define glLightModeli vt_glLightModeli
#define glLightfv vt_glLightfv
#define glLineWidth vt_glLineWidth
#define glLoadIdentity vt_glLoadIdentity
#define glLoadMatrixf vt_glLoadMatrixf
#define glMaterialf vt_glMaterialf
#define glMaterialfv vt_glMaterialfv
#define glMatrixMode vt_glMatrixMode
#define glMultMatrixf vt_glMultMatrixf
#define glNormal3fv vt_glNormal3fv
#define glNormalPointer vt_glNormalPointer
#define glOrtho vt_glOrtho
#define glPixelStorei vt_glPixelStorei
#define glPointSize vt_glPointSize
#define glPolygonMode vt_glPolygonMode
#define glPolygonOffset vt_glPolygonOffset
#define glPopClientAttrib vt_glPopClientAttrib
#define glPopMatrix vt_glPopMatrix
#define glPushClientAttrib vt_glPushClientAttrib
#define glPushMatrix vt_glPushMatrix
#define glRasterPos2i vt_glRasterPos2i
#define glReadBuffer vt_glReadBuffer
#define glReadPixels vt_glReadPixels
#define glRotated vt_glRotated
#define glRotatef vt_glRotatef
#define glScalef vt_glScalef
#define glScissor vt_glScissor
#define glShadeModel vt_glShadeModel
#define glStencilFunc vt_glStencilFunc
#define glStencilMask vt_glStencilMask
#define glStencilOp vt_glStencilOp
#define glTexCoord2f vt_glTexCoord2f
#define glTexCoord2fv vt_glTexCoord2fv
#define glTexCoord2i vt_glTexCoord2i
#define glTexCoordPointer vt_glTexCoordPointer
#define glTexEnvi vt_glTexEnvi
#define glTexImage2D vt_glTexImage2D
#define glTexParameterf vt_glTexParameterf
#define glTexParameterfv vt_glTexParameterfv
#define glTexParameteri vt_glTexParameteri
#define glTranslatef vt_glTranslatef
#define glVertex2d vt_glVertex2d
#define glVertex2f vt_glVertex2f
#define glVertex2i vt_glVertex2i
#define glVertex3d vt_glVertex3d
#define glVertex3f vt_glVertex3f
#define glVertex3fv vt_glVertex3fv
#define glVertex3i vt_glVertex3i
#define glVertexPointer vt_glVertexPointer
#define glViewport vt_glViewport
#endif
