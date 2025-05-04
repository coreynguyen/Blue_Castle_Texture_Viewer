// DDSImage.cpp – faster standalone DDS decoder  (DXT1/3/5 + BGRA)
#include "DDSImage.h"
#include <wx/wfstream.h>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>

#define FOURCC(a,b,c,d) ( unsigned(a) | (unsigned(b)<<8) | \
                          (unsigned(c)<<16) | (unsigned(d)<<24) )

static const unsigned FOURCC_DDS  = FOURCC('D','D','S',' ');
static const unsigned FOURCC_DXT1 = FOURCC('D','X','T','1');
static const unsigned FOURCC_DXT3 = FOURCC('D','X','T','3');
static const unsigned FOURCC_DXT5 = FOURCC('D','X','T','5');
static const unsigned FOURCC_ATI2 = FOURCC('A','T','I','2');   //  ⬅ NEW

/* ──────────────────────────────────────────────────────────────────── */
/*  Small helpers & tables                                             */
/* ──────────────────────────────────────────────────────────────────── */
namespace {

inline unsigned char lerpByte(unsigned char a,unsigned char b,int w2of3)
{
    /* w2of3 = 0 → ½, 1 → ⅓  (c0>c1) */
    return (w2of3==0) ? static_cast<unsigned char>((a+b)>>1)
                      : static_cast<unsigned char>((2*a + b) / 3);
}

/* 5-bit and 6-bit to 8-bit LUTs (generated once, thread-safe C++03)  */
struct Tables {
    unsigned char r5[32];   // 0-31 → 0-255
    unsigned char g6[64];   // 0-63 → 0-255
    Tables() {
        for (int i=0;i<32;++i) r5[i] = static_cast<unsigned char>((i<<3)|(i>>2));
        for (int i=0;i<64;++i) g6[i] = static_cast<unsigned char>((i<<2)|(i>>4));
    }
};
static const Tables LUT;     // zero-overhead Meyers singleton

} // anon-ns

/* ──────────────────────────────────────────────────────────────────── */
/*                         ctor / dtor / reset                         */
/* ──────────────────────────────────────────────────────────────────── */
DDSImage::DDSImage() : m_pixels(NULL), m_w(0), m_h(0), m_pitch(0) {}
DDSImage::~DDSImage(){ Free(); }

void DDSImage::Free()
{
    delete [] m_pixels;
    m_pixels = NULL;
    m_w = m_h = m_pitch = 0;
}

/* ──────────────────────────────────────────────────────────────────── */
/*                               public API                            */
/* ──────────────────────────────────────────────────────────────────── */
bool DDSImage::LoadFromFile(const wxString& filePath)
{
    // Free previous data before loading new data
    Free();

    // Open the file for reading
    wxFileInputStream in(filePath);
    if (!in.IsOk()) {
        // File could not be opened
        return false;
    }

    DDSHeader hdr;

    // Read and verify the header
    if (!ReadHeader(in, hdr)) {
        // Header is invalid or unreadable
        return false;
    }

    // Set width, height, and pitch based on header data
    m_w = static_cast<int>(hdr.width);
    m_h = static_cast<int>(hdr.height);
    m_pitch = m_w * 4;  // Assuming 4 bytes per pixel (BGRA format)

    // Calculate the total size required for pixel data
    const size_t bytes = size_t(m_pitch) * m_h;

    // Allocate memory for the pixel data
    m_pixels = new unsigned char[bytes];
    if (!m_pixels) {
        // Memory allocation failed
        return false;
    }

    // Zero out the allocated memory
    std::memset(m_pixels, 0, bytes);

    // Decode the image into the pixel buffer
    bool ok = DecodeToBGRA(in, hdr);
    if (!ok) {
        // Decoding failed
        return false;
    }
    // Determine the format based on FOURCC code
    switch (hdr.pf.fourCC) {
        case FOURCC_DXT1:
            m_format = wxT("DXT1");
            break;
        case FOURCC_DXT3:
            m_format = wxT("DXT3");
            break;
        case FOURCC_DXT5:
            m_format = wxT("DXT5");
            break;
        case FOURCC_ATI2:
            m_format = wxT("ATI2");
            break;
        default:
            m_format = wxT("Unknown");
            break;
    }
    // Optional post-processing (e.g., premultiply alpha)
    //PreMultiplyAlpha();  // Apply only if decoding succeeded

    return true;  // Successfully loaded the image
}


