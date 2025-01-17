// Copyright 2020 The Tint Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/transform/msl.h"

#include <utility>

#include "src/program_builder.h"

namespace tint {
namespace transform {
namespace {
const char* kReservedKeywords[] = {"access",
                                   "alignas",
                                   "alignof",
                                   "and",
                                   "and_eq",
                                   "array",
                                   "array_ref",
                                   "as_type",
                                   "asm",
                                   "atomic",
                                   "atomic_bool",
                                   "atomic_int",
                                   "atomic_uint",
                                   "auto",
                                   "bitand",
                                   "bitor",
                                   "bool",
                                   "bool2",
                                   "bool3",
                                   "bool4",
                                   "break",
                                   "buffer",
                                   "case",
                                   "catch",
                                   "char",
                                   "char16_t",
                                   "char2",
                                   "char3",
                                   "char32_t",
                                   "char4",
                                   "class",
                                   "compl",
                                   "const",
                                   "const_cast",
                                   "const_reference",
                                   "constant",
                                   "constexpr",
                                   "continue",
                                   "decltype",
                                   "default",
                                   "delete",
                                   "depth2d",
                                   "depth2d_array",
                                   "depth2d_ms",
                                   "depth2d_ms_array",
                                   "depthcube",
                                   "depthcube_array",
                                   "device",
                                   "discard_fragment",
                                   "do",
                                   "double",
                                   "dynamic_cast",
                                   "else",
                                   "enum",
                                   "explicit",
                                   "extern",
                                   "false",
                                   "final",
                                   "float",
                                   "float2",
                                   "float2x2",
                                   "float2x3",
                                   "float2x4",
                                   "float3",
                                   "float3x2",
                                   "float3x3",
                                   "float3x4",
                                   "float4",
                                   "float4x2",
                                   "float4x3",
                                   "float4x4",
                                   "for",
                                   "fragment",
                                   "friend",
                                   "goto",
                                   "half",
                                   "half2",
                                   "half2x2",
                                   "half2x3",
                                   "half2x4",
                                   "half3",
                                   "half3x2",
                                   "half3x3",
                                   "half3x4",
                                   "half4",
                                   "half4x2",
                                   "half4x3",
                                   "half4x4",
                                   "if",
                                   "imageblock",
                                   "inline",
                                   "int",
                                   "int16_t",
                                   "int2",
                                   "int3",
                                   "int32_t",
                                   "int4",
                                   "int64_t",
                                   "int8_t",
                                   "kernel",
                                   "long",
                                   "long2",
                                   "long3",
                                   "long4",
                                   "main",
                                   "metal",
                                   "mutable",
                                   "namespace",
                                   "new",
                                   "noexcept",
                                   "not",
                                   "not_eq",
                                   "nullptr",
                                   "operator",
                                   "or",
                                   "or_eq",
                                   "override",
                                   "packed_bool2",
                                   "packed_bool3",
                                   "packed_bool4",
                                   "packed_char2",
                                   "packed_char3",
                                   "packed_char4",
                                   "packed_float2",
                                   "packed_float3",
                                   "packed_float4",
                                   "packed_half2",
                                   "packed_half3",
                                   "packed_half4",
                                   "packed_int2",
                                   "packed_int3",
                                   "packed_int4",
                                   "packed_short2",
                                   "packed_short3",
                                   "packed_short4",
                                   "packed_uchar2",
                                   "packed_uchar3",
                                   "packed_uchar4",
                                   "packed_uint2",
                                   "packed_uint3",
                                   "packed_uint4",
                                   "packed_ushort2",
                                   "packed_ushort3",
                                   "packed_ushort4",
                                   "patch_control_point",
                                   "private",
                                   "protected",
                                   "ptrdiff_t",
                                   "public",
                                   "r16snorm",
                                   "r16unorm",
                                   "r8unorm",
                                   "reference",
                                   "register",
                                   "reinterpret_cast",
                                   "return",
                                   "rg11b10f",
                                   "rg16snorm",
                                   "rg16unorm",
                                   "rg8snorm",
                                   "rg8unorm",
                                   "rgb10a2",
                                   "rgb9e5",
                                   "rgba16snorm",
                                   "rgba16unorm",
                                   "rgba8snorm",
                                   "rgba8unorm",
                                   "sampler",
                                   "short",
                                   "short2",
                                   "short3",
                                   "short4",
                                   "signed",
                                   "size_t",
                                   "sizeof",
                                   "srgba8unorm",
                                   "static",
                                   "static_assert",
                                   "static_cast",
                                   "struct",
                                   "switch",
                                   "template",
                                   "texture",
                                   "texture1d",
                                   "texture1d_array",
                                   "texture2d",
                                   "texture2d_array",
                                   "texture2d_ms",
                                   "texture2d_ms_array",
                                   "texture3d",
                                   "texture_buffer",
                                   "texturecube",
                                   "texturecube_array",
                                   "this",
                                   "thread",
                                   "thread_local",
                                   "threadgroup",
                                   "threadgroup_imageblock",
                                   "throw",
                                   "true",
                                   "try",
                                   "typedef",
                                   "typeid",
                                   "typename",
                                   "uchar",
                                   "uchar2",
                                   "uchar3",
                                   "uchar4",
                                   "uint",
                                   "uint16_t",
                                   "uint2",
                                   "uint3",
                                   "uint32_t",
                                   "uint4",
                                   "uint64_t",
                                   "uint8_t",
                                   "ulong2",
                                   "ulong3",
                                   "ulong4",
                                   "uniform",
                                   "union",
                                   "unsigned",
                                   "ushort",
                                   "ushort2",
                                   "ushort3",
                                   "ushort4",
                                   "using",
                                   "vec",
                                   "vertex",
                                   "virtual",
                                   "void",
                                   "volatile",
                                   "wchar_t",
                                   "while",
                                   "xor",
                                   "xor_eq"};
}  // namespace

Msl::Msl() = default;
Msl::~Msl() = default;

Transform::Output Msl::Run(const Program* in, const DataMap&) {
  ProgramBuilder out;
  CloneContext ctx(&out, in);
  RenameReservedKeywords(&ctx, kReservedKeywords);
  ctx.Clone();
  return Output{Program(std::move(out))};
}

}  // namespace transform
}  // namespace tint
