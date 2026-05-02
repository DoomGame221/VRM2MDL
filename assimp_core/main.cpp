#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <filesystem>
#include <cstdarg>
#include <algorithm>
#include <cmath>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "../include/vrm2mdl.h"

#define VRM2MDL_VERSION "4.1.0"

namespace fs = std::filesystem;

// Helper to log to callback or stdout
void logMessage(LogCallback cb, void* user_data, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (cb) {
        cb(user_data, buffer);
    } else {
        std::cout << buffer << std::endl;
    }
}

struct Vector3 { float x, y, z; };
// Map Assimp (Y-up, Z-forward/back) to Source (Z-up, X-forward)
// VRM Core uses: Source X = Assimp Z, Source Y = -Assimp X, Source Z = Assimp Y
Vector3 toSourceCoord(float x, float y, float z) { return { z, -x, y }; }

struct Weight {
    int boneId;
    float weight;
};

struct Bone {
    int id;
    std::string name;
    int parent_id;
    aiMatrix4x4 localTransform;
    aiMatrix4x4 globalTransform;
};

std::vector<Bone> bones;
std::map<std::string, int> boneNameToId;

int getOrAddBone(const std::string& name, int parent_id = -1) {
    if (boneNameToId.find(name) != boneNameToId.end()) {
        return boneNameToId[name];
    }
    int new_id = bones.size();
    Bone b;
    b.id = new_id;
    b.name = name;
    b.parent_id = parent_id;
    bones.push_back(b);
    boneNameToId[name] = new_id;
    return new_id;
}

void buildSkeleton(const aiNode* node, int parent_id) {
    std::string nodeName = node->mName.C_Str();
    if (nodeName.empty()) nodeName = "unnamed_node_" + std::to_string(bones.size());
    for (char& c : nodeName) if (c == ' ') c = '_';
    
    int current_id = getOrAddBone(nodeName, parent_id);
    bones[current_id].localTransform = node->mTransformation;
    
    if (parent_id == -1) {
        bones[current_id].globalTransform = bones[current_id].localTransform;
    } else {
        bones[current_id].globalTransform = bones[parent_id].globalTransform * bones[current_id].localTransform;
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        buildSkeleton(node->mChildren[i], current_id);
    }
}

// Simple Matrix to Euler conversion for SMD (radians)
void decomposeToEuler(const aiMatrix4x4& m, float& x, float& y, float& z) {
    // Extract rotation part and convert to Euler
    // Note: This is a simplified version. For full production, use proper decomposition.
    float pitch = -asin(std::clamp(m.b3, -1.0f, 1.0f));
    if (cos(pitch) > 0.0001) {
        x = atan2(m.c3, m.a3);
        z = atan2(m.b1, m.b2);
    } else {
        x = 0;
        z = atan2(-m.a2, m.a1);
    }
    y = pitch;
}

struct UnrolledVertex {
    Vector3 pos;
    Vector3 nrm;
    float u, v;
    std::vector<Weight> weights;
};

