#ifndef BCTIMAGE_H
#define BCTIMAGE_H

#include "ImageBase.h"
#include <wx/wx.h>
#include <vector>

struct dr3BctMip_t {
    uint32_t dataAddr;  // Data address in the BCT file
    uint32_t dataSize;  // Size of the mipmap data
    uint32_t flags;     // Flags (0x80000000)
    uint32_t unk09;     // Unknown data (unused in your example)

    void Read(wxInputStream& in, bool isBigEndian);
};

struct BCTHeader {
    bool isBigEndian;
    uint8_t sig1;          // Custom signature bytes
    uint8_t sig2;
    uint8_t sig3;
    uint8_t sig4;
    uint16_t imgWidth;     // Image width
    uint16_t imgHeight;    // Image height
    uint8_t imgFormat;     // Image format
    uint8_t fmtVersion;    // Format version
    uint8_t imgMips;       // Number of mipmaps
    uint8_t bitsPerPixel;  // Bits per pixel
    uint32_t imgHash;      // Image hash
    uint32_t imgInfoAddr;  // Address of the mipmap info structure
    std::vector<uint8_t> unkBuf;    // Unused buffer (changed to std::vector)
    std::vector<dr3BctMip_t> imgInfo;  // Mipmap info (one entry per mipmap)
    std::vector<std::vector<uint8_t>> data; // Mipmap data

    bool Read(wxInputStream& in);
};

class BCTImage : public ImageBase {
public:
    BCTImage();
    ~BCTImage();

    bool LoadFromFile(const wxString& filePath) override;
    void Free();

    void DecodeToBGRA(wxInputStream& in);  // Decode the image to BGRA format
    void ApplyNormalRG() override;  // Apply normal map RG
    void ApplyNormalAG() override;  // Apply normal map AG
    void ApplyNormalARG() override; // Apply normal map ARG

    unsigned char* GetPixels() const { return m_pixels; }
    int Width() const override { return m_w; }
    int Height() const override { return m_h; }
    const unsigned char* Data() const override { return m_pixels; }

    // Implement missing functions
    wxString GetFormat() const override;
    wxString GetSize() const override;
    wxString GetMipCount() const override;
    wxString GetMemoryUsage() const override;

private:
    void DecodeDXT1Block(const unsigned char* s, int bx, int by);  // Decode DXT1 block
    void DecodeDXT3Block(const unsigned char* s, int bx, int by);  // Decode DXT3 block
    void DecodeDXT5Block(const unsigned char* s, int bx, int by);  // Decode DXT5 block
    void DecodeATI1Block(const unsigned char* s, int bx, int by);  // Decode ATI1 block (if needed)
    void DecodeATI2Block(const unsigned char* s, int bx, int by);  // Decode ATI2 block (if needed)
    void Expand565(unsigned c, unsigned char& r, unsigned char& g, unsigned char& b);  // Expand 565 format to RGB
    unsigned char lerpByte(unsigned char a, unsigned char b, int w2of3);  // Interpolate between two bytes

    unsigned char* m_pixels;  // Image pixel data in BGRA format
    int m_w, m_h;  // Image dimensions
    int m_pitch;  // Image pitch (width * 4 for BGRA)
    int m_format;  // Image format (DXGI format, for example)

    BCTHeader m_header;  // BCT header containing metadata and image data
};


#endif  // BCTIMAGE_H
