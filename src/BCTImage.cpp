#include "BCTImage.h"
#include <wx/wfstream.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <map>

namespace {

    // Helper to linearly interpolate two bytes based on weight
    inline unsigned char lerpByte(unsigned char a, unsigned char b, int w2of3) {
        return (w2of3 == 0) ? static_cast<unsigned char>((a + b) >> 1)
                            : static_cast<unsigned char>((2 * a + b) / 3);
    }

    // 5-bit and 6-bit to 8-bit LUTs for color conversion
    struct Tables {
        unsigned char r5[32];   // 0-31 → 0-255
        unsigned char g6[64];   // 0-63 → 0-255
        Tables() {
            for (int i = 0; i < 32; ++i) r5[i] = static_cast<unsigned char>((i << 3) | (i >> 2));
            for (int i = 0; i < 64; ++i) g6[i] = static_cast<unsigned char>((i << 2) | (i >> 4));
        }
    };
    static const Tables LUT;

    // Map BCT format ID to DXGI format (DXGI enumeration)
    inline int mapBctToDxgi(int fmtID) {
        switch (fmtID) {
            case 0x00: return 28;  // R8G8B8A8_UNORM
            case 0x08: return 71;  // BC1_UNORM ('DXT1')
            case 0x0A: return 77;  // BC3_UNORM ('DXT5')
            case 0x25: return 80;  // BC4_UNORM ('ATI1')
            case 0x26: return 83;  // BC5_UNORM ('ATI2')
            case 0x27: return 95;  // BC6H_UF16 (DX10 only)
            case 0x28: return 98;  // BC7_UNORM (DX10 only)
            case 0x30: return 71;  // Alias : BC1_UNORM
            case 0x32: return 77;  // Alias : BC3_UNORM
            case 0x35: return 28;  // Alias : RGBA8
            default: return 0;     // UNKNOWN
        }
    }
}

// Swap endian functions for 16 and 32-bit data
inline uint16_t SwapEndian16(uint16_t value) {
    return (value >> 8) | (value << 8);
}

inline uint32_t SwapEndian32(uint32_t value) {
    return (value >> 24) | ((value >> 8) & 0x0000FF00) | ((value << 8) & 0x00FF0000) | (value << 24);
}












// Swap byte order for big-endian to little-endian conversion
void FlipByteOrder16bit(std::vector<unsigned char>& data) {
    for (size_t i = 0; i < data.size(); i += 2) {
        std::swap(data[i], data[i + 1]);
    }
}




