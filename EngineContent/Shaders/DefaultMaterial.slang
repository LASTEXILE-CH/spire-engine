import ShaderLib;

struct DefaultMaterial : IMaterial
{
    typedef PbrMaterialPattern<NoSelfShadow> MaterialPattern;
	float3 getDisplacement<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        return float3(0.0);
    }
   	MaterialPattern evalPattern<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        MaterialPattern rs;
        rs.init();
        rs.albedo = float3(0.9, 0.9, 0.9);
        rs.normal = float3(0.0, 0.0, 1.0);
        rs.specular = 0.0;
        rs.roughness = 1.0;
        rs.metallic = 0.0;
        rs.isDoubleSided = false;
        rs.opacity = 1.0;
        rs.ao = 1.0;
        return rs;
    }
};