std::vector<UnrolledVertex> exportAssimpToSMD(const aiScene* scene, const std::string& filepath, float scale) {
    std::vector<UnrolledVertex> uniqueVertices;
    std::ofstream smd(filepath);
    if (!smd.is_open()) return uniqueVertices;

    smd << "version 1\nnodes\n";
    for (const auto& b : bones) {
        smd << b.id << " \"" << b.name << "\" " << b.parent_id << "\n";
    }
    smd << "end\nskeleton\ntime 0\n";
    for (const auto& b : bones) {
        aiVector3D pos;
        aiQuaternion rot;
        aiVector3D scl;
        b.localTransform.Decompose(scl, rot, pos);
        
        Vector3 sPos = toSourceCoord(pos.x * scale, pos.y * scale, pos.z * scale);
        // Bone rotations in SMD are often tricky. Using identity or 0 for now as most models 
        // rely on absolute vertex positions in reference SMD.
        smd << b.id << " " << sPos.x << " " << sPos.y << " " << sPos.z << " 0 0 0\n";
    }
    smd << "end\ntriangles\n";

    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        aiMesh* mesh = scene->mMeshes[m];
        
        // Pre-collect weights for this mesh
        std::vector<std::vector<Weight>> vertexWeights(mesh->mNumVertices);
        for (unsigned int b = 0; b < mesh->mNumBones; b++) {
            aiBone* aibone = mesh->mBones[b];
            int boneId = getOrAddBone(aibone->mName.C_Str());
            for (unsigned int w = 0; w < aibone->mNumWeights; w++) {
                vertexWeights[aibone->mWeights[w].mVertexId].push_back({boneId, aibone->mWeights[w].mWeight});
            }
        }

        std::string matName = "material_" + std::to_string(mesh->mMaterialIndex);
        if (scene->mMaterials[mesh->mMaterialIndex]) {
            aiString name;
            if (scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
                matName = name.C_Str();
                for (char& c : matName) if (c == ' ') c = '_';
            }
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            aiFace face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            
            smd << matName << "\n";
            for (int i = 0; i < 3; i++) {
                unsigned int vIdx = face.mIndices[i];
                aiVector3D p = mesh->mVertices[vIdx];
                aiVector3D n = mesh->mNormals ? mesh->mNormals[vIdx] : aiVector3D(0,1,0);
                aiVector3D uv = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][vIdx] : aiVector3D(0,0,0);
                
                Vector3 sPos = toSourceCoord(p.x * scale, p.y * scale, p.z * scale);
                Vector3 sNrm = toSourceCoord(n.x, n.y, n.z);
                
                UnrolledVertex uvx;
                uvx.pos = sPos; uvx.nrm = sNrm; uvx.u = uv.x; uvx.v = 1.0f - uv.y;
                uvx.weights = vertexWeights[vIdx];
                if (uvx.weights.empty()) uvx.weights.push_back({0, 1.0f});
                
                uniqueVertices.push_back(uvx);

                int parentBone = uvx.weights[0].boneId;
                smd << parentBone << " " << sPos.x << " " << sPos.y << " " << sPos.z << " " 
                    << sNrm.x << " " << sNrm.y << " " << sNrm.z << " " << uvx.u << " " << uvx.v << " " << uvx.weights.size();
                for (const auto& w : uvx.weights) smd << " " << w.boneId << " " << w.weight;
                smd << "\n";
            }
        }
    }
    smd << "end\n";
    smd.close();
    return uniqueVertices;
}

