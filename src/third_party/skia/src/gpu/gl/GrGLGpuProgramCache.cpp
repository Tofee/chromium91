/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <memory>

#include "src/gpu/gl/GrGLGpu.h"

#include "include/gpu/GrContextOptions.h"
#include "include/gpu/GrDirectContext.h"
#include "src/gpu/GrDirectContextPriv.h"
#include "src/gpu/GrProcessor.h"
#include "src/gpu/GrProgramDesc.h"
#include "src/gpu/gl/builders/GrGLProgramBuilder.h"
#include "src/gpu/glsl/GrGLSLFragmentProcessor.h"

struct GrGLGpu::ProgramCache::Entry {
    Entry(sk_sp<GrGLProgram> program)
        : fProgram(std::move(program)) {}

    Entry(const GrGLPrecompiledProgram& precompiledProgram)
        : fPrecompiledProgram(precompiledProgram) {}

    sk_sp<GrGLProgram> fProgram;
    GrGLPrecompiledProgram fPrecompiledProgram;
};

GrGLGpu::ProgramCache::ProgramCache(int runtimeProgramCacheSize)
    : fMap(runtimeProgramCacheSize) {
}

GrGLGpu::ProgramCache::~ProgramCache() {}

void GrGLGpu::ProgramCache::abandon() {
    fMap.foreach([](GrProgramDesc*, std::unique_ptr<Entry>* e) {
        if ((*e)->fProgram) {
            (*e)->fProgram->abandon();
        }
    });

    this->reset();
}

void GrGLGpu::ProgramCache::reset() {
    fMap.reset();
}

sk_sp<GrGLProgram> GrGLGpu::ProgramCache::findOrCreateProgram(GrDirectContext* dContext,
                                                              GrRenderTarget* renderTarget,
                                                              const GrProgramInfo& programInfo) {
    const GrCaps* caps = dContext->priv().caps();

    GrProgramDesc desc = caps->makeDesc(renderTarget, programInfo);
    if (!desc.isValid()) {
        GrCapsDebugf(caps, "Failed to gl program descriptor!\n");
        return nullptr;
    }

    Stats::ProgramCacheResult stat;
    sk_sp<GrGLProgram> tmp = this->findOrCreateProgram(dContext, renderTarget, desc,
                                                       programInfo, &stat);
    if (!tmp) {
        fStats.incNumInlineCompilationFailures();
    } else {
        fStats.incNumInlineProgramCacheResult(stat);
    }

    return tmp;
}

sk_sp<GrGLProgram> GrGLGpu::ProgramCache::findOrCreateProgram(GrDirectContext* dContext,
                                                              const GrProgramDesc& desc,
                                                              const GrProgramInfo& programInfo,
                                                              Stats::ProgramCacheResult* stat) {
    sk_sp<GrGLProgram> tmp = this->findOrCreateProgram(dContext, nullptr, desc, programInfo, stat);
    if (!tmp) {
        fStats.incNumPreCompilationFailures();
    } else {
        fStats.incNumPreProgramCacheResult(*stat);
    }

    return tmp;
}

sk_sp<GrGLProgram> GrGLGpu::ProgramCache::findOrCreateProgram(GrDirectContext* dContext,
                                                              GrRenderTarget* renderTarget,
                                                              const GrProgramDesc& desc,
                                                              const GrProgramInfo& programInfo,
                                                              Stats::ProgramCacheResult* stat) {
    *stat = Stats::ProgramCacheResult::kHit;
    std::unique_ptr<Entry>* entry = fMap.find(desc);
    if (entry && !(*entry)->fProgram) {
        // We've pre-compiled the GL program, but don't have the GrGLProgram scaffolding
        const GrGLPrecompiledProgram* precompiledProgram = &((*entry)->fPrecompiledProgram);
        SkASSERT(precompiledProgram->fProgramID != 0);
        (*entry)->fProgram = GrGLProgramBuilder::CreateProgram(dContext, renderTarget, desc,
                                                               programInfo, precompiledProgram);
        if (!(*entry)->fProgram) {
            // Should we purge the program ID from the cache at this point?
            SkDEBUGFAIL("Couldn't create program from precompiled program");
            fStats.incNumCompilationFailures();
            return nullptr;
        }
        fStats.incNumPartialCompilationSuccesses();
        *stat = Stats::ProgramCacheResult::kPartial;
    } else if (!entry) {
        // We have a cache miss
        sk_sp<GrGLProgram> program = GrGLProgramBuilder::CreateProgram(dContext, renderTarget,
                                                                       desc, programInfo);
        if (!program) {
            fStats.incNumCompilationFailures();
            return nullptr;
        }
        fStats.incNumCompilationSuccesses();
        entry = fMap.insert(desc, std::make_unique<Entry>(std::move(program)));
        *stat = Stats::ProgramCacheResult::kMiss;
    }

    return (*entry)->fProgram;
}

bool GrGLGpu::ProgramCache::precompileShader(GrDirectContext* dContext,
                                             const SkData& key,
                                             const SkData& data) {
    GrProgramDesc desc;
    if (!GrProgramDesc::BuildFromData(&desc, key.data(), key.size())) {
        return false;
    }

    std::unique_ptr<Entry>* entry = fMap.find(desc);
    if (entry) {
        // We've already seen/compiled this shader
        return true;
    }

    GrGLPrecompiledProgram precompiledProgram;
    if (!GrGLProgramBuilder::PrecompileProgram(dContext, &precompiledProgram, data)) {
        return false;
    }

    fMap.insert(desc, std::make_unique<Entry>(precompiledProgram));
    return true;
}
