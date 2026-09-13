#include "faithful_frame.hpp"
#include "fixed_function.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool close(float a, float b, float e = 1e-5f) { return std::abs(a - b) <= e; }
void okay() {
    require(glGetError() == GL_NO_ERROR, "Unexpected adapter GL error");
    require(vt::faithful_error_count() == 0, "Drained adapter error was hidden");
}
const vt::DrawBatch &last() { return std::get<vt::DrawBatch>(vt::faithful_frame().commands.back()); }
void point(float x = 0, float y = 0, float z = 0) {
    glBegin(GL_POINTS);
    glVertex3f(x, y, z);
    glEnd();
}
void triangle() {
    glBegin(GL_TRIANGLES);
    glVertex3f(-.5f, -.5f, 0);
    glVertex3f(.5f, -.5f, 0);
    glVertex3f(0, .5f, 0);
    glEnd();
}
void matrices() {
    vt::faithful_reset();
    point(.2f, .4f, .6f);
    const auto v = last().vertices[0];
    require(close(v.gl_clip_z, .6f), "Original GL clip depth was not retained");
    require(close(v.position[0], .2f) && close(v.position[1], -.4f) && close(v.position[2], .8f) &&
                v.position[3] == 1,
            "GL to Vulkan clip conversion changed");
    vt::faithful_end_frame();
    glTranslatef(2, 3, -6);
    glPushMatrix();
    glScalef(2, 4, 3);
    point(1, 2, 3);
    auto transformed = last().vertices[0];
    require(close(transformed.position[0], 4) && close(transformed.position[1], -11) &&
                close(transformed.position[2], 2),
            "Matrix postmultiplication order changed");
    glPopMatrix();
    point(1, 2, 3);
    require(close(last().vertices[0].position[0], 3) && close(last().vertices[0].position[1], -5),
            "Combined transform was stale after matrix pop");
    float mv[16]{};
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    require(mv[0] == 1 && mv[12] == 2 && mv[13] == 3 && mv[14] == -6, "Matrix stack restore failed");
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glFrustum(-1, 1, -1, 1, 1, 10);
    point(0, 0, -1);
    auto near = last().vertices[0];
    point(0, 0, -10);
    auto far = last().vertices[0];
    require(close(near.position[2], 0) && close(far.position[2] / far.position[3], 1),
            "Perspective depth range changed");
    glLoadIdentity();
    glOrtho(0, 100, 0, 50, 0, 1);
    point(25, 10, 0);
    auto ortho = last().vertices[0];
    require(close(ortho.position[0], -.5f) && close(ortho.position[1], .6f) && ortho.position[2] == 0,
            "Orthographic HUD projection changed");
    okay();
}
void primitives() {
    vt::faithful_reset();
    glShadeModel(GL_FLAT);
    glBegin(GL_QUADS);
    glColor3f(1, 0, 0);
    glVertex2f(-.5f, -.5f);
    glColor3f(0, 1, 0);
    glVertex2f(.5f, -.5f);
    glColor3f(0, 0, 1);
    glVertex2f(.5f, .5f);
    glColor3f(.2f, .4f, .6f);
    glVertex2f(-.5f, .5f);
    glEnd();
    require(last().vertices.size() == 6, "Quad did not triangulate");
    for (const auto &v : last().vertices)
        require(close(v.front_color[0], .2f) && close(v.front_color[1], .4f),
                "Quad flat color did not use fourth vertex");
    glBegin(GL_POLYGON);
    glColor3f(.3f, .1f, .7f);
    glVertex2f(-.5f, -.5f);
    glColor3f(1, 0, 0);
    glVertex2f(.5f, -.5f);
    glVertex2f(.5f, .5f);
    glVertex2f(-.5f, .5f);
    glEnd();
    for (const auto &v : last().vertices)
        require(close(v.front_color[0], .3f), "Single polygon flat color did not use first vertex");
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < 4; ++i)
        glVertex2i(i / 2, i % 2);
    glEnd();
    require(last().vertices.size() == 6 && last().vertices[3].position[0] == 1 &&
                last().vertices[4].position[0] == 0,
            "Triangle strip winding changed");
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i < 4; ++i) {
        glColor3f(i * .2f, 0, 0);
        glVertex2i(i / 2, i % 2);
    }
    glEnd();
    require(last().vertices.size() == 6 && close(last().vertices[0].front_color[0], .6f),
            "Quad strip triangle count or provoking color changed");
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_LINE_SMOOTH);
    glBegin(GL_QUADS);
    glVertex2f(-.5f, -.5f);
    glVertex2f(.5f, -.5f);
    glVertex2f(.5f, .5f);
    glVertex2f(-.5f, .5f);
    glEnd();
    require(last().primitive == vt::Primitive::LineList && last().vertices.size() == 8 &&
                last().state.line_smooth,
            "Wire quad exposed triangulation diagonal");
    auto commands = vt::faithful_frame().commands.size();
    glBegin(GL_QUADS);
    glVertex2f(-.5f, .5f);
    glVertex2f(.5f, .5f);
    glVertex2f(.5f, -.5f);
    glVertex2f(-.5f, -.5f);
    glEnd();
    require(vt::faithful_frame().commands.size() == commands, "Back-face wire quad escaped culling");
    glDisable(GL_CULL_FACE);
    glBegin(GL_QUADS);
    glVertex2f(-2, -.5f);
    glVertex2f(.5f, -.5f);
    glVertex2f(.5f, .5f);
    glVertex2f(-2, .5f);
    glEnd();
    require(last().vertices.size() == 8, "Clipped wire boundary changed");
    for (const auto &v : last().vertices)
        require(v.position[0] >= -v.position[3], "Wire polygon was not clipped");
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBegin(GL_LINE_LOOP);
    glVertex2f(0, 0);
    glVertex2f(1, 0);
    glVertex2f(1, 1);
    glEnd();
    require(last().vertices.size() == 6 && last().primitive == vt::Primitive::LineList,
            "Line loop did not close");
    okay();
}
void lights() {
    vt::faithful_reset();
    const float black[] = {0, 0, 0, 1}, white[] = {1, 1, 1, 1};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, black);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_AMBIENT, black);
    const float along_y[] = {0, 1, 0, 0};
    glLightfv(GL_LIGHT0, GL_POSITION, along_y);
    glScalef(2, 4, 1);
    const float normal[] = {.70710678f, .70710678f, 0};
    glNormal3fv(normal);
    point();
    require(close(last().vertices[0].front_color[0], .4472136f),
            "Normal did not use normalized inverse transpose");
    glLoadIdentity();
    const float front[] = {1, 0, 0, .4f}, back[] = {0, 1, 0, .8f}, normal_z[] = {0, 0, 1},
                behind[] = {0, 0, -1, 0};
    glMaterialfv(GL_FRONT, GL_DIFFUSE, front);
    glMaterialfv(GL_BACK, GL_DIFFUSE, back);
    glLightfv(GL_LIGHT0, GL_POSITION, behind);
    glNormal3fv(normal_z);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
    point();
    const auto two = last().vertices[0];
    require(two.front_color[0] == 0 && two.back_color[1] == 1 && close(two.front_color[3], .4f) &&
                close(two.back_color[3], .8f),
            "Two-sided material lighting changed");
    const float ahead[] = {0, 0, 1, 0};
    glLightfv(GL_LIGHT0, GL_POSITION, ahead);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, black);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glMaterialf(GL_FRONT, GL_SHININESS, 32);
    point();
    require(close(last().vertices[0].front_color[0], 1), "Specular half-vector contribution missing");
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glColor4f(.2f, .3f, .4f, .5f);
    point();
    require(close(last().vertices[0].front_color[2], .4f) && close(last().vertices[0].front_color[3], .5f),
            "Color-material tracking failed");
    glDisable(GL_LIGHTING);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogi(GL_FOG_START, 100);
    glFogi(GL_FOG_END, 350);
    glTranslatef(0, 0, -150);
    point();
    require(last().vertices[0].fog_distance == 150 && last().state.fog && last().state.fog_end == 350,
            "Production linear fog state changed");
    okay();
}
void arrays_and_state() {
    vt::faithful_reset();
    struct Packed {
        float xyz[3];
        unsigned char color[4];
        float uv[2];
    };
    const Packed data[] = {{{-.5f, -.5f, 0}, {255, 0, 0, 255}, {0, 0}},
                           {{.5f, -.5f, 0}, {0, 255, 0, 128}, {1, 0}},
                           {{0, .5f, 0}, {0, 0, 255, 0}, {.5f, 1}}};
    const unsigned short indices[] = {2, 0, 1};
    glVertexPointer(3, GL_FLOAT, sizeof(Packed), data[0].xyz);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Packed), data[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Packed), data[0].uv);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glViewport(10, 20, 300, 200);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_GREATER, 1, 1);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
    require(last().vertices[0].front_color[2] == 1 && last().vertices[2].front_color[1] == 1 &&
                close(last().vertices[2].front_color[3], 128.f / 255),
            "Interleaved array indexing/normalization changed");
    require(last().vertices[0].uv[1] == 1 && last().state.viewport[1] == 20 && last().state.depth_test &&
                !last().state.depth_write && last().state.stencil && last().state.stencil_func == GL_GREATER,
            "Array draw state incomplete");
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glDisableClientState(GL_COLOR_ARRAY);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPopClientAttrib();
    int enabled = 0, alignment = 0;
    glGetIntegerv(GL_COLOR_ARRAY, &enabled);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    require(enabled && alignment == 4, "Client attrib restore failed");
    glClearColor(.1f, .2f, .3f, .4f);
    glClearStencil(3);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto &clear = std::get<vt::ClearCommand>(vt::faithful_frame().commands.back());
    require(clear.stencil == 3 && close(clear.color[2], .3f) && !clear.depth_write,
            "Ordered clear lost state");
    require(vt::faithful_frame().commands.size() == 2, "Clear reordered relative to draw");
    okay();
}
int readback(int x, int y, int w, int h, unsigned format, unsigned type, void *data) {
    require(x == 2 && y == 3 && w == 2 && h == 2 && format == GL_RGB && type == GL_UNSIGNED_BYTE,
            "Readback callback parameters changed");
    const unsigned char rgb[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    std::memcpy(data, rgb, sizeof(rgb));
    return 1;
}
void textures_and_readback() {
    vt::faithful_reset();
    int stencil = -1;
    glGetIntegerv(GL_STENCIL_BITS, &stencil);
    require(stencil == 0, "Invented stencil attachment");
    VT_SetFramebufferInfo(24, 8, 8192, "test-device");
    glGetIntegerv(GL_STENCIL_BITS, &stencil);
    require(stencil == 8, "Real framebuffer info not exposed");
    require(std::strstr(reinterpret_cast<const char *>(glGetString(GL_RENDERER)), "test-device"),
            "Renderer query lost device");
    GLuint texture = 0;
    glGenTextures(1, &texture);
    require(!glIsTexture(texture), "Reserved name reported as texture object");
    glBindTexture(GL_TEXTURE_2D, texture);
    require(glIsTexture(texture), "Bound texture object missing");
    // RGB rows have 4-byte alignment; each row starts after the leading skipped pixel.
    const unsigned char pixels[] = {99, 99, 99, 1, 2, 3, 4,  5,  6,  99, 99, 99,
                                    99, 99, 99, 7, 8, 9, 10, 11, 12, 99, 99, 99};
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 3);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const unsigned char mip[] = {100, 110, 120};
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, mip);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glEnable(GL_TEXTURE_2D);
    triangle();
    const auto first = last().state.texture;
    require(first && first->levels.size() == 2 && first->base_format == GL_RGB,
            "Texture registry lost mip levels/base format");
    const auto &image = first->levels[0].rgba;
    require(image[0] == 1 && image[4] == 4 && image[8] == 7 && image[12] == 10 && image[3] == 255,
            "RGB upload alignment/skip conversion changed");
    require(first->levels[1].rgba[1] == 110 && first->wrap_s == GL_CLAMP,
            "Mip bytes or sampler state changed");
    int size = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &size);
    require(size == 1, "Texture level query failed");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    triangle();
    const auto second = last().state.texture;
    require(first != second && first->wrap_s == GL_CLAMP && second->wrap_s == GL_REPEAT &&
                first->revision != second->revision,
            "Texture mutation changed earlier draw snapshot");
    glDeleteTextures(1, &texture);
    require(!glIsTexture(texture) && first->levels[0].rgba[0] == 1,
            "Texture deletion invalidated recorded draw");
    VT_SetReadbackCallback(readback);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, 3);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
    unsigned char output[64];
    std::memset(output, 77, sizeof(output));
    glReadPixels(2, 3, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, output);
    require(output[20] == 1 && output[23] == 255 && output[24] == 4 && output[36] == 7 && output[40] == 10 &&
                output[19] == 77 && output[28] == 77,
            "Readback packing/row orientation changed");
    okay();
    vt::faithful_end_frame();
    require(vt::faithful_frame().commands.empty(), "End frame retained draw commands");
    glGetIntegerv(GL_PACK_ALIGNMENT, &size);
    require(size == 8, "End frame reset persistent state");
}
void command_budget() {
    vt::faithful_reset();
    for (int i = 0; i < 65536; ++i)
        glClear(GL_COLOR_BUFFER_BIT);
    require(vt::faithful_frame().commands.size() == 65536, "Command budget truncated valid commands");
    glClear(GL_COLOR_BUFFER_BIT);
    require(glGetError() == GL_OUT_OF_MEMORY && vt::faithful_error_count() == 1,
            "Unbounded clear commands were accepted");
    bool rejected = false;
    try {
        (void)vt::faithful_frame();
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Resource exhaustion silently returned an incomplete frame");
    vt::faithful_reset();
}
void errors() {
    vt::faithful_reset();
    glPopMatrix();
    require(glGetError() == GL_STACK_UNDERFLOW, "Missing matrix underflow");
    require(glGetError() == GL_NO_ERROR && vt::faithful_error_count() == 1,
            "Error draining reset cumulative count");
    glBegin(GL_TRIANGLES);
    glMatrixMode(GL_PROJECTION);
    glEnd();
    require(glGetError() == GL_INVALID_OPERATION, "Allowed forbidden state mutation inside begin");
    glEnable(0xdead);
    glPixelStorei(GL_PACK_ALIGNMENT, 3);
    require(glGetError() == GL_INVALID_ENUM && vt::faithful_error_count() == 4,
            "First GL error semantics changed");
    glTexImage2D(GL_TEXTURE_2D, 16, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    require(glGetError() == GL_INVALID_VALUE, "Unbounded texture level accepted");
    vt::faithful_reset();
    require(vt::faithful_error_count() == 0, "Context reset retained errors");
    okay();
}
} // namespace
int main() {
    try {
        matrices();
        primitives();
        lights();
        arrays_and_state();
        textures_and_readback();
        command_budget();
        errors();
        std::cout << "VT_FIXED_FUNCTION_TEST_OK matrices lighting materials clipping primitives arrays "
                     "textures readback errors\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "VT_FIXED_FUNCTION_TEST_FAILED: " << e.what() << '\n';
        return 1;
    }
}
