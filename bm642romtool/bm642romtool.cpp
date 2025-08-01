#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include "compress.h"

#define BIT_ALIGN(V, N) (V + ((N - (V % N)) % N))

struct GameEvFile {
    int num = 0;
    int bss_size = 0;
    std::vector<uint8_t> data;
};

struct GameEvBlock {
    std::string name = "";
    bool ext = false;
    std::vector<GameEvFile> files;
};

struct GameFile {
    int num = 0;
    std::string compType = "none";
    std::vector<uint8_t> data;
};

struct GameData {
    std::map<int, GameEvBlock> evblock;
    std::vector<GameFile> file;
    std::vector<uint8_t> header;
    std::vector<uint8_t> main;
    std::vector<uint8_t> symtab;
    uint32_t symtabofs = 0;
    uint32_t fileofs = 0;
};

GameData gamedata;
std::string game_id;
std::vector<uint8_t> rom_data;

bool MakeDirectory(std::string dir)
{
    int ret;
#if defined(_WIN32)
    ret = _mkdir(dir.c_str());
#else 
    ret = mkdir(dir.c_str(), 0777); // notice that 777 is different than 0777
#endif]
    return ret != -1 || errno == EEXIST;
}


uint8_t ReadRom8(uint32_t offset)
{
    if (offset < rom_data.size()) {
        return rom_data[offset];
    }
    else {
        return 0;
    }
}

uint16_t ReadRom16(uint32_t offset)
{
    return (ReadRom8(offset) << 8) | ReadRom8(offset + 1);
}

uint32_t ReadRom32(uint32_t offset)
{
    return (ReadRom16(offset) << 16) | ReadRom16(offset + 2);
}

std::string ReadRomGameID()
{
    std::string string;
    string.push_back(ReadRom8(59));
    string.push_back(ReadRom8(60));
    string.push_back(ReadRom8(61));
    string.push_back(ReadRom8(62));
    return string;
}

void ReadWholeFile(std::string path, std::vector<uint8_t>& data)
{
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        std::cout << "Failed to open " << path << " for reading." << std::endl;
        exit(1);
    }
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    if (size == 0) {
        fclose(file);
        data.clear();
        return;
    }
    data.resize(size);
    fseek(file, 0, SEEK_SET);
    fread(&data[0], 1, size, file);
    fclose(file);
}

void LoadROM(std::string path)
{
    ReadWholeFile(path, rom_data);
    if (ReadRom32(0) != 0x80371240) {
        std::cout << "File " << path << " is not a valid N64 ROM." << std::endl;
        exit(1);
    }
}

size_t DecodeLZ(size_t offset, size_t raw_size, std::vector<uint8_t>& data)
{
    size_t offset_start = offset;
    uint8_t window[1024];
    uint32_t window_ofs = 958;
    uint16_t flag = 0;
    uint8_t* dst = &data[0];
    memset(window, 0, 1024);
    while (raw_size > 0) {
        flag >>= 1;
        if (!(flag & 0x100)) {
            flag = 0xFF00 | ReadRom8(offset++);
        }
        if (flag & 0x1) {
            window[window_ofs++] = *dst++ = ReadRom8(offset++);
            window_ofs %= 1024;
            raw_size--;
        }
        else {
            uint32_t i;
            uint8_t byte1 = ReadRom8(offset++);
            uint8_t byte2 = ReadRom8(offset++);
            uint32_t ofs = ((byte2 & 0xC0) << 2) | byte1;
            uint32_t copy_len = (byte2 & 0x3F) + 3;
            for (i = 0; i < copy_len; i++) {
                window[window_ofs++] = *dst++ = window[(ofs + i) % 1024];
                window_ofs %= 1024;
            }
            raw_size -= i;
        }
    }
    return offset - offset_start;
}

static inline uint32_t ReadU32(uint8_t* buf)
{
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);
}

