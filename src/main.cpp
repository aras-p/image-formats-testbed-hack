#define _ITERATOR_DEBUG_LEVEL 0
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include "openexr/include/OpenEXR/ImfArray.h"
#include "openexr/include/OpenEXR/ImfChannelList.h"
#include "openexr/include/OpenEXR/ImfRgbaFile.h"
#include "openexr/include/OpenEXR/ImfStandardAttributes.h"
#include "sokol/sokol_time.h"
#include "xxHash/xxhash.h"
#include <sys/stat.h>
#ifdef _MSC_VER
#else
#include <sys/fcntl.h>
#endif
#include <thread>
#include "systeminfo.h"
#include "fileio.h"

const int kRunCount = 2;

struct CompressorTypeDesc
{
    const char* name;
    Imf::Compression cmp;
    const char* color;
    int large;
};

static const CompressorTypeDesc kComprTypes[] =
{
    {"Raw",     Imf::NUM_COMPRESSION_METHODS,   "a64436", 1}, // 0 - just raw bits read/write
    {"None",    Imf::NO_COMPRESSION,            "a64436", 1}, // 1, red
    {"RLE",     Imf::RLE_COMPRESSION,           "dc74ff", 0}, // 2, purple
    {"PIZ",     Imf::PIZ_COMPRESSION,           "ff9a44", 0}, // 3, orange
    {"Zips",    Imf::ZIPS_COMPRESSION,          "046f0e", 0}, // 4, dark green
    {"Zip",     Imf::ZIP_COMPRESSION,           "12b520", 0}, // 5, green
    {"Zstd",    Imf::ZSTD_COMPRESSION,          "0094ef", 1}, // 6, blue
};
constexpr size_t kComprTypeCount = sizeof(kComprTypes) / sizeof(kComprTypes[0]);

struct CompressorDesc
{
    int type;
    int level;
};

static const CompressorDesc kTestCompr[] =
{
    //{ 0, 0 }, // just raw bits read/write
    { 1, 0 }, // None
    { 2, 0 }, // RLE
    { 3, 0 }, // PIZ
    { 4, 0 }, // Zips

    // Zip
#if 1
    //{ 5, 0 },
    //{ 5, 1 },
    //{ 5, 2 },
    //{ 5, 3 },
    { 5, 4 },
    //{ 5, 5 },
    { 5, 6 }, // default
    //{ 5, 7 },
    //{ 5, 8 },
    //{ 5, 9 },
#endif

    // Zstd
#if 1
    //{ 6, -3 },
    //{ 6, -1 },
    { 6, 1 },
    //{ 6, 2 },
    //{ 6, 3 }, // default
    //{ 6, 4 },
    //{ 6, 5 },
    //{ 6, 6 },
    //{ 6, 8 },
    //{ 6, 10 },
    //{ 6, 12 },
    //{ 6, 14 },
    //{ 6, 16 },
    //{ 6, 18 },
    //{ 6, 20 },
    //{ 6, 22 },
#endif
};
constexpr size_t kTestComprCount = sizeof(kTestCompr) / sizeof(kTestCompr[0]);


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


struct ComprResult
{
    size_t rawSize = 0;
    size_t cmpSize = 0;
    double tRead = 0;
    double tWrite = 0;
};
static ComprResult s_ResultRuns[kTestComprCount][kRunCount];
static ComprResult s_Result[kTestComprCount];


