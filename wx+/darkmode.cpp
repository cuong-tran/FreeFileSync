// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "darkmode.h"
#include <memory>
#include <zen/sys_version.h>
#include <zen/build_info.h>
#include <wx/settings.h>
#include <wx/window.h>
#include "color_tools.h"
#if defined ZEN_LINUX
    #include <gtk/gtk.h>
#elif defined ZEN_MAC
    #include "darkmode_mac.h"
#endif

using namespace zen;


#ifdef ZEN_WX_NO_COLOR_HOOK //set by the Makefile if wx/settings.h lacks the wxColorHook patch from Bugs.txt
//declare the hook ourselves so that unpatched wxWidgets builds still compile: wxSystemSettings::GetColour() will not consult it,
//so the enhanced contrast for wxSYS_COLOUR_GRAYTEXT is lost, but the color theme otherwise works as usual
struct wxColorHook
{
    virtual ~wxColorHook() {}
    virtual wxColor getColor(wxSystemColour index) const = 0;
};

inline std::unique_ptr<wxColorHook>& refGlobalColorHook()
{
    static std::unique_ptr<wxColorHook> globalColorHook;
    return globalColorHook;
}
#endif


bool zen::darkModeAvailable()
{
    return true;

}


namespace
{
class SysColorsHook : public wxColorHook
{
public:

    wxColor getColor(wxSystemColour index) const override
    {
        //fix contrast e.g. Ubuntu's Adwaita-Dark theme and macOS dark mode:
        if (index == wxSYS_COLOUR_GRAYTEXT)
            return colGreyTextEnhContrast_;
#if 0
        auto colToString = [](wxColor c) { return utfTo<std::string>(c.GetAsString(wxC2S_HTML_SYNTAX)); /* #RRGGBB(AA) */ };
        std::cerr << "wxSYS_COLOUR_GRAYTEXT " << colToString(wxSystemSettingsNative::GetColour(wxSYS_COLOUR_GRAYTEXT)) << "\n";
#endif
        return wxSystemSettingsNative::GetColour(index); //fallback
    }

private:
    const wxColor colGreyTextEnhContrast_ =
        enhanceContrast(wxSystemSettingsNative::GetColour(wxSYS_COLOUR_GRAYTEXT),
                        wxSystemSettingsNative::GetColour(wxSYS_COLOUR_WINDOW), 4.5 /*contrastRatioMin*/); //W3C recommends >= 4.5
};


std::optional<bool> globalDefaultThemeIsDark;
}


void zen::colorThemeInit(wxApp& app, ColorTheme colTheme) //throw FileError
{
    assert(!refGlobalColorHook());
    assert(wxTopLevelWindows.empty());

    globalDefaultThemeIsDark = wxSystemSettings::GetAppearance().AreAppsDark();
    ZEN_ON_SCOPE_EXIT(if (!refGlobalColorHook()) refGlobalColorHook() = std::make_unique<SysColorsHook>()); //*after* SetAppearance()

    //caveat: on macOS there are more themes than light/dark: https://developer.apple.com/documentation/appkit/nsappearance/name-swift.struct
    if (colTheme != ColorTheme::System && //"System" is already the default for macOS/Linux(GTK3)
        darkModeAvailable())
        changeColorTheme(colTheme); //throw (ErrorRestartRequired), FileError
}


void zen::colorThemeCleanup()
{
    assert(refGlobalColorHook());
    refGlobalColorHook().reset();
}


void zen::updateDerivedSystemColors(ColorTheme currentTheme)
{
    /*  "System" in equalAppearance() means the system's appearance: follow it while the app itself follows the system. With an
        explicit Light/Dark theme the app's appearance no longer tells what the system has, and wxOSX reports no switch then; on
        macOS that value is not needed then: changeColorTheme(System) hands the appearance back to the system  */
    if (currentTheme == ColorTheme::System)
        globalDefaultThemeIsDark = wxSystemSettings::GetAppearance().AreAppsDark();

    if (refGlobalColorHook()) //SysColorsHook keeps the enhanced gray text color of the appearance it was created for
        refGlobalColorHook() = std::make_unique<SysColorsHook>();
}


bool zen::equalAppearance(ColorTheme colTheme1, ColorTheme colTheme2)
{
    if (colTheme1 == ColorTheme::System) colTheme1 = *globalDefaultThemeIsDark ? ColorTheme::Dark : ColorTheme::Light;
    if (colTheme2 == ColorTheme::System) colTheme2 = *globalDefaultThemeIsDark ? ColorTheme::Dark : ColorTheme::Light;
    return colTheme1 == colTheme2;
}


void zen::changeColorTheme(ColorTheme colTheme) //throw ErrorRestartRequired, FileError
{
#if defined ZEN_MAC //"System": really follow the system again, also through later switches (wxEVT_SYS_COLOUR_CHANGED redraws the rest)
    if (colTheme == ColorTheme::System)
    {
        macos::followSystemAppearance();
        globalDefaultThemeIsDark = wxSystemSettings::GetAppearance().AreAppsDark();
        refGlobalColorHook() = std::make_unique<SysColorsHook>();
        return;
    }
#endif
    if (colTheme == ColorTheme::System) //SetAppearance(System) isn't working reliably! surprise!?
        colTheme = *globalDefaultThemeIsDark ? ColorTheme::Dark : ColorTheme::Light;

    try
    {
        if (const wxApp::AppearanceResult rv = wxTheApp->SetAppearance(colTheme);
            rv != wxApp::AppearanceResult::Ok)
            assert(rv != wxApp::AppearanceResult::CannotChange), //unused since wx 3.3.4
                   throw SysError(formatSystemError("wxApp::SetAppearance", L"", L"" /*errorMsg*/));
                
        refGlobalColorHook() = std::make_unique<SysColorsHook>();
    }
    catch (const SysError& e) { throw FileError(_("Failed to update the color theme."), e.toString()); }
}