size_t DecodeYay0(uint8_t *srcbuf, std::vector<uint8_t>& data)
{
    uint32_t dstPos = 0;
    uint32_t srcPos = 0;
    uint8_t* codes = &srcbuf[16];
    uint8_t* counts = &srcbuf[ReadU32(&srcbuf[8])];
    uint8_t* srcData = &srcbuf[ReadU32(&srcbuf[12])];
    uint32_t uncompressedSize = ReadU32(&srcbuf[4]);

    //int srcPlace = 0, dstPlace = 0; //current read/write positions

    uint32_t codePos = 0;
    uint32_t countPos = 0;

    uint32_t validBitCount = 0; //number of valid bits left in "code" byte
    uint8_t currCodeByte;
    while (dstPos < uncompressedSize)
    {
        //read new "code" byte if the current one is used up
        if (validBitCount == 0)
        {
            currCodeByte = codes[codePos];
            ++codePos;
            validBitCount = 8;
        }

        if ((currCodeByte & 0x80) != 0)
        {
            //straight copy
            data[dstPos] = srcData[srcPos];
            dstPos++;
            srcPos++;
            //if(srcPos >= srcSize)
            //  return r;
        }
        else
        {
            //RLE part
            uint8_t byte1 = counts[countPos];
            uint8_t byte2 = counts[countPos + 1];
            countPos += 2;
            //if(srcPos >= srcSize)
            //  return r;

            uint32_t dist = ((byte1 & 0xF) << 8) | byte2;
            uint32_t copySource = dstPos - (dist + 1);

            uint32_t numBytes = byte1 >> 4;
            if (numBytes == 0)
            {
                numBytes = srcData[srcPos] + 0x12;
                srcPos++;
                //if(srcPos >= srcSize)
                //  return r;
            }
            else
                numBytes += 2;

            //copy run
            for (int i = 0; i < numBytes; ++i)
            {
                data[dstPos] = data[copySource];
                copySource++;
                dstPos++;
            }
        }

        //use next bit from "code" byte
        currCodeByte <<= 1;
        validBitCount -= 1;
    }

    return srcPos;
}

void ReadEvBlocks()
{
    gamedata.symtabofs = 0x100000;
    int blkid;
    for (int i = 0; i < 128; i++) {
        uint32_t ofs = 0xFF000 + (i * 2);
        uint8_t blknum = ReadRom8(ofs);
        uint8_t blkofs = ReadRom8(ofs + 1);
        //Check for invalid block
        if (blknum < 8) {
            continue;
        }
        //Build block
        std::string name = "evblk" + std::to_string(blknum);
        blkid = blknum * 2;
        if (blkofs) {
            name += "ext";
            blkid++;
            gamedata.evblock[blkid].ext = true;
        }
        gamedata.evblock[blkid].name = name;
        //Find block info
        uint32_t blkromofs = (blknum * 0x20000) + (blkofs * 0x800);
        uint32_t infoofs = ReadRom32(blkromofs) + ReadRom32(blkromofs + 8);
        infoofs += blkromofs;
        uint8_t infonum = ReadRom8(infoofs);
        uint16_t filenum = 0;
        infoofs++;
        //Search for block file
        for (int j = 0; j < infonum; j++) {
            if (ReadRom16(infoofs) == i) {
                filenum = ReadRom8(infoofs + 2);
                break;
            }
            infoofs += 3;
        }
        if (filenum != 0) {
            //Decode file
            GameEvFile evfile;
            uint32_t fileofs = ReadRom32(blkromofs) + ReadRom32(blkromofs + 8 + (filenum * 8));
            fileofs += blkromofs;
            uint32_t filesize = ReadRom32(blkromofs + 12 + (filenum * 8));
            evfile.num = i;
            evfile.bss_size = ReadRom32(fileofs + 4);
            evfile.data.resize(ReadRom32(fileofs));
            DecodeLZ(fileofs + 8, ReadRom32(fileofs), evfile.data);
            gamedata.evblock[blkid].files.push_back(evfile);
            if (fileofs + filesize >= gamedata.symtabofs) {
                gamedata.symtabofs = fileofs + filesize;
            }
        }
    }
    gamedata.symtabofs = BIT_ALIGN(gamedata.symtabofs, 0x20000);
}

std::vector<int> uncomp_file = {
    0, //Music File
    1, //SFX File
    2, //Kanji file
};

bool IsRawFile(int no, uint32_t ofs, uint32_t size)
{
    //Spec
    if (std::find(uncomp_file.begin(), uncomp_file.end(), no) != uncomp_file.end()) {
        return true;
    }
    if (size < 4) {
        return true;
    }
    //Check for common special uncompressed file
    if (BIT_ALIGN(ReadRom32(ofs), 2) == size - 4) {
        size_t count = ReadRom32(ofs);
        for (size_t i = 0; i < size; i++) {
            if (ReadRom8(ofs + i + 4) == 0x20) {
                return true;
            }
        }
    }
    return false;
}