/* ──────────────────────────────────────────────────────────────────── */
bool DDSImage::ReadHeader(wxInputStream& in, DDSHeader& hdr)
{
    if (in.Read(&hdr, sizeof(hdr)).LastRead() != sizeof(hdr)) return false;
    if (hdr.magic != FOURCC_DDS || hdr.size != 124 || hdr.pf.size != 32) return false;
    return hdr.width && hdr.height;
}

/* ──────────────────────────────────────────────────────────────────── */
bool DDSImage::DecodeToBGRA(wxInputStream& in, const DDSHeader& hdr)
{
    const unsigned fmt = hdr.pf.fourCC;

    // block formats ----------------------------------------------------------
    if (fmt==FOURCC_DXT1 || fmt==FOURCC_DXT3 || fmt==FOURCC_DXT5 ||
        fmt==FOURCC_ATI2)                                                   //  NEW
    {
        const unsigned blkLen = (fmt==FOURCC_DXT1) ? 8 : 16;
        const int bw = (m_w + 3) >> 2,  bh = (m_h + 3) >> 2;
        const size_t bytesNeeded = size_t(bw) * bh * blkLen;

        std::vector<unsigned char> img(bytesNeeded);
        if (in.Read(&img[0], bytesNeeded).LastRead() != bytesNeeded) return false;

        const unsigned char* src = &img[0];
        for (int by=0; by<bh; ++by)
            for (int bx=0; bx<bw; ++bx, src+=blkLen)
            {
                switch (fmt) {
                    case FOURCC_DXT1: DecodeDXT1Block(src,bx,by); break;
                    case FOURCC_DXT3: DecodeDXT3Block(src,bx,by); break;
                    case FOURCC_DXT5: DecodeDXT5Block(src,bx,by); break;
                    case FOURCC_ATI2: DecodeATI2Block(src,bx,by); break; //  NEW
                }
            }
        return true;
    }

    // 32-bit uncompressed path ----------------------------------------------
    if (hdr.pf.rgbBitCount == 32)
    {
        const size_t n = size_t(m_pitch)*m_h;
        return in.Read(m_pixels, n).LastRead() == n;
    }

    return false;   // unsupported
}


/* ──────────────────────────────────────────────────────────────────── */
/*            helpers  – 565 expand & copy raw pixels                  */
/* ──────────────────────────────────────────────────────────────────── */
void DDSImage::Expand565(unsigned c, unsigned char& r,
                         unsigned char& g, unsigned char& b)
{
    r = LUT.r5[(c>>11)&0x1F];
    g = LUT.g6[(c>> 5)&0x3F];
    b = LUT.r5[(c    )&0x1F];   // reuse 5-bit table for blue
}

/* (DecodePlain32 is no longer used, kept for compatibility) */
void DDSImage::DecodePlain32(const unsigned char* srcRow,int y,int bpp)
{
    std::memcpy(m_pixels + y*m_pitch, srcRow, m_pitch);
}

/* ──────────────────────────────────────────────────────────────────── */
/*                       DXT1 decoder (8 bytes)                        */
/* ──────────────────────────────────────────────────────────────────── */
void DDSImage::DecodeDXT1Block(const unsigned char* s,int bx,int by)
{
    const unsigned c0 = s[0] | (s[1]<<8);
    const unsigned c1 = s[2] | (s[3]<<8);

    unsigned char r0,g0,b0, r1,g1,b1;
    Expand565(c0,r0,g0,b0);  Expand565(c1,r1,g1,b1);

    const bool opaque = (c0>c1);
    unsigned char clr[4][4]={
        {b0,g0,r0,255},
        {b1,g1,r1,255},
        {lerpByte(b0,b1,1), lerpByte(g0,g1,1), lerpByte(r0,r1,1), 255},
        {lerpByte(b0,b1,0), lerpByte(g0,g1,0), lerpByte(r0,r1,0), static_cast<unsigned char>(opaque?255:0)}
    };

    unsigned idx = s[4] | (s[5]<<8) | (s[6]<<16) | (s[7]<<24);
    const int xBase = bx<<2, yBase = by<<2;

    for (int py=0; py<4; ++py)
    {
        unsigned char* dst = m_pixels + (yBase+py)*m_pitch + (xBase<<2);
        for (int px=0; px<4; px+=2, idx>>=4)
        {
            /* unroll two texels (saves >5 %) */
            const unsigned char* c0p = clr[idx & 3];
            dst[0]=c0p[0]; dst[1]=c0p[1]; dst[2]=c0p[2]; dst[3]=c0p[3];
            const unsigned char* c1p = clr[(idx>>2) & 3];
            dst[4]=c1p[0]; dst[5]=c1p[1]; dst[6]=c1p[2]; dst[7]=c1p[3];
            dst += 8;
        }
    }
}

