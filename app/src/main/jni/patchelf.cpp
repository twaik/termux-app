/*
 *  PatchELF is a utility to modify properties of ELF executables and libraries
 *  Copyright (C) 2004-2016  Eelco Dolstra <edolstra@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <memory>
#include <sstream>
#include <limits>
#include <stdexcept>

#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cassert>
#include <cstring>
#include <cerrno>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "elf.h"

using namespace std;
typedef std::shared_ptr<std::vector<unsigned char>> FileContents;


#define ElfFileParams class Elf_Ehdr, class Elf_Phdr, class Elf_Shdr, class Elf_Addr, class Elf_Off, class Elf_Dyn, class Elf_Sym, class Elf_Verneed
#define ElfFileParamNames Elf_Ehdr, Elf_Phdr, Elf_Shdr, Elf_Addr, Elf_Off, Elf_Dyn, Elf_Sym, Elf_Verneed

template<ElfFileParams>
class ElfFile
{
public:

    const FileContents fileContents;

private:

    unsigned char * contents;

    Elf_Ehdr * hdr;
    std::vector<Elf_Phdr> phdrs;
    std::vector<Elf_Shdr> shdrs;

    bool littleEndian;

    typedef std::string SectionName;

    std::string sectionNames; /* content of the .shstrtab section */

    /* Align on 4 or 8 bytes boundaries on 32- or 64-bit platforms
       respectively. */
    size_t sectionAlignment = sizeof(Elf_Off);

    std::vector<SectionName> sectionsByOldIndex;

public:

    ElfFile(FileContents fileContents);

private:
    std::string getSectionName(const Elf_Shdr & shdr);
    Elf_Shdr & findSection(const SectionName & sectionName);
    Elf_Shdr * findSection2(const SectionName & sectionName);
    unsigned int findSection3(const SectionName & sectionName);

public:
    char** dlneeds();

private:
    /* Convert an integer in big or little endian representation (as
       specified by the ELF header) to this platform's integer
       representation. */
    template<class I>
    I rdi(I i);
};


/* !!! G++ creates broken code if this function is inlined, don't know
   why... */
template<ElfFileParams>
template<class I>
I ElfFile<ElfFileParamNames>::rdi(I i)
{
    I r = 0;
    if (littleEndian) {
        for (unsigned int n = 0; n < sizeof(I); ++n) {
            r |= ((I) *(((unsigned char *) &i) + n)) << (n * 8);
        }
    } else {
        for (unsigned int n = 0; n < sizeof(I); ++n) {
            r |= ((I) *(((unsigned char *) &i) + n)) << ((sizeof(I) - n - 1) * 8);
        }
    }
    return r;
}


/* Ugly: used to erase DT_RUNPATH when using --force-rpath. */
#define DT_IGNORE       0x00726e67


void fmt2(std::ostringstream & out)
{
}


template<typename T, typename... Args>
void fmt2(std::ostringstream & out, T x, Args... args)
{
    out << x;
    fmt2(out, args...);
}


template<typename... Args>
std::string fmt(Args... args)
{
    std::ostringstream out;
    fmt2(out, args...);
    return out.str();
}


struct SysError : std::runtime_error
{
    int errNo;
    SysError(const std::string & msg)
        : std::runtime_error(fmt(msg + ": " + strerror(errno)))
        , errNo(errno)
    { }
};


__attribute__((noreturn)) static void error(std::string msg)
{
    if (errno)
        throw SysError(msg);
    else
        throw std::runtime_error(msg);
}

__attribute__((noreturn)) static void error(const char* msg)
{
    error(std::string(msg));
}

