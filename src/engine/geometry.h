#pragma once

// Level geometry: the world, as the level file stores it and the editor's Modelling and Texturing modes change it.
//
// A shape is a closed solid made of flat polygon faces (polygroups: a face is kept as one polygon, however
// many triangles it takes to draw). Its vertices are relative to its own origin. A mesh places a shape in
// the level, moved and turned; meshes can share a shape, so duplicates are instances of the same solid.
//
// Textures are world aligned, like Quake and Hammer: each face has two directions in world space that
// its texture's x and y run along, a scale (world units per texel) and an offset (in texels), the way the
// Valve 220 map format has them. Moving or resizing a mesh slides it under its texture rather than
// dragging the texture along, and faces next to each other with the same axes line up seamlessly.

#include "raylib.h"

#include <string>
#include <vector>

struct GeoFace
{
    std::vector<int> corners;            // Indices into the shape's vertices, counter-clockwise seen from outside
    std::string material;                // Diffuse texture, as a game data path. Its .materials entry applies
    Vector3 uAxis = { 1.0f, 0.0f, 0.0f };   // World space direction the texture's x runs along
    Vector3 vAxis = { 0.0f, 0.0f, 1.0f };   // ...and its y, down the image
    Vector2 offset = { 0.0f, 0.0f };     // In texels
    Vector2 scale = { 1.0f, 1.0f };      // World units per texel
};

struct GeoShape
{
    std::vector<Vector3> vertices;       // Relative to the mesh's position, before its rotation
    std::vector<GeoFace> faces;
};

struct GeoMesh
{
    int shape;                           // Index into Geometry::shapes
    Vector3 position;                    // Where the shape's origin goes: its pivot for rotating
    Quaternion rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
};

struct Geometry
{
    std::vector<GeoShape> shapes;
    std::vector<GeoMesh> meshes;
};

// A mesh's transform: rotated around its position, then moved there
Matrix GeoMeshTransform(const GeoMesh &mesh);

// A face's corners, and its normal, in world space for that mesh
std::vector<Vector3> GeoFacePoints(const GeoShape &shape, const GeoFace &face, const Matrix &transform);
Vector3 GeoPolygonNormal(const std::vector<Vector3> &points);  // Newell's method: right handed, so outward for counter-clockwise faces

// The world aligned texture directions for a face facing this way, like Quake's (but never mirrored): floors and ceilings along
// x and z, walls along the horizontal axis they face across and down the world's y
void GeoParaxialAxes(Vector3 normal, Vector3 &uAxis, Vector3 &vAxis);
void GeoResetTexture(GeoFace &face, Vector3 worldNormal);  // Paraxial axes, scale 1, no offset

// Where a world space point is on a face's texture, from 0 to 1 per repeat
Vector2 GeoTextureCoord(const GeoFace &face, Vector3 point, int textureWidth, int textureHeight);

// Splits a flat polygon into triangles, as index triples into points. Works for concave polygons (ear
// clipping); normal is the side it's seen from
std::vector<int> GeoTriangulate(const std::vector<Vector3> &points, Vector3 normal);

// Splits a shape into convex pieces, in its own space, the way Quake's map compiler finds the solid parts of a
// map: a BSP tree of its face planes, whose solid leaves are each convex. Each piece is a list of polygon faces
std::vector<std::vector<std::vector<Vector3>>> GeoConvexPieces(const GeoShape &shape);

// Joins faces that are on the same plane, share an edge, and have the same material and texture
// alignment back into single polygons, as long as the result has no hole. Tools that cut faces into pieces
// leave them like this. Then drops vertices no face uses
void GeoMergeFaces(GeoShape &shape);

// Imports an OBJ as geometry: each of its objects ("o" or "g") becomes a shape and a mesh, positioned at the
// middle of its bounds. scale converts the OBJ's units to world units. The faces keep their textures, with
// texture axes fitted to the OBJ's texture coordinates. Returns false if the file couldn't be read
bool ImportObjGeometry(const char *objPath, float scale, Geometry &geometry);

// Rounds every number to 4 decimals, the way level files store them, so geometry in memory is exactly what
// saving and loading it gives: the bounce lightmap's fingerprint of it then matches in the game
void GeoQuantize(Geometry &geometry);
