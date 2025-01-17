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

#include "gmock/gmock.h"
#include "src/reader/spirv/parser_impl_test_helper.h"
#include "src/reader/spirv/spirv_tools_helpers_test.h"

namespace tint {
namespace reader {
namespace spirv {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

TEST_F(SpvParserTest, Import_NoImport) {
  auto p = parser(test::Assemble("%1 = OpTypeVoid"));
  EXPECT_TRUE(p->BuildAndParseInternalModule());
  EXPECT_TRUE(p->error().empty());
  const auto program_ast = p->program().to_str();
  EXPECT_THAT(program_ast, Not(HasSubstr("Import")));
}

TEST_F(SpvParserTest, Import_ImportGlslStd450) {
  auto p = parser(test::Assemble(R"(%1 = OpExtInstImport "GLSL.std.450")"));
  EXPECT_TRUE(p->BuildAndParseInternalModule());
  EXPECT_TRUE(p->error().empty());
  EXPECT_THAT(p->glsl_std_450_imports(), ElementsAre(1));
}

TEST_F(SpvParserTest, Import_NonSemantic_IgnoredImport) {
  auto p = parser(test::Assemble(
      R"(%40 = OpExtInstImport "NonSemantic.ClspvReflection.1")"));
  EXPECT_TRUE(p->BuildAndParseInternalModule());
  EXPECT_TRUE(p->error().empty());
}

TEST_F(SpvParserTest, Import_NonSemantic_IgnoredExtInsts) {
  // This is the clspv-compiled output of this OpenCL C:
  //    kernel void foo(global int*A) { A=A; }
  // It emits NonSemantic.ClspvReflection.1 extended instructions.
  // But *tweaked*:
  //    - to remove gl_WorkgroupSize
  //    - to move one of the ExtInsts into the globals-and-constants
  //      section
  //    - to move one of the ExtInsts into the function body.
  auto p = parser(test::Assemble(R"(
               OpCapability Shader
               OpExtension "SPV_KHR_storage_buffer_storage_class"
               OpExtension "SPV_KHR_non_semantic_info"
         %20 = OpExtInstImport "NonSemantic.ClspvReflection.1"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %15 "foo"
               OpSource OpenCL_C 120
         %21 = OpString "foo"
         %23 = OpString "A"
               OpDecorate %_runtimearr_uint ArrayStride 4
               OpMemberDecorate %_struct_3 0 Offset 0
               OpDecorate %_struct_3 Block
               OpDecorate %12 DescriptorSet 0
               OpDecorate %12 Binding 0
               OpDecorate %7 SpecId 0
               OpDecorate %8 SpecId 1
               OpDecorate %9 SpecId 2
         %24 = OpExtInst %void %20 ArgumentInfo %23
       %uint = OpTypeInt 32 0
%_runtimearr_uint = OpTypeRuntimeArray %uint
  %_struct_3 = OpTypeStruct %_runtimearr_uint
%_ptr_StorageBuffer__struct_3 = OpTypePointer StorageBuffer %_struct_3
     %v3uint = OpTypeVector %uint 3
%_ptr_Private_v3uint = OpTypePointer Private %v3uint
          %7 = OpSpecConstant %uint 1
          %8 = OpSpecConstant %uint 1
          %9 = OpSpecConstant %uint 1
       %void = OpTypeVoid
         %14 = OpTypeFunction %void
%_ptr_StorageBuffer_uint = OpTypePointer StorageBuffer %uint
     %uint_0 = OpConstant %uint 0
     %uint_1 = OpConstant %uint 1
     %uint_2 = OpConstant %uint 2
         %12 = OpVariable %_ptr_StorageBuffer__struct_3 StorageBuffer
         %15 = OpFunction %void Const %14
         %16 = OpLabel
         %19 = OpAccessChain %_ptr_StorageBuffer_uint %12 %uint_0 %uint_0
         %22 = OpExtInst %void %20 Kernel %15 %21
               OpReturn
               OpFunctionEnd
         %25 = OpExtInst %void %20 ArgumentStorageBuffer %22 %uint_0 %uint_0 %uint_0 %24
         %28 = OpExtInst %void %20 SpecConstantWorkgroupSize %uint_0 %uint_1 %uint_2
)"));
  EXPECT_TRUE(p->BuildAndParseInternalModule());
  EXPECT_TRUE(p->error().empty());
}

// TODO(dneto): We don't currently support other kinds of extended instruction
// imports.

}  // namespace
}  // namespace spirv
}  // namespace reader
}  // namespace tint
