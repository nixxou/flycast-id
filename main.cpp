// flycast-id: the id Flycast gives a game - see CMakeLists.txt.
//
//   flycast-id.exe <rom path>   ->  "id<TAB><id>" on stdout, exit 0; a reason on stderr, exit 1
//
// A ZIP OR A 7Z is an arcade set, found by its name in Flycast's table (below). A .BIN, .DAT OR .LST is a legacy
// decrypted Naomi rom, as Flycast takes them (naomi_cart_GetPlatform): the whole rom, or a list of its files
// and where they go - Decrypted() below, naomi_cart.cpp's loadDecryptedRom without the BIOS. Its id is the
// boot id's title only: there is no set name to fall back on, for Flycast either. ANYTHING ELSE is a disc,
// opened by Flycast's own disc readers (imgread: chd, gdi, cdi, cue...): its IP.BIN is the first sector of
// its base FAD, as Flycast's box-art scraper reads it (ui/boxart/scraper.cpp), and the id the product number
// as the emulator takes it (emulator.cpp, loadGameSpecificSettings): trailing spaces trimmed, cut at the first
// NUL. The IP.BIN checked first, as the scraper checks it (SEGA SEGAKATANA): a disc of another console is no
// Dreamcast disc, and its bytes there no id. An empty product number is no id either - for Flycast too.
//
// Load() is hw/naomi/naomi_cart.cpp's loadMameRom, cut to what the boot id needs: no BIOS (the boot id is the
// cartridge's), no EEPROM, no GGPO digest, no inputs - and the parent set found beside the file with
// std::filesystem instead of Flycast's storage layer. The id itself is naomi_cart_LoadRom's, word for word:
// the boot id's title when the board name is one Flycast knows (the second title on a System SP), trailing
// spaces trimmed, the SAMPLE GAME names replaced; else the set's short name.
//
// Flycast's code, so under the GPL-2.0 like it - see LICENSE.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <fcntl.h>
#include <io.h>
#include <filesystem>
#include <memory>
#include <string>

#include "types.h"
#include "emulator.h"
#include "stdclass.h"
#include "archive/archive.h"
#include "hw/naomi/naomi_cart.h"
#include "hw/naomi/naomi_roms.h"
#include "hw/naomi/m1cartridge.h"
#include "hw/naomi/m4cartridge.h"
#include "hw/naomi/awcartridge.h"
#include "hw/naomi/gdcartridge.h"
#include "imgread/common.h"
#include "reios/reios.h"

Cartridge *CurrentCartridge;

static const Game *FindGame(const std::string& path)
{
    const std::string name = std::filesystem::path(path).stem().string();
    for (int i = 0; Games[i].name != nullptr; i++)
        if (name == Games[i].name)
            return &Games[i];
    return nullptr;
}

