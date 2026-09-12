#ifdef NDEBUG
#undef NDEBUG
#endif
#include "video/trail_geometry.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern float getDist(segment2 *s, float *eye);

SettingsCache gSettingsCache;
Game2 *game2;
int dirsX[] = { 0, -1, 0, 1 };
int dirsY[] = { -1, 0, 1, 0 };
int polycount;
float shadow_color[4];
float shadow_matrix[16];

static const TrailMesh *submitted_mesh;
static int draw_calls;

void checkGLError(const char *where) { (void)where; }
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
  assert(size == 3 && type == GL_FLOAT && stride == 0);
  assert(pointer == submitted_mesh->pVertices);
}
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
  assert(type == GL_FLOAT && stride == 0);
  assert(pointer == submitted_mesh->pNormals);
}
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
  assert(size == 2 && type == GL_FLOAT && stride == 0);
  assert(pointer == submitted_mesh->pTexCoords);
}
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
  assert(size == 4 && type == GL_UNSIGNED_BYTE && stride == 0);
  assert(pointer == submitted_mesh->pColors);
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
  assert(mode == GL_TRIANGLES && type == GL_UNSIGNED_INT);
  assert(count == (GLsizei)submitted_mesh->iUsed);
  assert(indices == submitted_mesh->pIndices);
  draw_calls++;
}
void glDisableClientState(GLenum state) { (void)state; }

static void expectDistance(float sx, float sy, float dx, float dy,
                           float x, float y, float expected) {
  segment2 line = { {{sx, sy}}, {{dx, dy}} };
  /* A real two-float eye catches any accidental third-component read. */
  float eye[2] = { x, y };
  float actual = getDist(&line, eye);
  assert(isfinite(actual));
  assert(fabsf(actual - expected) < 0.00001f);
}

static void testDistances(void) {
  expectDistance(0, 0, 0, 0, 3, 4, 5);
  expectDistance(5, -5, 0, 0, -7, 0, 13);
  expectDistance(5, 7, 10, 0, 100, 7.25f, 0.25f);
  expectDistance(5, 7, -10, 0, 100, 7.25f, 0.25f);
  expectDistance(4, 2, 0, 8, 3.875f, 100, 0.125f);
  expectDistance(3, 4, 3, 4, 7, 1, 5);
  expectDistance(103, -96, 3, 4, 107, -99, 5);
  expectDistance(3, 4, 3, 4, 9, 12, 0);
}

static void testMesh(int completedSegments) {
  Data data;
  Player player;
  PlayerVisual visual;
  TrailMesh mesh;
  float x = 0, y = 0;
  int i, vertexOffset = 0, indexOffset = 0;
  GLuint maximum = 0;
  const GLuint first_quad[] = { 0, 1, 2, 1, 3, 2 };
  memset(&data, 0, sizeof(data));
  memset(&player, 0, sizeof(player));
  memset(&visual, 0, sizeof(visual));
  data.trails = calloc((size_t)completedSegments + 1, sizeof(*data.trails));
  assert(data.trails != NULL);
  data.trailCapacity = completedSegments + 1;
  data.trailOffset = completedSegments;
  data.trail_height = 2.0f;
  data.speed = 10.0f;
  data.dir = completedSegments % 2 == 0 ? 3 : 2;
  player.data = &data;
  for(i = 0; i < 4; i++) {
    visual.pColorAlpha[i] = 0.5f;
    visual.pColorDiffuse[i] = 0.75f;
  }
  for(i = 0; i <= completedSegments; i++) {
    segment2 *segment = data.trails + i;
    segment->vStart.v[0] = x;
    segment->vStart.v[1] = y;
    if(i % 2 == 0) segment->vDirection.v[0] = 4;
    else segment->vDirection.v[1] = 4;
    x += segment->vDirection.v[0];
    y += segment->vDirection.v[1];
  }
  assert(trailMeshAllocate(&mesh, completedSegments));
  trailGeometry(&player, &visual, &mesh, &vertexOffset, &indexOffset);
  bowGeometry(&player, &visual, &mesh, &vertexOffset, &indexOffset);
  assert(vertexOffset == 4 * completedSegments + 30);
  assert(mesh.vertexCapacity == (unsigned int)vertexOffset);
  assert(indexOffset == 6 * completedSegments + 66);
  assert(mesh.iUsed == (unsigned int)indexOffset);
  /* The final classic bow quad is stored but not submitted. Preserve that
   * visual behavior while allocating enough room for all actual writes. */
  assert(mesh.iSize == mesh.iUsed + 6);
  for(i = 0; i < (int)mesh.iSize; i++) {
    assert(mesh.pIndices[i] < mesh.vertexCapacity);
    if(mesh.pIndices[i] > maximum) maximum = mesh.pIndices[i];
  }
  assert(maximum == mesh.vertexCapacity - 1);
  if(completedSegments == 20000) assert(maximum > 65535);
  if(completedSegments > 0)
    assert(memcmp(mesh.pIndices, first_quad, sizeof(first_quad)) == 0);
  for(i = 0; i < vertexOffset; i++) {
    assert(isfinite(mesh.pVertices[i].v[0]));
    assert(isfinite(mesh.pVertices[i].v[1]));
    assert(isfinite(mesh.pVertices[i].v[2]));
    assert(isfinite(mesh.pTexCoords[i].v[0]));
    assert(isfinite(mesh.pTexCoords[i].v[1]));
  }
  assert(mesh.pVertices[vertexOffset - 1].v[2] == 0.3f * data.trail_height);
  submitted_mesh = &mesh;
  draw_calls = 0;
  trailRender(&mesh);
  assert(draw_calls == 1);
  trailMeshFree(&mesh);
  assert(mesh.pVertices == NULL && mesh.pIndices == NULL && mesh.iSize == 0);
  free(data.trails);
  printf("PASS: %d trail segments, %d vertices, %d classic draw indices\n",
         completedSegments, vertexOffset, indexOffset);
}

int main(void) {
  const int cases[] = { 0, 3, 154, 155, 243, 999, 15000, 20000 };
  TrailMesh invalid;
  size_t i;
  testDistances();
  assert(!trailMeshAllocate(&invalid, -1));
  assert(invalid.pVertices == NULL && invalid.pIndices == NULL);
  assert(!trailMeshAllocate(&invalid, INT_MAX));
  assert(invalid.pVertices == NULL && invalid.pIndices == NULL);
  for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) testMesh(cases[i]);
  puts("PASS: two-dimensional trail distance and long-trail rendering bounds");
  return 0;
}
