/**
 * @file GL/gl.h
 * @brief Stand-in for the desktop OpenGL header, which webOS does not have.
 *
 * GSRecomp's optional GL paths (screen filters, the wider-view field
 * renderer) include <GL/gl.h> only for its types and constants and fetch
 * every function at run time through SDL_GL_GetProcAddress. SDL's own
 * SDL_opengl.h carries those declarations, so it stands in here. On the TV the
 * context is OpenGL ES, so functions only desktop GL has come back missing and
 * those paths switch themselves off; the game draws through SDL's renderer.
 */
#ifndef GSRECOMP_WEBOS_COMPAT_GL_H
#define GSRECOMP_WEBOS_COMPAT_GL_H
#include <SDL_opengl.h>
#endif