void exportAssimpToVTA(const aiScene* scene, const std::string& filepath, float scale, const std::vector<UnrolledVertex>& baseVertices) {
    // Collect all unique morph targets across all meshes
    std::vector<std::string> morphNames;
    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        aiMesh* mesh = scene->mMeshes[m];
        for (unsigned int a = 0; a < mesh->mNumAnimMeshes; a++) {
            std::string name = mesh->mAnimMeshes[a]->mName.C_Str();
            if (name.empty()) name = "morph_" + std::to_string(a);
            if (std::find(morphNames.begin(), morphNames.end(), name) == morphNames.end()) {
                morphNames.push_back(name);
            }
        }
    }

    if (morphNames.empty()) return;

    std::ofstream vta(filepath);
    if (!vta.is_open()) return;

    vta << "nodes\n";
    for (const auto& b : bones) vta << b.id << " \"" << b.name << "\" " << b.parent_id << "\n";
    vta << "end\nskeleton\n";
    
    // Write skeleton frames (time 0 to N)
    for (size_t t = 0; t <= morphNames.size(); t++) {
        vta << "time " << t << "\n";
        for (const auto& b : bones) {
            aiVector3D pos; aiQuaternion rot; aiVector3D scl;
            b.localTransform.Decompose(scl, rot, pos);
            Vector3 sPos = toSourceCoord(pos.x * scale, pos.y * scale, pos.z * scale);
            vta << b.id << " " << sPos.x << " " << sPos.y << " " << sPos.z << " 0 0 0\n";
        }
    }
    vta << "end\nvertexanimation\n";

    // Frame 0: Base
    vta << "time 0\n";
    for (size_t i = 0; i < baseVertices.size(); i++) {
        const auto& v = baseVertices[i];
        vta << i << " " << v.pos.x << " " << v.pos.y << " " << v.pos.z << " " << v.nrm.x << " " << v.nrm.y << " " << v.nrm.z << "\n";
    }

    // Frames 1..N: Morph Targets
    for (size_t t = 0; t < morphNames.size(); t++) {
        vta << "time " << (t + 1) << "\n";
        std::string targetName = morphNames[t];
        
        size_t vOffset = 0;
        for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
            aiMesh* mesh = scene->mMeshes[m];
            aiAnimMesh* targetMesh = nullptr;
            for (unsigned int a = 0; a < mesh->mNumAnimMeshes; a++) {
                if (std::string(mesh->mAnimMeshes[a]->mName.C_Str()) == targetName) {
                    targetMesh = mesh->mAnimMeshes[a];
                    break;
                }
            }

            // Iterate faces to match SMD unrolling
            for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
                aiFace face = mesh->mFaces[f];
                if (face.mNumIndices != 3) continue;
                for (int i = 0; i < 3; i++) {
                    unsigned int vIdx = face.mIndices[i];
                    aiVector3D p = targetMesh ? targetMesh->mVertices[vIdx] : mesh->mVertices[vIdx];
                    aiVector3D n = (targetMesh && targetMesh->mNormals) ? targetMesh->mNormals[vIdx] : (mesh->mNormals ? mesh->mNormals[vIdx] : aiVector3D(0,1,0));
                    
                    Vector3 sPos = toSourceCoord(p.x * scale, p.y * scale, p.z * scale);
                    Vector3 sNrm = toSourceCoord(n.x, n.y, n.z);
                    vta << vOffset << " " << sPos.x << " " << sPos.y << " " << sPos.z << " " << sNrm.x << " " << sNrm.y << " " << sNrm.z << "\n";
                    vOffset++;
                }
            }
        }
    }
    vta << "end\n";
    vta.close();
}

void generateAdvancedQC(const std::string& mdlName, const std::string& modelsrcDir, const aiScene* scene) {
    std::string qcPath = modelsrcDir + "/" + mdlName + ".qc";
    std::ofstream qc(qcPath);
    if (!qc.is_open()) return;

    qc << "$modelname \"" << mdlName << "/" << mdlName << ".mdl\"\n";
    
    // Find morph targets
    std::vector<std::string> morphNames;
    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        for (unsigned int a = 0; a < scene->mMeshes[m]->mNumAnimMeshes; a++) {
            std::string name = scene->mMeshes[m]->mAnimMeshes[a]->mName.C_Str();
            if (name.empty()) name = "morph_" + std::to_string(a);
            if (std::find(morphNames.begin(), morphNames.end(), name) == morphNames.end()) morphNames.push_back(name);
        }
    }

    if (morphNames.empty()) {
        qc << "$body \"body\" \"" << mdlName << "_ref.smd\"\n";
    } else {
        qc << "$model \"" << mdlName << "\" \"" << mdlName << "_ref.smd\" {\n";
        qc << "\tflexfile \"" << mdlName << ".vta\" {\n\t\tdefaultflex frame 0\n";
        for (size_t i = 0; i < morphNames.size(); i++) {
            std::string fName = morphNames[i];
            for (char& c : fName) if (c == ' ' || c == '-') c = '_';
            qc << "\t\tflex \"" << fName << "\" frame " << (i + 1) << "\n";
        }
        qc << "\t}\n";
        for (size_t i = 0; i < morphNames.size(); i++) {
            std::string fName = morphNames[i];
            for (char& c : fName) if (c == ' ' || c == '-') c = '_';
            qc << "\tflexcontroller \"phoneme\" \"" << fName << "\" \"range\" 0 1\n";
        }
        qc << "}\n";
    }

    qc << "$cdmaterials \"models/" << mdlName << "/\"\n";
    qc << "$sequence \"idle\" \"" << mdlName << "_ref.smd\" loop fps 30\n";
    qc.close();
}

