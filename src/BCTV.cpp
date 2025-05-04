#include "BCTV.h"
#include "DDSImage.h"
#include "BCTImage.h"

#include "ImageBase.h"  // Assuming ImageBase.h is included here

#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/clipbrd.h>
#include <wx/rawbmp.h>
#include <wx/wfstream.h>

#include <wx/display.h>
#include <wx/dir.h>
#include <memory>
#include <wx/icon.h>

IMPLEMENT_APP(BCTVApp)

#ifdef __WXMSW__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#endif

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

inline bool EnableDarkTitleBar(wxWindow* win, bool enable = true)
{
#ifdef __WXMSW__
enum { DWMWA_USE_IMMERSIVE_DARK_MODE = 20 };

typedef HRESULT (WINAPI *DwmSetWindowAttribute_t)
(HWND, DWORD, LPCVOID, DWORD);

HMODULE hDwm = ::LoadLibraryA("dwmapi.dll");
if (!hDwm) return false;

DwmSetWindowAttribute_t pSet =
(DwmSetWindowAttribute_t)::GetProcAddress(
hDwm, "DwmSetWindowAttribute");

if (!pSet) { ::FreeLibrary(hDwm); return false; }

BOOL val = enable ? TRUE : FALSE;
HWND hwnd = (HWND)win->GetHandle();

HRESULT hr = pSet(hwnd,
DWMWA_USE_IMMERSIVE_DARK_MODE,
&val,
sizeof(val));

if (FAILED(hr))
{
const DWORD alt = 19;
hr = pSet(hwnd, alt, &val, sizeof(val));
}

::FreeLibrary(hDwm);
return SUCCEEDED(hr);
#else
wxUnusedVar(win);
wxUnusedVar(enable);
return false;
#endif
}




class DropTarget : public wxFileDropTarget
{
    BCTVFrame* m_owner;
public:
    explicit DropTarget(BCTVFrame* o) : m_owner(o) {}

    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& f) override
    {
        for (auto& path : f)
            if (m_owner->LoadImage(path, /*recordDir=*/true))
                return true;          // opened first one that succeeds
        return false;
    }
};


void BCTVApp::OnInitCmdLine(wxCmdLineParser& parser)
{
    static const wxCmdLineEntryDesc desc[] =
    {
        { wxCMD_LINE_PARAM, nullptr, nullptr,
          "image files to open",
          wxCMD_LINE_VAL_STRING,
          wxCMD_LINE_PARAM_MULTIPLE | wxCMD_LINE_PARAM_OPTIONAL },
        { wxCMD_LINE_NONE }
    };
    parser.SetDesc(desc);
}

bool BCTVApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
    for (size_t i = 0; i < parser.GetParamCount(); ++i)
        m_startupFiles.push_back(parser.GetParam(i));
    return true;                                   // keep launching
}

/* ---------------------------------------------------------------------------
 *  1-C  Usual startup, then open the first valid file (if any)
 * ------------------------------------------------------------------------ */
bool BCTVApp::OnInit()
{
    /*  this runs the built-in command-line processing which
        in turn invokes OnInitCmdLine and OnCmdLineParsed   */
    if ( !wxApp::OnInit() )
        return false;            // parsing said “abort”, etc.

    wxInitAllImageHandlers();

    auto* frame = new BCTVFrame;
    SetTopWindow(frame);
    frame->Show();

    /* now m_startupFiles contains whatever the shell gave us -------- */
    for (const auto& f : m_startupFiles)
        if (frame->LoadImage(f, /*recordDir=*/true))
            break;               // stop at the first image that loads

    return true;
}


//bool BCTVApp::OnInit() {
//wxInitAllImageHandlers();
//BCTVFrame* frame = new BCTVFrame();
//frame->Show();
//return true;
//}

