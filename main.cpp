#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "include/tiny_gltf.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sys/stat.h>
#include <algorithm>
#include <limits>
#include <cctype>
#include <filesystem>
#include <stdarg.h>

#include "include/vrm2mdl.h"

#define VRM2MDL_VERSION "4.1.0"

namespace fs = std::filesystem;

// Mapping VRM names to ValveBiped names
std::map<std::string, std::string> boneMap = {
    {"hips", "ValveBiped.Bip01_Pelvis"}, {"spine", "ValveBiped.Bip01_Spine"},
    {"chest", "ValveBiped.Bip01_Spine1"}, {"upperChest", "ValveBiped.Bip01_Spine2"},
    {"neck", "ValveBiped.Bip01_Neck1"}, {"head", "ValveBiped.Bip01_Head1"},
    {"leftUpperArm", "ValveBiped.Bip01_L_UpperArm"}, {"leftLowerArm", "ValveBiped.Bip01_L_Forearm"},
    {"leftHand", "ValveBiped.Bip01_L_Hand"}, {"rightUpperArm", "ValveBiped.Bip01_R_UpperArm"},
    {"rightLowerArm", "ValveBiped.Bip01_R_Forearm"}, {"rightHand", "ValveBiped.Bip01_R_Hand"},
    {"leftUpperLeg", "ValveBiped.Bip01_L_Thigh"}, {"leftLowerLeg", "ValveBiped.Bip01_L_Calf"},
    {"leftFoot", "ValveBiped.Bip01_L_Foot"}, {"rightUpperLeg", "ValveBiped.Bip01_R_Thigh"},
    {"rightLowerLeg", "ValveBiped.Bip01_R_Calf"}, {"rightFoot", "ValveBiped.Bip01_R_Foot"},
    {"leftEye", "ValveBiped.Bip01_L_Eye"}, {"rightEye", "ValveBiped.Bip01_R_Eye"},
    {"leftThumbProximal", "ValveBiped.Bip01_L_Finger0"}, {"leftThumbIntermediate", "ValveBiped.Bip01_L_Finger01"}, {"leftThumbDistal", "ValveBiped.Bip01_L_Finger02"},
    {"leftIndexProximal", "ValveBiped.Bip01_L_Finger1"}, {"leftIndexIntermediate", "ValveBiped.Bip01_L_Finger11"}, {"leftIndexDistal", "ValveBiped.Bip01_L_Finger12"},
    {"leftMiddleProximal", "ValveBiped.Bip01_L_Finger2"}, {"leftMiddleIntermediate", "ValveBiped.Bip01_L_Finger21"}, {"leftMiddleDistal", "ValveBiped.Bip01_L_Finger22"},
    {"leftRingProximal", "ValveBiped.Bip01_L_Finger3"}, {"leftRingIntermediate", "ValveBiped.Bip01_L_Finger31"}, {"leftRingDistal", "ValveBiped.Bip01_L_Finger32"},
    {"leftLittleProximal", "ValveBiped.Bip01_L_Finger4"}, {"leftLittleIntermediate", "ValveBiped.Bip01_L_Finger41"}, {"leftLittleDistal", "ValveBiped.Bip01_L_Finger42"},
    {"rightThumbProximal", "ValveBiped.Bip01_R_Finger0"}, {"rightThumbIntermediate", "ValveBiped.Bip01_R_Finger01"}, {"rightThumbDistal", "ValveBiped.Bip01_R_Finger02"},
    {"rightIndexProximal", "ValveBiped.Bip01_R_Finger1"}, {"rightIndexIntermediate", "ValveBiped.Bip01_R_Finger11"}, {"rightIndexDistal", "ValveBiped.Bip01_R_Finger12"},
    {"rightMiddleProximal", "ValveBiped.Bip01_R_Finger2"}, {"rightMiddleIntermediate", "ValveBiped.Bip01_R_Finger21"}, {"rightMiddleDistal", "ValveBiped.Bip01_R_Finger22"},
    {"rightRingProximal", "ValveBiped.Bip01_R_Finger3"}, {"rightRingIntermediate", "ValveBiped.Bip01_R_Finger31"}, {"rightRingDistal", "ValveBiped.Bip01_R_Finger32"},
    {"rightLittleProximal", "ValveBiped.Bip01_R_Finger4"}, {"rightLittleIntermediate", "ValveBiped.Bip01_R_Finger41"}, {"rightLittleDistal", "ValveBiped.Bip01_R_Finger42"}
};

template<typename T>
const T* getBufferPtr(const tinygltf::Model& model, int accessorIdx) {
    if (accessorIdx < 0) return nullptr;
    const auto& accessor = model.accessors[accessorIdx];
    const auto& bufferView = model.bufferViews[accessor.bufferView];
    return reinterpret_cast<const T*>(&(model.buffers[bufferView.buffer].data[bufferView.byteOffset + accessor.byteOffset]));
}

// Helper: read JOINTS_0 supporting both UNSIGNED_BYTE and UNSIGNED_SHORT (Bug #1 fix)
std::vector<unsigned short> readJoints(const tinygltf::Model& model, int accessorIdx) {
    std::vector<unsigned short> result;
    if (accessorIdx < 0) return result;
    const auto& accessor = model.accessors[accessorIdx];
    const auto& bufferView = model.bufferViews[accessor.bufferView];
    const unsigned char* base = &(model.buffers[bufferView.buffer].data[bufferView.byteOffset + accessor.byteOffset]);
    size_t count = accessor.count * 4; // VEC4
    result.resize(count);
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        for (size_t i = 0; i < count; i++) result[i] = static_cast<unsigned short>(base[i]);
    } else { // UNSIGNED_SHORT
        const unsigned short* ptr = reinterpret_cast<const unsigned short*>(base);
        for (size_t i = 0; i < count; i++) result[i] = ptr[i];
    }
    return result;
}

struct Vector3 { float x, y, z; };
Vector3 toSourceCoord(float x, float y, float z) { return { z, -x, y }; }