// Bitwise next power of 2 (faster than floating-point)
uint32_t NextPowerOf2(uint32_t value) {
    if (value == 0) return 1;
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

// Calculate X offset for Xbox 360 swizzled texture (adjusted for NPOT)
int XGAddress2DTiledX(uint32_t blockOffset, uint32_t widthInBlocks, uint32_t texelBytePitch) {
    uint32_t alignedWidth = NextPowerOf2(widthInBlocks);  // Align to the next power of 2
    uint32_t logBpp = (texelBytePitch >> 2) + ((texelBytePitch >> 1) >> (texelBytePitch >> 2));
    uint32_t offsetByte = blockOffset << logBpp;
    uint32_t offsetTile = ((offsetByte & ~0xFFF) >> 3) + ((offsetByte & 0x700) >> 2) + (offsetByte & 0x3F);
    uint32_t offsetMacro = offsetTile >> (7 + logBpp);

    // Refined macroX to correctly account for NPOT edge wrapping
    uint32_t macroX = ((offsetMacro % (alignedWidth >> 5)) << 2);
    uint32_t tile = ((((offsetTile >> (5 + logBpp)) & 2) + (offsetByte >> 6)) & 3);
    uint32_t macro = (macroX + tile) << 3;

    // Handle the wraparound by including edge adjustments
    uint32_t micro = (((((offsetTile >> 1) & ~0xF) + (offsetTile & 0xF)) & ((texelBytePitch << 3) - 1))) >> logBpp;

    // Refine the offset to ensure that lower left area is correctly handled
    return macro + micro;
}

// Calculate Y offset for Xbox 360 swizzled texture (adjusted for NPOT)
int XGAddress2DTiledY(uint32_t blockOffset, uint32_t widthInBlocks, uint32_t heightInBlocks, uint32_t texelBytePitch) {
    uint32_t alignedHeight = NextPowerOf2(heightInBlocks);  // Align to the next power of 2
    uint32_t alignedWidth = NextPowerOf2(widthInBlocks);    // Needed for macro tile calculation
    uint32_t logBpp = (texelBytePitch >> 2) + ((texelBytePitch >> 1) >> (texelBytePitch >> 2));
    uint32_t offsetByte = blockOffset << logBpp;
    uint32_t offsetTile = ((offsetByte & ~0xFFF) >> 3) + ((offsetByte & 0x700) >> 2) + (offsetByte & 0x3F);
    uint32_t offsetMacro = offsetTile >> (7 + logBpp);

    // Corrected macroY calculation: divide by width in macro tiles, adjusted for NPOT handling
    uint32_t macroY = ((offsetMacro / (alignedWidth >> 5)) << 2);
    uint32_t tile = ((offsetTile >> (6 + logBpp)) & 1) + (((offsetByte & 0x800) >> 10));
    uint32_t macro = (macroY + tile) << 3;

    // Refined micro offset to ensure tiling is correct, especially for NPOT
    uint32_t micro = (((offsetTile & (((texelBytePitch << 6) - 1) & ~0x1F)) + ((offsetTile & 0xF) << 1)) >> (3 + logBpp)) & ~1;

    // Handle the wraparound of micro offsets in the lower-left corner
    return macro + micro + ((offsetTile & 0x10) >> 4);
}

std::vector<uint8_t> Xbox360ConvertToLinearTexture(const std::vector<uint8_t>& data, int pixelWidth, int pixelHeight, uint32_t texelBytePitch, uint32_t blockPixelSize) {
    uint32_t widthInBlocks = (pixelWidth + blockPixelSize - 1) / blockPixelSize;
    uint32_t heightInBlocks = (pixelHeight + blockPixelSize - 1) / blockPixelSize;

    uint32_t alignedWidth = NextPowerOf2(widthInBlocks);
    uint32_t alignedHeight = NextPowerOf2(heightInBlocks);

    uint32_t totalAlignedBlocks = alignedWidth * alignedHeight;

    std::vector<uint8_t> destData(widthInBlocks * heightInBlocks * texelBytePitch, 0);

    for (uint32_t blockOffset = 0; blockOffset < totalAlignedBlocks; blockOffset++) {
        uint32_t x = XGAddress2DTiledX(blockOffset, widthInBlocks, texelBytePitch);
        uint32_t y = XGAddress2DTiledY(blockOffset, widthInBlocks, heightInBlocks, texelBytePitch);

        if (x < widthInBlocks && y < heightInBlocks) {
            uint32_t srcByteOffset = blockOffset * texelBytePitch;
            uint32_t destByteOffset = y * widthInBlocks * texelBytePitch + x * texelBytePitch;

            if (srcByteOffset + texelBytePitch <= data.size() && destByteOffset + texelBytePitch <= destData.size()) {
                std::memcpy(&destData[destByteOffset], &data[srcByteOffset], texelBytePitch);
            } else {
                std::cerr << "Error: Byte offset out of range!" << std::endl;
            }
        }
    }

    return destData;
}











void dr3BctMip_t::Read(wxInputStream& in, bool isBigEndian) {
    uint32_t buffer[4];
    uint64_t pos = in.TellI();
    size_t bytesRead = in.Read(buffer, sizeof(buffer)).LastRead();

    if (bytesRead != sizeof(buffer)) {
        // Error handling without popup
        return;
    }

    // Swap endianness for all fields if big-endian
    dataAddr = isBigEndian ? SwapEndian32(buffer[0]) : buffer[0];
    dataSize = isBigEndian ? SwapEndian32(buffer[1]) : buffer[1];
    flags = isBigEndian ? SwapEndian32(buffer[2]) : buffer[2];
    unk09 = isBigEndian ? SwapEndian32(buffer[3]) : buffer[3];
}

bool BCTHeader::Read(wxInputStream& in) {
    uint64_t pos = in.TellI();
    uint64_t fileSize = in.SeekI(0, wxFromEnd);
    in.SeekI(pos);

    // Read the 20-byte header
    uint8_t buffer[20];
    size_t bytesRead = in.Read(buffer, sizeof(buffer)).LastRead();
    if (bytesRead != sizeof(buffer)) {
        return false;
    }

    // Parse header fields
    sig1 = buffer[0];
    sig2 = buffer[1];
    sig3 = buffer[2];
    sig4 = buffer[3];
    imgWidth = SwapEndian16((buffer[4] << 8) | buffer[5]);
    imgHeight = SwapEndian16((buffer[6] << 8) | buffer[7]);
    imgFormat = buffer[8];
    fmtVersion = buffer[9];
    imgMips = buffer[10];
    bitsPerPixel = buffer[11];
    imgHash = SwapEndian32((buffer[12] << 24) | (buffer[13] << 16) | (buffer[14] << 8) | buffer[15]);
    imgInfoAddr = SwapEndian32((buffer[16] << 24) | (buffer[17] << 16) | (buffer[18] << 8) | buffer[19]);

    // Determine endianness
    isBigEndian = imgInfoAddr > 16777216;
    if (isBigEndian) {
        imgWidth = SwapEndian16(imgWidth);
        imgHeight = SwapEndian16(imgHeight);
        imgInfoAddr = SwapEndian32(imgInfoAddr);
    }

    // Validate dimensions
    if (imgWidth == 0 || imgHeight == 0) {
        return false;
    }

    // Seek to mip info
    if (pos + imgInfoAddr > fileSize) {
        return false;
    }
    in.SeekI(pos + imgInfoAddr);

    // Read only the first mip
    imgInfo.clear();
    data.clear();
    imgInfo.resize(1);
    imgInfo[0].Read(in, isBigEndian);

    // Validate mip info
    if (imgInfo[0].dataAddr == 0 || imgInfo[0].dataSize == 0) {
        return false;
    }

    // For DXT5 (0x0A), calculate the correct size
    if (imgFormat == 0x0A) {
        size_t blockSize = 16; // DXT5 uses 16 bytes per 4x4 block
        int blocksX = (imgWidth + 3) / 4;
        int blocksY = (imgHeight + 3) / 4;
        size_t calculatedSize = blocksX * blocksY * blockSize;

        // Override the incorrect dataSize from the file
        imgInfo[0].dataSize = calculatedSize;
    }

    // Validate data position
    uint64_t dataPos = pos + imgInfo[0].dataAddr;
    if (dataPos + imgInfo[0].dataSize > fileSize) {
        return false;
    }

    // Read the image data
    data.resize(1);
    in.SeekI(dataPos);
    data[0].resize(imgInfo[0].dataSize);
    bytesRead = in.Read(&data[0][0], imgInfo[0].dataSize).LastRead();
    if (bytesRead != imgInfo[0].dataSize) {
        return false;
    }

    return true;
}

// BCTImage constructor and destructor
BCTImage::BCTImage() : m_pixels(nullptr), m_w(0), m_h(0), m_pitch(0), m_format(0) {}

BCTImage::~BCTImage() {
    Free();
}

void BCTImage::Free() {
    delete[] m_pixels;
    m_pixels = nullptr;
    m_w = m_h = m_pitch = 0;
}

// Load function to load a BCT file
bool BCTImage::LoadFromFile(const wxString& filePath) {
    Free();

    wxFileInputStream in(filePath);
    if (!in.IsOk()) {
        wxMessageBox("Failed to open file: " + filePath, "Error", wxOK | wxICON_ERROR);
        return false;
    }

    if (!m_header.Read(in)) {
        wxMessageBox("Failed to read header from file", "Error", wxOK | wxICON_ERROR);
        return false;
    }

    m_w = m_header.imgWidth;
    m_h = m_header.imgHeight;
    m_pitch = m_w * 4;
    m_format = mapBctToDxgi(m_header.imgFormat);

    size_t bytes = size_t(m_pitch) * m_h;
    m_pixels = new unsigned char[bytes];
    if (!m_pixels) {
        wxMessageBox("Failed to allocate memory for pixels", "Error", wxOK | wxICON_ERROR);
        return false;
    }

    if (!m_header.data.empty()) {
        DecodeToBGRA(in);
    }

    return true;
}

void BCTImage::DecodeToBGRA(wxInputStream& in) {
    const unsigned char* mipData = &m_header.data[0][0];
    if (!m_pixels || mipData == nullptr) {
        wxMessageBox("No pixel data or mipData is null", "Error", wxOK | wxICON_ERROR);
        return;
    }

    int mipWidth = m_header.imgWidth;
    int mipHeight = m_header.imgHeight;
    int blockSize = (m_format == 0x0A || m_format == 0x4D) ? 16 : 8;  // DXT5 = 16 bytes, DXT1 = 8 bytes
    int numBlocksX = (mipWidth + 3) / 4;
    int numBlocksY = (mipHeight + 3) / 4;
    int blockIdx = 0;

    uint32_t texelBytePitch = 0;
    uint32_t blockPixelSize = 0;

    // Set texelBytePitch and blockPixelSize based on the texture format
    switch (m_format) {
        case 0x1C: // Example case: ARGB (RGBA)
            blockPixelSize = 1;
            texelBytePitch = 4;  // 4 bytes per texel for RGBA
            break;
        case 0x00: // Palette-based formats (e.g., 8bpp)
            blockPixelSize = 1;
            texelBytePitch = 1;  // 1 byte per texel (palette index)
            break;
        case 0x0A: // DXT5 (4x4 blocks, 16 bytes)
        case 0x4D: // Another DXT5 variant
            blockPixelSize = 4;
            texelBytePitch = 16;  // 16 bytes per block
            break;
        case 0x47: // DXT1 (4x4 blocks, 8 bytes)
            blockPixelSize = 4;
            texelBytePitch = 8;  // 8 bytes per block
            break;
        case 0x50: // ATI1 (4x4 blocks, 8 bytes)
            blockPixelSize = 4;
            texelBytePitch = 8;  // 8 bytes per block
            break;
        case 0x53: // ATI2 (4x4 blocks, 16 bytes)
            blockPixelSize = 4;
            texelBytePitch = 16;  // 16 bytes per block
            break;
        default:
            wxMessageBox("Unsupported format", "Error", wxOK | wxICON_ERROR);
            return;
    }

    // If big-endian and certain formats, untile the data and flip byte order
    std::vector<unsigned char> untiledData;
    const unsigned char* decodedMipData = mipData;

    if (m_header.isBigEndian && (m_format == 0x0A || m_format == 0x4D || m_format == 0x47 || m_format == 0x50 || m_format == 0x53)) {
        // Flip byte order (assuming 16-bit)
        std::vector<unsigned char> flippedData(mipData, mipData + m_header.data[0].size());
        FlipByteOrder16bit(flippedData);
        untiledData = Xbox360ConvertToLinearTexture(flippedData, mipWidth, mipHeight, texelBytePitch, blockPixelSize);
        decodedMipData = untiledData.data();
    }

    // Decoding based on format
    if (m_format == 0x1C) {
        for (int y = 0; y < mipHeight; ++y) {
            unsigned char* dst = m_pixels + y * m_pitch;
            const unsigned char* src = decodedMipData + y * mipWidth * 4;
            std::memcpy(dst, src, mipWidth * 4);
        }
    } else if (m_format == 0x00) {
        unsigned char* palette = new unsigned char[256 * 4];
        std::memcpy(palette, decodedMipData, 256 * 4);

        for (int y = 0; y < mipHeight; ++y) {
            unsigned char* dst = m_pixels + y * m_pitch;
            const unsigned char* src = decodedMipData + 256 * 4 + y * mipWidth;

            for (int x = 0; x < mipWidth; ++x) {
                unsigned char index = src[x];
                unsigned char* color = &palette[index * 4];
                dst[x * 4] = color[0];
                dst[x * 4 + 1] = color[1];
                dst[x * 4 + 2] = color[2];
                dst[x * 4 + 3] = color[3];
            }
        }
        delete[] palette;
    } else {
        for (int by = 0; by < numBlocksY; ++by) {
            for (int bx = 0; bx < numBlocksX; ++bx) {
                unsigned char block[16];
                std::memcpy(block, &decodedMipData[blockIdx * blockSize], blockSize);

                switch (m_format) {
                    case 0x0A:
                        DecodeDXT5Block(block, bx, by);
                        break;
                    case 0x47:
                        DecodeDXT1Block(block, bx, by);
                        break;
                    case 0x4D:
                        DecodeDXT5Block(block, bx, by);
                        break;
                    case 0x50:
                        DecodeATI1Block(block, bx, by);
                        break;
                    case 0x53:
                        DecodeATI2Block(block, bx, by);
                        break;
                    default:
                        wxMessageBox("Unsupported format", "Error", wxOK | wxICON_ERROR);
                        return;
                }
                ++blockIdx;
            }
        }
    }
}




void BCTImage::Expand565(unsigned c, unsigned char& r, unsigned char& g, unsigned char& b) {
    r = LUT.r5[(c >> 11) & 0x1F];   // 5-bit red channel
    g = LUT.g6[(c >> 5) & 0x3F];    // 6-bit green channel
    b = LUT.r5[c & 0x1F];           // 5-bit blue channel
}


unsigned char BCTImage::lerpByte(unsigned char a, unsigned char b, int w2of3) {
    return (w2of3 == 0) ? static_cast<unsigned char>((a + b) >> 1)
                        : static_cast<unsigned char>((2 * a + b) / 3);
}




void BCTImage::DecodeDXT1Block(const unsigned char* s, int bx, int by) {
    const unsigned c0 = s[0] | (s[1] << 8);
    const unsigned c1 = s[2] | (s[3] << 8);

    unsigned char r0, g0, b0, r1, g1, b1;
    Expand565(c0, r0, g0, b0);
    Expand565(c1, r1, g1, b1);

    const bool opaque = (c0 > c1);
    unsigned char clr[4][4];
    clr[0][0] = b0; clr[0][1] = g0; clr[0][2] = r0; clr[0][3] = 255;
    clr[1][0] = b1; clr[1][1] = g1; clr[1][2] = r1; clr[1][3] = 255;
    if (opaque) {
        clr[2][0] = lerpByte(b0, b1, 1); clr[2][1] = lerpByte(g0, g1, 1); clr[2][2] = lerpByte(r0, r1, 1); clr[2][3] = 255;
        clr[3][0] = lerpByte(b1, b0, 1); clr[3][1] = lerpByte(g1, g0, 1); clr[3][2] = lerpByte(r1, r0, 1); clr[3][3] = 255;
    } else {
        clr[2][0] = lerpByte(b0, b1, 0); clr[2][1] = lerpByte(g0, g1, 0); clr[2][2] = lerpByte(r0, r1, 0); clr[2][3] = 255;
        clr[3][0] = 0; clr[3][1] = 0; clr[3][2] = 0; clr[3][3] = 0;
    }

    unsigned idx = s[4] | (s[5] << 8) | (s[6] << 16) | (s[7] << 24);
    const int xBase = bx << 2, yBase = by << 2;
    for (int py = 0; py < 4; ++py) {
        unsigned char* dst = m_pixels + (yBase + py) * m_pitch + (xBase << 2);
        for (int px = 0; px < 4; px += 2, idx >>= 4) {
            const unsigned char* c0p = clr[idx & 3];
            dst[0] = c0p[0]; dst[1] = c0p[1]; dst[2] = c0p[2]; dst[3] = c0p[3];
            const unsigned char* c1p = clr[(idx >> 2) & 3];
            dst[4] = c1p[0]; dst[5] = c1p[1]; dst[6] = c1p[2]; dst[7] = c1p[3];
            dst += 8;
        }
    }
}

/* ──────────────────────────────────────────────────────────────────── */
void BCTImage::DecodeDXT3Block(const unsigned char* s,int bx,int by)
{
    unsigned char alpha[16];
    for (int i=0;i<8;++i){
        unsigned v=s[i];
        alpha[i*2  ]=(unsigned char)((v & 0x0F)*17);
        alpha[i*2+1]=(unsigned char)(((v>>4)&0x0F)*17);
    }
    DecodeDXT1Block(s+8,bx,by);

    const int xBase = bx<<2, yBase = by<<2;
    for(int py=0;py<4;++py){
        unsigned char* dst=m_pixels+(yBase+py)*m_pitch+(xBase<<2);
        for(int px=0;px<4;++px)
            dst[px*4+3]=alpha[py*4+px];
    }
}

/* ──────────────────────────────────────────────────────────────────── */
void BCTImage::DecodeDXT5Block(const unsigned char* s, int bx, int by) {
    const unsigned a0 = s[0], a1 = s[1];  // Extract alpha0 and alpha1 values
    const unsigned char* abits = s + 2;    // The next 6 bytes represent alpha interpolation
    const unsigned char* colour = s + 8;   // The following 8 bytes represent the color data

    // Interpolate the alpha values
    unsigned char lut[8] = {a0, a1};
    if (a0 > a1) {
        for (int k = 1; k <= 6; ++k) {
            lut[1 + k] = static_cast<unsigned char>(((7 - k) * a0 + k * a1) / 7);
        }
    } else {
        for (int k = 1; k <= 4; ++k) {
            lut[1 + k] = static_cast<unsigned char>(((5 - k) * a0 + k * a1) / 5);
        }
        lut[6] = 0;
        lut[7] = 255;
    }

    // Unpack the 6 bytes for alpha values (using bit shifts to decode the bits)
    unsigned long long bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<unsigned long long>(abits[i]) << (8 * i);
    }

    // Interpolate the 16 alpha values based on the bits
    unsigned char alpha[16];
    for (int i = 0; i < 16; ++i) {
        alpha[i] = lut[(bits >> (3 * i)) & 7];
    }

    // Decode the color block (RGB)
    DecodeDXT1Block(colour, bx, by);  // Use DXT1 block decoding to get color

    // Set pixel positions for DXT5 block
    const int xBase = bx << 2, yBase = by << 2;
    for (int py = 0; py < 4; ++py) {
        unsigned char* dst = m_pixels + (yBase + py) * m_pitch + (xBase << 2);
        for (int px = 0; px < 4; ++px) {
            // Assign color and alpha to the pixel data (RGBA format)
            dst[px * 4 + 3] = alpha[py * 4 + px];  // Set alpha channel (A)
        }
    }
}