BEGIN_EVENT_TABLE(BCTVFrame, wxFrame)
EVT_MENU(ID_FILE_OPEN, BCTVFrame::OnOpen)
EVT_MENU(ID_FILE_EXIT, BCTVFrame::OnExit)
EVT_MENU_RANGE(ID_CH_R, ID_CH_A, BCTVFrame::OnToggleChannel)
EVT_MENU(ID_BG_COLOUR, BCTVFrame::OnBgColour)
EVT_MENU(ID_FILT_SHR, BCTVFrame::OnFilter)
EVT_MENU(ID_FILT_ENL, BCTVFrame::OnFilter)
EVT_MENU_RANGE(ID_WIN_CLIP, ID_WIN_TOP, BCTVFrame::OnWindowOpt)
EVT_MENU_RANGE(ID_WHEEL_CYCLE, ID_WHEEL_50, BCTVFrame::OnWheelMode)
EVT_MENU(ID_WRAP, BCTVFrame::OnWrapAuto)
EVT_MENU(ID_AUTOZOOM, BCTVFrame::OnWrapAuto)
EVT_MENU_RANGE(ID_PP_NONE, ID_PP_ARG, BCTVFrame::OnPostProcess)
EVT_MENU(ID_HELP_ABOUT, BCTVFrame::OnAbout)
EVT_CHAR_HOOK( BCTVFrame::OnKey)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(BCTVCanvas, wxPanel)
EVT_PAINT (BCTVCanvas::OnPaint)
EVT_ERASE_BACKGROUND(BCTVCanvas::OnErase)
EVT_MOTION (BCTVCanvas::OnMotion)
EVT_MOUSEWHEEL (BCTVCanvas::OnWheel)
EVT_LEFT_DOWN (BCTVCanvas::OnLeftDown)
END_EVENT_TABLE()

