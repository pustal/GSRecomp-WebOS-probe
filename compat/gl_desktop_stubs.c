/**
 * @file gl_desktop_stubs.c
 * @brief The three desktop OpenGL functions GSRecomp's engine calls directly
 *        (engine/src/runtime/gpu_surface.cpp) that webOS's libGLESv2 lacks.
 *
 * gpu_surface.cpp is the GPU path behind the wider views; on the TV's OpenGL
 * ES context it does not initialise, so these only have to link and behave
 * sensibly if reached:
 *   - glClearDepth: GLES spells it glClearDepthf (float);
 *   - glGetTexLevelParameteriv: GLES 3.1 has it, the stub library does not
 *     export it, so it is looked up in the driver; without it the value is 0;
 *   - glGetTexImage: no GLES equivalent; the destination is left untouched.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>

typedef double GLdouble;

void glClearDepth(GLdouble depth)
{
    glClearDepthf((GLfloat)depth);
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
    typedef void (*Fn)(GLenum, GLint, GLenum, GLint *);
    static Fn fn = 0;
    static int looked = 0;
    if (!looked) {
        looked = 1;
        fn = (Fn)eglGetProcAddress("glGetTexLevelParameteriv");
    }
    if (fn)
        fn(target, level, pname, params);
    else if (params)
        *params = 0;
}

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels)
{
    (void)target;
    (void)level;
    (void)format;
    (void)type;
    (void)pixels;
}