void ReadFiles()
{
    uint32_t fileofs = gamedata.fileofs;
    uint32_t baseofs = gamedata.fileofs + ReadRom32(fileofs);
    uint32_t filenum = ReadRom32(fileofs + 4);
    fileofs += 8;
    
    for (int i = 0; i < filenum; i++) {
        //Look for early terminator
        if (ReadRom32(fileofs) == 0xFFFFFFFF) {
            break;
        }
        //Get data information
        uint32_t dataofs = baseofs + ReadRom32(fileofs);
        uint32_t datasize = ReadRom32(fileofs + 4);

        //Build game file
        GameFile file;
        if (IsRawFile(i, dataofs, datasize)) {
            file.data.resize(datasize);
            std::copy(rom_data.begin() + dataofs, rom_data.begin() + (dataofs+datasize), file.data.begin());
        }
        else if (ReadRom32(dataofs+4) == 0x59617930) { //Yay0
            file.compType = "yay0";
            file.data.resize(ReadRom32(dataofs));
            std::vector<uint8_t> yay0buf;
            yay0buf.resize(datasize);
            std::copy(rom_data.begin() + dataofs, rom_data.begin() + (dataofs + datasize), yay0buf.begin());
            DecodeYay0(&yay0buf[4], file.data);
        }
        else {
            file.compType = "lzss";
            file.data.resize(ReadRom32(dataofs));
            DecodeLZ(dataofs + 4, ReadRom32(dataofs), file.data);
        }
        file.num = i;
        gamedata.file.push_back(file);
        fileofs += 8;
    }
}

void OutputFileData(std::string path, std::vector<uint8_t>& data)
{
    FILE* out_file = fopen(path.c_str(), "wb");
    if (!out_file) {
        std::cout << "Failed to open " << path << " for writing." << std::endl;
        exit(1);
    }
    fwrite(&data[0], 1, data.size(), out_file);
    fclose(out_file);
}

void OutputEvBlock(std::string basepath)
{
    std::ofstream outfile;
    outfile.open(basepath + "evblock.txt");
    size_t blknum = 0;
    for (auto& ev : gamedata.evblock) {
        GameEvBlock evblock = ev.second;
        int id = ev.first;
        std::string blockname = evblock.name;
        std::string blockpath = "evblock/";
        MakeDirectory(basepath + blockpath);
        if (!evblock.ext) {
            blknum++;
        }
        //Output files
        for (auto& f : evblock.files) {
            std::string path = blockpath + std::to_string(f.num) + ".bin";
            std::string ext = evblock.ext ? "ext " : "";
            outfile << ext << (blknum-1) << " " << f.bss_size << " " << f.num << " " << path << std::endl;
            OutputFileData(basepath + path, f.data);
        }
    }
    outfile.close();
}

void OutputFiles(std::string basepath)
{
    std::ofstream outfile;
    outfile.open(basepath + "file.txt");
    std::string filedir = basepath + "files/";
    MakeDirectory(filedir);
    for (auto& f : gamedata.file) {
        std::string path = filedir + std::to_string(f.num) + ".bin";
        std::string writePath = "files/" + std::to_string(f.num) + ".bin";
        outfile << f.compType << " " << writePath << std::endl;
        
        OutputFileData(path, f.data);
    }
    outfile.close();
}

void OutputExtractedFiles(std::string path)
{
    path += "/";
    MakeDirectory(path);
    OutputFileData(path + "header.bin", gamedata.header);
    OutputFileData(path + "main.bin", gamedata.main);
    OutputFileData(path + "symtab.bin", gamedata.symtab);
    OutputEvBlock(path);
    OutputFiles(path);
}

void ExtractROM(std::string path)
{
    //Fill in blocks with default size
    gamedata.header.resize(0x1000);
    gamedata.main.resize(0xFE000);
    gamedata.symtab.resize(0x20000);
    //Copy blocks from ROM
    std::cout << "Reading common parts" << std::endl;
    std::copy(rom_data.begin(), rom_data.begin() + 0x1000, gamedata.header.begin());
    std::copy(rom_data.begin() + 0x1000, rom_data.begin() + 0xFF000, gamedata.main.begin());
    std::cout << "Reading code blocks" << std::endl;
    ReadEvBlocks();
    //Copy symbol table
    std::copy(rom_data.begin() + gamedata.symtabofs, rom_data.begin() + (gamedata.symtabofs+0x20000), gamedata.symtab.begin());
    gamedata.fileofs = gamedata.symtabofs + 0x20000;
    std::cout << "Reading files" << std::endl;
    ReadFiles();
    OutputExtractedFiles(path);
}