BCTVFrame::BCTVFrame()
: wxFrame(nullptr, wxID_ANY, "BCTV", wxDefaultPosition, wxSize(636,478),
          wxDEFAULT_FRAME_STYLE &~(wxRESIZE_BORDER|wxMAXIMIZE_BOX)),
  m_canvas(new BCTVCanvas(this)),
  m_img(NULL), m_zoom(1.0),
  m_showR(true), m_showG(true), m_showB(true), m_showA(false),
  m_filtShr(true), m_filtEnl(false),
  m_clip(true), m_center(true), m_top(false),
  m_wheelMode(0), m_wrap(true), m_auto(true), m_pp(0),
  m_bg(*wxLIGHT_GREY), m_bgSecondary(wxColour(255, 0, 255)), m_curIdx(-1), m_wheelAccum(0), m_manualZoom(false)
{
    // Initialize the status bar with 5 fields
    m_statusBar = CreateStatusBar(5);
    m_statusBar->SetStatusWidths(5, new int[5]{100, 100, 100, 200, 200});

    wxMenuBar* mb = new wxMenuBar;

    wxMenu* mf = new wxMenu;
    mf->Append(ID_FILE_OPEN, "Open...\tO");
    mf->AppendSeparator();
    mf->Append(ID_FILE_EXIT, "Exit\tESC");
    mb->Append(mf, "File");

    wxMenu* mo = new wxMenu;
    mo->AppendCheckItem(ID_CH_R, "Show Red\tR");
    mo->AppendCheckItem(ID_CH_G, "Show Green\tG");
    mo->AppendCheckItem(ID_CH_B, "Show Blue\tB");
    mo->AppendCheckItem(ID_CH_A, "Show Alpha\tA");
    mo->AppendSeparator();
    mo->Append(ID_BG_COLOUR, "Background Color...\tC");

    wxMenu* mfilt = new wxMenu;
    mfilt->AppendCheckItem(ID_FILT_SHR, "When Shrinking");
    mfilt->AppendCheckItem(ID_FILT_ENL, "When Enlarging");
    mo->AppendSubMenu(mfilt, "Filter Image");

    wxMenu* mwnd = new wxMenu;
    mwnd->AppendCheckItem(ID_WIN_CLIP, "Clip to nearest monitor\tL");
    mwnd->AppendCheckItem(ID_WIN_CENTER, "Always in center");
    mwnd->AppendCheckItem(ID_WIN_TOP, "Always on top");
    mo->AppendSubMenu(mwnd, "Window");

    wxMenu* mwheel = new wxMenu;
    mwheel->AppendRadioItem(ID_WHEEL_CYCLE, "Cycle files");
    mwheel->AppendRadioItem(ID_WHEEL_5, "Zoom 5%");
    mwheel->AppendRadioItem(ID_WHEEL_10,"Zoom 10%");
    mwheel->AppendRadioItem(ID_WHEEL_25,"Zoom 25%");
    mwheel->AppendRadioItem(ID_WHEEL_50,"Zoom 50%");
    mo->AppendSubMenu(mwheel, "Mouse wheel behaviour");

    mo->AppendSeparator();
    mo->AppendCheckItem(ID_WRAP, "Wrap around while changing files");
    mo->AppendCheckItem(ID_AUTOZOOM, "Auto Zoom");

    wxMenu* mpp = new wxMenu;
    mpp->AppendRadioItem(ID_PP_NONE, "0: None");
    mpp->AppendRadioItem(ID_PP_RG, "1: Normal map RG");
    mpp->AppendRadioItem(ID_PP_AG, "2: Normal map AG");
    mpp->AppendRadioItem(ID_PP_ARG, "3: Normal map ARG");
    mo->AppendSubMenu(mpp, "Post process");

    mb->Append(mo, "Options");

    wxMenu* mh = new wxMenu;
    mh->Append(ID_HELP_ABOUT, "About");
    mb->Append(mh, "Help");

    SetMenuBar(mb);

    mb->Check(ID_CH_R, true);
    mb->Check(ID_CH_G, true);
    mb->Check(ID_CH_B, true);
    mb->Check(ID_CH_A, false);
    mb->Check(ID_FILT_SHR, true);
    mb->Check(ID_FILT_ENL, false);
    mb->Check(ID_WIN_CLIP, true);
    mb->Check(ID_WIN_CENTER, true);
    mb->Check(ID_WIN_TOP, false);
    mb->Check(ID_WHEEL_CYCLE, true);
    mb->Check(ID_WRAP, true);
    mb->Check(ID_AUTOZOOM, true);
    mb->Check(ID_PP_NONE, true);

    //SetPrimaryBackgroundColor(*wxLIGHT_GREY);
   // SetSecondaryBackgroundColor(wxColour(255, 182, 193));

    SetBackgroundColour(m_bg);                 // frame stays grey
    m_canvas->SetBackgroundColour(m_bgSecondary); // canvas shows pink
    SetIcon(wxICON(APP_ICON));
}

BCTVFrame::~BCTVFrame() {
delete m_img;
}

void BCTVFrame::UpdateStatusBar() {

if (!m_img) {
m_statusBar->SetStatusText("No image loaded", 0);
return;
}





wxString index = wxString::Format("%d / %d", m_curIdx + 1, m_fileList.GetCount());
wxString format = wxString::Format("Format: %s", m_img->GetFormat());


//wxString mips = wxString::Format("Mips: %d/%d", 1, m_img->GetMipCount());
//wxString memory = wxString::Format("Mem: %dKB/%dKB", m_img->GetMemoryUsage(), m_img->GetMemoryUsage());

//wxString format = wxT("Format: DXT1");
wxString size = wxString::Format("Size: %dx%d", m_img->Width(), m_img->Height());
wxString mips = wxString::Format("Mips: %d/%d", 1, 9);
wxString memory = wxString::Format("Mem: %.1fKB/%.1fKB", 32.0, 42.7);

int fieldWidths[5] = {100, 100, 100, 150, 250};

m_statusBar->SetStatusWidths(5, fieldWidths);
m_statusBar->SetStatusText(index, 0);
m_statusBar->SetStatusText(format, 1);
m_statusBar->SetStatusText(size, 2);
m_statusBar->SetStatusText(mips, 3);
m_statusBar->SetStatusText(memory, 4);
}

