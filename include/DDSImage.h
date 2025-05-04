// -----------------------------------------------------------------------------
//  DDSImage.h – standalone legacy DDS loader / decoder (DXT1/3/5 + BGRA)
//  Dialect: ISO C++03                       No external libs.
// -----------------------------------------------------------------------------
#ifndef DDSIMAGE_H
#define DDSIMAGE_H

#include "ImageBase.h"
#include <wx/string.h>
#include <wx/stream.h>

#ifdef __WXMSW__
#include <windows.h>          // <─ add
#endif

// -- low-level header structs (124-byte header, pre-DX10) ---------------------
#pragma pack(push,1)
struct DDSPixelFormat
{
    unsigned size, flags, fourCC, rgbBitCount;
    unsigned rMask, gMask, bMask, aMask;
};

struct DDSHeader
{
    unsigned magic;
    unsigned size;
    unsigned flags;
    unsigned height, width;
    unsigned pitchOrLinearSize;
    unsigned depth;
    unsigned mipMapCount;
    unsigned reserved1[11];
    DDSPixelFormat pf;
    unsigned caps, caps2, caps3, caps4, reserved2;
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// DDS Image Class Definition
// -----------------------------------------------------------------------------
class DDSImage : public ImageBase
{
public:
    DDSImage();
    ~DDSImage();

    bool LoadFromFile(const wxString& filePath) override;
    int  Width() const override { return m_w; }
    int  Height() const override { return m_h; }
    const unsigned char* Data() const override { return m_pixels; }

    // -------- optional post-process modes (normal-map rebuild) ---------------
    void ApplyNormalRG();
    void ApplyNormalAG();
    void ApplyNormalARG();
    void PreMultiplyAlpha();

    // Added reporting functions
    wxString GetFormat() const override;
    wxString GetSize() const override;
    wxString GetMipCount() const override;
    wxString GetMemoryUsage() const override;

private:
    bool ReadHeader(wxInputStream& in, DDSHeader& hdr);
    bool DecodeToBGRA(wxInputStream& in, const DDSHeader& hdr);

    void DecodeDXT1Block(const unsigned char* src, int bx, int by);
    void DecodeDXT3Block(const unsigned char* src, int bx, int by);
    void DecodeDXT5Block(const unsigned char* src, int bx, int by);
    void DecodeATI2Block(const unsigned char* s, int bx, int by);
    void DecodePlain32(const unsigned char* srcRow, int y, int bpp);

    static void Expand565(unsigned c, unsigned char& r,
                          unsigned char& g, unsigned char& b);

    void Free();

    unsigned char* m_pixels;
    int m_w, m_h;
    int m_pitch;
    int m_mipCount;          // Store the mip count from the DDS header
    size_t m_memoryUsed;
    size_t m_memoryTotal;    // Store total memory (based on width, height, and format)

    wxString m_format;       // Store the image format as a wxString
    unsigned m_fourCC;       // Store the FOURCC code for the image format
};


#endif // DDSIMAGE_H