static Cartridge *Load(const std::string& path, const Game *game)
{
    std::unique_ptr<Archive> archive(OpenArchive(path));
    std::unique_ptr<Archive> parent;
    if (game->parent_name != nullptr)
    {
        // Beside the set, with the set's own extension, then the other one.
        const auto dir = std::filesystem::path(path).parent_path();
        for (const char *ext : { ".zip", ".7z" })
        {
            const auto p = dir / (std::string(game->parent_name) + ext);
            if (std::filesystem::exists(p)) { parent.reset(OpenArchive(p.string())); if (parent) break; }
        }
    }
    if (archive == nullptr && parent == nullptr)
        throw NaomiCartException("cannot open " + path);

    Cartridge *cart = nullptr;
    switch (game->cart_type)
    {
    case M1: cart = new M1Cartridge(game->size); break;
    case M2: cart = new M2Cartridge(game->size); break;
    case M4: cart = new M4Cartridge(game->size); break;   // a System SP set: its boot id is its M4's (systemsp.h)
    case AW: cart = new AWCartridge(game->size); break;
    case GD:
        {
            auto *gd = new GDCartridge(game->size);
            gd->SetGDRomName(game->gdrom_name, game->parent_name);
            cart = gd;
        }
        break;
    default: throw NaomiCartException("unsupported cartridge type");
    }
    std::unique_ptr<Cartridge> owned(cart);
    cart->SetKey(game->key);
    cart->game = game;

    for (int i = 0; game->blobs[i].filename != nullptr; i++)
    {
        const auto& blob = game->blobs[i];
        u32 len = blob.length;
        if (blob.blob_type == Copy)
        {
            u8 *dst = (u8 *)cart->GetPtr(blob.offset, len);
            u8 *src = (u8 *)cart->GetPtr(blob.src_offset, len);
            if (dst == nullptr || src == nullptr) throw NaomiCartException("invalid ROM");
            memcpy(dst, src, blob.length);
            continue;
        }
        if (blob.blob_type == Eeprom) continue;   // not what the boot id is made of
        std::unique_ptr<ArchiveFile> file;
        if (archive) file.reset(archive->OpenFileByCrc(blob.crc));
        if (!file && parent) file.reset(parent->OpenFileByCrc(blob.crc));
        if (!file && archive) file.reset(archive->OpenFile(blob.filename));
        if (!file && parent) file.reset(parent->OpenFile(blob.filename));
        if (!file) throw NaomiCartException(std::string("cannot find ") + blob.filename);
        switch (blob.blob_type)
        {
        case Normal:
            {
                u8 *dst = (u8 *)cart->GetPtr(blob.offset, len);
                if (dst == nullptr) throw NaomiCartException(std::string("truncated ") + blob.filename);
                file->Read(dst, blob.length);
            }
            break;
        case InterleavedWord:
            {
                std::unique_ptr<u8[]> buf(new u8[blob.length]);
                file->Read(buf.get(), blob.length);
                u16 *to = (u16 *)cart->GetPtr(blob.offset, len);
                if (to == nullptr) throw NaomiCartException(std::string("truncated ") + blob.filename);
                u16 *from = (u16 *)buf.get();
                for (int n = blob.length / 2; --n >= 0; to++)
                    *to++ = *from++;
            }
            break;
        case Key:
            {
                // The cartridge keeps the pointer (M1/M4 key data): it must outlive the Init below.
                u8 *buf = (u8 *)malloc(blob.length);
                file->Read(buf, blob.length);
                cart->SetKeyData(buf);
            }
            break;
        default:
            break;
        }
    }
    CurrentCartridge = cart;
    cart->Init(nullptr, nullptr);
    return owned.release();
}

/// Flycast's trim_trailing_ws (stdclass.h), with its default: spaces and NULs, nothing else.
static std::string TrimTrailing(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0'))
        s.pop_back();
    return s;
}

/// A legacy decrypted rom: naomi_cart.cpp's loadDecryptedRom, without the BIOS.
static Cartridge *Decrypted(const std::string& path)
{
    std::vector<std::string> files;
    std::vector<u32> fstart, fsize;
    u32 romSize = 0;
    std::filesystem::path folder;
    auto ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    if (ext == ".lst")
    {
        std::ifstream lst(std::filesystem::u8path(path));
        if (!lst) throw NaomiCartException("can't open " + path);
        folder = std::filesystem::u8path(path).parent_path();
        std::string line;
        if (!std::getline(lst, line)) throw NaomiCartException("invalid LST file");   // the first line: the rom's name
        bool any = false;
        while (std::getline(lst, line))
        {
            any = true;
            char filename[512];
            u32 addr, sz;
            if (sscanf(line.c_str(), "\"%511[^\"]\",%x,%x", filename, &addr, &sz) == 3)
            {
                files.emplace_back(filename);
                fstart.push_back(addr);
                fsize.push_back(sz);
                romSize = std::max(romSize, addr + sz);
            }
        }
        if (!any) throw NaomiCartException("invalid LST file");
    }
    else
    {
        std::error_code ec;
        auto size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
        if (ec) throw NaomiCartException("can't open " + path);
        files.push_back(path);
        fstart.push_back(0);
        fsize.push_back((u32)size);
        romSize = (u32)size;
    }
    if (romSize == 0) throw NaomiCartException("invalid empty ROM");

    u8 *romBase = (u8 *)malloc(romSize);
    if (romBase == nullptr) throw NaomiCartException("out of memory");
    for (size_t i = 0; i < files.size(); i++)
    {
        u8 *dest = romBase + fstart[i];
        if (files[i] == "null") { memset(dest, -1, fsize[i]); continue; }
        auto p = folder.empty() ? std::filesystem::u8path(files[i]) : folder / std::filesystem::u8path(files[i]);
        std::ifstream f(p, std::ios::binary);
        if (!f.read((char *)dest, fsize[i])) { free(romBase); throw NaomiCartException("failed to load " + p.string()); }
    }
    return new DecryptedCartridge(romBase, romSize);
}