// Function to check 4CC of the file header
bool Check4CC(const wxString& path, uint32_t& file4CC) {
    wxFileInputStream in(path);
    if (!in.IsOk()) {
        wxLogError("Failed to open file %s", path.c_str());
        return false;
    }

    // Read the first 4 bytes of the file to check the 4CC signature
    uint8_t buffer[4];
    if (in.Read(buffer, sizeof(buffer)).LastRead() != sizeof(buffer)) {
        wxLogError("Failed to read 4CC from file %s", path.c_str());
        return false;
    }

    // Convert the 4 bytes into the 4CC signature (Big-endian to match the file format)
    file4CC = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];

    return true;
}


bool BCTVFrame::LoadImage(const wxString& path, bool recordDir)
{
    uint32_t file4CC;
    if (!Check4CC(path, file4CC)) {
        wxLogError("Failed to read 4CC for file %s", path.c_str());
        return false;
    }

    wxLogDebug("Read 4CC: 0x%08X", file4CC);

    // Polymorphic ImageBase pointer
    std::auto_ptr<ImageBase> tmp;

    // Check for .bct or .dds based on 4CC
    if ((file4CC == 0x07010220) || ((file4CC & 0x00FFFF00) == 0x00010100)) {  // BCT file signature (adjust as necessary)
        tmp.reset(new BCTImage);  // Load BCT image
    }
    else if (file4CC == 0x44445320) {  // DDS file signature ('DDS ' 0x44445320)
        tmp.reset(new DDSImage);  // Load DDS image
    }
    else {
        wxLogError("Unsupported file format for %s (4CC: 0x%08X)", path.c_str(), file4CC);
        return false;
    }

    // Load the image data
    if (!tmp->LoadFromFile(path)) {
        wxLogError("Failed to load %s", path.c_str());
        return false;
    }

    // Free previous image
    delete m_img;

    // Transfer ownership of the loaded image
    m_img = tmp.release();  // Transfers ownership of the ImageBase object

    // Record the file in the list if required
    if (recordDir) {
        wxFileName fn(path);
        m_fileList.Clear();

        // Single directory traversal to find both DDS and BCT files
        wxDir dir(fn.GetPath());
        if (dir.IsOpened()) {
            wxString filename;
            bool cont = dir.GetFirst(&filename, "*.dds", wxDIR_FILES);
            while (cont) {
                uint32_t file4CC;
                if (Check4CC(fn.GetPath() + "\\" + filename, file4CC)) {
                    if (file4CC == 0x44445320) {  // DDS file signature ('DDS ' 0x44445320)
                        m_fileList.Add(fn.GetPath() + "\\" + filename);
                    }
                }
                cont = dir.GetNext(&filename);
            }

            cont = dir.GetFirst(&filename, "*.bct", wxDIR_FILES);
            while (cont) {
                uint32_t file4CC;
                if (Check4CC(fn.GetPath() + "\\" + filename, file4CC)) {
                    if (file4CC == 0x07010220 || ((file4CC & 0x00FFFF00) == 0x00010100)) {  // BCT file signature
                        m_fileList.Add(fn.GetPath() + "\\" + filename);
                    }
                }
                cont = dir.GetNext(&filename);
            }
        }
    }

    // Get the current file index and add it to the list if needed
    int idx = m_fileList.Index(path);
    if (idx == wxNOT_FOUND) {
        m_fileList.Add(path);
        idx = m_fileList.GetCount() - 1;
    }
    m_curIdx = idx;

    m_zoom = 1.0;
    if (m_auto) {
        UpdateWindowForImage();
    }

    RebuildBitmap();

    UpdateStatusBar();

    return true;
}