static bool TestFile(const char* filePath, int runIndex)
{
    using namespace Imf;

    const char* fnamePart = strrchr(filePath, '/');
    printf("%s: ", fnamePart ? fnamePart+1 : filePath);
    
    // read the input file
    Array2D<Rgba> inPixels;
    int inWidth = 0, inHeight = 0;
    {
		MyIStream inStream(filePath);
        {
            RgbaInputFile inFile(inStream);
            const Header& inHeader = inFile.header();
            const ChannelList& channels = inHeader.channels();
            const auto dw = inFile.dataWindow();
            inWidth = dw.max.x - dw.min.x + 1;
            inHeight = dw.max.y - dw.min.y + 1;
            printf("%ix%i ", inWidth, inHeight);
            for(auto it = channels.begin(), itEnd = channels.end(); it != itEnd; ++it)
                printf("%s:%s ", it.name(), GetPixelType(it.channel().type));
            printf("\n");
            inPixels.resizeErase(inHeight, inWidth);
            inFile.setFrameBuffer(&inPixels[0][0] - dw.min.x - dw.min.y * inWidth, 1, inWidth);
            inFile.readPixels(dw.min.y, dw.max.y);
        }
    }

    // compute hash of pixel data
    const size_t rawSize = inWidth * inHeight * sizeof(Rgba);
    const uint64_t inPixelHash = XXH3_64bits(&inPixels[0][0], rawSize);
    
    // test various compression schemes
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& cmp = kTestCompr[cmpIndex];
        const auto cmpType = kComprTypes[cmp.type].cmp;
        const char* outFilePath = "_outfile.exr";
        //char outFilePath[1000];
        //sprintf(outFilePath, "_out%s-%i.exr", fnamePart+1, (int)cmpIndex);
        double tWrite = 0;
        double tRead = 0;
        // save the file with given compressor
        uint64_t tWrite0 = stm_now();
        if (cmpType == NUM_COMPRESSION_METHODS)
        {
            FILE* f = fopen(outFilePath, "wb");
            TurnOffFileCache(f);
            fwrite(&inPixels[0][0], inWidth*inHeight, sizeof(Rgba), f);
            fclose(f);
        }
        else
        {
            Header outHeader(inWidth, inHeight);
            outHeader.compression() = cmpType;
            if (cmp.level != 0)
            {
                if (cmpType == ZIP_COMPRESSION)
                    addZipCompressionLevel(outHeader, cmp.level);
                if (cmpType == ZSTD_COMPRESSION)
                    addZstdCompressionLevel(outHeader, cmp.level);
            }

			MyOStream outStream(outFilePath);
            {
                RgbaOutputFile outFile(outStream, outHeader);
                outFile.setFrameBuffer(&inPixels[0][0], 1, inWidth);
                outFile.writePixels(inHeight);
            }
        }
        tWrite = stm_sec(stm_since(tWrite0));
        size_t outSize = GetFileSize(outFilePath);
        
        // read the file back
#ifndef _MSC_VER
        int purgeVal = system("purge");
        if (purgeVal != 0)
            printf("WARN: failed to purge\n");
#endif
        
        Array2D<Rgba> gotPixels;
        int gotWidth = 0, gotHeight = 0;
        uint64_t tRead0 = stm_now();
        if (cmpType == NUM_COMPRESSION_METHODS)
        {
            FILE* f = fopen(outFilePath, "rb");
            TurnOffFileCache(f);
            gotWidth = inWidth;
            gotHeight = inHeight;
            gotPixels.resizeErase(gotHeight, gotWidth);
            fread(&gotPixels[0][0], gotWidth*gotHeight, sizeof(Rgba), f);
            fclose(f);
        }
        else
        {
			MyIStream gotStream(outFilePath);
            {
                RgbaInputFile gotFile(gotStream);
                const auto dw = gotFile.dataWindow();
                gotWidth = dw.max.x - dw.min.x + 1;
                gotHeight = dw.max.y - dw.min.y + 1;
                gotPixels.resizeErase(gotHeight, gotWidth);
                gotFile.setFrameBuffer(&gotPixels[0][0] - dw.min.x - dw.min.y * gotWidth, 1, gotWidth);
                gotFile.readPixels(dw.min.y, dw.max.y);
            }
        }
        tRead = stm_sec(stm_since(tRead0));
        const uint64_t gotPixelHash = XXH3_64bits(&gotPixels[0][0], gotWidth * gotHeight * sizeof(Rgba));
        if (gotPixelHash != inPixelHash)
        {
            printf("ERROR: file did not roundtrip exactly with compression %s\n", kComprTypes[cmp.type].name);
            return false;
        }

        auto& res = s_ResultRuns[cmpIndex][runIndex];
        res.rawSize += rawSize;
        res.cmpSize += outSize;
        res.tRead += tRead;
        res.tWrite += tWrite;
        
        /*
        double perfWrite = rawSize / (1024.0*1024.0) / tWrite;
        double perfRead = rawSize / (1024.0*1024.0) / tRead;
        printf("  %6s: %7.1f MB (%5.1f%%) W: %7.3f s (%5.0f MB/s) R: %7.3f s (%5.0f MB/s)\n",
               cmp.name,
               outSize/1024.0/1024.0,
               (double)outSize/(double)rawSize*100.0,
               tWrite,
               perfWrite,
               tRead,
               perfRead);
         */
        remove(outFilePath);
    }
    
    return true;
}

