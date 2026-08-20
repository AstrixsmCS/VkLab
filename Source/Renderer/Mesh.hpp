#pragma once

#include "Buffer.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>
#include <filesystem>

struct Vertex
{
	glm::vec3 Position;
	glm::vec3 Normal;
};

struct Submesh
{
	uint32_t BaseVertex  = 0;
	uint32_t VertexCount = 0;
	uint32_t BaseIndex   = 0;
	uint32_t IndexCount  = 0;
};

struct Node
{
	uint32_t Parent = UINT32_MAX;

	std::vector<uint32_t> Children;
	std::vector<uint32_t> Submeshes;

	std::string Name;

	glm::vec3 Translation{ 0.0f };
	glm::quat Rotation   { 1.0f, 0.0f, 0.0f, 0.0f };
	glm::vec3 Scale      { 1.0f };

	glm::mat4 LocalTransform { 1.0f };
	glm::mat4 WorldTransform { 1.0f };

	bool IsRoot() const { return Parent == UINT32_MAX; }
};

class Mesh
{
public:
	Mesh()  = default;
	~Mesh() = default;

	bool Load(const std::filesystem::path& path);
	void Destroy();

	// Traverses the node hierarchy depth-first starting from the root.
	void TraverseNodes(const std::function<void(const Node&)>& callback) const;

	const std::string&           GetName()      const { return m_Name; }
	const std::vector<Submesh>&  GetSubmeshes() const { return m_Submeshes; }
	const std::vector<Node>&     GetNodes()     const { return m_Nodes; }
	const Node&                  GetRootNode()  const { return m_Nodes[0]; }

	VkBuffer GetVertexBuffer() const { return m_VertexBuffer.GetBuffer(); }
	VkBuffer GetIndexBuffer()  const { return m_IndexBuffer.GetBuffer(); }
private:
	std::string m_Name;

	std::vector<Vertex>   m_Vertices;
	std::vector<uint32_t> m_Indices;

	std::vector<Submesh>  m_Submeshes;
	std::vector<Node>     m_Nodes;   // m_Nodes[0] is always the root

	VertexBuffer m_VertexBuffer;
	IndexBuffer  m_IndexBuffer;
};