void BCTVFrame::RebuildBitmap() {
    if (!m_img) return;

    const int orig_w = m_img->Width(), orig_h = m_img->Height();

    // Calculate scaled dimensions
    const int w = static_cast<int>(orig_w * m_zoom);
    const int h = static_cast<int>(orig_h * m_zoom);

    // Prevent creation of invalid (zero or negative) bitmap sizes
    if (w <= 0 || h <= 0) return;

    // Create a new bitmap with scaled dimensions, 32 bits per pixel
    wxBitmap bmp(w, h, 32);

    // Initialize alpha channel based on wxWidgets version
#if wxCHECK_VERSION(3,1,0)
    bmp.UseAlpha();
#else
    bmp.InitAlpha();
#endif

    // Access the bitmap's pixel data
    wxAlphaPixelData dst(bmp);
    if (!dst) return;

    // Get the source image data (assumed to be in BGRA format)
    const unsigned char* src = m_img->Data();

    // Compute inverse zoom factor once for efficiency
    double inv_zoom = 1.0 / m_zoom;

    // Iterate over each pixel in the scaled bitmap
    for (int y = 0; y < h; ++y) {
        // Calculate the source y-coordinate using nearest-neighbor interpolation
        int src_y = static_cast<int>(y * inv_zoom);

        // Initialize pixel iterator for the current row
        wxAlphaPixelData::Iterator p(dst);
        p.MoveTo(dst, 0, y);

        for (int x = 0; x < w; ++x, ++p) {
            // Calculate source x-coordinate
            int src_x = static_cast<int>(x * inv_zoom);

            // Compute the index in the source data (4 bytes per pixel: B, G, R, A)
            int src_index = (src_y * orig_w + src_x) * 4;

            // Extract BGRA components from the source image
            unsigned char b = src[src_index + 0];
            unsigned char g = src[src_index + 1];
            unsigned char r = src[src_index + 2];
            unsigned char a = src[src_index + 3];

            // Apply channel toggles
            if (!m_showB) b = 0;
            if (!m_showG) g = 0;
            if (!m_showR) r = 0;

            // Handle alpha premultiplication
            if (m_showA) {
                // Pre-multiply RGB by alpha for transparency effect
                r = static_cast<unsigned char>((r * a) / 255);
                g = static_cast<unsigned char>((g * a) / 255);
                b = static_cast<unsigned char>((b * a) / 255);
            } else {
                // Ignore alpha, set to fully opaque
                a = 255;
            }

            // Set the pixel values in the destination bitmap
            p.Blue()  = b;
            p.Green() = g;
            p.Red()   = r;
            p.Alpha() = a;
        }
    }

    // Assign the scaled bitmap and update the canvas and title
    m_bmp = bmp;
    m_canvas->RecreateBitmap(m_bmp);
    UpdateFrameTitle();
}

void BCTVCanvas::RecreateBitmap(const wxBitmap& bmp) {
    m_bmp = bmp;
    Refresh();  // Forces the canvas to be redrawn
}


void BCTVFrame::ChangeZoom(double factor)
{
m_zoom *= factor;
UpdateWindowForImage();
}

void BCTVFrame::UpdateFrameTitle() {
wxString title = "BCTV";
if (m_img) {
wxFileName fn(m_fileList[m_curIdx]);
title << " - [" << fn.GetFullName() << "] Zoom:"
<< int(m_zoom*100+0.5) << "%";
}
SetTitle(title);
}

