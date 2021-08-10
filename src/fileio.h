#pragma once
#define _ITERATOR_DEBUG_LEVEL 0
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include "openexr/include/OpenEXR/ImfIO.h"

void TurnOffFileCache(FILE* f);
size_t GetFileSize(const char* path);


class C_IStream : public Imf::IStream
{
public:
    C_IStream (FILE *file, const char fileName[])
    : IStream (fileName), _file (file)
    {
        TurnOffFileCache(file);
    }
    virtual bool read(char c[/*n*/], int n);
    virtual uint64_t tellg();
    virtual void seekg(uint64_t pos);
    virtual void clear();
private:
    FILE* _file;
};
class C_OStream : public Imf::OStream
{
public:
    C_OStream (FILE *file, const char fileName[])
    : OStream (fileName), _file (file)
    {
        TurnOffFileCache(file);
    }
    virtual void write(const char c[/*n*/], int n);
    virtual uint64_t tellp();
    virtual void seekp(uint64_t pos);
private:
    FILE* _file;
};