struct BBox {
    float min[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float max[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    void add(float x, float y, float z) {
        min[0] = std::min(min[0], x); min[1] = std::min(min[1], y); min[2] = std::min(min[2], z);
        max[0] = std::max(max[0], x); max[1] = std::max(max[1], y); max[2] = std::max(max[2], z);
    }
};

struct VRMBind { int mesh; int index; float weight; };
struct VRMExpression { std::string name; std::vector<VRMBind> binds; };
struct VRMSpringJoint { int node; float stiffness; float dragForce; float gravityPower; };

float globalScale = 39.37f;
std::map<int, BBox> boneBBoxes;

struct UnrolledVertex {
    int primIdx;
    int origVtxIdx;
    Vector3 pos;
    Vector3 nrm;
    float u, v;
    std::vector<std::pair<int, float>> weights;
};

void parseVRMExpressions(const tinygltf::Model& model, std::vector<VRMExpression>& outExpressions) {
    auto vrm0it = model.extensions.find("VRM");
    if (vrm0it != model.extensions.end()) {
        const auto& vrm = vrm0it->second;
        if (vrm.Has("blendShapeMaster") && vrm.Get("blendShapeMaster").Has("blendShapeGroups")) {
            const auto& groups = vrm.Get("blendShapeMaster").Get("blendShapeGroups");
            for (size_t i = 0; i < groups.ArrayLen(); i++) {
                const auto& group = groups.Get(i);
                VRMExpression expr; expr.name = group.Get("name").Get<std::string>();
                if (group.Has("binds")) {
                    const auto& binds = group.Get("binds");
                    for (size_t j = 0; j < binds.ArrayLen(); j++) {
                        const auto& b = binds.Get(j);
                        VRMBind bind; bind.mesh = b.Get("mesh").GetNumberAsInt(); bind.index = b.Get("index").GetNumberAsInt(); bind.weight = (float)b.Get("weight").GetNumberAsDouble() / 100.0f;
                        expr.binds.push_back(bind);
                    }
                }
                outExpressions.push_back(expr);
            }
        }
    }
    auto vrm1it = model.extensions.find("VRMC_vrm");
    if (vrm1it != model.extensions.end()) {
        const auto& vrm = vrm1it->second;
        if (vrm.Has("expressions")) {
            const auto& exprs = vrm.Get("expressions");
            std::vector<std::string> types = {"preset", "custom"};
            for (const auto& type : types) {
                if (exprs.Has(type)) {
                    const auto& typeObj = exprs.Get(type);
                    for (const auto& key : typeObj.Keys()) {
                        const auto& group = typeObj.Get(key);
                        VRMExpression expr; expr.name = key;
                        if (group.Has("morphTargetBinds")) {
                            const auto& binds = group.Get("morphTargetBinds");
                            for (size_t j = 0; j < binds.ArrayLen(); j++) {
                                const auto& b = binds.Get(j);
                                VRMBind bind; bind.mesh = -1;
                                int nodeIdx = b.Get("node").GetNumberAsInt();
                                for (size_t m = 0; m < model.meshes.size(); m++) {
                                    for (size_t n = 0; n < model.nodes.size(); n++) {
                                        if (model.nodes[n].mesh == (int)m && (int)n == nodeIdx) { bind.mesh = (int)m; break; }
                                    }
                                    if (bind.mesh != -1) break;
                                }
                                bind.index = b.Get("index").GetNumberAsInt(); bind.weight = (float)b.Get("weight").GetNumberAsDouble();
                                if (bind.mesh != -1) expr.binds.push_back(bind);
                            }
                        }
                        outExpressions.push_back(expr);
                    }
                }
            }
        }
    }

    // Fallback: Standard GLTF Morph Targets
    if (outExpressions.empty()) {
        for (size_t mIdx = 0; mIdx < model.meshes.size(); mIdx++) {
            const auto& mesh = model.meshes[mIdx];
            if (mesh.primitives.empty() || mesh.primitives[0].targets.empty()) continue;
            
            std::vector<std::string> targetNames;
            if (mesh.extras.Has("targetNames")) {
                const auto& tn = mesh.extras.Get("targetNames");
                for (size_t i = 0; i < tn.ArrayLen(); i++) targetNames.push_back(tn.Get(i).Get<std::string>());
            }

            for (size_t tIdx = 0; tIdx < mesh.primitives[0].targets.size(); tIdx++) {
                VRMExpression expr;
                if (tIdx < targetNames.size()) expr.name = targetNames[tIdx];
                else expr.name = "target_" + std::to_string(mIdx) + "_" + std::to_string(tIdx);
                
                VRMBind bind; bind.mesh = (int)mIdx; bind.index = (int)tIdx; bind.weight = 1.0f;
                expr.binds.push_back(bind);
                outExpressions.push_back(expr);
            }
        }
    }
}

void parseVRMSprings(const tinygltf::Model& model, std::vector<VRMSpringJoint>& outSprings) {
    auto vrm0it = model.extensions.find("VRM");
    if (vrm0it != model.extensions.end()) {
        const auto& vrm = vrm0it->second;
        if (vrm.Has("secondaryAnimation") && vrm.Get("secondaryAnimation").Has("boneGroups")) {
            const auto& groups = vrm.Get("secondaryAnimation").Get("boneGroups");
            for (size_t i = 0; i < groups.ArrayLen(); i++) {
                const auto& group = groups.Get(i);
                float stiffness = group.Has("stiffness") ? (float)group.Get("stiffness").GetNumberAsDouble() : 0.5f;
                float drag = group.Has("dragForce") ? (float)group.Get("dragForce").GetNumberAsDouble() : 0.5f;
                float gravity = group.Has("gravityPower") ? (float)group.Get("gravityPower").GetNumberAsDouble() : 0.0f;
                if (group.Has("bones")) {
                    for (size_t j = 0; j < group.Get("bones").ArrayLen(); j++) {
                        VRMSpringJoint sj; sj.node = group.Get("bones").Get(j).GetNumberAsInt(); sj.stiffness = stiffness; sj.dragForce = drag; sj.gravityPower = gravity;
                        outSprings.push_back(sj);
                    }
                }
            }
        }
    }
    auto vrm1it = model.extensions.find("VRMC_springBone");
    if (vrm1it != model.extensions.end()) {
        const auto& vrm = vrm1it->second;
        if (vrm.Has("springs")) {
            const auto& springs = vrm.Get("springs");
            for (size_t i = 0; i < springs.ArrayLen(); i++) {
                const auto& joints = springs.Get(i).Get("joints");
                for (size_t j = 0; j < joints.ArrayLen(); j++) {
                    const auto& joint = joints.Get(j);
                    VRMSpringJoint sj; sj.node = joint.Get("node").GetNumberAsInt();
                    sj.stiffness = joint.Has("stiffness") ? (float)joint.Get("stiffness").GetNumberAsDouble() : 1.0f;
                    sj.dragForce = joint.Has("dragForce") ? (float)joint.Get("dragForce").GetNumberAsDouble() : 0.5f;
                    sj.gravityPower = joint.Has("gravityPower") ? (float)joint.Get("gravityPower").GetNumberAsDouble() : 0.0f;
                    outSprings.push_back(sj);
                }
            }
        }
    }
}

#pragma pack(push, 1)
struct VTFHeader {
    char signature[4];          // "VTF\0"
    unsigned int version[2];    // 7, 2
    unsigned int headerSize;    // 80
    unsigned short width;
    unsigned short height;
    unsigned int flags;         // 0x00002000 (Trilinear)
    unsigned short frames;      // 1
    unsigned short firstFrame;  // 0
    unsigned char padding0[4];  // 0
    float reflectivity[3];      // 0.5f, 0.5f, 0.5f
    unsigned char padding1[4];  // 0
    float bumpScale;            // 1.0f
    unsigned int highResImageFormat; 
    unsigned char mipmapCount;  // 1
    unsigned int lowResImageFormat; // -1
    unsigned char lowResWidth;  // 0
    unsigned char lowResHeight; // 0
    unsigned short depth;       // 1
    unsigned char padding2[15]; // Pad to 80 bytes
};
#pragma pack(pop)

void writeVTF(const std::string& path, const std::vector<unsigned char>& imgData, int width, int height, int component) {
    // Cap texture size to 1024x1024 for performance
    int maxSize = 1024;
    std::vector<unsigned char> resized;
    int outW = width, outH = height;
    const unsigned char* srcData = imgData.data();
    if (width > maxSize || height > maxSize) {
        float scale = std::min((float)maxSize / width, (float)maxSize / height);
        outW = std::max(1, (int)(width * scale));
        outH = std::max(1, (int)(height * scale));
        // Round down to power of 2
        int pw = 1; while (pw * 2 <= outW) pw *= 2; outW = pw;
        int ph = 1; while (ph * 2 <= outH) ph *= 2; outH = ph;
        resized.resize(outW * outH * component);
        for (int y = 0; y < outH; y++) {
            for (int x = 0; x < outW; x++) {
                int sx = x * width / outW;
                int sy = y * height / outH;
                for (int c = 0; c < component; c++)
                    resized[(y * outW + x) * component + c] = imgData[(sy * width + sx) * component + c];
            }
        }
        srcData = resized.data();
    }

    VTFHeader h;
    memset(&h, 0, sizeof(h));
    h.signature[0] = 'V'; h.signature[1] = 'T'; h.signature[2] = 'F'; h.signature[3] = '\0';
    h.version[0] = 7; h.version[1] = 2;
    h.headerSize = 80;
    h.width = outW;
    h.height = outH;
    h.flags = 0x00000100 | 0x00000200 | 0x00002000; // NOMIP | NOLOD | Trilinear
    h.frames = 1;
    h.firstFrame = 0;
    h.reflectivity[0] = 0.5f; h.reflectivity[1] = 0.5f; h.reflectivity[2] = 0.5f;
    h.bumpScale = 1.0f;
    h.mipmapCount = 1;
    h.lowResImageFormat = 0xFFFFFFFF;
    h.depth = 1;

    size_t pixelCount = outW * outH * component;
    std::vector<unsigned char> pixels(srcData, srcData + pixelCount);
    if (component == 4) {
        h.highResImageFormat = 12; // BGRA8888
        for (size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i+2]); // Swap R and B
        }
    } else if (component == 3) {
        h.highResImageFormat = 3; // BGR888
        for (size_t i = 0; i < pixels.size(); i += 3) {
            std::swap(pixels[i], pixels[i+2]); // Swap R and B
        }
    } else {
        std::cerr << "[WARN] Unsupported texture component: " << component << " for VTF export." << std::endl;
        return;
    }