static void WriteReportRow(FILE* fout, uint64_t gotTypeMask, size_t cmpIndex, double xval, double yval)
{
    const int cmpLevel = kTestCompr[cmpIndex].level;
    const size_t typeIndex = kTestCompr[cmpIndex].type;
    const char* cmpName = kComprTypes[typeIndex].name;

    for (size_t ii = 0; ii < typeIndex; ++ii)
    {
        if ((gotTypeMask & (1ull<<ii)) == 0)
            continue;
        fprintf(fout, ",null,null");
    }
    fprintf(fout, ",%.2f,'", yval);
    if (cmpLevel != 0)
        fprintf(fout, "%s%i", cmpName, cmpLevel);
    else
        fprintf(fout, "%s", cmpName);
    fprintf(fout, ": %.3f ratio, %.1f MB/s'", xval, yval);
    for (size_t ii = typeIndex+1; ii < kComprTypeCount; ++ii)
    {
        if ((gotTypeMask & (1ull<<ii)) == 0)
            continue;
        fprintf(fout, ",null,null");
    }
}


static void WriteReportFile(int threadCount, int fileCount, size_t fullSize)
{
    std::string curTime = sysinfo_getcurtime();
    std::string outName = curTime + ".html";
    FILE* fout = fopen(outName.c_str(), "wb");
    fprintf(fout,
R"(<script type='text/javascript' src='https://www.gstatic.com/charts/loader.js'></script>
<center style='font-family: Arial;'>
<p><b>EXR compression ratio vs throughput</b>, %i files (%.1fMB) <span style='color: #ccc'>%s, %i runs</span</p>
<div style='border: 1px solid #ccc;'>
<div id='chart_w' style='width: 640px; height: 640px; display:inline-block;'></div>
<div id='chart_r' style='width: 640px; height: 640px; display:inline-block;'></div>
</div>
<p>%s, %s, %i threads</p>
<script type='text/javascript'>
google.charts.load('current', {'packages':['corechart']});
google.charts.setOnLoadCallback(drawChart);
function drawChart() {
var dw = new google.visualization.DataTable();
var dr = new google.visualization.DataTable();
dw.addColumn('number', 'Ratio');
dr.addColumn('number', 'Ratio');
)",
            fileCount, fullSize/1024.0/1024.0, curTime.c_str(), kRunCount,
            sysinfo_getplatform().c_str(), sysinfo_getcpumodel().c_str(), threadCount);

    uint64_t gotCmpTypeMask = 0;
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        gotCmpTypeMask |= 1ull << kTestCompr[cmpIndex].type;
    }

    for (size_t cmpType = 0; cmpType < kComprTypeCount; ++cmpType)
    {
        if ((gotCmpTypeMask & (1ull << cmpType)) == 0)
            continue;
        const auto& cmp = kComprTypes[cmpType];
        fprintf(fout,
R"(dw.addColumn('number', '%s'); dw.addColumn({type:'string', role:'tooltip'});
dr.addColumn('number', '%s'); dr.addColumn({type:'string', role:'tooltip'});
)", cmp.name, cmp.name);
    }
    fprintf(fout, "dw.addRows([\n");
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& res = s_Result[cmpIndex];
        double ratio = (double)res.rawSize/(double)res.cmpSize;
        fprintf(fout, "[%.3f", ratio);
        double perf = res.rawSize / (1024.0*1024.0) / res.tWrite;
        WriteReportRow(fout, gotCmpTypeMask, cmpIndex, ratio, perf);
        fprintf(fout, "]%s\n", cmpIndex == kTestComprCount-1 ? "" : ",");
    }
    fprintf(fout, "]);\n");
    fprintf(fout, "dr.addRows([\n");
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& res = s_Result[cmpIndex];
        double ratio = (double)res.rawSize/(double)res.cmpSize;
        fprintf(fout, "[%.3f", ratio);
        double perf = res.rawSize / (1024.0*1024.0) / res.tRead;
        WriteReportRow(fout, gotCmpTypeMask, cmpIndex, ratio, perf);
        fprintf(fout, "]%s\n", cmpIndex == kTestComprCount-1 ? "" : ",");
    }
    fprintf(fout, "]);\n");

    fprintf(fout,
R"(var options = {
    title: 'Writing',
    pointSize: 18,
    series: {
)");
    int seriesIdx = 0;
    for (size_t cmpType = 0; cmpType < kComprTypeCount; ++cmpType)
    {
        if ((gotCmpTypeMask & (1ull<<cmpType)) == 0)
            continue;
        fprintf(fout, "        %i: {pointSize: %i},\n", seriesIdx, kComprTypes[cmpType].large ? 18 : 8);
        ++seriesIdx;
    }
    fprintf(fout,
R"(        100:{}},
    hAxis: {title: 'Compression ratio', viewWindow: {min:1.0,max:4.0}},
    vAxis: {title: 'Writing, MB/s', viewWindow: {min:0, max:1000}},
    chartArea: {left:60, right:10, top:50, bottom:50},
    legend: {position: 'top'},
    colors: [
)");
    bool firstCol = true;
    for (size_t cmpType = 0; cmpType < kComprTypeCount; ++cmpType)
    {
        if ((gotCmpTypeMask & (1ull << cmpType)) == 0)
            continue;
        if (!firstCol)
            fprintf(fout, ",");
        firstCol = false;
        const auto& cmp = kComprTypes[cmpType];
        fprintf(fout, "'#%s'", cmp.color);
    }

    fprintf(fout,
R"(]
};
var chw = new google.visualization.ScatterChart(document.getElementById('chart_w'));
chw.draw(dw, options);

options.title = 'Reading';
options.vAxis.title = 'Reading, MB/s';
options.vAxis.viewWindow.max = 2500;
var chr = new google.visualization.ScatterChart(document.getElementById('chart_r'));
chr.draw(dr, options);
}
</script>
)");
    
    fclose(fout);
}

