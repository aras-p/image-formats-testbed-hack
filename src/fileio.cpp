#include "fileio.h"
#include "openexr/include/OpenEXR/Iex.h"

void TurnOffFileCache(FILE* f)
{
    /*
     //@TODO: looks like this turns off a whole bunch of caching somwhere in CRT level too, blergh
    int fd = fileno(f);
    int err = fcntl(fd, F_NOCACHE, 1);
    if (err != 0)
    {
        throw new std::string("Argh");
    }
     */
}


bool C_IStream::read (char c[/*n*/], int n)
{
    if (n != static_cast<int>(fread (c, 1, n, _file)))
    {
        // fread() failed, but the return value does not distinguish
        // between I/O errors and end of file, so we call ferror() to
        // determine what happened.
        //if (ferror (_file))
        //    IEX_NAMESPACE::throwErrnoExc();
        //else
            throw IEX_NAMESPACE::InputExc ("Unexpected end of file.");
    }
    return feof (_file);
}
uint64_t C_IStream::tellg ()
{
    return ftell (_file);
}
void C_IStream::seekg (uint64_t pos)
{
    clearerr (_file);
    fseek (_file, static_cast<long>(pos), SEEK_SET);
}
void C_IStream::clear ()
{
    clearerr (_file);
}
void C_OStream::write (const char c[/*n*/], int n)
{
    clearerr (_file);
    if (n != static_cast<int>(fwrite (c, 1, n, _file)))
        //IEX_NAMESPACE::throwErrnoExc();
        throw IEX_NAMESPACE::InputExc ("Failed to write.");
}
uint64_t C_OStream::tellp()
{
    return ftell (_file);
}
void C_OStream::seekp (uint64_t pos)
{
    clearerr (_file);
    fseek (_file, static_cast<long>(pos), SEEK_SET);
}

size_t GetFileSize(const char* path)
{
    struct stat st = {};
    stat(path, &st);
    return st.st_size;
}
