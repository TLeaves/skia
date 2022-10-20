/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_TextureProxy_DEFINED
#define skgpu_graphite_TextureProxy_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "include/gpu/graphite/TextureInfo.h"

enum SkColorType : int;

namespace skgpu::graphite {

class Caps;
enum class Renderable : bool;
class ResourceProvider;
class Texture;

class TextureProxy : public SkRefCnt {
public:
    TextureProxy(SkISize dimensions, const TextureInfo& info, SkBudgeted budgeted);
    TextureProxy(sk_sp<Texture>);

    ~TextureProxy() override;

    int numSamples() const { return fInfo.numSamples(); }
    Mipmapped mipmapped() const { return fInfo.mipmapped(); }

    SkISize dimensions() const { return fDimensions; }
    const TextureInfo& textureInfo() const { return fInfo; }

    bool instantiate(ResourceProvider*);
    bool isInstantiated() const { return SkToBool(fTexture); }
    sk_sp<Texture> refTexture() const;
    const Texture* texture() const;

    static sk_sp<TextureProxy> Make(const Caps*,
                                    SkISize dimensions,
                                    SkColorType,
                                    Mipmapped,
                                    Protected,
                                    Renderable,
                                    SkBudgeted);

private:
#ifdef SK_DEBUG
    void validateTexture(const Texture*);
#endif

    SkISize fDimensions;
    TextureInfo fInfo;

    SkBudgeted fBudgeted;

    sk_sp<Texture> fTexture;
};

} // namepsace skgpu::graphite

#endif // skgpu_graphite_TextureProxy_DEFINED
