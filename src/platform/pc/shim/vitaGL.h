// SPDX-License-Identifier: GPL-3.0-or-later
// PC shim: the vitaGL calls the game uses, on an SDL2 window with a
// compatibility-profile OpenGL context. Also pulls in the GL 1.3 and
// framebuffer-object entry points the renderer uses.
#pragma once
#include <SDL_opengl.h>
#include <cstddef>

enum { VGL_MEM_VRAM = 0, VGL_MEM_RAM = 1, VGL_MEM_PHYCONT = 2, VGL_MEM_ALL = 3 };
enum { SCE_GXM_MULTISAMPLE_NONE = 0 };

GLboolean vglInitWithCustomThreshold(int pool, int width, int height, int ramThreshold, int cdramThreshold,
                                     int phycontThreshold, int cdlgThreshold, int msaa);
GLboolean vglInitExtended(int pool, int width, int height, int ramThreshold, int msaa);
void vglSwapBuffers(GLboolean hasCommonDialog);
size_t vglMemFree(int type);
size_t vglMemTotal(int type);
void vglPhycontMemLazyInit(size_t size);

// GL entry points beyond OpenGL 1.1 (opengl32.dll on Windows).
#define glActiveTexture pcglActiveTexture
#define glClientActiveTexture pcglClientActiveTexture
#define glGenFramebuffers pcglGenFramebuffers
#define glBindFramebuffer pcglBindFramebuffer
#define glFramebufferTexture2D pcglFramebufferTexture2D
#define glDeleteFramebuffers pcglDeleteFramebuffers
extern void (*pcglActiveTexture)(GLenum);
extern void (*pcglClientActiveTexture)(GLenum);
extern void (*pcglGenFramebuffers)(GLsizei, GLuint *);
extern void (*pcglBindFramebuffer)(GLenum, GLuint);
extern void (*pcglFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
extern void (*pcglDeleteFramebuffers)(GLsizei, const GLuint *);