    std::ofstream out(path, std::ios::binary);
    out.write((char*)&h, sizeof(h));
    out.write((char*)pixels.data(), pixels.size());
    out.close();
}

void generateVMT(const tinygltf::Model& model, const std::string& mdlName, const std::string& matsDir) {
    for (const auto& mat : model.materials) {
        std::string matName = mat.name.empty() ? "material_default" : mat.name;
        for (char &c : matName) if (c == ' ') c = '_';
        std::ofstream vmt(matsDir + "/" + matName + ".vmt");

        // Detect material type from name
        std::string nameLower = matName;
        for (char &c : nameLower) c = std::tolower(c);
        bool isEyeHighlight = nameLower.find("eyehighlight") != std::string::npos;
        bool isEye = nameLower.find("eye") != std::string::npos;
        bool isFace = nameLower.find("face") != std::string::npos || nameLower.find("mouth") != std::string::npos;
        bool isBrow = nameLower.find("brow") != std::string::npos || nameLower.find("eyeline") != std::string::npos;
        bool isHair = nameLower.find("hair") != std::string::npos;

        // Check VRM alphaMode
        bool needsAlpha = false;
        if (mat.alphaMode == "BLEND" || mat.alphaMode == "MASK") needsAlpha = true;

        vmt << "\"VertexLitGeneric\"\n{\n";

        // $basetexture
        int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (texIdx >= 0) vmt << "\t\"$basetexture\" \"models/" << mdlName << "/" << mdlName << "_tex_" << model.textures[texIdx].source << "\"\n";

        // $bumpmap — skip if normal map texture is too small (placeholder)
        int normIdx = mat.normalTexture.index;
        if (normIdx >= 0) {
            int normSrc = model.textures[normIdx].source;
            if (normSrc >= 0 && normSrc < (int)model.images.size()) {
                int nw = model.images[normSrc].width;
                int nh = model.images[normSrc].height;
                if (nw >= 32 && nh >= 32) {
                    vmt << "\t\"$bumpmap\" \"models/" << mdlName << "/" << mdlName << "_tex_" << normSrc << "\"\n";
                }
            }
        }

        // Transparency handling & Culling
        // Anime models rely heavily on single-sided geometry (clothes/hair)
        // Removing $nocull causes the model to look terrifying or hollow.
        if (isEyeHighlight) {
            vmt << "\t\"$additive\" 1\n";
            vmt << "\t\"$nocull\" 1\n";
        } else if (mat.alphaMode == "BLEND") {
            vmt << "\t\"$translucent\" 1\n";
            vmt << "\t\"$nocull\" 1\n";
        } else if (mat.alphaMode == "MASK") {
            double cutoff = mat.alphaCutoff;
            if (cutoff <= 0.0) cutoff = 0.5;
            vmt << "\t\"$alphatest\" 1\n";
            vmt << "\t\"$alphatestreference\" \"" << cutoff << "\"\n";
            vmt << "\t\"$nocull\" 1\n";
        } else {
            vmt << "\t\"$nocull\" 1\n";
        }

        // Lighting — flat/anime style
        vmt << "\t\"$halflambert\" 1\n";
        vmt << "\t\"$ambientocclusion\" 0\n"; // Disable SSAO shadows to keep anime look
        
        // Fix for "Black Model" bug: If a normal map ($bumpmap) is present, Source Engine 
        // often requires $phong to calculate lighting properly, otherwise it renders pitch black.
        // We set it to extremely low values so it doesn't look like shiny plastic.
        if (!isEyeHighlight && !isFace && !isEye) {
            vmt << "\t\"$phong\" 1\n";
            vmt << "\t\"$phongboost\" \"0.01\"\n";
            vmt << "\t\"$phongexponent\" \"1\"\n";
            vmt << "\t\"$phongfresnelranges\" \"[0.5 0.5 1]\"\n";
        }

        vmt << "\t\"$surfaceprop\" \"flesh\"\n";
        vmt << "}\n";
    }
}