void generateBasicVMTs(const aiScene* scene, const std::string& matsDir, const std::string& mdlName) {
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        aiMaterial* mat = scene->mMaterials[i];
        aiString name;
        std::string matName = "material_" + std::to_string(i);
        if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
            matName = name.C_Str();
            for (char& c : matName) if (c == ' ') c = '_';
        }

        std::ofstream vmt(matsDir + "/" + matName + ".vmt");
        vmt << "\"VertexLitGeneric\"\n{\n";
        vmt << "\t\"$basetexture\" \"models/" << mdlName << "/texture_placeholder\"\n";
        vmt << "\t\"$nocull\" 1\n";
        vmt << "\t\"$halflambert\" 1\n";
        vmt << "}\n";
    }
}

extern "C" {
    VRM2MDL_API const char* vrm2mdl_version() {
        return VRM2MDL_VERSION;
    }

    VRM2MDL_API int vrm2mdl_convert(
        const char* file_path,
        const char* output_dir_path,
        float scale,
        LogCallback log_cb,
        void* user_data
    ) {
        if (!file_path || !output_dir_path) return 1;

        std::string filename = file_path;
        std::string outputDir = output_dir_path;

        std::string mdlName = filename;
        size_t ls = mdlName.find_last_of("/\\");
        if (ls != std::string::npos) mdlName = mdlName.substr(ls + 1);
        size_t ld = mdlName.find_last_of(".");
        if (ld != std::string::npos) mdlName = mdlName.substr(0, ld);
        for (char &c : mdlName) if (c == ' ') c = '_';

        logMessage(log_cb, user_data, "Loading file via Assimp: %s", filename.c_str());

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(filename, 
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            logMessage(log_cb, user_data, "[ERROR] Assimp failed to load file: %s", importer.GetErrorString());
            return 1;
        }

        std::string baseDir = outputDir + "/" + mdlName;
        std::string modelsrcDir = baseDir + "/modelsrc";
        std::string matsDir = baseDir + "/materials/models/" + mdlName;
        
        try {
            fs::create_directories(modelsrcDir);
            fs::create_directories(matsDir);
        } catch (const fs::filesystem_error& e) {
            logMessage(log_cb, user_data, "[ERROR] Failed to create directories: %s", e.what());
            return 1;
        }

        logMessage(log_cb, user_data, "[1/3] Parsing skeleton...");
        bones.clear();
        boneNameToId.clear();
        buildSkeleton(scene->mRootNode, -1);

        logMessage(log_cb, user_data, "[1/3] Exporting Reference SMD...");
        std::vector<UnrolledVertex> baseVertices = exportAssimpToSMD(scene, modelsrcDir + "/" + mdlName + "_ref.smd", scale);

        logMessage(log_cb, user_data, "[1/3] Exporting Facial VTA (if any)...");
        exportAssimpToVTA(scene, modelsrcDir + "/" + mdlName + ".vta", scale, baseVertices);

        logMessage(log_cb, user_data, "[1/3] Generating QC script...");
        generateAdvancedQC(mdlName, modelsrcDir, scene);

        logMessage(log_cb, user_data, "[1/3] Generating Materials...");
        generateBasicVMTs(scene, matsDir, mdlName);

        logMessage(log_cb, user_data, "[OK] Core Assimp Conversion Complete.");
        return 0;
    }
}