void BCTVFrame::UpdateWindowForImage() {
    if (!m_img) return;

    wxDisplay disp(this);
    wxRect screenRect = disp.GetClientArea();
    int maxWidth = screenRect.GetWidth();
    int maxHeight = screenRect.GetHeight();

    int windowWidth, windowHeight;
    GetSize(&windowWidth, &windowHeight);

    int imgWidth = m_img->Width();
    int imgHeight = m_img->Height();

    // Calculate the available space in the window
    int availableWidth = maxWidth - windowWidth + GetClientSize().GetWidth();
    int availableHeight = maxHeight - windowHeight + GetClientSize().GetHeight();

    // Apply auto-scaling only if m_auto is true and avoid changing manual zoom
    if (m_auto && !m_manualZoom) {
        // Auto-fit to window size if image is larger than the available space
        if (imgWidth > availableWidth || imgHeight > availableHeight) {
            double scaleX = double(availableWidth) / imgWidth;
            double scaleY = double(availableHeight) / imgHeight;
            m_zoom = std::min(scaleX, scaleY);  // Fit to the smallest dimension
        } else {
            m_zoom = 1.0;  // No scaling needed if image fits within the window
        }
    }

    // Calculate the new width and height after applying the zoom
    int newWidth = int(imgWidth * m_zoom);
    int newHeight = int(imgHeight * m_zoom);

    // Ensure the window doesn't shrink below the minimum size
    int minWidth = 636;
    int minHeight = 478;

    if (newWidth < minWidth) newWidth = minWidth;
    if (newHeight < minHeight) newHeight = minHeight;

    // Adjust window size only if auto-scaling is active or the window needs resizing
    if (newWidth != windowWidth || newHeight != windowHeight) {
        SetClientSize(newWidth, newHeight);  // Resize the window
    }

    // Optionally center the window
    if (m_center) {
        Centre();  // Center the window
    }

    // Ensure the window stays within screen bounds
    wxRect windowRect = GetRect();
    if (!screenRect.Contains(windowRect)) {
        Centre();  // Recenter if out of bounds
    }

    // Set background colors and refresh the canvas
    SetBackgroundColour(m_bg);  // Frame stays grey
    m_canvas->SetBackgroundColour(m_bgSecondary); // Canvas shows pink
    UpdateFrameTitle();
    m_canvas->Refresh();  // Force the canvas to be redrawn
}


void BCTVFrame::StepImage(int delta)
{
if (m_fileList.IsEmpty()) return;

const int n = m_fileList.GetCount();
int idx = m_curIdx + delta;

if (m_wrap)
idx = (idx % n + n) % n;
else
idx = std::min(std::max(idx,0), n-1);

if (idx != m_curIdx)
LoadImage(m_fileList[idx], false);
}

void BCTVFrame::JumpImage(int idx) {
if (0 <= idx && idx < m_fileList.GetCount())
LoadImage(m_fileList[idx], false);
}

void BCTVFrame::ShowCursorInfo(int ix,int iy,unsigned char* p) {
wxString base = GetTitle();
int pos = base.Find(" Pos:");
if (pos != wxNOT_FOUND) base.Remove(pos);
base << wxString::Format(" Pos:%dx%d [A:%d R:%d G:%d B:%d]",
ix,iy,p[3],p[2],p[1],p[0]);
SetTitle(base);
}

void BCTVFrame::OnOpen(wxCommandEvent&) {
    // Update the file dialog to support DDS and BCT files together
    wxFileDialog dlg(this, "Open DDS or BCT", "", "",
                     "All supported files (*.dds;*.bct)|*.dds;*.bct|DDS files (*.dds)|*.dds|BCT files (*.bct)|*.bct",  // Filter for both DDS and BCT
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dlg.ShowModal() == wxID_OK) {
        // Get the file path and load the image
        LoadImage(dlg.GetPath());
    }
}



void BCTVFrame::OnExit(wxCommandEvent&) { Close(); }

void BCTVFrame::OnToggleChannel(wxCommandEvent& e) {
switch (e.GetId()) {
case ID_CH_R: m_showR = !m_showR; break;
case ID_CH_G: m_showG = !m_showG; break;
case ID_CH_B: m_showB = !m_showB; break;
case ID_CH_A: m_showA = !m_showA; break;
}
RebuildBitmap();
}

void BCTVFrame::OnBgColour(wxCommandEvent&)
{
    wxColourData cd;
    cd.SetColour(m_bgSecondary);              // start with current canvas colour
    wxColourDialog dlg(this, &cd);

    if (dlg.ShowModal() == wxID_OK)
    {
        m_bgSecondary = dlg.GetColourData().GetColour();

        // apply only to the canvas
        //m_canvas->SetBackgroundColour(m_bgSecondary);
        m_canvas->Refresh();                  // force repaint so the new pink shows
    }
}


void BCTVFrame::SetPrimaryBackgroundColor(const wxColour& color) {
m_bg = color;
}

void BCTVFrame::SetSecondaryBackgroundColor(const wxColour& color) {
m_bgSecondary = color;
}