/// naomi_cart_LoadRom's id: the boot id's title, else the set's short name (none for a decrypted rom).
static std::string IdOf(Cartridge *cart, const Game *game)
{
    std::string id = game != nullptr ? game->name : "";
    RomBootID bootId;
    if (cart->GetBootId(&bootId)
            && (!memcmp(bootId.boardName, "NAOMI", 5)
                || !memcmp(bootId.boardName, "Naomi2", 6)
                || !memcmp(bootId.boardName, "SYSTEM_X_APP", 12)
                || !memcmp(bootId.boardName, "SystemSP", 8)))
    {
        bool systemSP = memcmp(bootId.boardName, "SystemSP", 8) == 0;
        std::string title = TrimTrailing(std::string(bootId.gameTitle[systemSP ? 1 : 0], &bootId.gameTitle[systemSP ? 1 : 0][32]));
        std::string romName = game != nullptr ? game->name : "";
        if (title == "SAMPLE GAME MAX LONG NAME-")
        {
            if (romName == "sgdrvsim") title = "SEGA DRIVING SIMULATOR";
            else if (romName == "dragntr") title = "DRAGON TREASURE";
            else if (romName == "dragntr2") title = "DRAGON TREASURE 2";
            else if (romName == "dragntr3") title = "DRAGON TREASURE 3";
        }
        if (!title.empty()) id = title;
    }
    return id;
}

/// A disc's id: its IP.BIN's product number, as the emulator takes it.
static std::string DiscIdOf(const std::string& path)
{
    std::unique_ptr<Disc> disc(OpenDisc(path));
    if (disc == nullptr) throw NaomiCartException("not a disc Flycast can open");
    u8 sector[2048] = {};
    disc->ReadSectors(disc->GetBaseFAD(), 1, sector, sizeof(sector));
    ip_meta_t meta;
    memcpy(&meta, sector, sizeof(meta));
    // A DREAMCAST disc, checked as Flycast's box-art scraper checks it: another console's header read at
    // that place gives a product number that is none (measured: a Saturn disc gave "E").
    if (memcmp(meta.hardware_id, "SEGA SEGAKATANA ", sizeof(meta.hardware_id))
            || memcmp(meta.maker_id, "SEGA ENTERPRISES", sizeof(meta.maker_id)))
        throw NaomiCartException("not a Dreamcast disc (no SEGA SEGAKATANA IP.BIN)");
    std::string id = TrimTrailing(std::string(meta.product_number, sizeof(meta.product_number)));
    return std::string(id.c_str());   // "in case there is a null character followed by garbage" - emulator.cpp
}

static std::string ExtensionOf(const std::string& path)
{
    auto ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    return ext;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: flycast-id <rom path>\n");
        return 2;
    }
    const std::string path = argv[1];
    // The id's bytes as they are: no \r added before the \n.
    _setmode(_fileno(stdout), _O_BINARY);
    try
    {
        std::string id;
        const auto ext = ExtensionOf(path);
        if (ext == ".zip" || ext == ".7z")
        {
            const Game *game = FindGame(path);
            if (game == nullptr) throw NaomiCartException("not a set Flycast knows");
            // Where the GD-ROM cartridge looks for its image: <folder of the set>\<set name>\<gdrom name>.chd.
            settings.content.path = path;
            settings.content.fileName = std::filesystem::path(path).filename().string();
            std::unique_ptr<Cartridge> cart(Load(path, game));
            id = IdOf(cart.get(), game);
            CurrentCartridge = nullptr;
        }
        else if (ext == ".bin" || ext == ".dat" || ext == ".lst")
        {
            std::unique_ptr<Cartridge> cart(Decrypted(path));
            id = IdOf(cart.get(), nullptr);
        }
        else id = DiscIdOf(path);
        // Never an empty id, nor one a line cannot carry: a caller would take it for a section name.
        if (id.find_first_not_of(" \t") == std::string::npos || id.find_first_of("\r\n") != std::string::npos)
            throw NaomiCartException("no usable id");
        fwrite("id\t", 1, 3, stdout);
        fwrite(id.data(), 1, id.size(), stdout);
        fwrite("\n", 1, 1, stdout);
        return 0;
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