std::vector<UnrolledVertex> exportToSMD(const tinygltf::Model& model, const std::string& outFilename, int targetMeshIdx) {
    std::vector<UnrolledVertex> uniqueVertices;
    std::ofstream smd(outFilename); if (!smd.is_open()) return uniqueVertices;
    smd << "version 1\nnodes\n";
    for (size_t i = 0; i < model.nodes.size(); i++) {
        int parent = -1;
        for (size_t j = 0; j < model.nodes.size(); j++) {
            for (int childIdx : model.nodes[j].children) { if (childIdx == (int)i) { parent = (int)j; break; } }
            if (parent != -1) break;
        }
        std::string mappedName = boneMap.count(model.nodes[i].name) ? boneMap[model.nodes[i].name] : (model.nodes[i].name.empty() ? "bone_"+std::to_string(i) : model.nodes[i].name);
        for (char &c : mappedName) if (c == ' ') c = '_';
        smd << i << " \"" << mappedName << "\" " << parent << "\n";
    }
    smd << "end\nskeleton\ntime 0\n";
    for (size_t i = 0; i < model.nodes.size(); i++) {
        const auto& node = model.nodes[i];
        float tx = 0, ty = 0, tz = 0;
        if (node.translation.size() == 3) { tx = (float)node.translation[0]; ty = (float)node.translation[1]; tz = (float)node.translation[2]; }
        Vector3 sPos = toSourceCoord(tx * globalScale, ty * globalScale, tz * globalScale);
        smd << i << " " << sPos.x << " " << sPos.y << " " << sPos.z << " 0 0 0\n";
    }
    smd << "end\ntriangles\n";
    
    if (targetMeshIdx < 0 || targetMeshIdx >= (int)model.meshes.size()) { smd << "end\n"; return uniqueVertices; }
    
    int mIdx = targetMeshIdx;
    std::vector<int> triangleIndices;
    std::vector<int> triangleMaterials;
    std::map<std::string, int> vertexMap;
    
    for (size_t pIdx = 0; pIdx < model.meshes[mIdx].primitives.size(); pIdx++) {
        const auto& prim = model.meshes[mIdx].primitives[pIdx];
        const float *pos = getBufferPtr<float>(model, prim.attributes.at("POSITION"));
        const float *nrm = prim.attributes.count("NORMAL") ? getBufferPtr<float>(model, prim.attributes.at("NORMAL")) : nullptr;
        const float *uv = prim.attributes.count("TEXCOORD_0") ? getBufferPtr<float>(model, prim.attributes.at("TEXCOORD_0")) : nullptr;
        const float *weights = prim.attributes.count("WEIGHTS_0") ? getBufferPtr<float>(model, prim.attributes.at("WEIGHTS_0")) : nullptr;
        std::vector<unsigned short> jointsVec = prim.attributes.count("JOINTS_0") ? readJoints(model, prim.attributes.at("JOINTS_0")) : std::vector<unsigned short>();
        const unsigned short* joints = jointsVec.empty() ? nullptr : jointsVec.data();
        const auto& posAcc = model.accessors[prim.attributes.at("POSITION")];
        std::vector<unsigned int> indices;
        if (prim.indices >= 0) {
            const auto& idxAcc = model.accessors[prim.indices];
            if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const unsigned short* idxPtr = getBufferPtr<unsigned short>(model, prim.indices);
                for (size_t i = 0; i < idxAcc.count; i++) indices.push_back(idxPtr[i]);
            } else { const unsigned int* idxPtr = getBufferPtr<unsigned int>(model, prim.indices); for (size_t i = 0; i < idxAcc.count; i++) indices.push_back(idxPtr[i]); }
        } else { for (size_t i = 0; i < posAcc.count; i++) indices.push_back(i); }
        for (size_t i = 0; i < indices.size(); i++) {
            unsigned int idx = indices[i];
            UnrolledVertex uvx;
            uvx.primIdx = pIdx; uvx.origVtxIdx = idx;
            uvx.pos = toSourceCoord(pos[idx*3] * globalScale, pos[idx*3+1] * globalScale, pos[idx*3+2] * globalScale);
            if (nrm) uvx.nrm = toSourceCoord(nrm[idx*3], nrm[idx*3+1], nrm[idx*3+2]); else uvx.nrm = {0,0,1};
            if (uv) { uvx.u = uv[idx*2]; uvx.v = 1.0f - uv[idx*2+1]; } else { uvx.u = 0; uvx.v = 0; }
            if (joints && weights) {
                for (int k = 0; k < 4; k++) {
                    if (weights[idx*4+k] > 0.01f) {
                        uvx.weights.push_back({(int)joints[idx*4+k], weights[idx*4+k]});
                        boneBBoxes[(int)joints[idx*4+k]].add(pos[idx*3], pos[idx*3+1], pos[idx*3+2]);
                    }
                }
            }
            if (uvx.weights.empty()) uvx.weights.push_back({0, 1.0f});
            
            std::ostringstream ss;
            ss << uvx.pos.x << " " << uvx.pos.y << " " << uvx.pos.z << " " 
               << uvx.nrm.x << " " << uvx.nrm.y << " " << uvx.nrm.z << " " << uvx.u << " " << uvx.v;
            
            std::string key = ss.str();
            if (vertexMap.find(key) == vertexMap.end()) {
                vertexMap[key] = uniqueVertices.size();
                uniqueVertices.push_back(uvx);
            }
            triangleIndices.push_back(vertexMap[key]);
            if (i % 3 == 0) triangleMaterials.push_back(prim.material);
        }
    }
    for (size_t i = 0; i < triangleIndices.size(); i += 3) {
        int matIdx = triangleMaterials[i / 3];
        std::string matName = (matIdx >= 0) ? model.materials[matIdx].name : "material_default";
        for (char &c : matName) if (c == ' ') c = '_';
        smd << matName << "\n";
        for (int v = 0; v < 3; v++) {
            const auto& uvx = uniqueVertices[triangleIndices[i + v]];
            smd << uvx.weights[0].first << " " << uvx.pos.x << " " << uvx.pos.y << " " << uvx.pos.z << " " 
                << uvx.nrm.x << " " << uvx.nrm.y << " " << uvx.nrm.z << " " << uvx.u << " " << uvx.v << " " << uvx.weights.size();
            for (const auto& w : uvx.weights) smd << " " << w.first << " " << w.second;
            smd << "\n";
        }
    }
    smd << "end\n";
    return uniqueVertices;
}

void exportPhysMesh(const tinygltf::Model& model, const std::string& outFilename) {
    std::ofstream smd(outFilename); if (!smd.is_open()) return;
    smd << "version 1\nnodes\n";
    for (size_t i = 0; i < model.nodes.size(); i++) {
        int p = -1;
        for (size_t j = 0; j < model.nodes.size(); j++) { for (int cIdx : model.nodes[j].children) { if (cIdx == (int)i) { p = (int)j; break; } } if (p != -1) break; }
        std::string n = boneMap.count(model.nodes[i].name) ? boneMap[model.nodes[i].name] : (model.nodes[i].name.empty() ? "bone_"+std::to_string(i) : model.nodes[i].name);
        for (char &c : n) if (c == ' ') c = '_';
        smd << i << " \"" << n << "\" " << p << "\n";
    }
    smd << "end\nskeleton\ntime 0\n";
    for (size_t i = 0; i < model.nodes.size(); i++) {
        float tx=0, ty=0, tz=0; if (model.nodes[i].translation.size()==3) { tx=(float)model.nodes[i].translation[0]; ty=(float)model.nodes[i].translation[1]; tz=(float)model.nodes[i].translation[2]; }
        Vector3 s = toSourceCoord(tx*globalScale, ty*globalScale, tz*globalScale); smd << i << " " << s.x << " " << s.y << " " << s.z << " 0 0 0\n";
    }
    smd << "end\ntriangles\n";
    for (auto it = boneBBoxes.begin(); it != boneBBoxes.end(); ++it) {
        int bIdx = it->first; const BBox& box = it->second; if (box.min[0] > box.max[0]) continue;
        Vector3 c[8];
        for (int i=0; i<8; i++) { Vector3 s = toSourceCoord((i&1?box.max[0]:box.min[0])*globalScale, (i&2?box.max[1]:box.min[1])*globalScale, (i&4?box.max[2]:box.min[2])*globalScale); c[i] = s; }
        auto w = [&](int a, int b, int d) { smd << "phys\n"; for (int idx : {a,b,d}) smd << bIdx << " " << c[idx].x << " " << c[idx].y << " " << c[idx].z << " 0 0 1 0 0 1 1 " << bIdx << " 1.0\n"; };
        w(0,1,3); w(0,3,2); w(4,5,7); w(4,7,6); w(0,1,5); w(0,5,4); w(2,3,7); w(2,7,6); w(0,2,6); w(0,6,4); w(1,3,7); w(1,7,5);
    }
    smd << "end\n";
}

