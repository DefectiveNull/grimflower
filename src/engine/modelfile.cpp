#include "modelfile.h"
#include "gamedata.h"

#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cstring>

// A file read whole through the game data
class GameDataStream : public Assimp::IOStream
{
public:
    GameDataStream(unsigned char *data, size_t size) : data(data), size(size), position(0) {}
    ~GameDataStream() override { UnloadFileData(data); }

    size_t Read(void *buffer, size_t itemSize, size_t count) override
    {
        if (itemSize == 0) return 0;
        size_t items = std::min(count, (size - position)/itemSize);
        memcpy(buffer, data + position, items*itemSize);
        position += items*itemSize;
        return items;
    }

    size_t Write(const void *, size_t, size_t) override { return 0; }

    aiReturn Seek(size_t offset, aiOrigin origin) override
    {
        size_t base = (origin == aiOrigin_SET) ? 0 : (origin == aiOrigin_CUR) ? position : size;
        if (base + offset > size) return aiReturn_FAILURE;
        position = base + offset;
        return aiReturn_SUCCESS;
    }

    size_t Tell() const override { return position; }
    size_t FileSize() const override { return size; }
    void Flush() override {}

private:
    unsigned char *data;
    size_t size;
    size_t position;
};

// Lets assimp open a model, and the files it needs like a .mtl or .bin, through the game data
class GameDataIOSystem : public Assimp::IOSystem
{
public:
    bool Exists(const char *path) const override { return GameDataFileExists(path); }
    char getOsSeparator() const override { return '/'; }

    Assimp::IOStream *Open(const char *path, const char *) override
    {
        if (!GameDataFileExists(path)) return nullptr;
        int size = 0;
        unsigned char *data = LoadFileData(path, &size);
        return (data != nullptr) ? new GameDataStream(data, (size_t)size) : nullptr;
    }

    void Close(Assimp::IOStream *file) override { delete file; }
};

static std::string DiffuseTexturePath(const aiMaterial *material)
{
    aiString path;
    if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS) return path.C_Str();
    if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &path) == AI_SUCCESS) return path.C_Str();
    return "";
}

static void ReadMaterials(const aiScene *scene, const char *path, ModelFile &model)
{
    model.materials.resize(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
    {
        ModelMaterial &material = model.materials[i];
        material.name = scene->mMaterials[i]->GetName().C_Str();
        material.texturePath = DiffuseTexturePath(scene->mMaterials[i]);

        const aiTexture *embedded = material.texturePath.empty() ? nullptr : scene->GetEmbeddedTexture(material.texturePath.c_str());
        if (embedded == nullptr) continue;

        // A height of 0 means it's an image file. Otherwise it's raw pixels, which exporters don't write for these formats
        if (embedded->mHeight == 0)
        {
            const unsigned char *bytes = (const unsigned char *)embedded->pcData;
            material.embeddedData.assign(bytes, bytes + embedded->mWidth);
            material.texturePath = std::string(path) + "#" + material.texturePath;
        }
        else
        {
            TraceLog(LOG_WARNING, "MODEL: [%s] Material \"%s\" has an uncompressed embedded texture, its faces will be untextured", path, material.name.c_str());
            material.texturePath.clear();
        }
    }
}

// Adds a mesh's faces to the object, moved by the node's place in the file. Points and lines are left out
static void ReadMesh(const aiMesh *mesh, const aiMatrix4x4 &transform, ModelObject &object)
{
    aiMatrix3x3 normalTransform = aiMatrix3x3(transform);
    normalTransform.Inverse().Transpose();
    const bool mirrored = transform.Determinant() < 0.0f;  // Mirroring turns faces inside out, so their corners go the other way round

    for (unsigned int f = 0; f < mesh->mNumFaces; f++)
    {
        const aiFace &source = mesh->mFaces[f];
        if (source.mNumIndices < 3) continue;

        ModelFace face;
        face.material = (int)mesh->mMaterialIndex;
        for (unsigned int k = 0; k < source.mNumIndices; k++)
        {
            unsigned int index = source.mIndices[k];
            aiVector3D p = transform*mesh->mVertices[index];

            ModelCorner corner = {};
            corner.position = { p.x, p.y, p.z };
            if (mesh->HasNormals())
            {
                aiVector3D n = normalTransform*mesh->mNormals[index];
                n.NormalizeSafe();
                corner.normal = { n.x, n.y, n.z };
                corner.hasNormal = true;
            }
            if (mesh->HasTextureCoords(0))
            {
                const aiVector3D &uv = mesh->mTextureCoords[0][index];
                corner.texcoord = { uv.x, uv.y };
            }
            face.corners.push_back(corner);
        }
        if (mirrored) std::reverse(face.corners.begin(), face.corners.end());
        object.faces.push_back(face);
    }
}

// Each node that has meshes is an object. A node has one mesh per material
static void ReadNode(const aiScene *scene, const aiNode *node, const aiMatrix4x4 &parent, ModelFile &model)
{
    aiMatrix4x4 transform = parent*node->mTransformation;

    ModelObject object;
    for (unsigned int i = 0; i < node->mNumMeshes; i++) ReadMesh(scene->mMeshes[node->mMeshes[i]], transform, object);
    if (!object.faces.empty()) model.objects.push_back(object);

    for (unsigned int i = 0; i < node->mNumChildren; i++) ReadNode(scene, node->mChildren[i], transform, model);
}

bool LoadModelFile(const char *path, bool keepPolygons, ModelFile &model)
{
    Assimp::Importer importer;
    importer.SetIOHandler(new GameDataIOSystem());  // The importer owns it

    unsigned int steps = aiProcess_FlipUVs | aiProcess_ValidateDataStructure;
    if (!keepPolygons) steps |= aiProcess_Triangulate;

    const aiScene *scene = importer.ReadFile(path, steps);
    if (scene == nullptr || scene->mRootNode == nullptr)
    {
        TraceLog(LOG_WARNING, "MODEL: [%s] Couldn't read it: %s", path, importer.GetErrorString());
        return false;
    }

    ReadMaterials(scene, path, model);
    ReadNode(scene, scene->mRootNode, aiMatrix4x4(), model);
    return true;
}

std::string FindModelTexture(const std::string &texturePath, const std::string &modelFolder)
{
    std::string path = texturePath;
    for (char &c : path) if (c == '\\') c = '/';

    if (GameDataFileExists(path.c_str())) return path;

    std::string beside = modelFolder + "/" + path;
    if (GameDataFileExists(beside.c_str())) return beside;

    std::string justTheName = modelFolder + "/" + GetFileName(path.c_str());
    if (GameDataFileExists(justTheName.c_str())) return justTheName;
    return "";
}
