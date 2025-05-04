// ============================================================================
//  ImageBase.h – tiny polymorphic interface used by the frame
// ============================================================================

#pragma once
#include <wx/string.h>

class ImageBase
{
public:
    virtual ~ImageBase() = default;

    virtual bool LoadFromFile(const wxString& path) = 0;
    virtual int  Width()  const = 0;
    virtual int  Height() const = 0;
    virtual const unsigned char* Data() const = 0;

    virtual void ApplyNormalRG() {}
    virtual void ApplyNormalAG() {}
    virtual void ApplyNormalARG() {}

    virtual void PreMultiplyAlpha() {}

    // Added reporting functions
    virtual wxString GetFormat() const = 0;
    virtual wxString GetSize() const;
    virtual wxString GetMipCount() const;
    virtual wxString GetMemoryUsage() const;
};