void BCTImage::DecodeATI1Block(const unsigned char* s, int bx, int by) {
    unsigned char alpha[8];
    const unsigned a0 = s[0];
    const unsigned a1 = s[1];

    unsigned char lut[8] = {a0, a1};
    for (int i = 2; i < 6; ++i) {
        lut[i] = static_cast<unsigned char>((7 - i) * a0 + i * a1 / 7);
    }
    lut[6] = 0;
    lut[7] = 255;

    unsigned idx = 0;
    for (int i = 0; i < 6; ++i) {
        idx |= static_cast<unsigned>(s[2 + i]) << (8 * i);
    }

    const int xBase = bx << 2, yBase = by << 2;
    for (int py = 0; py < 4; ++py) {
        unsigned char* dst = m_pixels + (yBase + py) * m_pitch + (xBase << 2);
        for (int px = 0; px < 4; px += 2, idx >>= 4) {
            unsigned char c0 = lut[idx & 7];
            unsigned char c1 = lut[(idx >> 2) & 7];

            dst[3] = c0;  // Store alpha at the correct location
            dst[7] = c1;  // Store second alpha value

            dst += 8;
        }
    }
}


void BCTImage::DecodeATI2Block(const unsigned char* s, int bx, int by) {
    // The layout is similar to two DXT5 alpha blocks back-to-back
    // bytes 0-7: red channel, bytes 8-15: green channel.

    auto expandChannel = [](const unsigned char* q, unsigned char out[16]) {
        const unsigned a0 = q[0], a1 = q[1];
        unsigned char lut[8] = {a0, a1};

        if (a0 > a1) {
            for (int k = 1; k <= 6; ++k) {
                lut[1 + k] = static_cast<unsigned char>((7 - k) * a0 + k * a1 / 7);
            }
        } else {
            for (int k = 1; k <= 4; ++k) {
                lut[1 + k] = static_cast<unsigned char>((5 - k) * a0 + k * a1 / 5);
            }
            lut[6] = 0;
            lut[7] = 255;
        }

        unsigned long long bits = 0;
        for (int i = 0; i < 6; ++i) {
            bits |= static_cast<unsigned long long>(q[2 + i]) << (8 * i);
        }

        for (int i = 0; i < 16; ++i) {
            out[i] = lut[(bits >> (3 * i)) & 7];
        }
    };

    unsigned char red[16], green[16];
    expandChannel(s, red);
    expandChannel(s + 8, green);

    const int xBase = bx << 2, yBase = by << 2;
    for (int py = 0; py < 4; ++py) {
        unsigned char* dst = m_pixels + (yBase + py) * m_pitch + (xBase << 2);
        for (int px = 0; px < 4; ++px) {
            const int idx = py * 4 + px;
            dst[2] = 127;  // Default Blue channel to 127, will fix later
            dst[1] = green[idx];  // Green channel
            dst[0] = red[idx];  // Red channel (will swap later)
            dst[3] = 255;  // Full Alpha (opaque)
            dst += 4;
        }
    }
}