void CollectEvBlock(std::string indir)
{
    std::ifstream infile;
    infile.open(indir + "evblock.txt");
    std::string line;
    while (std::getline(infile, line)) {
        std::stringstream ss;
        GameEvFile evfile;
        int blkidx;
        int evnum;
        //Init string stream
        ss.clear();
        ss.str("");
        ss << line;
        //Check for extension and discard
        bool hasext = false;
        if (line.rfind("ext", 0) != std::string::npos) {
            std::string temp;
            ss >> temp;
            hasext = true;
        }
        //Read data from string stream
        ss >> blkidx;
        ss >> evfile.bss_size;
        ss >> evfile.num;
        std::string path;
        ss >> path;
        path = indir + path;
        ReadWholeFile(path, evfile.data);
        //Build key
        int evkey = (blkidx * 2);
        if (hasext) {
            evkey++;
        }

        gamedata.evblock[evkey].ext = hasext;
        gamedata.evblock[evkey].name = "";
        //Handle overwriting files
        bool overwrite = false;
        for (auto& f : gamedata.evblock[evkey].files) {
            if (f.num == evfile.num) {
                overwrite = true;
                f = evfile;
                break;
            }
        }
        if (!overwrite) {
            //Push new file
            gamedata.evblock[evkey].files.push_back(evfile);
        }
    }
    infile.close();
}

void CollectFile(std::string indir)
{
    std::ifstream infile;
    infile.open(indir + "file.txt");
    std::string line;
    size_t num = 0;
    while (std::getline(infile, line)) {
        std::stringstream ss;
        GameFile file;
        //Init string stream
        ss.clear();
        ss.str("");
        ss << line;
        
        //Read fle data
        ss >> file.compType;
        std::string path;
        ss >> path;
        path = indir + path;
        ReadWholeFile(path, file.data);
        file.num = num++;
        gamedata.file.push_back(file);
    }
    infile.close();
}

void WriteU8(FILE* file, uint8_t value)
{
    fwrite(&value, 1, 1, file);
}

void WriteU16(FILE* file, uint16_t value)
{
    uint8_t temp[2];
    temp[0] = value >> 8;
    temp[1] = value & 0xFF;
    fwrite(temp, 2, 1, file);
}

void WriteU16At(FILE* file, uint16_t value, size_t offset)
{
    size_t prev_ofs = ftell(file);
    uint8_t temp[2];
    temp[0] = value >> 8;
    temp[1] = value & 0xFF;
    fseek(file, offset, SEEK_SET);
    fwrite(temp, 2, 1, file);
    fseek(file, prev_ofs, SEEK_SET);
}

void WriteU32(FILE* file, uint32_t value)
{
    uint8_t temp[4];
    temp[0] = value >> 24;
    temp[1] = (value >> 16) & 0xFF;
    temp[2] = (value >> 8) & 0xFF;
    temp[3] = value & 0xFF;
    fwrite(temp, 4, 1, file);
}

void WriteU32At(FILE* file, uint32_t value, size_t offset)
{
    size_t prev_ofs = ftell(file);
    uint8_t temp[4];
    temp[0] = value >> 24;
    temp[1] = (value >> 16) & 0xFF;
    temp[2] = (value >> 8) & 0xFF;
    temp[3] = value & 0xFF;
    fseek(file, offset, SEEK_SET);
    fwrite(temp, 4, 1, file);
    fseek(file, prev_ofs, SEEK_SET);
}

void WriteRawBuffer(FILE* file, std::vector<uint8_t>& data)
{
    fwrite(&data[0], 1, data.size(), file);
}

void WriteAlign(FILE* file, size_t align)
{
    while ((ftell(file) % align) != 0) {
        WriteU8(file, 0);
    }
}

void WriteFileTab(FILE* file, std::vector<uint32_t>& filetab)
{
    for (size_t i = 0; i < filetab.size(); i++) {
        WriteU32(file, filetab[i]);
    }
}


