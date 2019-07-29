/*
 * DISTRHO Plugin Framework (DPF)
 * Copyright (C) 2012-2019 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "SVG.hpp"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"


START_NAMESPACE_DGL

SVG::SVG()
    : fSize(),
      fRGBAData(nullptr)
{
}

void SVG::loadFromMemory(const char* const rawData, const uint dataSize, const float scaling) noexcept
{
    DISTRHO_SAFE_ASSERT_RETURN(rawData != nullptr, )
    DISTRHO_SAFE_ASSERT_RETURN(scaling > 0.0f, )

    if (fRGBAData != nullptr)
    {
        free(fRGBAData);
        fRGBAData = nullptr;
    }
    
    // nsvgParse modifies the input data, so we must use a temporary buffer
    char* tmpBuffer = (char*)malloc(dataSize);

    DISTRHO_SAFE_ASSERT_RETURN(tmpBuffer != nullptr, )

    strncpy(tmpBuffer, rawData, dataSize);
    tmpBuffer[dataSize - 1] = '\0';

    const float dpi = 96;

    NSVGimage* image = nsvgParse(tmpBuffer, "px", dpi);

    free(tmpBuffer);

    DISTRHO_SAFE_ASSERT_RETURN(image != nullptr, )

    const uint scaledWidth = image->width * scaling;
    const uint scaledHeight = image->height * scaling;

    DISTRHO_SAFE_ASSERT_RETURN(scaledWidth > 0 && scaledHeight > 0, )

    fRGBAData = (unsigned char*)malloc(scaledWidth * scaledHeight * 4);

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    nsvgRasterize(rasterizer, image, 0, 0, scaling, fRGBAData, scaledWidth, scaledHeight, scaledWidth * 4);

    fSize.setSize(scaledWidth, scaledHeight);

    nsvgDelete(image);
    nsvgDeleteRasterizer(rasterizer);
}

SVG::~SVG()
{
    free(fRGBAData);
}
        
const Size<uint>& SVG::getSize() const noexcept
{
    return fSize;
}

const unsigned char* SVG::getRGBAData() const noexcept
{
    return fRGBAData;
}

bool SVG::isValid() const noexcept
{
    return (fRGBAData != nullptr && fSize.isValid());
}

END_NAMESPACE_DGL