static FileContents readFile(std::string fileName,
    size_t cutOff = std::numeric_limits<size_t>::max())
{
    struct stat st;
    if (stat(fileName.c_str(), &st) != 0)
        throw SysError(fmt("getting info about '", fileName, "'"));

    if ((uint64_t) st.st_size > (uint64_t) std::numeric_limits<size_t>::max())
        throw SysError(fmt("cannot read file of size ", st.st_size, " into memory"));

    size_t size = std::min(cutOff, (size_t) st.st_size);

    FileContents contents = std::make_shared<std::vector<unsigned char>>();
    contents->reserve(size + 32 * 1024 * 1024);
    contents->resize(size, 0);

    int fd = open(fileName.c_str(), O_RDONLY);
    if (fd == -1) throw SysError(fmt("opening '", fileName, "'"));

    size_t bytesRead = 0;
    ssize_t portion;
    while ((portion = read(fd, contents->data() + bytesRead, size - bytesRead)) > 0)
        bytesRead += portion;

    if (bytesRead != size)
        throw SysError(fmt("reading '", fileName, "'"));

    close(fd);

    return contents;
}


struct ElfType
{
    bool is32Bit;
    int machine; // one of EM_*
};


ElfType getElfType(const FileContents & fileContents)
{
    /* Check the ELF header for basic validity. */
    if (fileContents->size() < (off_t) sizeof(Elf32_Ehdr)) error("missing ELF header");

    auto contents = fileContents->data();

    if (memcmp(contents, ELFMAG, SELFMAG) != 0)
        error("not an ELF executable");

    if (contents[EI_VERSION] != EV_CURRENT)
        error("unsupported ELF version");

    if (contents[EI_CLASS] != ELFCLASS32 && contents[EI_CLASS] != ELFCLASS64)
        error("ELF executable is not 32 or 64 bit");

    bool is32Bit = contents[EI_CLASS] == ELFCLASS32;

    // FIXME: endianness
    return ElfType{is32Bit, is32Bit ? ((Elf32_Ehdr *) contents)->e_machine : ((Elf64_Ehdr *) contents)->e_machine};
}


static void checkPointer(const FileContents & contents, void * p, unsigned int size)
{
    unsigned char * q = (unsigned char *) p;
    assert(q >= contents->data() && q + size <= contents->data() + contents->size());
}


template<ElfFileParams>
ElfFile<ElfFileParamNames>::ElfFile(FileContents fileContents)
    : fileContents(fileContents)
    , contents(fileContents->data())
{
    /* Check the ELF header for basic validity. */
    if (fileContents->size() < (off_t) sizeof(Elf_Ehdr)) error("missing ELF header");

    hdr = (Elf_Ehdr *) fileContents->data();

    if (memcmp(hdr->e_ident, ELFMAG, SELFMAG) != 0)
        error("not an ELF executable");

    littleEndian = hdr->e_ident[EI_DATA] == ELFDATA2LSB;

    if (rdi(hdr->e_type) != ET_EXEC && rdi(hdr->e_type) != ET_DYN)
        error("wrong ELF type");

    if ((size_t) (rdi(hdr->e_phoff) + rdi(hdr->e_phnum) * rdi(hdr->e_phentsize)) > fileContents->size())
        error("program header table out of bounds");

    if (rdi(hdr->e_shnum) == 0)
        error("no section headers. The input file is probably a statically linked, self-decompressing binary");

    if ((size_t) (rdi(hdr->e_shoff) + rdi(hdr->e_shnum) * rdi(hdr->e_shentsize)) > fileContents->size())
        error("section header table out of bounds");

    if (rdi(hdr->e_phentsize) != sizeof(Elf_Phdr))
        error("program headers have wrong size");

    /* Copy the program and section headers. */
    for (int i = 0; i < rdi(hdr->e_phnum); ++i) {
        phdrs.push_back(* ((Elf_Phdr *) (contents + rdi(hdr->e_phoff)) + i));
    }

    for (int i = 0; i < rdi(hdr->e_shnum); ++i)
        shdrs.push_back(* ((Elf_Shdr *) (contents + rdi(hdr->e_shoff)) + i));

    /* Get the section header string table section (".shstrtab").  Its
       index in the section header table is given by e_shstrndx field
       of the ELF header. */
    unsigned int shstrtabIndex = rdi(hdr->e_shstrndx);
    assert(shstrtabIndex < shdrs.size());
    unsigned int shstrtabSize = rdi(shdrs[shstrtabIndex].sh_size);
    char * shstrtab = (char * ) contents + rdi(shdrs[shstrtabIndex].sh_offset);
    checkPointer(fileContents, shstrtab, shstrtabSize);

    assert(shstrtabSize > 0);
    assert(shstrtab[shstrtabSize - 1] == 0);

    sectionNames = std::string(shstrtab, shstrtabSize);

    sectionsByOldIndex.resize(hdr->e_shnum);
    for (unsigned int i = 1; i < rdi(hdr->e_shnum); ++i)
        sectionsByOldIndex[i] = getSectionName(shdrs[i]);
}

