#ifndef BCTV_H
#define BCTV_H

#include <wx/wx.h>
#include <wx/dnd.h>
#include <wx/filename.h>
#include <wx/colordlg.h>
#include <wx/dir.h>          // <-- Added for wxDir
#include <wx/cmdline.h>
#include <memory>            // for std::auto_ptr
#include <wx/icon.h>

// Include ImageBase header for polymorphism
#include "ImageBase.h" // <-- Add this to use ImageBase and its derived classes

// Forward declaration of BCTVCanvas class
class BCTVCanvas;

extern "C" { extern const char *APP_ICON; }   // <- name from RC (no .ico)

class BCTVApp : public wxApp
{
public:
    wxVector<wxString> m_startupFiles;

    /* wxApp hooks ---------------------------------------------------- */
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;
    bool OnInit() override;
};

class BCTVFrame : public wxFrame {
public:
    BCTVFrame();
    virtual ~BCTVFrame();

    double GetZoom() const            { return m_zoom; }

    /* zoom helpers -------------------------------------------------- */
    void ChangeZoom(double factor);          // already present

    /* wheel-delta accumulator --------------------------------------- */
    int  GetWheelAccum()        const { return m_wheelAccum; }        // **NEW**
    void AddWheelAccum(int d)         { m_wheelAccum += d; }          // **NEW**

    bool   ShowChR() const            { return m_showR; }
    bool   ShowChG() const            { return m_showG; }
    bool   ShowChB() const            { return m_showB; }
    bool   ShowChA() const            { return m_showA; }
    int    GetWheelMode() const       { return m_wheelMode; }
bool m_manualZoom;

    // Core operations
    bool LoadImage(const wxString& path, bool recordDir = true);
    void RebuildBitmap();             // apply channel masks & post-process
    void UpdateFrameTitle();
    void UpdateWindowForImage();

    void StepImage(int step);
    void JumpImage(int idx);

    void ShowCursorInfo(int ix, int iy, unsigned char* px);

    // Add status bar
    void UpdateStatusBar();  // **NEW** Status bar declaration

    // Background Colors
    void SetPrimaryBackgroundColor(const wxColour& color);
    void SetSecondaryBackgroundColor(const wxColour& color);
    wxColour GetSecondaryBackgroundColour() const { return m_bgSecondary; }

    const wxColour& GetCanvasBgColour() const { return m_bgSecondary; }
    bool            IsAlphaShown()    const { return m_showA;        }

    enum {
        // File
        ID_FILE_OPEN = wxID_HIGHEST+1,
        ID_FILE_EXIT,

        // Channels
        ID_CH_R, ID_CH_G, ID_CH_B, ID_CH_A,
        ID_BG_COLOUR,

        // Filter
        ID_FILT_SHR, ID_FILT_ENL,

        // Window options
        ID_WIN_CLIP, ID_WIN_CENTER, ID_WIN_TOP,

        // Mouse wheel modes
        ID_WHEEL_CYCLE, ID_WHEEL_5, ID_WHEEL_10, ID_WHEEL_25, ID_WHEEL_50,

        // Wrap / Auto-zoom
        ID_WRAP, ID_AUTOZOOM,

        // Post-process modes
        ID_PP_NONE, ID_PP_RG, ID_PP_AG, ID_PP_ARG,

        // Help
        ID_HELP_ABOUT
    };

private:
    BCTVCanvas*     m_canvas;

    // Change m_img to ImageBase* to support polymorphism
    ImageBase*      m_img;  // Changed from DDSImage* to ImageBase*

    wxBitmap        m_bmp;

    double          m_zoom;
    bool            m_showR, m_showG, m_showB, m_showA;
    bool            m_filtShr, m_filtEnl;
    bool            m_clip, m_center, m_top;
    int             m_wheelMode;
    bool            m_wrap, m_auto;
    int             m_pp;
    wxColour        m_bg;              // Frame background color
    wxColour        m_bgSecondary;     // Canvas background color
    wxStatusBar*    m_statusBar;       // **NEW** Status bar declaration

    wxArrayString   m_fileList;
    int             m_curIdx;
    int  m_wheelAccum;        // leftover wheel delta (in units of WHEEL_DELTA)

    // Event handlers
    void OnOpen(wxCommandEvent&);
    void OnExit(wxCommandEvent&);
    void OnToggleChannel(wxCommandEvent&);
    void OnBgColour(wxCommandEvent&);
    void OnFilter(wxCommandEvent&);
    void OnWindowOpt(wxCommandEvent&);
    void OnWheelMode(wxCommandEvent&);
    void OnWrapAuto(wxCommandEvent&);
    void OnPostProcess(wxCommandEvent&);
    void OnAbout(wxCommandEvent&);
    void OnKey(wxKeyEvent&);

    DECLARE_EVENT_TABLE()
};

class BCTVCanvas : public wxPanel {
public:
    explicit BCTVCanvas(BCTVFrame* host);
    void RecreateBitmap(const wxBitmap& bmp);

private:
    BCTVFrame*      m_host;
    wxBitmap        m_bmp;

    void OnPaint(wxPaintEvent&);
    void OnErase(wxEraseEvent&) {}
    void OnMotion(wxMouseEvent&);
    void OnWheel(wxMouseEvent&);
    void OnLeftDown(wxMouseEvent&);

    class DropTarget : public wxFileDropTarget {
    public:
        DropTarget(BCTVFrame* f) : m_frame(f) {}
        bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& files) {
            return !files.IsEmpty() && m_frame->LoadImage(files[0]);
        }
    private:
        BCTVFrame* m_frame;
    };

    DECLARE_EVENT_TABLE()
};

#endif // BCTV_H
