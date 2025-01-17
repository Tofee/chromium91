#!/bin/bash

#  Copyright The ANGLE Project Authors. All rights reserved.
#  Use of this source code is governed by a BSD-style license that can be
#  found in the LICENSE file.
#
# Generates a roll CL within the ANGLE repository of AOSP.

# exit when any command fails
set -e

# Change the working directory to the ANGLE root directory
cd "${0%/*}/.."

GN_OUTPUT_DIRECTORY=out/Android

function generate_Android_bp_file() {
    rm -rf ${GN_OUTPUT_DIRECTORY}

    abis=(
        "arm"
        "arm64"
        "x86"
        "x64"
    )

    for abi in ${abis[@]}; do
        # generate gn build files and convert them to blueprints
        gn_args=(
            "target_os = \"android\""
            "is_component_build = false"
            "is_debug = false"

            # Build for 64-bit CPUs
            "target_cpu = \"$abi\""

            # Target ndk API 26 to make sure ANGLE can use the Vulkan backend on Android
            "android32_ndk_api_level = 26"
            "android64_ndk_api_level = 26"

            # Disable all backends except Vulkan
            "angle_enable_vulkan = true"
            "angle_enable_gl = true" # TODO(geofflang): Disable GL once Andrid no longer requires it. anglebug.com/4444
            "angle_enable_d3d9 = false"
            "angle_enable_d3d11 = false"
            "angle_enable_null = false"
            "angle_enable_metal = false"

            # SwiftShader is loaded as the system Vulkan driver on Android, not compiled by ANGLE
            "angle_enable_swiftshader = false"

            # Disable all shader translator targets except desktop GL (for Vulkan)
            "angle_enable_essl = true" # TODO(geofflang): Disable ESSL once Andrid no longer requires it. anglebug.com/4444
            "angle_enable_glsl = true" # TODO(geofflang): Disable ESSL once Andrid no longer requires it. anglebug.com/4444
            "angle_enable_hlsl = false"

            "angle_enable_commit_id = false"

            # Disable histogram/protobuf support
            "angle_has_histograms = false"

            # Disable _LIBCPP_ABI_UNSTABLE, since it breaks std::string
            "libcxx_abi_unstable = false"
        )

        if [[ "$1" == "--enableApiTrace" ]]; then
            gn_args=(
                "${gn_args[@]}"
                "angle_enable_trace = true"
                "angle_enable_trace_android_logcat = true"
            )
        fi

        gn gen ${GN_OUTPUT_DIRECTORY} --args="${gn_args[*]}"
        gn desc ${GN_OUTPUT_DIRECTORY} --format=json "*" > ${GN_OUTPUT_DIRECTORY}/desc.$abi.json
    done

    python scripts/generate_android_bp.py \
        ${GN_OUTPUT_DIRECTORY}/desc.arm.json \
        ${GN_OUTPUT_DIRECTORY}/desc.arm64.json \
        ${GN_OUTPUT_DIRECTORY}/desc.x86.json \
        ${GN_OUTPUT_DIRECTORY}/desc.x64.json > Android.bp

    rm -rf ${GN_OUTPUT_DIRECTORY}
}


if [[ "$1" == "--genAndroidBp" ]];then
    generate_Android_bp_file "$2"
    exit 0
fi

# Check out depot_tools locally and add it to the path
DEPOT_TOOLS_DIR=_depot_tools
rm -rf ${DEPOT_TOOLS_DIR}
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git ${DEPOT_TOOLS_DIR}
export PATH=`pwd`/${DEPOT_TOOLS_DIR}:$PATH

deps=(
    "third_party/abseil-cpp"
    "third_party/jsoncpp"
    "third_party/jsoncpp/source"
    "third_party/vulkan-deps/glslang/src"
    "third_party/vulkan-deps/spirv-headers/src"
    "third_party/vulkan-deps/spirv-tools/src"
    "third_party/vulkan-deps/vulkan-headers/src"
    "third_party/vulkan_memory_allocator"
    "third_party/zlib"
)

# Only add the parts of NDK and vulkan-deps that are required by ANGLE. The entire dep is too large.
delete_only_deps=(
    "third_party/android_ndk"
    "third_party/vulkan-deps"
)

add_only_deps=(
    "third_party/android_ndk/sources/android/cpufeatures"
)

# Delete dep directories so that gclient can check them out
for dep in ${deps[@]} ${delete_only_deps[@]}; do
    rm -rf $dep
done

# Sync all of ANGLE's deps so that 'gn gen' works
python scripts/bootstrap.py
gclient sync --reset --force --ignore_locks --delete_unversioned_trees --break_repo_locks

generate_Android_bp_file

git add Android.bp

# Delete the .git files in each dep so that it can be added to this repo. Some deps like jsoncpp
# have multiple layers of deps so delete everything before adding them.
for dep in ${deps[@]} ${delete_only_deps[@]}; do
   rm -rf $dep/.git
done

extra_removal_files=(
   # Some third_party deps have OWNERS files which contains users that have not logged into
   # the Android gerrit. Repo cannot upload with these files present.
   "third_party/abseil-cpp/OWNERS"
   "third_party/jsoncpp/OWNERS"
   "third_party/vulkan_memory_allocator/OWNERS"
   "third_party/zlib/OWNERS"
   "third_party/zlib/google/OWNERS"
   "third_party/zlib/contrib/tests/OWNERS"
   "third_party/zlib/contrib/bench/OWNERS"
   "third_party/zlib/contrib/tests/fuzzers/OWNERS"
)

for removal_file in ${extra_removal_files[@]}; do
   rm -f $removal_file
done

for dep in ${deps[@]} ${add_only_deps[@]}; do
   git add -f $dep
done

# Done with depot_tools
rm -rf $DEPOT_TOOLS_DIR