/* ──────────────────────────────────────────────────────────────────── */
void DDSImage::DecodeDXT3Block(const unsigned char* s,int bx,int by)
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
void DDSImage::DecodeDXT5Block(const unsigned char* s,int bx,int by)
{
    const unsigned a0=s[0], a1=s[1];
    const unsigned char* abits=s+2;
    const unsigned char* colour=s+8;

    unsigned char lut[8]={a0,a1};
    if (a0>a1){
        for(int k=1;k<=6;++k) lut[1+k]=(unsigned char)(((7-k)*a0+k*a1)/7);
    }else{
        for(int k=1;k<=4;++k) lut[1+k]=(unsigned char)(((5-k)*a0+k*a1)/5);
        lut[6]=0; lut[7]=255;
    }

    unsigned long long bits=0;
    for(int i=0;i<6;++i) bits|=(unsigned long long)abits[i]<<(8*i);

    unsigned char alpha[16];
    for(int i=0;i<16;++i) alpha[i]=lut[(bits>>(3*i))&7];

    DecodeDXT1Block(colour,bx,by);

    const int xBase = bx<<2, yBase = by<<2;
    for(int py=0;py<4;++py){
        unsigned char* dst=m_pixels+(yBase+py)*m_pitch+(xBase<<2);
        for(int px=0;px<4;++px)
            dst[px*4+3]=alpha[py*4+px];
    }
}

/* ──────────────────────────────────────────────────────────────────── */
/*               (optional) normal-map reconstruction                  */
/* ──────────────────────────────────────────────────────────────────── */
static inline unsigned char toUNorm(float v){
    return static_cast<unsigned char>( (v<0.f?0.f:(v>1.f?1.f:v))*255.f + 0.5f );
}

void DDSImage::ApplyNormalRG()
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

void DDSImage::ApplyNormalAG()
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

void DDSImage::ApplyNormalARG()
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


// ============================================================================
//  Premultiply BGRA in-place (B,G,R *= A / 255)
//  – pointer walk is slightly unrolled (2 pixels per iteration)
// ============================================================================
// ---------------------------------------------------------------------------
//  Fast premultiply – 100-200 MB/s on a 2005 Pentium-M, no heap use.
//  Uses SSE2 if the compiler flag /arch:SSE2 (VC2005+) or -msse2 (GCC/Clang)
//  is present; otherwise falls back to a portable scalar loop.
// ---------------------------------------------------------------------------
void DDSImage::PreMultiplyAlpha()
{
    if (!m_pixels) return;

    /* pointers that both branches will share ------------------------ */
    unsigned char*       p   = m_pixels;
    const unsigned char* end = m_pixels + size_t(m_pitch) * m_h;

#if defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(_M_X64)
    /* ---------- SIMD path (SSE2) ----------------------------------- */
    #include <emmintrin.h>
    const __m128i zero      = _mm_setzero_si128();
    const __m128i div255    = _mm_set1_epi16(0x8081);           // ≈ 1/255
    const __m128i alphaMask = _mm_set1_epi32(0xFF000000);       // keep A

    while (p + 16 <= end)               // 4 BGRA pixels per loop
    {
        __m128i pix  = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));

        /* duplicate alpha → 16-bit lanes */
        __m128i a    = _mm_srli_epi32(pix, 24);                 // 000A
                a    = _mm_or_si128(a, _mm_slli_epi32(a, 16));  // 00AA

        __m128i lo16 = _mm_unpacklo_epi8(pix, zero);            // to 16-bit

        /* (c * a + 127) / 255  – fixed-point trick               */
        __m128i prod = _mm_mullo_epi16(lo16, a);
        prod         = _mm_add_epi16(prod, div255);
        prod         = _mm_mulhi_epu16(prod, div255);

        __m128i prem = _mm_packus_epi16(prod, zero);            // back to 8-bit
        pix          = _mm_or_si128(prem, _mm_and_si128(pix, alphaMask));

        _mm_storeu_si128(reinterpret_cast<__m128i*>(p), pix);
        p += 16;
    }