template<ElfFileParams>
std::string ElfFile<ElfFileParamNames>::getSectionName(const Elf_Shdr & shdr)
{
    return std::string(sectionNames.c_str() + rdi(shdr.sh_name));
}


template<ElfFileParams>
Elf_Shdr & ElfFile<ElfFileParamNames>::findSection(const SectionName & sectionName)
{
    Elf_Shdr * shdr = findSection2(sectionName);
    if (!shdr) {
        std::string extraMsg = "";
        if (sectionName == ".interp" || sectionName == ".dynamic" || sectionName == ".dynstr")
            extraMsg = ". The input file is most likely statically linked";
        error("cannot find section '" + sectionName + "'" + extraMsg);
    }
    return *shdr;
}


template<ElfFileParams>
Elf_Shdr * ElfFile<ElfFileParamNames>::findSection2(const SectionName & sectionName)
{
    unsigned int i = findSection3(sectionName);
    return i ? &shdrs[i] : 0;
}


template<ElfFileParams>
unsigned int ElfFile<ElfFileParamNames>::findSection3(const SectionName & sectionName)
{
    for (unsigned int i = 1; i < rdi(hdr->e_shnum); ++i)
        if (getSectionName(shdrs[i]) == sectionName) return i;
    return 0;
}

template<ElfFileParams>
char** ElfFile<ElfFileParamNames>::dlneeds()
{
    Elf_Shdr & shdrDynamic = findSection(".dynamic");
    Elf_Shdr & shdrDynStr = findSection(".dynstr");
    char *strTab = (char *)contents + rdi(shdrDynStr.sh_offset);

    Elf_Dyn *dyn = (Elf_Dyn *) (contents + rdi(shdrDynamic.sh_offset));
    Elf_Dyn *d = NULL;
    
    size_t n_needed = 0;
    char** result = NULL;
    size_t result_size = 0;
    int i = 0;

    for (d = dyn; rdi(d->d_tag) != DT_NULL; d++) {
        if (rdi(d->d_tag) == DT_NEEDED) {
            n_needed++;
        }
    }
    
    result_size = sizeof(char**) * (n_needed + 1);
    result = (char**) malloc(result_size);
    if (result == NULL) return NULL;
    memset(result, 0, result_size);

    for (d = dyn; rdi(d->d_tag) != DT_NULL; d++) {
        if (rdi(d->d_tag) == DT_NEEDED) {
            result[i] = strdup(strTab + rdi(d->d_un.d_val));
            i++;
        }
    }
    
    return result;
}

extern "C"
char** android_dlneeds(char* fileName)
{
	char** result;
    auto fileContents = readFile(fileName);

	try {
        if (getElfType(fileContents).is32Bit)
            result = (ElfFile<Elf32_Ehdr, Elf32_Phdr, Elf32_Shdr, Elf32_Addr, Elf32_Off, Elf32_Dyn, Elf32_Sym, Elf32_Verneed>(fileContents)).dlneeds();
        else
            result = (ElfFile<Elf64_Ehdr, Elf64_Phdr, Elf64_Shdr, Elf64_Addr, Elf64_Off, Elf64_Dyn, Elf64_Sym, Elf64_Verneed>(fileContents)).dlneeds();
    } catch (std::exception & e) {
        fprintf(stderr, "patchelf: %s\n", e.what());
        return NULL;
    }
    
    return result;
}

int main(int argc, char * * argv)
{
    if (argc < 2) error("missing filename");
    char** libs = android_dlneeds(argv[1]);
    
    printf("dlneeds: %p\n", libs);
    int i;
    for(i=0; libs[i]; i++) {
 	    printf("%d: %s\n", i, libs[i]);
    }
}