void WriteEvBlockFile(FILE* file, GameEvBlock &blk, std::vector<uint8_t>& evblktbl)
{
    std::vector<uint32_t> filetab;
    std::vector<uint8_t> evfile;
    filetab.resize(8194, 0xFFFFFFFF);
    evfile.resize(BIT_ALIGN((blk.files.size() * 3) + 1, 2), 0);
    size_t baseofs = ftell(file);
    uint8_t blknum = baseofs / 0x20000;
    uint8_t blkofs = (baseofs / 0x800) % 64;
    size_t endofs;

    //Init file table
    filetab[0] = 0x00008008;
    filetab[1] = 0x00001000;
    filetab[2] = 0;
    filetab[3] = BIT_ALIGN((blk.files.size() * 3) + 1, 2);
    evfile[0] = blk.files.size();
    //Write default tables
    WriteFileTab(file, filetab);
    WriteRawBuffer(file, evfile);
    endofs = ftell(file);
    uint8_t filenum = 1;
    for (auto& f : blk.files) {
        //Add block to table
        evblktbl[f.num * 2] = blknum;
        evblktbl[(f.num * 2) + 1] = blkofs;
        //Init file table
        size_t fileofs = ftell(file);
        filetab[(filenum * 2) + 2] = fileofs - baseofs - 0x8008;
        //Write data
        WriteU32(file, f.data.size());
        WriteU32(file, f.bss_size);
        EncodeLZSS(file, f.data);
        WriteAlign(file, 2);
        //Update data end
        size_t fileend = ftell(file);
        endofs = fileend;
        filetab[(filenum * 2) + 3] = fileend - fileofs;

        //Record file
        evfile[((filenum - 1) * 3) + 1] = f.num >> 8;
        evfile[((filenum - 1) * 3) + 2] = f.num & 0xFF;
        evfile[((filenum - 1) * 3) + 3] = filenum;
        filenum++;
    }
    //Update event files
    fseek(file, baseofs, SEEK_SET);
    WriteFileTab(file, filetab);
    WriteRawBuffer(file, evfile);
    fseek(file, endofs, SEEK_SET);
}

void WriteEVBlock(FILE* file, std::vector<uint8_t>& evblktbl)
{
    for (auto& ev : gamedata.evblock) {
        GameEvBlock &blk = ev.second;
        int blknum = ev.first / 2;
        //Align block
        if (blk.ext) {
            WriteAlign(file, 0x10000);
        }
        else {
            WriteAlign(file, 0x20000);
        }
        WriteEvBlockFile(file, blk, evblktbl);
    }
    //Align next data
    WriteAlign(file, 0x20000);
    gamedata.symtabofs = ftell(file);
}

void WriteRomFile(FILE* file)
{
    std::vector<uint32_t> filetab;
    //Init file table
    size_t rounded_filenum = BIT_ALIGN(gamedata.file.size(), 4096);
    filetab.resize((2 * rounded_filenum) + 2, 0xFFFFFFFF);
    filetab[0] = (8 * rounded_filenum) + 8;
    filetab[1] = rounded_filenum;
    WriteFileTab(file, filetab);
    size_t file_baseofs = ftell(file);
    size_t filenum = 0;

    for (auto& f : gamedata.file) {
        //Update file start
        size_t fileofs = ftell(file);
        filetab[(filenum * 2) + 2] = ftell(file) - file_baseofs;
        if (filenum % 100 == 0) {

            size_t filenum_max = filenum + 99;
            if (filenum_max >= gamedata.file.size()) {
                filenum_max = gamedata.file.size() - 1;
            }
            std::cout << "Writing files " << filenum << " to " << filenum_max << std::endl;
        }
        //Encode file
        if (f.compType == "lzss") {
            WriteU32(file, f.data.size());
            EncodeLZSS(file, f.data);
        }
        else if (f.compType == "yay0") {
            WriteU32(file, f.data.size());
            EncodeYay0(file, f.data);
        }
        else {
            WriteRawBuffer(file, f.data);
        }
        WriteAlign(file, 2);
        //Update file end
        size_t fileend = ftell(file);
        filetab[(filenum * 2) + 3] = fileend - fileofs;
        filenum++;
    }
    //Update file table
    fseek(file, gamedata.fileofs, SEEK_SET);
    WriteFileTab(file, filetab);
}

#include "crc.inc"

