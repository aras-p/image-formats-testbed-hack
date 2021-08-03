#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include "openexr/include/OpenEXR/ImfArray.h"
#include "openexr/include/OpenEXR/ImfChannelList.h"
#include "openexr/include/OpenEXR/ImfRgbaFile.h"
#include "sokol/sokol_time.h"
#include "xxHash/xxhash.h"


static const char* GetComprName(Imf::Compression c)
{
    switch (c)
    {
    case Imf::NO_COMPRESSION: return "no";
    case Imf::RLE_COMPRESSION: return "rle";
    case Imf::ZIPS_COMPRESSION: return "zips";
    case Imf::ZIP_COMPRESSION: return "zip";
    case Imf::PIZ_COMPRESSION: return "piz";
    case Imf::PXR24_COMPRESSION: return "pxr24";
    case Imf::B44_COMPRESSION: return "b44";
    case Imf::B44A_COMPRESSION: return "b44a";
    case Imf::DWAA_COMPRESSION: return "dwaa";
    case Imf::DWAB_COMPRESSION: return "dwab";
    default: return "<unknown>";
    }
}

static const char* GetPixelType(Imf::PixelType p)
{
    switch (p)
    {
    case Imf::UINT: return "int";
    case Imf::HALF: return "half";
    case Imf::FLOAT: return "float";
    default: return "<unknown>";
    }
}

static bool TestFile(const char* filePath)
{
    using namespace Imf;

    const char* fnamePart = strrchr(filePath, '/');
    std::string filePathNoExt = filePath;
    filePathNoExt.resize(filePathNoExt.size()-4); // remove .exr
    printf("%20s: ", fnamePart ? fnamePart+1 : filePath);
    
    // read the input EXR file
    FILE* f = fopen(filePath, "rb");
    if (!f)
    {
        printf("ERROR: failed to open the file\n");
        return false;
    }
    fseek(f, 0, SEEK_END);
    long inFileSize = ftell(f);
    printf("%.1fMB ", inFileSize/1024.0/1024.0);
    fseek(f, 0, SEEK_SET);
    
    // read input via openexr
    Array2D<Rgba> inPixels;
    int inWidth = 0, inHeight = 0;
    {
        RgbaInputFile inFile(filePath);
        const Header& inHeader = inFile.header();
        const Compression inCompr = inFile.compression();
        const ChannelList& channels = inHeader.channels();
        const auto dw = inFile.dataWindow();
        inWidth = dw.max.x - dw.min.x + 1;
        inHeight = dw.max.y - dw.min.y + 1;
        printf("%ix%i, compr:%s ", inWidth, inHeight, GetComprName(inCompr));
        for(auto it = channels.begin(), itEnd = channels.end(); it != itEnd; ++it)
            printf("%s:%s ", it.name(), GetPixelType(it.channel().type));
        printf("\n");
        inPixels.resizeErase(inWidth, inHeight);
        inFile.setFrameBuffer(&inPixels[0][0] - dw.min.x - dw.min.y * inWidth, 1, inWidth);
        inFile.readPixels(dw.min.y, dw.max.y);
    }
    
    // save
    const Compression kTestCompr[] =
    {
        NO_COMPRESSION,
        RLE_COMPRESSION,
        ZIPS_COMPRESSION,
        ZIP_COMPRESSION,
        PIZ_COMPRESSION
    };
    for (auto cmp : kTestCompr)
    {
        std::string outFilePath = filePathNoExt + "-" + GetComprName(cmp) + ".exr";
        RgbaOutputFile outFile(
                               outFilePath.c_str(), inWidth, inHeight, WRITE_RGBA,
                               1, // pixelAspectRatio
                               IMATH_NAMESPACE::V2f (0, 0), // screenWindowCenter
                               1, // screenWindowWidth
                               INCREASING_Y, // lineOrder
                               cmp);
        outFile.setFrameBuffer(&inPixels[0][0], 1, inWidth);
        outFile.writePixels(inHeight);
    }
    
    return true;
}


int main()
{
    stm_setup();
    TestFile("graphicstests/Explosion0_01_5x5.exr");
    TestFile("graphicstests/ReflectionProbe-0.exr");
    return 0;
}
