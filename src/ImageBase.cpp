// -----------------------------------------------------------------------------
//  BCTImage.cpp – implementation
// -----------------------------------------------------------------------------
#include "BCTImage.h"
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/wfstream.h>

#define FOURCC(a,b,c,d) ( unsigned(a) | (unsigned(b)<<8) | \
                          (unsigned(c)<<16) | (unsigned(d)<<24) )

// -----------------------------------------------------------------------------
//  Table helpers
// -----------------------------------------------------------------------------
uint32_t BCTImage::BctToFourCC(uint8_t f)
{
    switch (f)
    {
        case 0x00: return 0;                        // uncompressed RGBA32
        case 0x08: return FOURCC('D','X','T','1');
        case 0x0A: return FOURCC('D','X','T','5');
        case 0x25: return FOURCC('A','T','I','1');  // BC4
        case 0x26: return FOURCC('A','T','I','2');  // BC5
        case 0x30: return FOURCC('D','X','T','1');  // alias
        case 0x32: return FOURCC('D','X','T','5');  // alias
        default  : return FOURCC('D','X','1','0');  // sentinel → DX10 header follows
    }
}

uint32_t BCTImage::BctToDxgi(uint8_t f)
{
    switch (f)
    {
        case 0x25: return 80;   // BC4_UNORM
        case 0x26: return 83;   // BC5_UNORM
        case 0x27: return 95;   // BC6H_UF16
        case 0x28: return 98;   // BC7_UNORM
        default  : return 28;   // RGBA8 (fallback for 0x00 / 0x35)
    }
}

uint32_t BCTImage::BytesPerBlock(uint8_t f)
{
    switch (f)
    {
        case 0x08: case 0x25: case 0x30:               return 8;  // BC1 / BC4
        case 0x0A: case 0x26: case 0x27: case 0x28:    return 16; // BC3/5/6/7
        default:                                       return 0;  // uncompressed
    }
}

// -----------------------------------------------------------------------------
//  Loader
// -----------------------------------------------------------------------------
bool BCTImage::LoadFromFile(const wxString& filePath)
{
    // ------------------------------------------------------------------
    //  read whole file
    // ------------------------------------------------------------------
    wxFileInputStream fin(filePath);
    if (!fin.IsOk()) { BCT_ERR("file open failed"); return false; }

    const size_t fileSz = fin.GetLength();
    if (fileSz < 20)  { BCT_ERR("file too small (%zu bytes)", fileSz); return false; }

    std::vector<unsigned char> blob(fileSz);
    if (fin.Read(&blob[0], fileSz).LastRead() != fileSz) {
        BCT_ERR("read error");
        return false;
    }

    // ------------------------------------------------------------------
    //  header
    // ------------------------------------------------------------------
    const BctHeader* hdr = reinterpret_cast<const BctHeader*>(&blob[0]);

    if (hdr->magic[0] != 0x07 || hdr->magic[1] != 0x01 ||
        hdr->magic[2] != 0x02 || hdr->magic[3] != 0x20)
    {
        BCT_ERR("bad magic %02X %02X %02X %02X",
                hdr->magic[0],hdr->magic[1],hdr->magic[2],hdr->magic[3]);
        return false;
    }

    const uint16_t W   = hdr->width;
    const uint16_t H   = hdr->height;
    const uint8_t  fmt = hdr->format;
    const uint8_t  mips= hdr->mipCount ? hdr->mipCount : 1;

    BCT_LOG("w=%u h=%u fmt=0x%02X mips=%u", W, H, fmt, mips);

    if (!W || !H) { BCT_ERR("zero width/height"); return false; }

    // ------------------------------------------------------------------
    //  mip-table
    // ------------------------------------------------------------------
    const size_t mipOfs = hdr->infoOffset;
    if (mipOfs + size_t(mips)*16 > fileSz) {
        BCT_ERR("mipTable outside file (off=%zu)", mipOfs);
        return false;
    }
    const BctMip* mt = reinterpret_cast<const BctMip*>(&blob[mipOfs]);

    // ------------------------------------------------------------------
    //  translate format
    // ------------------------------------------------------------------
    const uint32_t fourCC = BctToFourCC(fmt);
    const bool bc   = (fmt != 0x00 && fmt != 0x35);
    const bool dx10 = (fourCC == FOURCC('D','X','1','0'));

    if (bc && BytesPerBlock(fmt)==0) {
        BCT_ERR("unsupported imgFormat 0x%02X", fmt);
        return false;
    }

    // ------------------------------------------------------------------
    //  build DDS   (same code as previous reply, no changes) ----------
    // ------------------------------------------------------------------
    wxMemoryOutputStream dds;
    /* …  (omitted here for brevity – keep previous good header code) … */

    // -------- raw mip copy -------------------------------------------
    for (uint8_t i=0;i<mips;++i) {
        if (mt[i].ofs + mt[i].size > blob.size()) {
            BCT_ERR("mip %u points past EOF (ofs=%u size=%u)",
                    i, mt[i].ofs, mt[i].size);
            return false;
        }
        dds.Write(&blob[mt[i].ofs], mt[i].size);
    }

    // -------- temp file -> DDSImage ----------------------------------
    wxString tmp = wxFileName::CreateTempFileName("bct2dds_");
    {
        wxFileOutputStream t(tmp);
        if (!t.IsOk()) { BCT_ERR("tmp create fail"); return false; }
        t.Write(dds.GetOutputStreamBuffer()->GetBufferStart(), dds.GetSize());
    }
    BCT_LOG("wrote temp DDS %s (%zu bytes)", tmp.mb_str(), (size_t)dds.GetSize());

    bool ok = DDSImage::LoadFromFile(tmp);
    if (!ok) BCT_ERR("DDSImage refused temp file");
    wxRemoveFile(tmp);
    return ok;
}


// -----------------------------------------------------------------------------
//  helper – on POSIX tmpname may live in /tmp, remove when done.
// -----------------------------------------------------------------------------
wxString BCTImage::MakeTempDDS(const void*, size_t) { return wxEmptyString; } // (unused)