void WriteRom(std::string outfile)
{
    FILE* file = fopen(outfile.c_str(), "wb");
    if (!file) {
        std::cout << "Failed to open " << outfile << " for writing." << std::endl;
        exit(1);
    }
    std::cout << "Writing common parts" << std::endl;
    //Write common portions
    WriteRawBuffer(file, gamedata.header);
    WriteRawBuffer(file, gamedata.main);
    //Write Code Blocks
    std::cout << "Writing code blocks" << std::endl;
    std::vector<uint8_t> evblktbl;
    evblktbl.resize(4096, 0);
    fseek(file, 0xFF000, SEEK_SET);
    WriteRawBuffer(file, evblktbl);
    WriteEVBlock(file, evblktbl);
    //Update Block Table
    fseek(file, 0xFF000, SEEK_SET);
    WriteRawBuffer(file, evblktbl);
    //Write symbol table
    fseek(file, gamedata.symtabofs, SEEK_SET);
    WriteRawBuffer(file, gamedata.symtab);
    WriteAlign(file, 0x20000);
    //Write file data
    std::cout << "Writing files" << std::endl;
    gamedata.fileofs = ftell(file);
    WriteRomFile(file);
    //Fix file address
    if (game_id == "NBVE") {
        WriteU32At(file, 0x3C040000 | (gamedata.fileofs >> 16), 0x2E98);
        WriteU32At(file, 0x3C040000 | (gamedata.fileofs >> 16), 0x3094);
        WriteU32At(file, 0x3C040000 | (gamedata.fileofs >> 16), 0x32B8);
        WriteU32At(file, 0x24040000 | (gamedata.fileofs >> 17), 0x3C7C);
        WriteU32At(file, 0x24040000 | (gamedata.fileofs >> 17), 0x28730);
    }
    else if (game_id == "NBVJ") {
        WriteU32At(file, 0x3C040000 | (gamedata.fileofs >> 16), 0x2EA8);
        WriteU32At(file, 0x3C040000 | (gamedata.fileofs >> 16), 0x30A4);
        WriteU32At(file, 0x3C040000 | (gamedata.fileofs >> 16), 0x32C8);
        WriteU32At(file, 0x24040000 | (gamedata.fileofs >> 17), 0x3C8C);
        WriteU32At(file, 0x24040000 | (gamedata.fileofs >> 17), 0x28740);
    }
    fclose(file);
    fix_crc(outfile.c_str());
}

void RebuildRom(std::string indir, std::string output)
{
    indir += "/";
    ReadWholeFile(indir + "header.bin", gamedata.header);
    ReadWholeFile(indir + "main.bin", gamedata.main);
    ReadWholeFile(indir + "symtab.bin", gamedata.symtab);
    CollectEvBlock(indir);
    CollectFile(indir);
    WriteRom(output);
}

void PrintHelp(char* prog_name)
{
    std::cout << "Usage: " << prog_name << " [flags] args" << std::endl;
    std::cout << std::endl;
    std::cout << "-h/--help: Display this page" << std::endl;
    std::cout << "-b/--build: Build a new ROM" << std::endl;
    std::cout << "-a/--base: Path to base ROM" << std::endl;
    std::cout << "args represent an output directory when not building ROM." << std::endl;
    std::cout << "args represent an input directory and output ROM in that order when building ROM." << std::endl;
}

int main(int argc, char **argv)
{
    bool build_rom = false;
    size_t last_opt = 1;
    for (int i = 1; i < argc; i++) {
        std::string option = argv[i];
        if (option[0] != '-') {
            last_opt = i;
            break;
        }
        if (option == "-h" || option == "--help") {
            PrintHelp(argv[0]);
            exit(0);
        }
        if (option == "-b" || option == "--build") {
            build_rom = true;
        }
        else if (option == "-a" || option == "--base") {
            if (++i >= argc) {
                std::cout << "Missing argument for option " << option << "." << std::endl;
                PrintHelp(argv[0]);
                exit(1);
            }
            if (rom_data.size() != 0) {
                std::cout << "Multiple Base ROM Arguments disallowed" << std::endl;
                PrintHelp(argv[0]);
                exit(1);
            }
            LoadROM(argv[i]);
        }
        else {
            std::cout << "Invalid option " << option << "." << std::endl;
            PrintHelp(argv[0]);
            exit(1);
        }
    }
    if (rom_data.size() == 0) {
        std::cout << "Missing Base ROM." << std::endl;
        PrintHelp(argv[0]);
        exit(1);
    }
    game_id = ReadRomGameID();
    if (game_id != "NBVE" && game_id != "NBVJ") {
        std::cout << "Unsupported base ROM." << std::endl;
        PrintHelp(argv[0]);
        exit(1);
    }
    if (!build_rom) {
        if (argc - last_opt != 1) {
            std::cout << "Invalid arguments after flags." << std::endl;
            PrintHelp(argv[0]);
            exit(1);
        }
        ExtractROM(argv[last_opt]);
    }
    else {
        if (argc - last_opt != 2) {
            std::cout << "Invalid arguments after flags." << std::endl;
            PrintHelp(argv[0]);
            exit(1);
        }
        RebuildRom(argv[last_opt], argv[last_opt + 1]);
    }
}