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
#include <sys/stat.h>
#include <thread>

static size_t GetFileSize(const char* path)
{
    struct stat st = {};
    stat(path, &st);
    return st.st_size;
}

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
    case Imf::NUM_COMPRESSION_METHODS: return "raw";
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

const Imf::Compression kTestCompr[] =
{
    Imf::NUM_COMPRESSION_METHODS, // just raw bits read/write
    Imf::NO_COMPRESSION,
    Imf::RLE_COMPRESSION,
    Imf::ZIPS_COMPRESSION,
    Imf::ZIP_COMPRESSION,
    Imf::PIZ_COMPRESSION
};
constexpr size_t kTestComprCount = sizeof(kTestCompr) / sizeof(kTestCompr[0]);

struct ComprResult
{
    size_t rawSize = 0;
    size_t cmpSize = 0;
    double tRead = 0;
    double tWrite = 0;
};
static ComprResult s_Results[kTestComprCount];


static bool TestFile(const char* filePath)
{
    using namespace Imf;

    const char* fnamePart = strrchr(filePath, '/');
    printf("%20s: ", fnamePart ? fnamePart+1 : filePath);
    
    // read the input file
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
        inPixels.resizeErase(inHeight, inWidth);
        inFile.setFrameBuffer(&inPixels[0][0] - dw.min.x - dw.min.y * inWidth, 1, inWidth);
        inFile.readPixels(dw.min.y, dw.max.y);
    }

    // compute hash of pixel data
    const size_t rawSize = inWidth * inHeight * sizeof(Rgba);
    const uint64_t inPixelHash = XXH3_64bits(&inPixels[0][0], rawSize);
    
    // test various compression schemes
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        Imf::Compression cmp = kTestCompr[cmpIndex];
        const char* outFilePath = "_outfile.exr";
        double tWrite = 0;
        double tRead = 0;
        // save the file with given compressor
        uint64_t tWrite0 = stm_now();
        if (cmp == NUM_COMPRESSION_METHODS)
        {
            FILE* f = fopen(outFilePath, "wb");
            fwrite(&inPixels[0][0], inWidth*inHeight, sizeof(Rgba), f);
            fclose(f);
        }
        else
        {
            RgbaOutputFile outFile(
                                   outFilePath, inWidth, inHeight, WRITE_RGBA,
                                   1, // pixelAspectRatio
                                   IMATH_NAMESPACE::V2f (0, 0), // screenWindowCenter
                                   1, // screenWindowWidth
                                   INCREASING_Y, // lineOrder
                                   cmp);
            outFile.setFrameBuffer(&inPixels[0][0], 1, inWidth);
            outFile.writePixels(inHeight);
        }
        tWrite = stm_sec(stm_since(tWrite0));
        size_t outSize = GetFileSize(outFilePath);
        
        // read the file back
        Array2D<Rgba> gotPixels;
        int gotWidth = 0, gotHeight = 0;
        uint64_t tRead0 = stm_now();
        if (cmp == NUM_COMPRESSION_METHODS)
        {
            FILE* f = fopen(outFilePath, "rb");
            gotWidth = inWidth;
            gotHeight = inHeight;
            gotPixels.resizeErase(gotHeight, gotWidth);
            fread(&gotPixels[0][0], gotWidth*gotHeight, sizeof(Rgba), f);
            fclose(f);
        }
        else
        {
            RgbaInputFile gotFile(outFilePath);
            const auto dw = gotFile.dataWindow();
            gotWidth = dw.max.x - dw.min.x + 1;
            gotHeight = dw.max.y - dw.min.y + 1;
            gotPixels.resizeErase(gotHeight, gotWidth);
            gotFile.setFrameBuffer(&gotPixels[0][0] - dw.min.x - dw.min.y * gotWidth, 1, gotWidth);
            gotFile.readPixels(dw.min.y, dw.max.y);
        }
        tRead = stm_sec(stm_since(tRead0));
        const uint64_t gotPixelHash = XXH3_64bits(&gotPixels[0][0], gotWidth * gotHeight * sizeof(Rgba));
        if (gotPixelHash != inPixelHash)
        {
            printf("ERROR: file did not roundtrip exactly with compression %s\n", GetComprName(cmp));
            return false;
        }

        auto& res = s_Results[cmpIndex];
        res.rawSize += rawSize;
        res.cmpSize += outSize;
        res.tRead += tRead;
        res.tWrite += tWrite;
        
        double perfWrite = rawSize / (1024.0*1024.0) / tWrite;
        double perfRead = rawSize / (1024.0*1024.0) / tRead;
        printf("  %6s: %7.1f MB (%5.1f%%) W: %7.3f s (%5.0f MB/s) R: %7.3f s (%5.0f MB/s)\n",
               GetComprName(cmp),
               outSize/1024.0/1024.0,
               (double)outSize/(double)rawSize*100.0,
               tWrite,
               perfWrite,
               tRead,
               perfRead);
        remove(outFilePath);
    }
    
    return true;
}


int main()
{
    unsigned nThreads = std::thread::hardware_concurrency();
    printf("Setting OpenEXR to %i threads\n", nThreads);
    Imf::setGlobalThreadCount(nThreads);
    stm_setup();
    const char* kTestFiles[] = {
        "graphicstests/21_DepthBuffer.exr",
        "graphicstests/Distord_Test.exr",
        "graphicstests/Explosion0_01_5x5.exr",
        "graphicstests/Lightmap-0_comp_light.exr",
        "graphicstests/Lightmap-1_comp_light.exr",
        "graphicstests/ReflectionProbe-0.exr",
        "graphicstests/ReflectionProbe-2.exr",
        "graphicstests/Treasure Island - White balanced.exr",
        "polyhaven/lilienstein_4k.exr",
        "polyhaven/rocks_ground_02_nor_gl_4k.exr",
        "UnityHDRI/GareoultWhiteBalanced.exr",
        "UnityHDRI/KirbyCoveWhiteBalanced.exr",
        "ACES/DigitalLAD.2048x1556.exr",
        "ACES/SonyF35.StillLife.exr",
    };
    for (auto tf : kTestFiles)
        TestFile(tf);

    printf("==== Summary:\n");
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        Imf::Compression cmp = kTestCompr[cmpIndex];
        const auto& res = s_Results[cmpIndex];

        double perfWrite = res.rawSize / (1024.0*1024.0) / res.tWrite;
        double perfRead = res.rawSize / (1024.0*1024.0) / res.tRead;
        printf("  %6s: %7.1f MB (%5.1f%%) W: %7.3f s (%5.0f MB/s) R: %7.3f s (%5.0f MB/s)\n",
               GetComprName(cmp),
               res.cmpSize/1024.0/1024.0,
               (double)res.cmpSize/(double)res.rawSize*100.0,
               res.tWrite,
               perfWrite,
               res.tRead,
               perfRead);
    }
    
    return 0;
}
