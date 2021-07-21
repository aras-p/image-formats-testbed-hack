#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "tinyexr/tinyexr.h"

static bool TestFile(const char* filePath)
{
    const char* fnamePart = strrchr(filePath, '/');
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
    
    uint8_t* inFileData = (uint8_t*)malloc(inFileSize);
    fread(inFileData, 1, inFileSize, f);
    fclose(f);

    float* pixels;
    int width, height;
    const char* err = nullptr;
    int ret = LoadEXRFromMemory(&pixels, &width, &height, inFileData, inFileSize, &err);
    free(inFileData);
    if (ret != TINYEXR_SUCCESS)
    {
        if (err)
        {
            printf("ERROR: failed to load EXR %s\n", err);
            FreeEXRErrorMessage(err);
        }
        return false;
    }
    printf("%ix%i ", width, height);
    free(pixels);
    
    return true;
}

int main()
{
    TestFile("graphicstests/ReflectionProbe-0.exr");
    return 0;
}