void BCTVFrame::OnFilter(wxCommandEvent& e) {
if (e.GetId()==ID_FILT_SHR) m_filtShr = !m_filtShr;
else m_filtEnl = !m_filtEnl;
}

void BCTVFrame::OnWindowOpt(wxCommandEvent& e) {
if (e.GetId()==ID_WIN_CLIP) m_clip = !m_clip;
if (e.GetId()==ID_WIN_CENTER) m_center = !m_center;
if (e.GetId()==ID_WIN_TOP) {
m_top = !m_top;
SetWindowStyleFlag(
(GetWindowStyleFlag() & ~wxSTAY_ON_TOP) |
(m_top ? wxSTAY_ON_TOP : 0)
);
}
}

void BCTVFrame::OnWheelMode(wxCommandEvent& e) {
m_wheelMode = e.GetId() - ID_WHEEL_CYCLE;
}

void BCTVFrame::OnWrapAuto(wxCommandEvent& e) {
if (e.GetId()==ID_WRAP) m_wrap = !m_wrap;
if (e.GetId()==ID_AUTOZOOM) m_auto = !m_auto;
}

void BCTVFrame::OnPostProcess(wxCommandEvent& e) {
m_pp = e.GetId() - ID_PP_NONE;
RebuildBitmap();
}

void BCTVFrame::OnAbout(wxCommandEvent&) {
wxMessageBox(
"BCTV Version v0.1\n"
"Corey Nguyen\n"
"github.com/coreynguyen",
"Blue Castle Texture Viewer",
wxOK|wxICON_INFORMATION
);
}

void BCTVFrame::OnKey(wxKeyEvent& k) {
    int code = k.GetKeyCode();
    switch (code) {
    case WXK_ESCAPE:
        Close();
        return;

    case 'O': case 'o': {
        wxCommandEvent ev(wxEVT_MENU, ID_FILE_OPEN);
        OnOpen(ev);
        return;
    }
    case 'C': case 'c': {
        wxCommandEvent ev(wxEVT_MENU, ID_BG_COLOUR);
        OnBgColour(ev);
        return;
    }
    case 'R': case 'r': {
        bool shift = k.ShiftDown();
        if (shift) m_showG = m_showB = m_showA = false;
        m_showR = !m_showR;
        RebuildBitmap();
        return;
    }
    case 'G': case 'g': {
        bool shift = k.ShiftDown();
        if (shift) m_showR = m_showB = m_showA = false;
        m_showG = !m_showG;
        RebuildBitmap();
        return;
    }
    case 'B': case 'b': {
        bool shift = k.ShiftDown();
        if (shift) m_showR = m_showG = m_showA = false;
        m_showB = !m_showB;
        RebuildBitmap();
        return;
    }
    case 'A': case 'a': {
        bool shift = k.ShiftDown();
        if (shift) m_showR = m_showG = m_showB = false;
        m_showA = !m_showA;
        RebuildBitmap();
        return;
    }

    case 'L': case 'l':
        Centre();
        return;

    case WXK_PAGEUP:
        StepImage(-1);
        return;

    case WXK_PAGEDOWN:
        StepImage(1);
        return;

    case WXK_ADD: case WXK_NUMPAD_ADD: case '+': case '=':
            m_manualZoom = true;  // Flag that the user is manually zooming
        m_zoom *= 1.25; // Zoom in
        RebuildBitmap();  // Apply zoom
        UpdateWindowForImage();  // Adjust window size and update the image
        return;

    case WXK_SUBTRACT: case WXK_NUMPAD_SUBTRACT: case '-':
            m_manualZoom = true;  // Flag that the user is manually zooming
        m_zoom /= 1.25; // Zoom out
        RebuildBitmap();  // Apply zoom
        UpdateWindowForImage();  // Adjust window size and update the image
        return;

    case 'N': case 'n':
        m_filtShr = !m_filtShr;
        m_filtEnl = !m_filtEnl;
        return;

    case WXK_HOME:
        JumpImage(0);
        return;

    case WXK_END:
        JumpImage(m_fileList.GetCount() - 1);
        return;

    default:
        k.Skip();
        return;
    }
}