// Standard Flex Mapping for Source Engine
std::map<std::string, std::string> flexMap = {
    {"A", "phoneme_aa"}, {"I", "phoneme_iy"}, {"U", "phoneme_uw"}, {"E", "phoneme_eh"}, {"O", "phoneme_ow"},
    {"Blink", "blink"}, {"Blink_L", "blink_l"}, {"Blink_R", "blink_r"},
    {"Joy", "happy"}, {"Angry", "angry"}, {"Sorrow", "sad"}, {"Fun", "smile"}
};

void exportToVTA(const tinygltf::Model& model, const std::string& vtaFilename, const std::vector<VRMExpression>& exprs, int targetMeshIdx, const std::vector<UnrolledVertex>& uniqueVertices) {
    if (targetMeshIdx < 0 || targetMeshIdx >= (int)model.meshes.size()) return;
    std::ofstream vta(vtaFilename); if (!vta.is_open()) return;
    
    // Write nodes - MUST match SMD exactly
    vta << "nodes\n";
    for (size_t i = 0; i < model.nodes.size(); i++) {
        int parent = -1;
        for (size_t j = 0; j < model.nodes.size(); j++) {
            for (int childIdx : model.nodes[j].children) { if (childIdx == (int)i) { parent = (int)j; break; } }
            if (parent != -1) break;
        }
        std::string mappedName = boneMap.count(model.nodes[i].name) ? boneMap[model.nodes[i].name] : (model.nodes[i].name.empty() ? "bone_"+std::to_string(i) : model.nodes[i].name);
        for (char &c : mappedName) if (c == ' ') c = '_';
        vta << i << " \"" << mappedName << "\" " << parent << "\n";
    }
    vta << "end\n";

    // skeleton section
    vta << "skeleton\n";
    auto writeSkeletonFrame = [&](int time) {
        vta << "time " << time << "\n";
        for (size_t i = 0; i < model.nodes.size(); i++) {
            const auto& node = model.nodes[i];
            float tx = 0, ty = 0, tz = 0;
            if (node.translation.size() == 3) { tx = (float)node.translation[0]; ty = (float)node.translation[1]; tz = (float)node.translation[2]; }
            Vector3 sPos = toSourceCoord(tx * globalScale, ty * globalScale, tz * globalScale);
            vta << i << " " << sPos.x << " " << sPos.y << " " << sPos.z << " 0 0 0\n";
        }
    };
    for (size_t i = 0; i <= exprs.size(); i++) writeSkeletonFrame(i);
    vta << "end\n";

    // vertexanimation section: Write EVERY vertex for EVERY frame
    vta << "vertexanimation\ntime 0\n";
    for (size_t i = 0; i < uniqueVertices.size(); i++) {
        const auto& uvx = uniqueVertices[i];
        vta << i << " " << uvx.pos.x << " " << uvx.pos.y << " " << uvx.pos.z << " " << uvx.nrm.x << " " << uvx.nrm.y << " " << uvx.nrm.z << "\n";
    }

    for (size_t eIdx = 0; eIdx < exprs.size(); eIdx++) {
        vta << "time " << (eIdx + 1) << "\n";
        for (size_t i = 0; i < uniqueVertices.size(); i++) {
            const auto& uvx = uniqueVertices[i];
            Vector3 mPos = uvx.pos; Vector3 mNrm = uvx.nrm;
            
            for (const auto& bind : exprs[eIdx].binds) {
                if (bind.mesh == targetMeshIdx) {
                    const auto& prim = model.meshes[targetMeshIdx].primitives[uvx.primIdx];
                    if (bind.index >= 0 && bind.index < (int)prim.targets.size()) {
                        const float* deltaPos = getBufferPtr<float>(model, prim.targets[bind.index].at("POSITION"));
                        float dx = deltaPos[uvx.origVtxIdx*3], dy = deltaPos[uvx.origVtxIdx*3+1], dz = deltaPos[uvx.origVtxIdx*3+2];
                        if (dx != 0 || dy != 0 || dz != 0) {
                            Vector3 sDelta = toSourceCoord(dx * globalScale, dy * globalScale, dz * globalScale);
                            mPos.x += sDelta.x * bind.weight; mPos.y += sDelta.y * bind.weight; mPos.z += sDelta.z * bind.weight;
                        }
                    }
                }
            }
            vta << i << " " << mPos.x << " " << mPos.y << " " << mPos.z << " " << mNrm.x << " " << mNrm.y << " " << mNrm.z << "\n";
        }
    }
    vta << "end\n";
}

