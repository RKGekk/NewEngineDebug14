#include "mesh_render_component.h"

#include "transform_component.h"
#include "mesh_component.h"
#include "../tools/memory_utility.h"
#include "../nodes/d3d_11_mesh.h"
#include "../engine/engine.h"
#include "../engine/d3d_renderer11.h"

const std::string MeshRenderComponent::g_Name = "MeshRenderComponent";
int MeshRenderComponent::m_last_mesh_id = 0;

const std::string& MeshRenderComponent::VGetName() const {
	return g_Name;
}

MeshRenderComponent::MeshRenderComponent() {}

const std::string& MeshRenderComponent::GetPixelShaderResource() {
	return m_pixelShaderResource;
}

const std::string& MeshRenderComponent::GetVertexShaderResource() {
	return m_vertexShaderResource;
}

const std::vector<D3D11_INPUT_ELEMENT_DESC>& MeshRenderComponent::GetLayout() {
	return m_vs_layout;
}

MeshHolder& MeshRenderComponent::GetMesh(int key){
	return m_meshes[key];
}

bool MeshRenderComponent::VDelegateInit(TiXmlElement* pData) {
	TiXmlElement* pPixelShader = pData->FirstChildElement("PixelShader");
	if (pPixelShader) {
		m_pixelShaderResource = pPixelShader->FirstChild()->Value();
	}

	TiXmlElement* pVertexShader = pData->FirstChildElement("VertexShader");
	if (pVertexShader) {
		m_vertexShaderResource = pVertexShader->FirstChild()->Value();
	}

	m_vs_layout = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};

	return true;
}

std::shared_ptr<SceneNode> MeshRenderComponent::VCreateSceneNode() {
	std::shared_ptr<TransformComponent> pTransformComponent = MakeStrongPtr(m_pOwner->GetComponent<TransformComponent>(TransformComponent::g_Name));
	std::shared_ptr<MeshComponent> pMeshComponent = MakeStrongPtr(m_pOwner->GetComponent<MeshComponent>(MeshComponent::g_Name));
	if (!pTransformComponent || !pMeshComponent) {
		return std::shared_ptr<SceneNode>();
	}
	m_resource_directory = pMeshComponent->GetResourceDirecory();
	
	std::shared_ptr<SceneNode> root_node(new SceneNode(this, RenderPass::RenderPass_Actor, &pTransformComponent->GetTransform4x4f()));
	const aiScene* pScene = pMeshComponent->GetScene();
	ProcessNode(pScene->mRootNode, pScene, root_node);

	return root_node;
}

void MeshRenderComponent::VCreateInheritedXmlElements(TiXmlElement* pBaseElement) {}

void MeshRenderComponent::ProcessNode(aiNode* node, const aiScene* scene, std::shared_ptr<SceneNode> parent) {
	DirectX::XMMATRIX node_transform_matrix = DirectX::XMMatrixTranspose(DirectX::XMMATRIX(&node->mTransformation.a1));
	for (UINT i = 0; i < node->mNumMeshes; ++i) {
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		parent->VAddChild(ProcessMesh(mesh, scene, node_transform_matrix));
	}
	for (UINT i = 0; i < node->mNumChildren; ++i) {
		ActorId owner = this->m_pOwner->GetId();
		//std::shared_ptr<SceneNode> n = std::make_shared<SceneNode>(this, RenderPass::RenderPass_Actor, node_transform_matrix, DirectX::XMMatrixIdentity(), false);
		std::shared_ptr<SceneNode> n = std::make_shared<SceneNode>(nullptr, RenderPass::RenderPass_Actor, node_transform_matrix, DirectX::XMMatrixIdentity(), false);
		parent->VAddChild(n);
		ProcessNode(node->mChildren[i], scene, n);
	}
}

std::shared_ptr<SceneNode> MeshRenderComponent::ProcessMesh(aiMesh* mesh, const aiScene* scene, DirectX::FXMMATRIX nodeMatrix) {
	std::vector<Vertex> vertices;
	std::vector<DWORD> indices;

	for (UINT i = 0; i < mesh->mNumVertices; ++i) {
		Vertex vertex;
		vertex.color = { 0.7f, 0.1f, 0.6f, 1.0f };
		vertex.pos = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
		vertex.normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
		vertex.tg = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
		if (mesh->mTextureCoords[0]) {
			vertex.uv = { (float)mesh->mTextureCoords[0][i].x, (float)mesh->mTextureCoords[0][i].y };
		}
		vertices.push_back(vertex);
	}
	for (UINT i = 0; i < mesh->mNumFaces; ++i) {
		aiFace face = mesh->mFaces[i];
		for (UINT j = 0; j < face.mNumIndices; ++j) {
			indices.push_back(face.mIndices[j]);
		}
	}

	aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
	D3DRenderer11* renderer = static_cast<D3DRenderer11*>(g_pApp->GetRenderer());
	std::vector<MaterialTexture> diffuseTextures = LoadMaterialTexures(renderer->GetDevice(), material, aiTextureType::aiTextureType_DIFFUSE, scene);
	DirectX::XMFLOAT4X4 nodeTransformMatrix4x4f;
	DirectX::XMStoreFloat4x4(&nodeTransformMatrix4x4f, nodeMatrix);
	m_meshes[++m_last_mesh_id] = { std::move(vertices), std::move(indices), std::move(diffuseTextures), nodeTransformMatrix4x4f };

	std::shared_ptr<D3D11Mesh> result = std::make_shared<D3D11Mesh>(m_last_mesh_id, this, nodeMatrix, DirectX::XMMatrixIdentity(), true);
	return result;
}

