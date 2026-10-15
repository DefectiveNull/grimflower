#pragma once

// The editing operations behind Edit mode's tools, on the level's geometry (src/engine/geometry.h). They
// work in a shape's own space unless they say otherwise, and leave shapes closed and their faces flat:
// faces a change bends out of flat are split into triangles.

#include "geometry.h"

#include <string>
#include <vector>

// A box from corner, sizeU along u, sizeV along v and depth along normal (any of them negative goes the other
// way), with the given material on every face, world aligned. Returns the new mesh's index
int GeoAddBox(Geometry &geometry, Vector3 corner, Vector3 u, Vector3 v, Vector3 normal, float sizeU, float sizeV, float depth,
              const std::string &material);

// Another mesh using the same shape (an instance). Returns its index
int GeoDuplicateMesh(Geometry &geometry, int mesh);

// Deletes a mesh, and its shape if no other mesh uses it
void GeoDeleteMesh(Geometry &geometry, int mesh);

// Turns a mesh around its position by degrees around a world space axis
void GeoRotateMesh(Geometry &geometry, int mesh, Vector3 axis, float degrees);

// Moves a face's vertices along its own normal, resizing the shape that way
void GeoMoveFace(GeoShape &shape, int face, float distance);

// Moves vertices
void GeoMoveVertices(GeoShape &shape, const std::vector<int> &vertices, Vector3 offset);

// Cuts a face in two along a line through point, running along direction (both in the shape's space, on the
// face). Faces next to it get the new corners on their edges too, so the shape stays closed. Returns false if
// the line misses the face
bool GeoCutFace(GeoShape &shape, int face, Vector3 point, Vector3 direction);

// Extrudes a face along its normal: it moves out by distance, and new faces join it to where it was
void GeoExtrudeFace(GeoShape &shape, int face, float distance);

// Extrudes a face around a pivot: swung by degrees around an axis through pivot, in segments steps, for
// curves. The new faces join each step to the next
void GeoExtrudeFaceAround(GeoShape &shape, int face, Vector3 pivot, Vector3 axis, float degrees, int segments);

// Push/pull, like SketchUp's: pulling out (distance > 0) extrudes the face. Pushing in cuts away the face's
// outline, that deep, from the shape, whatever its faces are like: deeper than the shape is thick makes a hole
// through it. The cut faces get the face's material, world aligned
void GeoPushPull(Geometry &geometry, int mesh, int face, float distance);

// Bevels edges (pairs of vertices), rounding them off over width with segments flat steps (1 is a plain
// chamfer). width is cut down to the most the faces next to the edges allow. Each edge's ends must have
// exactly three faces meeting, and no two of the edges can share an end.
// Returns false, changing nothing, if they don't; error says why
bool GeoBevelEdges(GeoShape &shape, const std::vector<std::pair<int, int>> &edges, float &width, int segments, std::string &error);

// Turns a shape inside out: every face turns to face the other way, so a box becomes a room seen from inside,
// and collision makes it solid outside its faces instead (see GeoConvexPieces). rotation is the mesh's, so
// faces still world aligned the default way get the default for the way they face now; others keep theirs
void GeoFlipShape(GeoShape &shape, Quaternion rotation);

// Deletes a face, and the faces joined to it on the same flat surface (the pieces cuts left of it), for cleaning
// up broken meshes. Leaves a hole: the shape isn't closed any more. Returns how many faces went
int GeoDeleteFaces(GeoShape &shape, int face);

// Extrudes edges (pairs of vertices): copies of them, moved by offset, joined to them by new faces, wound to
// carry on from the face already on each edge and with its material. Edges that share an end share its copy.
// Pulling an open edge (Shift+D left a hole) carries the surface on; an edge with faces both sides grows a
// fin. rotation is the mesh's, for world aligning the new faces. Returns the copies, in the same order
std::vector<std::pair<int, int>> GeoExtrudeEdges(GeoShape &shape, const std::vector<std::pair<int, int>> &edges, Vector3 offset, Quaternion rotation);

// Splits faces that aren't flat any more into triangles
void GeoFixBentFaces(GeoShape &shape);