void generateQC(const std::string& mdlName, const std::vector<VRMExpression>& exprs, const std::vector<VRMSpringJoint>& springs, const tinygltf::Model& model, const std::string& outDir = ".") {
    std::ofstream qc(outDir + "/" + mdlName + ".qc");
    qc << "$modelname \"" << mdlName << "/" << mdlName << ".mdl\"\n";
    std::set<int> morphedMeshes;
    for (const auto& expr : exprs) for (const auto& bind : expr.binds) morphedMeshes.insert(bind.mesh);
    
    for (size_t mIdx = 0; mIdx < model.meshes.size(); mIdx++) {
        std::string partName = mdlName + "_part" + std::to_string(mIdx);
        std::string mName = model.meshes[mIdx].name.empty() ? "part_" + std::to_string(mIdx) : model.meshes[mIdx].name;
        for (char &c : mName) if (c == ' ') c = '_';
        
        if (morphedMeshes.count(mIdx)) {
            qc << "$model \"" << mName << "\" \"" << partName << ".smd\" {\n";
            qc << "\tflexfile \"" << partName << ".vta\" {\n\t\tdefaultflex frame 0\n";
            for (size_t i = 0; i < exprs.size(); i++) {
                std::string fName = flexMap.count(exprs[i].name) ? flexMap[exprs[i].name] : exprs[i].name;
                for (char &c : fName) if (c == ' ' || c == '-') c = '_';
                qc << "\t\tflex \"" << fName << "\" frame " << (i+1) << "\n";
            }
            qc << "\t}\n";
            for (size_t i = 0; i < exprs.size(); i++) {
                std::string fName = flexMap.count(exprs[i].name) ? flexMap[exprs[i].name] : exprs[i].name;
                for (char &c : fName) if (c == ' ' || c == '-') c = '_';
                qc << "\tflexcontroller \"phoneme\" \"" << fName << "\" \"range\" 0 1\n";
            }
            qc << "}\n";
        } else {
            qc << "$bodygroup \"" << mName << "\"\n{\n\tstudio \"" << partName << ".smd\"\n\tblank\n}\n";
        }
    }
    // Resolve actual bone names for attachments (use boneMap if VRM humanoid names match, otherwise use raw names)
    auto resolveBone = [&](const std::string& vrmName) -> std::string {
        // First check boneMap for humanoid name
        if (boneMap.count(vrmName)) return boneMap[vrmName];
        // Otherwise search model nodes for the raw name
        for (int i = 0; i < (int)model.nodes.size(); i++) {
            if (model.nodes[i].name == vrmName) {
                std::string n = model.nodes[i].name;
                for (char &c : n) if (c == ' ') c = '_';
                return n;
            }
        }
        return vrmName;
    };
    // Find head bone: try humanoid "head" first, then VRM "J_Bip_C_Head"
    std::string headBone = "J_Bip_C_Head";
    std::string lHandBone = "J_Bip_L_Hand";
    std::string rHandBone = "J_Bip_R_Hand";
    for (int i = 0; i < (int)model.nodes.size(); i++) {
        const auto& nn = model.nodes[i].name;
        if (nn == "head" || nn == "J_Bip_C_Head") { headBone = boneMap.count(nn) ? boneMap[nn] : nn; }
        if (nn == "leftHand" || nn == "J_Bip_L_Hand") { lHandBone = boneMap.count(nn) ? boneMap[nn] : nn; }
        if (nn == "rightHand" || nn == "J_Bip_R_Hand") { rHandBone = boneMap.count(nn) ? boneMap[nn] : nn; }
    }
    int hn = -1, leIdx = -1, reIdx = -1;
    for (int i = 0; i < (int)model.nodes.size(); i++) { if (model.nodes[i].name == "head" || model.nodes[i].name == "J_Bip_C_Head") hn = i; if (model.nodes[i].name == "leftEye" || model.nodes[i].name == "J_Bip_L_Eye") leIdx = i; if (model.nodes[i].name == "rightEye" || model.nodes[i].name == "J_Bip_R_Eye") reIdx = i; }
    if (hn != -1 && leIdx != -1 && reIdx != -1) {
        auto le = model.nodes[leIdx]; auto re = model.nodes[reIdx];
        if (le.translation.size() == 3 && re.translation.size() == 3) {
            Vector3 sL = toSourceCoord((float)le.translation[0]*globalScale, (float)le.translation[1]*globalScale, (float)le.translation[2]*globalScale);
            Vector3 sR = toSourceCoord((float)re.translation[0]*globalScale, (float)re.translation[1]*globalScale, (float)re.translation[2]*globalScale);
            qc << "$attachment \"righteye\" \"" << headBone << "\" " << sR.x << " " << sR.y << " " << sR.z << " absolute\n";
            qc << "$attachment \"lefteye\" \"" << headBone << "\" " << sL.x << " " << sL.y << " " << sL.z << " absolute\n";
        }
    }
    qc << "$attachment \"eyes\" \"" << headBone << "\" 0 0 0 absolute\n";
    qc << "$attachment \"hand_L\" \"" << lHandBone << "\" 0 0 0 absolute\n";
    qc << "$attachment \"hand_R\" \"" << rHandBone << "\" 0 0 0 absolute\n";
    for (auto it = boneBBoxes.begin(); it != boneBBoxes.end(); ++it) {
        if (it->first < 0 || it->first >= (int)model.nodes.size()) continue;
        std::string n = boneMap.count(model.nodes[it->first].name) ? boneMap[model.nodes[it->first].name] : model.nodes[it->first].name; if (n.empty()) continue; for (char &c : n) if (c == ' ') c = '_';
        Vector3 minS = toSourceCoord(it->second.min[0]*globalScale, it->second.min[1]*globalScale, it->second.min[2]*globalScale);
        Vector3 maxS = toSourceCoord(it->second.max[0]*globalScale, it->second.max[1]*globalScale, it->second.max[2]*globalScale);
        qc << "$hbox 0 \"" << n << "\" " << std::min(minS.x,maxS.x) << " " << std::min(minS.y,maxS.y) << " " << std::min(minS.z,maxS.z) << " " << std::max(minS.x,maxS.x) << " " << std::max(minS.y,maxS.y) << " " << std::max(minS.z,maxS.z) << "\n";
    }
    // Limit jigglebones to max 12 for stability
    int jiggleCount = 0;
    const int maxJigglebones = 12;
    for (const auto& sj : springs) {
        if (jiggleCount >= maxJigglebones) break;
        if (sj.node < 0 || sj.node >= (int)model.nodes.size()) continue;
        std::string n = boneMap.count(model.nodes[sj.node].name) ? boneMap[model.nodes[sj.node].name] : model.nodes[sj.node].name; for (char &c : n) if (c == ' ') c = '_';
        float stiff = std::min(1000.0f, sj.stiffness * 250.0f);
        float damp = std::min(10.0f, sj.dragForce * 10.0f);
        float mass = std::min(1000.0f, sj.gravityPower * 100.0f);
        qc << "$jigglebone \"" << n << "\" {\n\tis_flexible {\n\t\tyaw_stiffness " << stiff << "\n\t\tyaw_damping " << damp << "\n\t\tpitch_stiffness " << stiff << "\n\t\tpitch_damping " << damp << "\n\t\ttip_mass " << mass << "\n\t\tlength 10\n\t\tangle_constraint 45\n\t}\n}\n";
        jiggleCount++;
    }
    // Resolve actual hips bone name for $collisionjoints rootbone
    std::string hipsBone = "J_Bip_C_Hips";
    for (int i = 0; i < (int)model.nodes.size(); i++) {
        const auto& nn = model.nodes[i].name;
        if (nn == "hips" || nn == "J_Bip_C_Hips" || nn == "Hips") {
            hipsBone = boneMap.count(nn) ? boneMap[nn] : nn;
            for (char &c : hipsBone) if (c == ' ') c = '_';
            break;
        }
    }
    qc << "$collisionjoints \"" << mdlName << "_phys.smd\" {\n\t$mass 80.0\n\t$rootbone \"" << hipsBone << "\"\n\t$noselfcollisions\n}\n";
    qc << "$cdmaterials \"models/" << mdlName << "/\"\n$sequence \"idle\" \"" << mdlName << "_part0.smd\" loop fps 30\n";
    qc.close();
}

#include <filesystem>
#include <process.h>

namespace fs = std::filesystem;

// Helper to run external commands cross-platform
int run_command(const std::string& cmd) {
    std::cout << "Running: " << cmd << std::endl;
    return std::system(cmd.c_str());
}

// Helper to remove Windows long path prefix (\\?\) which breaks legacy tools
std::string clean_path(const fs::path& p) {
    std::string s = fs::absolute(p).string();
#ifdef _WIN32
    if (s.size() >= 4 && s.substr(0, 4) == "\\\\?\\") {
        return s.substr(4);
    }
#endif
    return s;
}