TextureStorageType MeshRenderComponent::DetermineTextureStorageType(const aiScene* pScene, aiMaterial* pMat, unsigned int index, aiTextureType textureType) {
	if (pMat->GetTextureCount(textureType) == 0) {
		return TextureStorageType::None;
	}

	aiString path;
	pMat->GetTexture(textureType, index, &path);
	std::string texturePath = path.C_Str();
	if (texturePath[0] == '*') {
		if (pScene->mTextures[0]->mHeight == 0) {
			return TextureStorageType::EmbeddedIndexCompressed;
		}
		else {
			return TextureStorageType::EmbeddedIndexNonCompressed;
		}
	}

	if (auto pTex = pScene->GetEmbeddedTexture(texturePath.c_str())) {
		if (pTex->mHeight == 0) {
			return TextureStorageType::EmbeddedCompressed;
		}
		else {
			return TextureStorageType::EmbeddedNonCompressed;
		}
	}

	return TextureStorageType::Disk;
}

int MeshRenderComponent::GetTextureIndex(aiString* pString) {
	return atoi(&pString->C_Str()[1]);
}

DirectX::XMMATRIX MeshRenderComponent::InverseTranspose(DirectX::CXMMATRIX M) {
	DirectX::XMMATRIX A = M;
	A.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	DirectX::XMVECTOR det = XMMatrixDeterminant(A);
	return XMMatrixTranspose(XMMatrixInverse(&det, A));;
}

DirectX::XMMATRIX MeshRenderComponent::Inverse(DirectX::CXMMATRIX M) {
	DirectX::XMMATRIX A = M;
	A.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	DirectX::XMVECTOR det = XMMatrixDeterminant(A);
	return XMMatrixInverse(&det, A);
}

std::vector<MaterialTexture> MeshRenderComponent::LoadMaterialTexures(ID3D11Device* device, aiMaterial* pMaterial, aiTextureType textureType, const aiScene* pScene) {
	std::vector<MaterialTexture> materialTextures;
	TextureStorageType storeType = TextureStorageType::Invalid;
	unsigned int textureCount = pMaterial->GetTextureCount(textureType);
	if (textureCount == 0) {
		storeType = TextureStorageType::None;
		aiColor3D aiColor(0.0f, 0.0f, 0.0f);
		switch (textureType) {
			case aiTextureType_DIFFUSE: {
				pMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, aiColor);
				if (aiColor.IsBlack()) {
					materialTextures.push_back(MaterialTexture(device, MaterialColors::UnloadedTextureColor, textureType));
				}
				else {
					materialTextures.push_back(MaterialTexture(device, MaterialColor(aiColor.r * 255, aiColor.g * 255, aiColor.b * 255), textureType));
				}
			}
			break;
		}
	}
	else {
		for (UINT i = 0u; i < textureCount; ++i) {
			aiString path;
			pMaterial->GetTexture(textureType, i, &path);
			TextureStorageType store_type = DetermineTextureStorageType(pScene, pMaterial, i, textureType);
			switch (store_type) {
				case TextureStorageType::EmbeddedIndexCompressed: {
					int index = GetTextureIndex(&path);
					materialTextures.push_back(MaterialTexture(device, reinterpret_cast<uint8_t*>(pScene->mTextures[index]->pcData), pScene->mTextures[index]->mWidth, textureType));
				}
				break;
				case TextureStorageType::EmbeddedCompressed: {
					const aiTexture* pTexture = pScene->GetEmbeddedTexture(path.C_Str());
					materialTextures.push_back(MaterialTexture(device, reinterpret_cast<uint8_t*>(pTexture->pcData), pTexture->mWidth, textureType));
				}
				break;
				case TextureStorageType::Disk: {
					std::string fileName = path.C_Str();
					materialTextures.push_back(MaterialTexture(device, fileName, textureType));
				}
				break;
			}
		}
	}

	if (materialTextures.size() == 0u) {
		materialTextures.push_back(MaterialTexture(device, MaterialColors::UnhandeledTextureColor, aiTextureType::aiTextureType_DIFFUSE));
	}

	return materialTextures;
}
