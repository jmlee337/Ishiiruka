// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/UberShaderVertex.h"
#include "VideoCommon/DriverDetails.h"
#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/UberShaderCommon.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

namespace UberShader
{
VertexUberShaderUid GetVertexUberShaderUid(u32 components, const XFMemory &xfr)
{
  VertexUberShaderUid out;
  out.ClearUID();
  vertex_ubershader_uid_data& uid_data = out.GetUidData<vertex_ubershader_uid_data>();  
  uid_data.num_texgens = xfr.numTexGen.numTexGens;
  uid_data.per_pixel_lighting = g_ActiveConfig.PixelLightingEnabled(xfr, components);
  out.CalculateUIDHash();
  return out;
}

static void GenVertexShaderTexGens(API_TYPE ApiType, u32 numTexgen, bool pixel_ligthing_enabled, ShaderCode& out);

void GenVertexShader(ShaderCode& out, API_TYPE ApiType, const ShaderHostConfig& host_config,
  const vertex_ubershader_uid_data& uid_data)
{
  const bool msaa = host_config.msaa;
  const bool ssaa = host_config.ssaa;
  const bool per_pixel_lighting = uid_data.per_pixel_lighting != 0;
  const u32 numTexgen = uid_data.num_texgens;

  out.Write("// Vertex UberShader\n\n");
  out.Write("%s", s_lighting_struct);

  // uniforms
  if (ApiType == API_OPENGL || ApiType == API_VULKAN)
    out.Write("UBO_BINDING(std140, 2) uniform VSBlock {\n");
  else
    out.Write("cbuffer VSBlock {\n");
  out.Write(s_shader_uniforms);
  out.Write("};\n");

  out.Write("struct VS_OUTPUT {\n");
  GenerateVSOutputMembers(out, ApiType, per_pixel_lighting, numTexgen);
  out.Write("};\n\n");

  WriteUberShaderCommonHeader(out, ApiType, host_config);
  WriteLightingFunction(out);

  if (ApiType == API_OPENGL || ApiType == API_VULKAN)
  {
    out.Write("ATTRIBUTE_LOCATION(%d) in float4 rawpos;\n", SHADER_POSITION_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION(%d) in uint4 posmtx;\n", SHADER_POSMTX_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION(%d) in float3 rawnorm0;\n", SHADER_NORM0_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION(%d) in float3 rawnorm1;\n", SHADER_NORM1_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION(%d) in float3 rawnorm2;\n", SHADER_NORM2_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION(%d) in float4 rawcolor0;\n", SHADER_COLOR0_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION(%d) in float4 rawcolor1;\n", SHADER_COLOR1_ATTRIB);
    for (int i = 0; i < 8; ++i)
      out.Write("ATTRIBUTE_LOCATION(%d) in float3 rawtex%d;\n", SHADER_TEXTURE0_ATTRIB + i, i);

    // We need to always use output blocks for Vulkan, but geometry shaders are also optional.
    if (host_config.backend_geometry_shaders || ApiType == API_VULKAN)
    {
      out.Write("VARYING_LOCATION(0) out VertexData {\n");
      GenerateVSOutputMembers(out, ApiType, per_pixel_lighting, numTexgen,
                              GetInterpolationQualifier(ApiType, msaa, ssaa, false, true));
      out.Write("} vs;\n");
    }
    else
    {
      const char* optCentroid = GetInterpolationQualifier(ApiType, msaa, ssaa);
      // Let's set up attributes
      if (uid_data.num_texgens < 7)
      {
        for (int i = 0; i < 8; ++i)
          out.Write("%s out float3 tex%d;\n", optCentroid, i);
        out.Write("%s out float4 clipPos;\n", optCentroid);
        if (per_pixel_lighting)
          out.Write("%s out float4 Normal;\n", optCentroid);
      }
      else
      {
        // wpos is in w of first 4 texcoords
        if (per_pixel_lighting)
        {
          for (int i = 0; i < 8; ++i)
            out.Write("%s out float4 tex%d;\n", optCentroid, i);
        }
        else
        {
          for (unsigned int i = 0; i < uid_data.num_texgens; ++i)
            out.Write("%s out float%d tex%d;\n", optCentroid, i < 4 ? 4 : 3, i);
        }
      }
      out.Write("%s out float4 colors_0;\n", optCentroid);
      out.Write("%s out float4 colors_1;\n", optCentroid);
    }

    out.Write("void main()\n{\n");
  }
  else  // D3D
  {
    out.Write("VS_OUTPUT main(\n");

    // inputs
    out.Write("  float3 rawnorm0 : NORMAL0,\n");
    out.Write("  float3 rawnorm1 : NORMAL1,\n");
    out.Write("  float3 rawnorm2 : NORMAL2,\n");
    out.Write("  float4 rawcolor0 : COLOR0,\n");
    out.Write("  float4 rawcolor1 : COLOR1,\n");
    for (int i = 0; i < 8; ++i)
      out.Write("  float3 rawtex%d : TEXCOORD%d,\n", i, i);
    out.Write("  float4 posmtx : BLENDINDICES,\n");
    out.Write("  float4 rawpos : POSITION) {\n");
  }

  out.Write("VS_OUTPUT o;\n"
            "\n");

  // Transforms
  out.Write("// Position matrix\n"
            "float4 P0;\n"
            "float4 P1;\n"
            "float4 P2;\n"
            "\n"
            "// Normal matrix\n"
            "float3 N0;\n"
            "float3 N1;\n"
            "float3 N2;\n"
            "\n");
  out.Write("  // Vertex format has a per-vertex matrix\n");
  if (ApiType == API_D3D11)
  {
    out.Write("int posidx = int(round(posmtx.x * 255.0));\n");
  }
  else
  {
    out.Write("  int posidx = int(posmtx.r);\n");
  }
  out.Write("  P0 = " I_TRANSFORMMATRICES "[posidx];\n"
            "  P1 = " I_TRANSFORMMATRICES "[posidx+1];\n"
            "  P2 = " I_TRANSFORMMATRICES "[posidx+2];\n"
            "\n"
            "  int normidx = posidx >= 32 ? (posidx - 32) : posidx;\n"
            "  N0 = " I_NORMALMATRICES "[normidx].xyz;\n"
            "  N1 = " I_NORMALMATRICES "[normidx+1].xyz;\n"
            "  N2 = " I_NORMALMATRICES "[normidx+2].xyz;\n"            
            "\n"
            "float4 pos = float4(dot(P0, rawpos), dot(P1, rawpos), dot(P2, rawpos), 1.0);\n"
            "o.pos = float4(dot(" I_PROJECTION "[0], pos), dot(" I_PROJECTION
            "[1], pos), dot(" I_PROJECTION "[2], pos), dot(" I_PROJECTION "[3], pos));\n"
            "\n"
            "// Only the first normal gets normalized (TODO: why?)\n"
            "float3 _norm0 = float3(0.0, 0.0, 0.0);\n"
            "if ((components & %uu) != 0u) // VB_HAS_NRM0\n",
            VB_HAS_NRM0);
  out.Write(
      "  _norm0 = normalize(float3(dot(N0, rawnorm0), dot(N1, rawnorm0), dot(N2, rawnorm0)));\n"
      "\n"
      "float3 _norm1 = float3(0.0, 0.0, 0.0);\n"
      "if ((components & %uu) != 0u) // VB_HAS_NRM1\n",
      VB_HAS_NRM1);
  out.Write("  _norm1 = float3(dot(N0, rawnorm1), dot(N1, rawnorm1), dot(N2, rawnorm1));\n"
            "\n"
            "float3 _norm2 = float3(0.0, 0.0, 0.0);\n"
            "if ((components & %uu) != 0u) // VB_HAS_NRM2\n",
            VB_HAS_NRM2);
  out.Write("  _norm2 = float3(dot(N0, rawnorm2), dot(N1, rawnorm2), dot(N2, rawnorm2));\n"
            "\n");

  // Hardware Lighting
  WriteVertexLighting(out, ApiType, "pos.xyz", "_norm0", "rawcolor0", "rawcolor1", "o.colors_0",
                      "o.colors_1");

  // Texture Coordinates
  if (numTexgen > 0)
    GenVertexShaderTexGens(ApiType, numTexgen, per_pixel_lighting, out);

  // clipPos/w needs to be done in pixel shader, not here
  if (uid_data.num_texgens < 7)
  {
    out.Write("o.clipPos = float4(pos.x,pos.y,o.pos.z,o.pos.w);\n");
  }
  else
  {
    out.Write("o.tex0.w = pos.x;\n");
    out.Write("o.tex1.w = pos.y;\n");
    out.Write("o.tex2.w = o.pos.z;\n");
    out.Write("o.tex3.w = o.pos.w;\n");
  }

  if (per_pixel_lighting)
  {
    if (uid_data.num_texgens < 7)
    {
      out.Write("o.Normal = float4(_norm0.x,_norm0.y,_norm0.z,pos.z);\n");
    }
    else
    {
      out.Write("o.tex4.w = _norm0.x;\n");
      out.Write("o.tex5.w = _norm0.y;\n");
      out.Write("o.tex6.w = _norm0.z;\n");
      if (uid_data.num_texgens < 8)
        out.Write("o.tex7 = pos.xyzz;\n");
      else
        out.Write("o.tex7.w = pos.z;\n");
    }
    out.Write("if ((components & %uu) != 0u) // VB_HAS_COL0\n", VB_HAS_COL0);
    out.Write("  o.colors_0 = rawcolor0;\n");
    out.Write("if ((components & %uu) != 0u) // VB_HAS_COL1\n", VB_HAS_COL1);
    out.Write("  o.colors_1 = rawcolor1;\n");
  }

  // If we can disable the incorrect depth clipping planes using depth clamping, then we can do
  // our own depth clipping and calculate the depth range before the perspective divide if
  // necessary.
  if (host_config.backend_depth_clamp)
  {
    // Since we're adjusting z for the depth range before the perspective divide, we have to do our
    // own clipping. We want to clip so that -w <= z <= 0, which matches the console -1..0 range.
    // We adjust our depth value for clipping purposes to match the perspective projection in the
    // software backend, which is a hack to fix Sonic Adventure and Unleashed games.
    out.Write("float clipDepth = o.pos.z * 0.9999999;\n");
    out.Write("o.clipDist.x = clipDepth + o.pos.w;\n");  // Near: z < -w
    out.Write("o.clipDist.y = -clipDepth;\n");           // Far: z > 0
  }

  // Write the true depth value. If the game uses depth textures, then the pixel shader will
  // override it with the correct values if not then early z culling will improve speed.
  // There are two different ways to do this, when the depth range is oversized, we process
  // the depth range in the vertex shader, if not we let the host driver handle it.
  //
  // Adjust z for the depth range. We're using an equation which incorperates a depth inversion,
  // so we can map the console -1..0 range to the 0..1 range used in the depth buffer.
  // We have to handle the depth range in the vertex shader instead of after the perspective
  // divide, because some games will use a depth range larger than what is allowed by the
  // graphics API. These large depth ranges will still be clipped to the 0..1 range, so these
  // games effectively add a depth bias to the values written to the depth buffer.
  out.Write("o.pos.z = o.pos.w * " I_DEPTHPARAMS ".x - o.pos.z * " I_DEPTHPARAMS ".y;\n");

  if (!host_config.backend_clip_control)
  {
    // If the graphics API doesn't support a depth range of 0..1, then we need to map z to
    // the -1..1 range. Unfortunately we have to use a substraction, which is a lossy floating-point
    // operation that can introduce a round-trip error.
    out.Write("o.pos.z = o.pos.z * 2.0 - o.pos.w;\n");
  }

  // Correct for negative viewports by mirroring all vertices. We need to negate the height here,
  // since the viewport height is already negated by the render backend.
  out.Write("o.pos.xy *= sign(" I_DEPTHPARAMS ".zw * float2(-1.0, 1.0));\n");

  // The console GPU places the pixel center at 7/12 in screen space unless
  // antialiasing is enabled, while D3D and OpenGL place it at 0.5. This results
  // in some primitives being placed one pixel too far to the bottom-right,
  // which in turn can be critical if it happens for clear quads.
  // Hence, we compensate for this pixel center difference so that primitives
  // get rasterized correctly.
  out.Write("o.pos.xy = o.pos.xy + o.pos.w * " I_DEPTHPARAMS".zw;\n");

  // By now our position is in clip space
  // however, higher resolutions than the Wii outputs
  // cause an additional pixel offset
  // due to a higher pixel density
  // we need to correct this by converting our
  // clip-space position into the Wii's screen-space
  // acquire the right pixel and then convert it back
  out.Write("if (o.pos.w == 1.0)\n");
  out.Write("{\n");
  out.Write("\to.pos.xy = round(o.pos.xy * " I_VIEWPARAMS ".xy) * " I_VIEWPARAMS ".zw;\n");
  out.Write("}\n");

  if (ApiType == API_OPENGL || ApiType == API_VULKAN)
  {
    if (host_config.backend_geometry_shaders || ApiType == API_VULKAN)
    {
      AssignVSOutputMembers(out, ApiType, "vs", "o", per_pixel_lighting, numTexgen);
    }
    else
    {
      if (uid_data.num_texgens < 7)
      {
        for (unsigned int i = 0; i < 8; ++i)
        {
          if (i < uid_data.num_texgens)
            out.Write(" tex%d.xyz =  o.tex%d.xyz;\n", i, i);
          else
            out.Write(" tex%d.xyz =  float3(0.0, 0.0, 0.0);\n", i);
        }
        out.Write("  clipPos = o.clipPos;\n");
        if (per_pixel_lighting)
          out.Write("  Normal = o.Normal;\n");
      }
      else
      {
        // clip position is in w of first 4 texcoords
        if (per_pixel_lighting)
        {
          for (int i = 0; i < 8; ++i)
            out.Write(" tex%d = o.tex%d;\n", i, i);
        }
        else
        {
          for (unsigned int i = 0; i < uid_data.num_texgens; ++i)
            out.Write("  tex%d%s = o.tex%d;\n", i, i < 4 ? ".xyzw" : ".xyz", i);
        }
      }
      out.Write("colors_0 = o.colors_0;\n");
      out.Write("colors_1 = o.colors_1;\n");
    }

    if (host_config.backend_depth_clamp)
    {
      out.Write("gl_ClipDistance[0] = o.clipDist.x;\n");
      out.Write("gl_ClipDistance[1] = o.clipDist.y;\n");
    }

    // Vulkan NDC space has Y pointing down (right-handed NDC space).
    if (ApiType == API_VULKAN)
      out.Write("gl_Position = float4(o.pos.x, -o.pos.y, o.pos.z, o.pos.w);\n");
    else
      out.Write("gl_Position = o.pos;\n");
  }
  else  // D3D
  {
    out.Write("return o;\n");
  }
  out.Write("}\n");
}

void GenVertexShaderTexGens(API_TYPE ApiType, u32 numTexgen, bool pixel_ligthing_enabled, ShaderCode& out)
{
  // The HLSL compiler complains that the output texture coordinates are uninitialized when trying
  // to dynamically index them.
  for (u32 i = 0; i < numTexgen; i++)
    out.Write("o.tex%u.xyz = float3(0.0, 0.0, 0.0);\n", i);
  

  out.Write("// Texture coordinate generation\n");
  if (numTexgen == 1)
    out.Write("{ const uint texgen = 0u;\n");
  else
    out.Write("%sfor (uint texgen = 0u; texgen < %uu; texgen++) {\n",
              ApiType == API_D3D11 ? "[loop] " : "", numTexgen);

  out.Write("  // Texcoord transforms\n");
  out.Write("  float4 coord = float4(0.0, 0.0, 1.0, 1.0);\n"
            "  uint texMtxInfo = xfmem_texMtxInfo(texgen);\n");
  out.Write("  switch (%s) {\n", BitfieldExtract("texMtxInfo", TexMtxInfo().sourcerow).c_str());
  out.Write("  case %uu: // XF_SRCGEOM_INROW\n", XF_SRCGEOM_INROW);
  out.Write("    coord.xyz = rawpos.xyz;\n");
  out.Write("    break;\n\n");
  out.Write("  case %uu: // XF_SRCNORMAL_INROW\n", XF_SRCNORMAL_INROW);
  out.Write(
      "    coord.xyz = ((components & %uu /* VB_HAS_NRM0 */) != 0u) ? rawnorm0.xyz : coord.xyz;",
      VB_HAS_NRM0);
  out.Write("    break;\n\n");
  out.Write("  case %uu: // XF_SRCBINORMAL_T_INROW\n", XF_SRCBINORMAL_T_INROW);
  out.Write(
      "    coord.xyz = ((components & %uu /* VB_HAS_NRM1 */) != 0u) ? rawnorm1.xyz : coord.xyz;",
      VB_HAS_NRM1);
  out.Write("    break;\n\n");
  out.Write("  case %uu: // XF_SRCBINORMAL_B_INROW\n", XF_SRCBINORMAL_B_INROW);
  out.Write(
      "    coord.xyz = ((components & %uu /* VB_HAS_NRM2 */) != 0u) ? rawnorm2.xyz : coord.xyz;",
      VB_HAS_NRM2);
  out.Write("    break;\n\n");
  for (u32 i = 0; i < 8; i++)
  {
    out.Write("  case %uu: // XF_SRCTEX%u_INROW\n", XF_SRCTEX0_INROW + i, i);
    out.Write(
        "    coord = ((components & %uu /* VB_HAS_UV%u */) != 0u) ? float4(rawtex%u.x, rawtex%u.y, "
        "1.0, 1.0) : coord;\n",
        VB_HAS_UV0 << i, i, i, i);
    out.Write("    break;\n\n");
  }
  out.Write("  }\n");
  out.Write("\n");

  out.Write("  // Input form of AB11 sets z element to 1.0\n");
  out.Write("  if (%s == %uu) // inputform == XF_TEXINPUT_AB11\n",
            BitfieldExtract("texMtxInfo", TexMtxInfo().inputform).c_str(), XF_TEXINPUT_AB11);
  out.Write("    coord.z = 1.0f;\n");
  out.Write("\n");

  out.Write("  // first transformation\n");
  out.Write("  uint texgentype = %s;\n",
            BitfieldExtract("texMtxInfo", TexMtxInfo().texgentype).c_str());
  out.Write("  float3 output_tex;\n"
            "  switch (texgentype)\n"
            "  {\n");
  out.Write("  case %uu: // XF_TEXGEN_EMBOSS_MAP\n", XF_TEXGEN_EMBOSS_MAP);
  out.Write("    {\n");
  out.Write("      uint light = %s;\n",
            BitfieldExtract("texMtxInfo", TexMtxInfo().embosslightshift).c_str());
  out.Write("      uint source = %s;\n",
            BitfieldExtract("texMtxInfo", TexMtxInfo().embosssourceshift).c_str());
  out.Write("      switch (source) {\n");
  for (u32 i = 0; i < numTexgen; i++)
    out.Write("      case %uu: output_tex.xyz = o.tex%u.xyz; break;\n", i, i);
  out.Write("      default: output_tex.xyz = float3(0.0, 0.0, 0.0); break;\n"
            "      }\n");
  out.Write("      if ((components & %uu) != 0u) { // VB_HAS_NRM1 | VB_HAS_NRM2\n",
            VB_HAS_NRM1 | VB_HAS_NRM2);  // Should this be VB_HAS_NRM1 | VB_HAS_NRM2
  out.Write("        float3 ldir = normalize(" I_LIGHTS "[light].pos.xyz - pos.xyz);\n"
            "        output_tex.xyz += float3(dot(ldir, _norm1), dot(ldir, _norm2), 0.0);\n"
            "      }\n"
            "    }\n"
            "    break;\n\n");
  out.Write("  case %uu: // XF_TEXGEN_COLOR_STRGBC0\n", XF_TEXGEN_COLOR_STRGBC0);
  out.Write("    output_tex.xyz = float3(o.colors_0.x, o.colors_0.y, 1.0);\n"
            "    break;\n\n");
  out.Write("  case %uu: // XF_TEXGEN_COLOR_STRGBC1\n", XF_TEXGEN_COLOR_STRGBC1);
  out.Write("    output_tex.xyz = float3(o.colors_1.x, o.colors_1.y, 1.0);\n"
            "    break;\n\n");
  out.Write("  default:  // Also XF_TEXGEN_REGULAR\n"
            "    {\n");
  out.Write("      if ((components & (%uu /* VB_HAS_TEXMTXIDX0 */ << texgen)) != 0u) {\n",
            VB_HAS_TEXMTXIDX0);
  out.Write("        // This is messy, due to dynamic indexing of the input texture coordinates.\n"
            "        // Hopefully the compiler will unroll this whole loop anyway and the switch.\n"
            "        int tmp = 0;\n"
            "        switch (texgen) {\n");
  for (u32 i = 0; i < numTexgen; i++)
    out.Write("        case %uu: tmp = int(rawtex%u.z); break;\n", i, i);
  out.Write("        }\n"
            "\n");
  out.Write("        if (%s == %uu) {\n",
            BitfieldExtract("texMtxInfo", TexMtxInfo().projection).c_str(), XF_TEXPROJ_STQ);
  out.Write("          output_tex.xyz = float3(dot(coord, " I_TRANSFORMMATRICES "[tmp]),\n"
            "                                  dot(coord, " I_TRANSFORMMATRICES "[tmp + 1]),\n"
            "                                  dot(coord, " I_TRANSFORMMATRICES "[tmp + 2]));\n"
            "        } else {\n"
            "          output_tex.xyz = float3(dot(coord, " I_TRANSFORMMATRICES "[tmp]),\n"
            "                                  dot(coord, " I_TRANSFORMMATRICES "[tmp + 1]),\n"
            "                                  1.0);\n"
            "        }\n"
            "      } else {\n");
  out.Write("        if (%s == %uu) {\n",
            BitfieldExtract("texMtxInfo", TexMtxInfo().projection).c_str(), XF_TEXPROJ_STQ);
  out.Write("          output_tex.xyz = float3(dot(coord, " I_TEXMATRICES "[3u * texgen]),\n"
            "                                  dot(coord, " I_TEXMATRICES "[3u * texgen + 1u]),\n"
            "                                  dot(coord, " I_TEXMATRICES "[3u * texgen + 2u]));\n"
            "        } else {\n"
            "          output_tex.xyz = float3(dot(coord, " I_TEXMATRICES "[3u * texgen]),\n"
            "                                  dot(coord, " I_TEXMATRICES "[3u * texgen + 1u]),\n"
            "                                  1.0);\n"
            "        }\n"
            "      }\n"
            "    }\n"
            "    break;\n\n"
            "  }\n"
            "\n");

  out.Write("  if (xfmem_dualTexInfo != 0u) {\n");
  out.Write("    uint postMtxInfo = xfmem_postMtxInfo(texgen);");
  out.Write("    uint base_index = %s;\n",
            BitfieldExtract("postMtxInfo", PostMtxInfo().index).c_str());
  out.Write("    float4 P0 = " I_POSTTRANSFORMMATRICES "[base_index & 0x3fu];\n"
            "    float4 P1 = " I_POSTTRANSFORMMATRICES "[(base_index + 1u) & 0x3fu];\n"
            "    float4 P2 = " I_POSTTRANSFORMMATRICES "[(base_index + 2u) & 0x3fu];\n"
            "\n");
  out.Write("    if (%s != 0u)\n", BitfieldExtract("postMtxInfo", PostMtxInfo().normalize).c_str());
  out.Write("      output_tex.xyz = normalize(output_tex.xyz);\n"
            "\n"
            "    // multiply by postmatrix\n"
            "    output_tex.xyz = float3(dot(P0.xyz, output_tex.xyz) + P0.w,\n"
            "                            dot(P1.xyz, output_tex.xyz) + P1.w,\n"
            "                            dot(P2.xyz, output_tex.xyz) + P2.w);\n"
            "  }\n\n");

  // When q is 0, the GameCube appears to have a special case
  // This can be seen in devkitPro's neheGX Lesson08 example for Wii
  // Makes differences in Rogue Squadron 3 (Hoth sky) and The Last Story (shadow culling)
  out.Write("  if (texgentype == %uu && output_tex.z == 0.0) // XF_TEXGEN_REGULAR\n",
            XF_TEXGEN_REGULAR);
  out.Write(
      "    output_tex.xy = clamp(output_tex.xy / 2.0f, float2(-1.0f,-1.0f), float2(1.0f,1.0f));\n"
      "\n");

  out.Write("  // Hopefully GPUs that can support dynamic indexing will optimize this.\n");
  out.Write("  switch (texgen) {\n");
  for (u32 i = 0; i < numTexgen; i++)
    out.Write("  case %uu: o.tex%u.xyz = output_tex; break;\n", i, i);
  out.Write("  }\n"
            "}\n");
}

void EnumerateVertexUberShaderUids(const std::function<void(const VertexUberShaderUid&, size_t)>& callback)
{
  VertexUberShaderUid uid = {};
  UberShader::vertex_ubershader_uid_data& vuid = uid.GetUidData<UberShader::vertex_ubershader_uid_data>();
  for (u32 texgens = 0; texgens <= 8; texgens++)
  {
    vuid.num_texgens = texgens;
    vuid.per_pixel_lighting = 0;
    uid.ClearHASH();
    uid.CalculateUIDHash();
    callback(uid, 18);
    vuid.per_pixel_lighting = 1;
    uid.ClearHASH();
    uid.CalculateUIDHash();
    callback(uid, 18);
  }
}
}