// Helper for studiomdl execution
int run_studiomdl(const std::string& studiomdlPath, const std::string& gameDir, const std::string& qcFile) {
    std::string cmd;
#ifdef __linux__
    // Minimal attempt to set WINEPREFIX if default steam layout is used
    const char* homeEnv = getenv("HOME");
    std::string winePrefix = "";
    if (homeEnv) {
        winePrefix = std::string(homeEnv) + "/.local/share/Steam/steamapps/compatdata/1840/pfx";
    }
    cmd = "WINEPREFIX=\"" + winePrefix + "\" WINEDEBUG=\"-all\" wine \"" + studiomdlPath + "\" -nop4 -game \"" + gameDir + "\" \"" + qcFile + "\"";
#else
    cmd = "\"" + studiomdlPath + "\" -nop4 -game \"" + gameDir + "\" \"" + qcFile + "\"";
#endif
    return run_command(cmd);
}

void runTUI() {
    std::cout << "\033[2J\033[1;1H"; // Clear screen
    std::cout << "===============================================\n";
    std::cout << "   VRM to Source Filmmaker Converter (v" << VRM2MDL_VERSION << ")\n";
    std::cout << "===============================================\n\n";

    std::vector<std::string> vrmFiles;
    for (const auto& entry : fs::directory_iterator(".")) {
        if (entry.is_regular_file() && entry.path().extension() == ".vrm") {
            vrmFiles.push_back(entry.path().filename().string());
        }
    }

    if (vrmFiles.empty()) {
        std::cout << "[!] No .vrm files found in the current directory.\n";
        std::cout << "Please place your model here and restart.\n";
        return;
    }

    std::cout << "Available Models:\n";
    for (size_t i = 0; i < vrmFiles.size(); i++) {
        std::cout << "  " << (i + 1) << ". " << vrmFiles[i] << "\n";
    }

    std::cout << "\nSelect a model (1-" << vrmFiles.size() << ") or 0 to exit: ";
    int choice = 0;
    std::cin >> choice;
    if (choice <= 0 || choice > (int)vrmFiles.size()) return;

    std::string targetVRM = vrmFiles[choice - 1];

    // Auto-scan and config handling for studiomdl
    fs::path studiomdlPath;
    if (fs::exists("config.txt")) {
        std::ifstream cfg("config.txt");
        std::string pathLine;
        std::getline(cfg, pathLine);
        cfg.close();
        studiomdlPath = fs::path(pathLine).lexically_normal();
    } 
    
    if (studiomdlPath.empty() || !fs::exists(studiomdlPath)) {
        // Auto-scan common paths
        std::vector<fs::path> searchPaths;
#ifdef __linux__
        const char* homeEnv = getenv("HOME");
        if (homeEnv) {
            fs::path home(homeEnv);
            searchPaths.push_back(home / ".local/share/Steam/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe");
            searchPaths.push_back(home / ".steam/steam/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe");
            searchPaths.push_back(home / ".steam/root/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe");
        }
#else
        searchPaths.push_back("C:/Program Files (x86)/Steam/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe");
        searchPaths.push_back("C:/Program Files/Steam/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe");
        searchPaths.push_back("D:/SteamLibrary/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe");
#endif

        for (const auto& sp : searchPaths) {
            if (fs::exists(sp)) {
                studiomdlPath = sp;
                std::cout << "[*] Auto-detected studiomdl.exe at: " << studiomdlPath.string() << "\n";
                std::ofstream cfg("config.txt");
                cfg << studiomdlPath.string();
                cfg.close();
                break;
            }
        }
    }

    if (studiomdlPath.empty() || !fs::exists(studiomdlPath)) {
        std::cout << "\n[!] studiomdl.exe not found automatically.\n";
        std::cout << "Enter the absolute path to studiomdl.exe: ";
        std::string pathInput;
        std::cin.ignore(10000, '\n');
        std::getline(std::cin, pathInput);
        
        studiomdlPath = fs::path(pathInput).lexically_normal();
        
#ifdef __linux__
        if (!studiomdlPath.empty() && pathInput[0] == '~') {
            const char* homeEnv = getenv("HOME");
            if (homeEnv) {
                std::string p = pathInput;
                p.replace(0, 1, homeEnv);
                studiomdlPath = fs::path(p).lexically_normal();
            }
        }
#endif

        if (!studiomdlPath.empty()) {
            std::ofstream cfg("config.txt");
            cfg << studiomdlPath.string();
            cfg.close();
            std::cout << "Saved to config.txt!\n";
        }
    }

    std::cout << "\n[1/3] Converting VRM to SMD/VTA/QC...\n";
    std::string outputDir = "./output";
    std::string mdlName = fs::path(targetVRM).stem().string();
    for (char &c : mdlName) if (c == ' ') c = '_';

    tinygltf::Model model; tinygltf::TinyGLTF loader; std::string err, warn;
    if (!loader.LoadBinaryFromFile(&model, &err, &warn, targetVRM)) {
        std::cerr << "[ERROR] " << err << "\n"; return;
    }

    boneBBoxes.clear();
    fs::path baseDir = fs::path(outputDir) / mdlName;
    fs::path modelsrcDir = baseDir / "modelsrc";
    fs::path modelsDir   = baseDir / "models" / mdlName;
    fs::path matsDir     = baseDir / "materials" / "models" / mdlName;
    
    fs::create_directories(modelsrcDir);
    fs::create_directories(modelsDir);
    fs::create_directories(matsDir);

    std::vector<VRMExpression> exprs; parseVRMExpressions(model, exprs);
    std::vector<VRMSpringJoint> springs; parseVRMSprings(model, springs);
    std::set<int> morphedMeshes;
    for (const auto& expr : exprs) for (const auto& bind : expr.binds) morphedMeshes.insert(bind.mesh);
    
    for (size_t mIdx = 0; mIdx < model.meshes.size(); mIdx++) {
        std::string partName = mdlName + "_part" + std::to_string(mIdx);
        std::vector<UnrolledVertex> uniqueVertices = exportToSMD(model, (modelsrcDir / (partName + ".smd")).string(), mIdx);
        if (morphedMeshes.count(mIdx)) {
            exportToVTA(model, (modelsrcDir / (partName + ".vta")).string(), exprs, mIdx, uniqueVertices);
        }
    }
    exportPhysMesh(model, (modelsrcDir / (mdlName + "_phys.smd")).string());
    generateQC(mdlName, exprs, springs, model, modelsrcDir.string());
    generateVMT(model, mdlName, matsDir.string());
    for (size_t i = 0; i < model.images.size(); i++) {
        std::string texPath = (matsDir / (mdlName + "_tex_" + std::to_string(i) + ".vtf")).string();
        writeVTF(texPath, model.images[i].image, model.images[i].width, model.images[i].height, model.images[i].component);
    }
    std::cout << "[OK] Source Engine files generated.\n";

    if (!studiomdlPath.empty() && fs::exists(studiomdlPath)) {
        std::cout << "\n[2/3] Compiling MDL with studiomdl...\n";
        
        // Create gameinfo.txt for local compilation
        std::ofstream gameinfo(baseDir / "gameinfo.txt");
        gameinfo << "\"GameInfo\"\n{\n\tgame \"vrm2mdl Local Compile\"\n\ttype singleplayer_only\n\tFileSystem\n\t{\n\t\tSteamAppId 1840\n\t\tSearchPaths\n\t\t{\n\t\t\tgame \".\"\n\t\t}\n\t}\n}\n";
        gameinfo.close();

        std::string qcFile = clean_path(modelsrcDir / (mdlName + ".qc"));
        std::string gameDir = clean_path(baseDir);
        
        int result = run_studiomdl(studiomdlPath.string(), gameDir, qcFile);
        if (result == 0) {
            std::cout << "[OK] MDL Compiled Successfully!\n";
            
            std::cout << "\n[3/3] Deploying to SFM...\n";
            std::string sfmBaseStr;
            std::string pathStr = studiomdlPath.string();
            size_t binPos = pathStr.find("/game/bin/");
            if (binPos == std::string::npos) binPos = pathStr.find("\\game\\bin\\");
            
            if (binPos != std::string::npos) {
                fs::path sfmBase = fs::path(pathStr.substr(0, binPos)) / "game" / "usermod";
                
                fs::path deployModels = sfmBase / "models" / mdlName;
                fs::path deployMats = sfmBase / "materials" / "models" / mdlName;
                
                // Cleanup old
                fs::remove_all(deployModels);
                fs::remove_all(deployMats);
                
                // Copy new
                auto copyOptions = fs::copy_options::recursive | fs::copy_options::overwrite_existing;
                try {
                    fs::create_directories(sfmBase / "models");
                    fs::create_directories(sfmBase / "materials" / "models");
                    fs::copy(baseDir / "models" / mdlName, deployModels, copyOptions);
                    fs::copy(baseDir / "materials" / "models" / mdlName, deployMats, copyOptions);
                    std::cout << "[OK] Deployed to: " << sfmBase.string() << "\n";
                } catch (const fs::filesystem_error& e) {
                    std::cerr << "[ERROR] Deployment failed: " << e.what() << "\n";
                }
            } else {
                std::cerr << "[WARN] Could not parse SFM usermod directory from studiomdl path.\n";
            }
        } else {
            std::cerr << "[ERROR] Compilation failed (exit code " << result << ")\n";
        }
    } else {
        std::cout << "\n[!] studiomdl compilation skipped (Invalid path).\n";
    }

    std::cout << "\n===============================================\n";
    std::cout << "All operations complete. Press Enter to exit.\n";
    std::cin.ignore(10000, '\n');
    std::cin.get();
}

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