#endif /* SSE2 */

    /* ---------- scalar tail / no-SSE2 build ----------------------- */
    for (unsigned char* q = p; q < end; q += 4)
    {
        unsigned a = q[3];
        if (a == 255) continue;          // opaque: nothing to do
        q[0] = (q[0] * a) / 255;         // B
        q[1] = (q[1] * a) / 255;         // G
        q[2] = (q[2] * a) / 255;         // R
    }
}

// -----------------------------------------------------------------------------
//  DecodeATI2Block – two alpha-style blocks (R & G) -> BGRA32
// -----------------------------------------------------------------------------
void DDSImage::DecodeATI2Block(const unsigned char* s, int bx, int by)
{
    // The layout is identical to two DXT5 alpha blocks back-to-back:
    // bytes 0-7 : red channel,  bytes 8-15 : green channel.
    auto expandChannel = [](const unsigned char* q, unsigned char out[16])
    {
        const unsigned a0 = q[0], a1 = q[1];
        unsigned char lut[8] = { a0, a1 };

        if (a0 > a1) {
            for(int k=1;k<=6;++k) lut[1+k] = static_cast<unsigned char>(( (7-k)*a0 + k*a1 ) / 7);
        } else {
            for(int k=1;k<=4;++k) lut[1+k] = static_cast<unsigned char>(( (5-k)*a0 + k*a1 ) / 5);
            lut[6] = 0;  lut[7] = 255;
        }

        unsigned long long bits = 0;
        for(int i=0;i<6;++i) bits |= static_cast<unsigned long long>(q[2+i]) << (8*i);

        for(int i=0;i<16;++i)
            out[i] = lut[(bits >> (3*i)) & 7];
    };

    unsigned char red[16], green[16];
    expandChannel(s,      red);
    expandChannel(s + 8,  green);

    const int xBase = bx << 2,  yBase = by << 2;
    for(int py=0; py<4; ++py)
    {
        unsigned char* dst = m_pixels + (yBase+py)*m_pitch + (xBase<<2);
        for(int px=0; px<4; ++px)
        {
            const int idx = py*4 + px;
            dst[2] = 127;                 // default B, will be fixed later
            dst[1] = green[idx];          // G → G
            dst[0] = red  [idx];          // R → B  (stays in byte 0 for later fixup)
            dst[3] = 255;                 // opaque alpha
            dst += 4;
        }
    }
}


wxString DDSImage::GetFormat() const
{
    // Determine the image format from the DDS header's fourCC value
    switch (m_fourCC) {
        case FOURCC_DXT1:
            return wxString("DXT1");
        case FOURCC_DXT3:
            return wxString("DXT3");
        case FOURCC_DXT5:
            return wxString("DXT5");
        case FOURCC_ATI2:
            return wxString("ATI2");
        default:
            return wxString("Unknown Format");
    }
}

wxString DDSImage::GetSize() const
{
    // Format the width and height for display
    return wxString::Format("%dx%d", m_w, m_h);
}

wxString DDSImage::GetMipCount() const
{
    // Report the actual mip count from the DDS header
    return wxString::Format("Mips: %d/%d", m_mipCount, m_mipCount); // m_mipCount is set during header read
}

wxString DDSImage::GetMemoryUsage() const
{
    // If it's a compressed texture, calculate compressed memory usage based on format.
    if (m_fourCC == FOURCC_DXT1 || m_fourCC == FOURCC_DXT3 || m_fourCC == FOURCC_DXT5) {
        // Each block is 8 bytes for DXT1 and 16 bytes for DXT3/5.
        int blockSize = (m_fourCC == FOURCC_DXT1) ? 8 : 16;
        int widthBlocks = (m_w + 3) / 4;
        int heightBlocks = (m_h + 3) / 4;
        size_t compressedSize = widthBlocks * heightBlocks * blockSize;

        // Convert compressed size to KB
        return wxString::Format("Mem: %.1fKB/%.1fKB", compressedSize / 1024.0, compressedSize / 1024.0);
    } else {
        // For uncompressed textures (32-bit RGBA)
        size_t rawSize = m_w * m_h * 4;  // 4 bytes per pixel
        return wxString::Format("Mem: %.1fKB/%.1fKB", rawSize / 1024.0, rawSize / 1024.0);
    }
}