BCTVCanvas::BCTVCanvas(BCTVFrame* host)
: wxPanel(host, wxID_ANY, wxDefaultPosition, wxDefaultSize,
wxBORDER_NONE | wxWANTS_CHARS),
m_host(host)
{
SetBackgroundStyle(wxBG_STYLE_PAINT);
//SetBackgroundColour( wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW) );
//SetBackgroundColour(host->GetBackgroundColour());
SetDropTarget(new DropTarget(host));
}



void BCTVCanvas::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);

    // 1) clear the whole panel to the frame/window colour (grey)
    dc.SetBackground(m_host->GetBackgroundColour());
    dc.Clear();

    if (!m_bmp.IsOk())
        return;

    // 2) Apply the zoom factor here for drawing the image with the updated zoom
    double z = m_host->GetZoom();
    int bw = m_bmp.GetWidth(), bh = m_bmp.GetHeight();
    wxSize cs = GetClientSize();
    int dw = int(bw * z + .5), dh = int(bh * z + .5);
    int x0 = (cs.GetWidth()  - dw) / 2;
    int y0 = (cs.GetHeight() - dh) / 2;

    // 3) Finally, draw the bitmap with transparency (or normal)
    dc.SetUserScale(z, z);
    dc.DrawBitmap(m_bmp, int(x0 / z), int(y0 / z), true);
    dc.SetUserScale(1, 1);
}

void BCTVCanvas::OnMotion(wxMouseEvent& e)
{
if (!m_bmp.IsOk()) { e.Skip(); return; }

double z = m_host->GetZoom();
int bw = m_bmp.GetWidth();
int bh = m_bmp.GetHeight();
wxSize cs = GetClientSize();

int x0 = (cs.GetWidth() - int(bw*z)) / 2;
int y0 = (cs.GetHeight() - int(bh*z)) / 2;

int ix = int((e.GetX() - x0) / z);
int iy = int((e.GetY() - y0) / z);

if (ix < 0 || ix >= bw || iy < 0 || iy >= bh)
{
e.Skip();
return;
}

wxAlphaPixelData pd(m_bmp);
if (!pd) { e.Skip(); return; }

wxAlphaPixelData::Iterator it(pd);
it.MoveTo(pd, ix, iy);

unsigned char px[4] =
{
it.Blue(),
it.Green(),
it.Red(),
it.Alpha()
};

m_host->ShowCursorInfo(ix, iy, px);
e.Skip();
}

void BCTVCanvas::OnWheel(wxMouseEvent& e)
{
if (!m_bmp.IsOk()) { e.Skip(); return; }

const int wheelDelta = 120;
m_host->AddWheelAccum(e.GetWheelRotation());

while (m_host->GetWheelAccum() >= wheelDelta) {
m_host->AddWheelAccum(-wheelDelta);
(m_host->GetWheelMode()==0)
? m_host->StepImage(-1)
: m_host->ChangeZoom( m_host->GetWheelMode()==1 ? 1.05 :
m_host->GetWheelMode()==2 ? 1.10 :
m_host->GetWheelMode()==3 ? 1.25 : 1.5 );
}
while (m_host->GetWheelAccum() <= -wheelDelta) {
m_host->AddWheelAccum( wheelDelta);
(m_host->GetWheelMode()==0)
? m_host->StepImage( 1)
: m_host->ChangeZoom( m_host->GetWheelMode()==1 ? 1.0/1.05 :
m_host->GetWheelMode()==2 ? 1.0/1.10 :
m_host->GetWheelMode()==3 ? 1.0/1.25 : 1.0/1.5 );
}
}

void BCTVCanvas::OnLeftDown(wxMouseEvent&)
{
if (!m_bmp.IsOk())
return;

if (wxTheClipboard->Open())
{
wxBitmapDataObject* dobj = new wxBitmapDataObject;
dobj->SetBitmap(m_bmp);
wxTheClipboard->SetData(dobj);
wxTheClipboard->Close();
}
}

