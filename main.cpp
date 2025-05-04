#include <iostream>

using namespace std;

#include <wx/wx.h>
IMPLEMENT_APP_NO_MAIN(BCTVApp)   // declares BCTVApp but **no** main()

int main(int argc, char** argv)
{
    // custom pre-initialisation here …

    return wxEntry(argc, argv);  // hands control to wxWidgets
}