static inline unsigned char toUNorm(float v){
    return static_cast<unsigned char>( (v<0.f?0.f:(v>1.f?1.f:v))*255.f + 0.5f );
}
void BCTImage::ApplyNormalRG()
{
    if(!m_pixels) return;
    for(int y=0;y<m_h;++y){
        unsigned char* row=m_pixels+y*m_pitch;
        for(int x=0;x<m_w;++x, row+=4){
            float nx=row[2]/127.5f-1.f;
            float ny=row[1]/127.5f-1.f;
            float nz=std::sqrt(std::max(0.f,1.f-nx*nx-ny*ny));
            row[0]=toUNorm((nz+1.f)*0.5f);
            row[3]=255;
        }
    }
}

void BCTImage::ApplyNormalAG()
{
    if(!m_pixels) return;
    for(int y=0;y<m_h;++y){
        unsigned char* row=m_pixels+y*m_pitch;
        for(int x=0;x<m_w;++x, row+=4){
            float nx=row[3]/127.5f-1.f;
            float ny=row[1]/127.5f-1.f;
            float nz=std::sqrt(std::max(0.f,1.f-nx*nx-ny*ny));
            row[2]=toUNorm((nx+1.f)*0.5f);
            row[0]=toUNorm((nz+1.f)*0.5f);
            row[3]=255;
        }
    }
}