int main()
{
    unsigned nThreads = std::thread::hardware_concurrency();
    //nThreads = 0;
    printf("Setting OpenEXR to %i threads\n", nThreads);
    Imf::setGlobalThreadCount(nThreads);
    stm_setup();
    const char* kTestFiles[] = {
#if 1
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
        "blender/lone-monk.exr",
        "blender/monster_under_the_bed.exr",
        "houdini/AdrianA1.exr",
        "houdini/AdrianC1.exr",
        "UnityHDRI/GareoultWhiteBalanced.exr",
        "UnityHDRI/KirbyCoveWhiteBalanced.exr",
        
        "ACES/DigitalLAD.2048x1556.exr",
        "ACES/SonyF35.StillLife.exr",
#else
        "openexr-images/ScanLines/Blobbies.exr",
        "openexr-images/ScanLines/CandleGlass.exr",
        "openexr-images/ScanLines/Cannon.exr",
        "openexr-images/ScanLines/Desk.exr",
        "openexr-images/ScanLines/MtTamWest.exr",
        "openexr-images/ScanLines/PrismsLenses.exr",
        "openexr-images/ScanLines/StillLife.exr",
        "openexr-images/ScanLines/Tree.exr",
        "openexr-images/Tiles/GoldenGate.exr",
        "openexr-images/Tiles/Ocean.exr",
        "openexr-images/Tiles/Spirals.exr",
#endif
    };
    int fileCount = (int)(sizeof(kTestFiles)/sizeof(kTestFiles[0]));
    for (int ri = 0; ri < kRunCount; ++ri)
    {
        printf("Run %i/%i...\n", ri+1, kRunCount);
        for (int fi = 0; fi < fileCount; ++fi)
        {
            bool ok = TestFile(kTestFiles[fi], ri);
            if (!ok)
                return 1;
        }
        
        for (int ci = 0; ci < kTestComprCount; ++ci)
        {
            const ComprResult& res = s_ResultRuns[ci][ri];
            ComprResult& dst = s_Result[ci];
            if (ri == 0)
            {
                dst = res;
            }
            else
            {
                if (res.cmpSize != dst.cmpSize)
                {
                    printf("ERROR: compressor case %i non deterministic compressed size (%zi vs %zi)\n", ci, res.cmpSize, dst.cmpSize);
                    return 1;
                }
                if (res.rawSize != dst.rawSize)
                {
                    printf("ERROR: compressor case %i non deterministic raw size (%zi vs %zi)\n", ci, res.rawSize, dst.rawSize);
                    return 1;
                }
                if (res.tRead < dst.tRead) dst.tRead = res.tRead;
                if (res.tWrite < dst.tWrite) dst.tWrite = res.tWrite;
            }
        }
    }

    WriteReportFile(nThreads, fileCount, s_Result[0].rawSize);
    printf("==== Summary (%i files, %i runs):\n", fileCount, kRunCount);
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& cmp = kTestCompr[cmpIndex];
        const auto& res = s_Result[cmpIndex];

        double perfWrite = res.rawSize / (1024.0*1024.0) / res.tWrite;
        double perfRead = res.rawSize / (1024.0*1024.0) / res.tRead;
        printf("  %6s: %7.1f MB (%4.2fx) W: %6.3f s (%5.0f MB/s) R: %6.3f s (%5.0f MB/s)\n",
               kComprTypes[cmp.type].name,
               res.cmpSize/1024.0/1024.0,
               (double)res.rawSize/(double)res.cmpSize,
               res.tWrite,
               perfWrite,
               res.tRead,
               perfRead);
    }
    
    return 0;
}