extern "C" {
    VRM2MDL_API const char* vrm2mdl_version() {
        return VRM2MDL_VERSION;
    }

    VRM2MDL_API int vrm2mdl_convert(
        const char* vrm_path,
        const char* output_dir_path,
        float scale,
        LogCallback log_cb,
        void* user_data
    ) {
        if (!vrm_path || !output_dir_path) return 1;

        std::string filename = vrm_path;
        std::string outputDir = output_dir_path;
        globalScale = scale;

        std::string mdlName = filename;
        size_t ls = mdlName.find_last_of("/\\");
        if (ls != std::string::npos) mdlName = mdlName.substr(ls + 1);
        size_t ld = mdlName.find_last_of(".");
        if (ld != std::string::npos) mdlName = mdlName.substr(0, ld);
        for (char &c : mdlName) if (c == ' ') c = '_';

        tinygltf::Model model; tinygltf::TinyGLTF loader; std::string err, warn;
        if (!loader.LoadBinaryFromFile(&model, &err, &warn, filename)) {
            logMessage(log_cb, user_data, "[ERROR] Failed to load VRM file: %s", filename.c_str());
            if (!err.empty()) logMessage(log_cb, user_data, "  Error: %s", err.c_str());
            return 1;
        }
        if (!warn.empty()) logMessage(log_cb, user_data, "[WARN] %s", warn.c_str());

        boneBBoxes.clear();
        std::string baseDir = outputDir + "/" + mdlName;
        std::string modelsrcDir = baseDir + "/modelsrc";
        std::string modelsDir   = baseDir + "/models/" + mdlName;
        std::string matsDir     = baseDir + "/materials/models/" + mdlName;
        
        try {
            fs::create_directories(modelsrcDir);
            fs::create_directories(modelsDir);
            fs::create_directories(matsDir);
        } catch (const fs::filesystem_error& e) {
            logMessage(log_cb, user_data, "[ERROR] Failed to create directories: %s", e.what());
            return 1;
        }

        logMessage(log_cb, user_data, "[1/3] Converting VRM to SMD/VTA/QC...");
        std::vector<VRMExpression> exprs; parseVRMExpressions(model, exprs);
        std::vector<VRMSpringJoint> springs; parseVRMSprings(model, springs);
        std::set<int> morphedMeshes;
        for (const auto& expr : exprs) for (const auto& bind : expr.binds) morphedMeshes.insert(bind.mesh);
        
        for (size_t mIdx = 0; mIdx < model.meshes.size(); mIdx++) {
            std::string partName = mdlName + "_part" + std::to_string(mIdx);
            std::vector<UnrolledVertex> uniqueVertices = exportToSMD(model, modelsrcDir + "/" + partName + ".smd", mIdx);
            if (morphedMeshes.count(mIdx)) {
                exportToVTA(model, modelsrcDir + "/" + partName + ".vta", exprs, mIdx, uniqueVertices);
            }
        }
        exportPhysMesh(model, modelsrcDir + "/" + mdlName + "_phys.smd");
        generateQC(mdlName, exprs, springs, model, modelsrcDir);
        generateVMT(model, mdlName, matsDir);
        for (size_t i = 0; i < model.images.size(); i++) {
            std::string texPath = matsDir + "/" + mdlName + "_tex_" + std::to_string(i) + ".vtf";
            writeVTF(texPath, model.images[i].image, model.images[i].width, model.images[i].height, model.images[i].component);
        }
        logMessage(log_cb, user_data, "[OK] Source Engine files generated.");

        logMessage(log_cb, user_data, "===============================================");
        logMessage(log_cb, user_data, "   VRM to MDL Conversion Complete (v%s)", VRM2MDL_VERSION);
        logMessage(log_cb, user_data, "===============================================");
        logMessage(log_cb, user_data, " > Model:       %s", mdlName.c_str());
        logMessage(log_cb, user_data, " > Scale:       %f", globalScale);
        logMessage(log_cb, user_data, " > Bones:       %zu", model.nodes.size());
        logMessage(log_cb, user_data, " > Expressions: %zu flexes", exprs.size());
        logMessage(log_cb, user_data, " > Jigglebones: %d/%zu", std::min((int)springs.size(), 12), springs.size());
        logMessage(log_cb, user_data, " > Textures:    %zu VTF (max 1024x1024)", model.images.size());
        logMessage(log_cb, user_data, " > Output:      %s", baseDir.c_str());
        logMessage(log_cb, user_data, "===============================================");

        return 0;
    }
} // extern "C"

#ifndef VRM2MDL_SHARED
int main(int argc, char** argv) {
    if (argc < 2) {
        runTUI();
        return 0;
    }
    std::string filename = argv[1];
    std::string outputDir = "./output";
    float scale = globalScale;
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--scale" && i + 1 < argc) scale = std::stof(argv[++i]);
        if (std::string(argv[i]) == "--output-dir" && i + 1 < argc) outputDir = argv[++i];
    }
    
    int result = vrm2mdl_convert(filename.c_str(), outputDir.c_str(), scale, nullptr, nullptr);
    if (result != 0) {
        std::cerr << "Conversion failed with error code: " << result << std::endl;
    }
    return result;
}
#endif