void BCTImage::ApplyNormalARG()
{
    if(!m_pixels) return;

    for(int y=0;y<m_h;++y){
        unsigned char* row=m_pixels+y*m_pitch;
        for(int x=0;x<m_w;++x, row+=4){
            float nx=(row[3]*row[2]/255.f)/127.5f-1.f;
            float ny=row[1]/127.5f-1.f;
            float nz=std::sqrt(std::max(0.f,1.f-nx*nx-ny*ny));
            row[2]=toUNorm((nx+1.f)*0.5f);
            row[0]=toUNorm((nz+1.f)*0.5f);
            row[3]=255;
        }
    }
}

wxString BCTImage::GetFormat() const
{
    // Return the image format based on the BCT header format
    switch (m_format) {
        case 28:
            return wxT("DXT1");
        case 71:
            return wxT("DXT3");
        case 77:
            return wxT("DXT5");
        case 80:
            return wxT("ATI1");
        case 83:
            return wxT("ATI2");
        case 95:
            return wxT("DXT5");
        default:
            return wxT("Unknown");
    }
}

wxString BCTImage::GetSize() const
{
    // Return the image size in width x height format
    return wxString::Format("%dx%d", m_w, m_h);
}

wxString BCTImage::GetMipCount() const
{
    // Return the number of mipmaps from the BCT header
    return wxString::Format("%d", m_header.imgMips);
}

wxString BCTImage::GetMemoryUsage() const
{
    // Assuming the format uses 4 bytes per pixel (BGRA format)
    size_t memoryUsed = static_cast<size_t>(m_w) * m_h * 4;  // Memory used
    return wxString::Format("Mem: %.1fKB", memoryUsed / 1024.0);
}

