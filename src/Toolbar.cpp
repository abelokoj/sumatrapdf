/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "gui/Dpi.h"
#include "base/File.h"
#include "base/Pixmap.h"
#include "base/UITask.h"
#include "base/Win.h"

#include "gui/UIModels.h"

#include "Accelerators.h"
#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "DisplayMode.h"
#include "DisplayModel.h"
#include "ProgressUpdateUI.h"
#include "TextSelection.h"
#include "TextSearch.h"
#include "SumatraPDF.h"
#include "SumatraConfig.h"
#include "MainWindow.h"
#include "AnnotPlacement.h"
#include "Notifications.h"
#include "Canvas.h"
#include "WindowTab.h"
#include "resource.h"
#include "Commands.h"
#include "AppTools.h"
#include "CommandAvailability.h"
#include "Menu.h"
#include "SearchAndDDE.h"
#include "AnnotEditToolbar.h"
#include "AnnotFilterToolbar.h"
#include "Tabs.h"
#include "PagePosition.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/PlatformWindow.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "SidebarPanel.h"
#include "gui/VirtHost.h"
#include "gui/win/TabsCtrl.h"
#include "gui/win/ScrollableMenu.h"
#include "FindBar.h"
#include "SumatraDialogs.h"
#include "Translations.h"
#include "SvgIcons.h"
#include "EnhancedIcons.h"
#include "Theme.h"
#include "ReadAloud.h"
#include "RenderCache.h"
#include "Toolbar.h"

// https://docs.microsoft.com/en-us/windows/win32/controls/toolbar-control-reference

constexpr int kButtonSpacingX = 6;

// distance between label and edit field
constexpr int kTextPaddingRight = 6;

struct ToolbarButtonInfo {
    const char* icon = nullptr; // gIcon*, or null for a separator / page box / text
    int cmdId = 0;
    Str toolTip;
    Str svgIcon; // custom SVG from settings
    bool isText = false;
};

static void CollectPaletteControls(ILayout*, Vec<VirtCtrl*>&);
static int PinnedToolCommand(Str);
static int PinnedToolIndex(MainWindow*, int);
static bool ShowPinToolMenu(MainWindow*, int);
static const char* kHandToolIcon =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M8 12V6a2 2 0 0 1 4 0v5-7a2 2 0 0 1 4 0v7-5a2 2 0 0 1 4 0v8c0 5-3 8-7 8-2 0-4-1-5-3l-4-5a2 2 0 0 1 3-3l1 1Z" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/></svg>)";
static const char* kHandQuillIcon =
    R"quill(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1254 1254"><path fill="currentColor" fill-rule="evenodd" d="M464 1180h-43l-1 -2h-7h-1l-12 -2h-1h-4l-1 -2h-8l-4 -2l-7 -2l-39 -12h-3l-1 -2l-11 -2h-1h-4l-1 -2h-12l-1 -2h-8h-1h-26h-1h-10l-1 2h-12l-1 2h-4h-1h-3l-1 2l-14 2l-15 6l-18 8l-2 -2v-2l10 -23v-2l2 -5v-2l4 -5l4 -15l2 -3l14 -43l18 -18l18 -23l10 -13l-2 -2v-4l6 -8l-10 -4l-2 -3l-1 -1h-2l-3 -5v-8l2 -6v-6v-1v-4l2 -1l2 -19l4 -12v-2v-8l-2 -1v-12v-1v-21v-1v-6l2 -1l2 -10l2 -3l4 -13l2 -3v-2l4 -10v-2l2 -3v-2l2 -3v-2l8 -16l2 -25l2 -1v-3l2 -6l4 -11l8 -10l7 -8l10 -6l34 -18l2 -2l11 -4l8 -6h2l7 -4h2l7 -4l6 -2l2 -1l-2 -4v-3l18 31l1 17v-20h1v3l2 1v10h0v-13l-2 -6l-2 -2l-4 -7l-6 -8v-2l-2 -2l-6 -10l-2 -9v-4l-2 -1l1 -18l5 18l8 13l10 11l6 12l2 10v-9v-1v-3l-2 -1v-3l-2 -6l-6 -12l-12 -15l-6 -11v-3l-2 -5v-4v-1v-24v-1v-4l2 -3l2 16l2 3h0v-10l-2 -1v-21l2 -1v-7l3 -5l1 1v1v6l-2 1l2 21v1v5l2 1l2 11l6 17l8 13l2 2l2 5l2 2v4l2 3l2 7v1v4l2 1v5v1v6h2v-21l-2 -1v-5v-1v-3l-2 -1v-3l-2 -6l-14 -29v-2l-4 -8v-6l-2 -4l-2 -9v-1v-6l-2 -1v-18l2 -1v-6v-1v-5l2 -1v-7l5 -9l1 26h0l2 -21v-1l2 -12v-1l2 -7l2 -3l2 -8l6 -13v5v1v5l-2 1v8v1v10v1v6h2v-15v-1v-6l2 -1v-5v-1l2 -7l6 -18l2 -5v-2l2 -4l4 -3l6 -13l2 -3l4 -8l9 -14l-1 8l-2 1v4v1v5l-2 1v9v1v12v-6l2 -1v-16l2 -1v-4v-1v-3l2 -4v-3l4 -9l4 -10l8 -9l6 -11l6 -7l4 -7l4 -6l3 -2l-9 26v3l-6 15v4l-2 6v5v1l-2 19v1v17v1v9l2 6v5l-2 -7l-2 -11v-1v-7l-2 -1v-25l2 -1v-7v-1v-3l-2 1v6v1v5l-2 6v9l-2 1v19l2 1v18l2 1v4l2 5l-1 3l-3 -5l-2 -11v-1v-4l-2 -1v-4v-1l-2 -25l2 -3v-7h-2v12l-2 1v32l2 1v12l4 9l-1 5l-3 -7v-7l-2 -4l-2 -10v-1l-2 -31v9l-2 1v28l2 1v8v1l2 10l2 8v3l4 9l4 10l2 6v3l2 1v3v1l2 10v1v14l2 1l-2 1l1 9l1 -5v-8l2 -1l-1 -38l1 3v5l2 6v32l2 -7v-5l2 -1v-18l2 -1v-26l-2 -1v-7v-1v-5l2 7v6l2 1v6v1v8l2 1v14l-2 1v13l2 -7v-9l2 -1v-9v-1v-21v-1v-8l-2 -1v-7v-1v-6l2 7v1l2 9v1v4l2 1l1 31l1 -4v-1v-9l2 -1v-6l-2 -1v-18v-1l-2 -15v-1v-5l-2 -1v-5v-1v-5l-2 -1l1 -7l1 3l2 14l4 9v4v1l3 21v-30l1 4v5l2 1v24v1l-2 16v1v6l2 -4v-4v-1v-5l2 -1v-5v-1v-9l2 -1v-19l-2 -1v-16l-2 -1v-4v-1v-4l-2 -1l-2 -15l-4 -12l-2 -12v-1v-32l-2 3v9l-2 1v5v1l-2 33l-2 -4v-33l2 -1v-14l2 -5v-3l-4 10v4v1v4l-2 1v4l-2 6v7v1v29v1v5l2 1l-1 6l-1 -2l-2 -13l-2 -1v-8v-1v-29l2 -1v-10l2 -1v-8l2 -4l2 -7l8 -22l10 -12l5 -5l4 -2h1l-2 2l-8 16v3v1v3l-4 9v11l-2 1v4h2v-7l2 -1v-4v-1v-3l4 -12l10 -22l8 -10l11 -10h1l-2 2l-2 3l1 1l1 -2l12 -16l18 -22v2l-10 23l-2 3l1 2l9 -19l4 -6l12 -16l12 -11l2 -1l-8 13l-6 16v2l-2 3l-2 10l-6 14l1 3l5 -16l2 -3v-2l10 -20l4 -5l4 -6l14 -17l6 -8l6 -7l6 -8l7 -6l1 2l-4 7v2l-2 3l-6 18v2l2 -7l8 -17l14 -20l7 -6h1l-4 5l-4 8l1 3l7 -10l10 -10l8 -6l6 -2v1l-3 3l-7 8l-8 10l-8 18l-4 12v3l-2 1v4l-2 6v5v1v9l-2 1v3l2 1l-2 1v3l2 1v10v1v7l2 1v13l2 1v21l-2 1v16l-2 1v3v1v3l-2 6l-2 3v1l6 -11l2 -7l4 -7l2 -6v-3l2 -1v-3v-1v-3l2 -1v-10l2 -1v-11v-1v-24l2 -1v-8v-1v-6l2 -1v-4l2 -5v-4l2 -7v-3l14 -30l6 -9l12 -15l21 -18l18 -12l34 -18h1l-10 9l1 1l14 -12l8 -6l14 -6l1 1l-14 14l-4 6l1 1l16 -18l13 -10l17 -8l9 -2h2l-30 28l-2 6l-10 13l-9 19l-1 -2l2 -3v-3l2 -2l4 -7l4 -5l4 -9l6 -10v-1l-14 14l-12 19l-8 14l-4 14l-1 1l1 -6l8 -21l20 -27l2 -4l6 -6h-1h-1l-12 11l-12 16l-4 8)quill"
    R"quill(l-2 2l-8 14v2l-4 8v6l-2 4l-2 8v1l-2 8v1v5l-2 1v5v1v5l-2 1v9l-6 14l1 2l3 -6v-2l6 -11v-3l4 -12v-8l2 -1l2 -15l3 -4l1 2l-2 4v4l-2 5v5l-2 6l-2 11l-4 12l1 3l5 -10l6 -18v-3l2 -4v-6h2v4l-2 1v10l-2 1v7l-4 7l1 3l5 -9l4 -12v-3l2 -3v-4v-1l2 -10l2 -2v1v1v5l-2 6v5l-2 1l-2 11l-2 4l1 2l7 -14l7 -24l1 4l-2 6v5l-4 11v3l-4 6l1 1l5 -7v-2l4 -8v-3l1 -2l1 5l-2 1v3h2v-2l4 -7l2 -11l2 -3l2 -8l2 -3l2 -7l2 -4v8l-2 3v3l-4 10v3l-2 6l-4 7l-2 3l1 1l5 -6l6 -14v-2l2 -3v-2l2 -3l4 -10l4 -11l2 -1h0l-2 3l-4 15v3l-2 1v3l-2 9l-6 10l1 2l3 -3l6 -12v-5l4 -9v-4l4 -12l2 -1h0v1v5l-2 5l-2 11l-2 4v4l-2 4v2l2 -4l4 -9l4 -9v-2l4 -8l6 -15l1 -1h1l-4 10v3l-6 18l-4 6l1 3l5 -9l4 -12l4 -7v-2l6 -16h2v2l-8 24l-6 11v3v2l4 -6l9 -16l1 2l-2 3v3l4 -5l10 -22l8 -9l2 -6l15 -16l-1 3l-6 8l-8 14l-6 8l-6 10v2l-4 6l1 1l7 -10l4 -9l6 -7v2l-2 2l-6 15v2l-4 5h1l3 -2l22 -36l18 -20v2l-4 3l-16 24l-2 7l-8 10l1 2l3 -4l2 -4l5 -4h1v-2l4 -3v-3l10 -15l12 -15l7 -6l4 -2l11 -10l4 -1l-7 7l-8 6l-11 12l-8 12l-6 8l-2 5l-2 2l1 1l19 -24l22 -20h2l-22 25l1 3l9 -12h2l33 -32l2 -2l1 1l-12 13l-14 16l1 2l5 -4l11 -12h1l15 -14l4 -2h1l-14 13v1l-6 7v1l10 -10l12 -10l7 -4l7 -4l6 -4l4 -4h-1l-8 4l-3 2l-2 -1l3 -3l7 -3l-2 -1l-11 6l-7 4h-1l1 -1l-2 -1l-3 4h-3h-2l8 -6l3 -2l5 -3l-2 -1l-19 10l-3 2h-2l-3 2l-2 2l1 -4l6 -4l3 -2v-2l-12 6l-12 8l-6 6h-1h0l7 -8l9 -7l-1 -1l-16 10l-12 10h0l15 -14h-2l-14 8l-15 14h-1l2 -3l10 -8l-2 -1l-3 4l-4 2l-6 6h-1l-13 14l-1 -2l4 -4l10 -10l20 -16h-2h-2l-2 2l-7 4l-3 2l-3 2l-11 8l-10 10l-14 18l-7 14l-1 -3l8 -15l8 -14l12 -13l-2 -1l-18 18l-10 14v-3l12 -17l10 -9l-2 -1l-22 21l-14 21v-2l6 -11l18 -25h-1l-6 4l-18 20l1 -3l14 -17h-1l-17 14v1l-8 8l-12 17l-6 13l-2 7l-4 5l-5 19l-1 -4l2 -1l4 -19l2 -4v-6l4 -5l2 -6l16 -22l20 -19l9 -6l12 -6l8 -2l3 -2v1l2 1l2 -2l8 -4l12 -4h3l1 -2l2 1l-7 5l-3 2h2l5 -4l9 -4l2 -2l13 -4h3l9 -4h4l9 -2h5l5 -2l6 -2h1h7l6 -4h10l1 -2h8l5 -2l14 -2l1 -2h3h1h3l1 -2h3h1h3l9 -4l23 -10l9 -6l19 -18h1l-1 8l-2 3l-2 7l-12 21l-16 24l-4 4v4l-6 9l-6 11l-2 2l-6 14l-12 17l-17 18l-6 4l-4 2l-3 2h-3h-1h-10l-1 2h-4l-6 2l-7 6h-3l-1 -2l-9 2l-1 -2h-4l9 -2h0l-3 -1l3 -1h8l3 -2l-3 -2h-32l-1 2h-12l-1 2h-3l-8 2h36l2 1l-1 1h-25l-1 2h-4l-7 2h29h0h-17l-1 2h-7h-1l-5 1l20 -1l1 2h9h1h5l4 2l14 4l2 1v3l2 4l4 4l6 2l2 -2h5l8 -4l7 -6v2l-14 26l-16 24l-6 6v2l-4 6l-4 5l-8 7v3l-2 3l-16 19l-1 1l-13 12h-1l-4 6l-14 10l-13 4l-16 2l-1 -2h-5l-5 -2h-4l-5 -2h-7h-1h-13h-1h-4l-8 4l-4 4h-3h-7l-1 -2h-7l-1 2h-20l-3 -2v-2l-1 2h-4l-5 2h-7l-7 4h-3l-3 2l-10 2l-10 6h2l10 -4h7l4 -2l9 -2h1h19l2 1l-2 1h-16l-1 2h-5l-4 2h-3l-4 2h-6l-3 2l-8 2l-4 3l2 1l1 -2h7l5 -2h5l1 -2h5h1h13l1 -2l28 4l4 2h3l13 6l6 8l11 8l15 6h15l1 -2h4l6 -2l10 -4l11 -8v1l-2 6l-18 24l-4 6l-14 12l-2 6l-10 12l-10 10v1l-20 18l-19 14l-19 10h-2l-3 2l-9 4h-3l-5 2h-5h-1h-8l-1 2h-14l-1 -2h-8h-1h-5l-1 -2h-13l-1 -2h-9h-1h-22h-1h-9l-1 2h-5l-5 2h-4l-21 6v2h3l18 -6l10 -2h1h3l-2 2l-11 2l-4 2h-3l-12 4l-3 2l-11 2l-2 2h2l8 -2h3l5 -2h4l5 -2h9l1 -2h15h1h10l1 2h4l5 2l16 2h1l9 2h1h7l1 2h16l1 -2h13l4 -2h3l9 -4l10 -4h2l-14 12l-5 4l-4 4l-6 2l-1 2v1l-2 3h-1l-14 12l-20 14l-15 8h-2l-12 6l-12 4l-7 2l-4 2h-3l-5 2h-4h-1h-4l-1 2h-4h-1h-4l-1 2h-5h-1l-16 2l-1 )quill"
    R"quill(2h-8l-8 4h-3l-6 2l-9 3l4 1l1 -2h6h2h1l-2 2h-8l-15 6h-3l-14 6h0h2l8 -2h8l4 -2l8 -1l-2 1h-3l-7 4h-3l-12 4l-22 10h0h2l3 -2l12 -4h7l1 -2h6l6 -2l4 1l-13 5l-10 2l-9 4h-2l-3 2h-2l-13 6l-4 4h1l16 -8l12 -4l13 -2l9 -2h4l1 -2l15 -2l4 -2h3l8 -4l8 -2l6 -4h-2l-11 4l-3 -1l8 -3l3 -2h6l6 -4h3l6 -3l-2 -1l-4 2l-20 4v-2l15 -2l7 -3l-8 -1l-1 2h-9v-2l19 -2l5 -2h7l9 -4h2l3 -2h2l5 -4h3h1l-40 28l-18 8l-18 6h-3l-1 2h-3l-9 2l-9 2l-12 4h-3l-29 12l-2 2l-8 4l-3 3l2 1l18 -10l15 -4l16 -4l7 -2l10 -2l3 -2l11 -4l17 -8h2l21 -12v1l-5 5l-12 12l-8 6h-3l-4 6l-8 4h11l1 2h4l12 4l16 8l14 8l7 4l2 2l23 12l5 4l3 2l47 30l3 4l20 12l46 34l6 4l15 4l10 2h1h8l1 2h22l5 -8l8 -10l13 -8h8l1 -2h12l1 2h5l14 4h3l5 2h5l1 2h14l1 -2h5l4 -2h6l11 -10l8 -2h17l1 -2h10l9 -4h4h1l9 -2h1h6l1 -2h7h1l23 -2h1h8l6 -2l15 -2h1l21 -2h1l26 -2l1 -2h12l1 2l-6 3l3 1l1 1l-3 1l-1 2l-4 1l4 1v2h-4l-1 -2l-9 1l9 3l7 4l4 3l-2 3l-7 -6h-4l-5 -4l-6 -2h-3h-1h-2l1 2h6l8 4l7 7l2 2l-1 1l-11 -4h-4l8 4l10 11v1l-15 -12l-4 -2l-1 1l4 3l8 10l6 7l-1 1l-19 -16l-3 -2l-3 -2h-4l13 12l11 12l8 11l4 7l-1 2l-8 -8l-6 -6h-2h-1l12 19l2 4v1l-20 -20l-2 1l6 7l6 7l10 17h0l-4 -3l-12 -15v2l10 16v2l2 5v2l4 5l1 6l-1 -1v-2l-10 -13v-3l-14 -18l-6 -7l-2 2l12 16l4 8l6 7l2 6v2l4 7l6 15v8l2 1l-1 8l-9 -20l-8 -10v-3h-2l2 9l8 14l2 9v3l4 7v3l2 5v5v1v5l2 1l-1 5h-1v-3l-4 -8l-2 -7l-6 -9v-6l-2 -3l-2 -7l-6 -9v-2l-8 -12l-4 -6l-10 -14l-10 -10h-2l20 24v3l12 16l8 17v2l2 3v5l2 3l6 18v3l2 4v3l2 5v4l2 1v6v1v15l2 1l-2 2l-6 -23l-2 -3l-2 -8l-2 -3l-6 -18l-4 -7v-2l-4 -7l-2 -4l-2 -2l-7 -15l-1 2l12 22l2 7l6 13l2 8l2 3l2 10l4 11v4l2 4v4l2 5v4l2 6v5v1v8l2 1l-1 18l-15 -48v4l2 4v3v1v3l2 5l2 7v1l2 14l2 1v5v1v5l2 1v7v1v6l2 1v12l-2 -1l-2 -4v-6l-4 -5v-6l-2 -2v3v1l2 8l2 5v4v1v3l4 9v11l2 4v5h-1l-2 -4l-1 1l4 10l-2 1l-3 -2l-1 2l2 3v2l4 9h-1l-5 -4l-6 -6l-2 -5l-3 -3l-1 2l2 2l6 11l8 10v2l2 1v1l-2 1h2l2 4h-57h-1h-6h-1h-41h-1l-24 -2l-1 2h-9h-1h-7l-1 2h-7l-12 -4l-7 -6l-6 -8l-22 -2h-1h-5l-9 -4h-6h-1h-15h-1h-5l-1 2h-8l-9 6h-2l-9 4h-12l-8 -4l-8 -11l-6 -12l-4 -12v-3l-2 -4l-4 -18h-15l-1 -2h-29h-1h-7l-1 2h-8l-27 12h-3l-7 2h-3l-5 2h-4l-1 2h-4h-1l-17 2h-1h-35l-1 -2h-7h-1h-5l-1 -2h-5l-6 -2h-4h-1l-15 -4l-4 -2h-3l-9 -2l-3 -2l-5 -2l-18 -8h-2l-14 -6h-2l-13 -8h-3l-6 -2l-12 -6l-6 -2h-3l-13 -4h-6l-5 -2h-1l6 8l6 13v3l2 1v18l-4 12l-6 8l-6 7l-9 4h-3l-1 2h-13l-1 -2h-5l-5 -2h-4l-4 -2h-3l-27 -10l-9 10l-9 4h-3l-1 2h-12l-6 -4l-10 -8l-20 -20l-15 -10l-13 -10l-11 -13l-9 -17l-1 4l-6 6v5l-2 4l-4 3v3l-2 5l-4 2l-6 2l-2 3l-6 10v2l-2 3v2l-2 3v2l-4 9v3l-2 9v4l-2 1v4l-2 3l-10 4l-12 8l-23 20l-19 20l-18 22v1l2 1l21 -12h2l3 -2l18 -6h3l4 -2h4h1h4l1 -2h4h1l17 -2h1h27h1h8l1 2h6h1l10 2h1l11 2l7 4h3l44 14h3l9 4h4h1h4l1 2h12l1 2h10h1h28h1h10l1 -2h6h1h5l1 -2l14 -2l12 -4l3 1l-25 9h-3l-9 4h-4h-1h-4l-1 2h-14ZM1098 75l4 -4v-1l-6 5ZM1094 79l2 -3h0l-4 3ZM360 872l6 -2h3l5 -3v-2l6 -10l8 -14l1 -1h1l2 -1l18 -37l8 -10l10 -20l2 -2l14 -26l4 -7l14 -26l6 -7l2 -5l8 -12l2 -5l2 -2l6 -13l8 -11l10 -16l6 -8v-3l4 -5l2 -3l4 -6l2 -3l2 -3l2 -3l18 -27l2 -3l4 -6l8 -10l4 -7l2 -3l20 -28l32 -42l8 -9l2 -5l26 -30l28 -30v-1l38 -39v-1l2 -1l2 -4l50 -48h1l13 -14h1l39 -36l50 -40l2 -2l1 2l3 -2l)quill"
    R"quill(5 -6l13 -10l8 -6l3 -2h2l9 -8l12 -8l19 -14l46 -30l62 -36l21 -14l4 -2l4 -4h-1l-28 18l-5 2l-14 10l-9 4l-2 2l-7 2l-2 2l-9 4l-5 4l-19 10l-10 8l-13 6l-8 6l-3 2l-5 4h-3l-44 30l-7 6l-16 12l-4 4l-4 2l-51 40l-39 36h-1l-46 44h-1l-72 72v1l-10 11l-16 18l-8 8l-42 53l-52 71l-8 13l-4 3l-14 22l-2 3l-10 15l-2 3l-10 14l-4 8l-6 8l-18 31l-6 10l-4 7l-8 12l-2 5l-4 7l-2 5l-2 2l-2 5l-2 2l-2 5l-6 7l-6 14l-2 2l-10 18l-6 11l-4 7l-2 2l-4 9l-2 2l-2 7l-6 9l-4 8v2l6 -10l1 -2l1 1l-14 25l-4 7v1ZM1092 88l6 -5v-1l-8 6ZM1016 113l15 -11l4 -2l1 1l-3 3l-3 3v1l16 -12h2l6 -2h0l-4 4h1l8 -6h3l12 -8h-2l-4 2l-9 6l-1 -2l6 -4h-1l-4 2h-2l-4 4l-14 6h-1l4 -4l6 -3l-2 -1l-2 2l-2 2l-9 4l-11 8l-8 7ZM1088 91l2 -2v-1l-4 3ZM1026 96l2 -1l-3 -1l-1 2ZM1066 112l7 -2l7 -5l-1 -1l-15 8ZM1028 110l2 -2h-1l-3 2ZM1028 136l12 -2h1h3l9 -4l14 -6l5 -5l-1 -1l-10 6l-8 2l-9 4l-6 2l-14 4ZM1014 147l11 -1h1l11 -2l6 -2l5 -2h2l8 -5l-2 -1l-6 4h-3l-9 2h-4l-1 2h-4l-4 2h-4l-11 3ZM990 163l8 -1h1h15l1 -2h5l4 -2h5l18 -8l7 -4h-3l-5 2l-12 4l-13 2l-1 2h-5l-6 2h-4h-1h-3l-1 2h-3h-1h-3l-7 3ZM840 158l2 -1l-1 -1l-1 1v1ZM855 170l1 -3l2 -2v-2l3 -3h1l-2 2l-2 5ZM980 173l33 -3h1h3l11 -4l6 -2l8 -4h-2l-3 2h-6l-3 2h-3l-1 2h-19l-1 2l-17 2h-1l-8 2l-4 1ZM968 180h12l1 -2h32h1l7 -2l3 -2h6l5 -4h2l7 -4h-2l-5 2l-9 4h-4h-1l-13 2h-1h-11l-1 2h-18l-1 2h-5h-1h-4l-4 2ZM1000 189l8 -1h1h5l1 -2h7l14 -8h-1l-3 2l-9 2h-4l-1 2h-13h-2h-1l-6 -1l7 -1h0h-8h-1h-13l-1 2h-14l-1 2h-5h-1l-7 2h28h1ZM844 186v-2l3 -6l1 1ZM910 181l2 -1l-1 -2l-1 2v1ZM756 306l8 -25l6 -25v6v1l-2 11v1v4l-2 1v4v1l-2 7l-2 7v3l6 -16l2 -6v-3l2 -1v-3v-1v-3l3 -7l1 2l-2 1v12l-2 1v4v1l-2 7l-2 7v1l6 -9l4 -15l2 -7v-1v-4l2 -6v-6l2 -1v-7l2 -6v-5v-1l2 -9l6 -18l10 -22v-1l-8 12l-10 19l-7 19l-1 -2l2 -1v-7l4 -12l4 -8v-2l4 -5l-1 -1l-7 9l-10 22l-6 21l-2 10l-2 4l-2 9v1v6l-2 1v6v1v5l-2 1v4v1l-2 7l-2 4v2ZM738 324l6 -15v-3l4 -9v-4v-1v-4l2 -1v-5l1 -2l1 6l-2 1v8v1l-2 11l-2 5l1 2l1 -2l2 -6l2 -3l2 -9v-3l2 -4v-3l2 -1v-10l2 -6v-5l2 -6v-5l2 -1l4 -19l2 -4v-3l2 -3l2 -10l10 -18l6 -10v-1l-5 4l-1 -2v1l-18 24l-6 11v2l-4 4l-4 15l-2 3l-2 6v3l-2 1v9l-2 1v32l-2 1v13l-4 12v2ZM792 191l4 -5l-1 -2l-3 6v1ZM766 200v-1v-1l-2 1v1ZM764 203v-1v-2l-2 2v1ZM978 206h-11l-1 -2l5 -2h9v2ZM678 385l6 -11v-2l4 -8l5 -22l1 4l-2 1v5v1v5l-2 1v7l-2 4v2l2 -4v-2l2 -3l4 -9l4 -22v9v1v6l-2 1v9l-4 8h1l1 -1v-2l2 -3l6 -18l2 -13l2 -1v-5v-1v-4l2 -4v7v1v7l-2 1v13l-2 6v3h0l4 -12l2 -7v-1v-4l2 -1v-3l2 -5v-5v-1v-6l2 -3v6v1v8l-2 1v6v1l-2 16l-2 4v3l2 -5l6 -18l4 -21h2l-2 15v1l-2 9l-2 4v3h2l4 -15l2 -7v-1v-4l2 -1v-9l2 -1v-7l1 -3l1 12l-2 1v8v1v5l-2 1v8l-4 10v2l4 -8v-2l2 -3l4 -9v-3l2 -5v-4v-1l2 -13v-1v-7l2 -1v-7v-1v-10l2 -1v-6v-1l2 -10v-1l2 -7l4 -12l8 -16l2 -4v-1l-6 7l-6 14l-2 2v2l-4 7l-6 15v3l-1 2l-1 -2l2 -1v-4v-1l2 -7l2 -9l2 -3l2 -8l4 -7v-2l-6 11l-2 2l-4 10l-2 7l-4 5l-2 9v3l-2 4l-2 7v1l-2 10l-1 1l1 -13v-1v-5l2 -1v-4l2 -5v-3l4 -12l2 -3v-2l6 -11l-1 -2l-5 9l-10 18v2l-4 9v3l-4 14l-2 10v1v7l-2 5v-10v-1v-10l2 -1v-5v-1l2 -9l4 -15l6 -9l-1 -3l-11 24l-4 9v5v1l-2 13l-3 8l-1 -5l2 -1v-6v-1l2 -10v-1v-8l4 -7l-1 -3l-3 5l-2 6v3v1v3l-4 8v8l-2 1l-2 19l-2 -1v-3l2 -1v-10v-1v-6l2 -1l-1 -3l-3 7v3v1v4l-2 6v5l)quill"
    R"quill(-2 1v7v1v10l-2 1v16l-2 1v4v1l-2 13l-2 4v3l-2 6l-6 10v1ZM757 248l-1 -4l2 -4v-3l4 -12l8 -15v2l-6 15l-2 6ZM984 223l-7 -3l-11 -3l9 -1l1 2h4l3 2l1 -1l-4 -3l-9 -4h-13l-1 -2h-8l-1 2h-8h-1l-11 2h-1h-3l-1 2h-3h-1h-3l-8 3l5 1l1 -2h12l1 -2h17l1 2h9h1h5l6 2l11 2l4 2ZM978 236h8l1 -2h4l3 -1l-4 -1l-1 2h-7l-1 -2h-6h-1l-11 -2v-2l2 2l23 -1l-12 -3h-1l-9 -2h-1h-6l-1 -2h-30l-1 2h-6h-1h-5l-1 2h-4l-4 1l4 1l1 -2h9h1h22h1l6 1l-23 1h-1h-8l-1 2l-17 2h40h1l12 2l9 2ZM852 226v-1v-1l-2 1v1ZM958 250l16 -2h1h3l1 -2h3l6 -2l8 -3l-3 -1l-5 2h-5l-1 2h-18l-1 -2h-9h-1h-6l-1 -2h-4l1 -2h18h1l-2 -2h-4h-1l-25 -2l-1 2h-15h-1h-7l-1 2h-10l-1 2l-8 1l5 1l1 -2h7h1h27h1h3v2h-27l-1 2l-17 2h-1l-7 2h4h1h6l1 -2h11h1h12h1h21l4 2l-14 1l2 1ZM840 256l4 -3l-1 -1l-5 4ZM944 265l17 -3l21 -8h-2h-1h-4l-6 2h-17h-1l-17 -2h-1h-5l-1 -2h-30l-1 2h-6h-1h-4l-1 2h-4h-1l-13 3l6 1l6 -2l14 -2h1h22h1h7l3 2h-10h-1h-11h-1h-12l-1 2h-6h-1h-4l-1 2h-4h-1h-4l-3 2h4h1h6l1 -2h8h1h11h1h10l1 2h5h1h6l1 2ZM840 279l7 -1l1 -2h4h1l17 -2h1h21l1 2h6h1h5l1 2h30l1 -2h5h1h4l1 -2h7l7 -4h3l6 -2l12 -6l7 -5l-1 -1l-9 6l-18 6h-6h-1h-20l-6 -2l-1 2h-9l-1 -2h-30l-1 2h-7h-1h-5l-1 2h-9l-1 2l-11 2l-4 2h-3l-9 3ZM834 261l4 -3h-1l-5 3ZM826 269l4 -4l-1 -1l-5 5ZM816 277l6 -6v-1h-1l-7 7ZM796 317l11 -3h5l1 -2h5h1h5l1 -2h12h1h21l1 2h6h1h1h22h1h12l1 -2h12l14 -5l-4 -1l-1 2h-6h-1h-18l-2 -1l17 -1l5 -2h10l4 -2h3l1 -2h-2h-3h-1h-1h-1h-1h-16h-1l-15 -2l-3 -1l30 -1h1l12 -2l17 -5l-4 -1l-3 2l-10 2h-1h-8l-1 2l-5 -1l2 -1h6l5 -2h5l1 -2h5l4 -2h9l17 -9l-3 -1l-3 2l-9 4h-7l-1 2h-4h-1h-12l-1 2l-17 -2h-1h-8l-1 -2h-13h-1h-24l-1 2l-17 2l-5 2l-11 2l-9 2l-3 2l-5 2l-5 2h3l3 -2h3l9 -2h4l1 -2h15l1 -2h17l1 2h7h1h6l1 2h4h-34l-1 2h-12l-1 2h-4h-1l-11 2l-9 4l-3 2l-8 2l-2 1l2 1l7 -4h8l5 -2h8l1 -2h26l1 2h6h-17l-1 2h-9h-1l-12 2h-1l-15 4l-3 2l-8 2l-7 3ZM806 287l6 -7h0h-1l-7 7ZM636 429l4 -15l2 -10v-1v-7l2 -1v-5l2 -4v-9l1 -5l1 15l-2 1v17v1v4l-2 6v4l6 -20l2 -8v-1v-5l2 -1v-8v-1v-15h0v4l2 6v14v1v9l-2 6v3l2 -5v-3l2 -5v-4l2 -5l3 -28l1 28l-2 1v12l-4 8l1 3l5 -13v-3l4 -8v-9l2 -1v-22l-2 -1v-8l-2 -9l-2 -11v-1l-1 -4l-2 20l-1 -4v-7l-2 -1v-14l2 -1l-1 -7l-1 3h0v-9l2 -1l-1 -5l-1 4l-2 10v1v4l-2 1v15l-2 1v10v1v11h-2v-6v-1v-3v-1v-24l2 -1v-7v-1v-5l2 -1v-4l2 -6l-1 -4l-5 14v3l-2 5v10l-2 1v8v1v8l-2 1l-1 19l-1 -6v-1v-13v-1l2 -18v-1l2 -10v-1v-3l-2 6l-4 15v1v4l-2 6v6l-2 1v9v1v22l2 4v32v1v9l-2 1v4l-2 5v2ZM794 298l6 -7v-1h-1l-7 8ZM936 304l11 -4l15 -8h-2l-6 2h-3l-3 2l-7 2l-5 4l-6 2ZM588 486l2 -6l2 -8l2 -8v-1v-4l2 -1l2 -31h2v18l-2 1v19l-2 1v5v1v4l2 -7l2 -13l4 -9l2 -30h2v14l-2 1v15v1v7l-2 1l1 4l1 -3v-5l2 -1v-4l2 -5v-4v-1l2 -18v-1v-11h2v32l-2 1v6v1v6l-2 1l1 4l3 -8v-3v-1v-3l2 -1v-3v-1v-7l2 -1v-6v-1v-6l2 -1v-9l1 -5l1 14l-2 1v14v1v8h0v-3l2 -1v-7l2 -5v-7l2 -1v-5v-1l2 -28v4l2 1v22l-2 1v18l-2 1l-2 13l-2 3l1 1l3 -5l4 -9l2 -13v-1v-5l2 -1v-17l2 -1v-22l-2 -1v-7v-1l-2 -26l2 -1v-13v-1v-6l2 -1v-7v-1v-4l2 -5v-4l-6 16v3l-2 9v4l-2 1v9l-2 1v13l-2 2v-5v-1v-8l2 -1v-6v-1v-7l2 -1v-9l4 -9l-1 -2l-5 10v3l-2 4v3l-2 4v3l-4 12v11l-2 1v5v1v11l-2 4v-23v-1v-7l2 -1v-10l4 -9v-3h-2l-4 15v3l-2 4l-2 7v1v4l-2 5l-2 10l-2 6v9l-1 2l-1 -3l2 -1l-2 -1v-3l2 -2l-2 -1l2 -1v-8)quill"
    R"quill(v-1v-9l2 -1v-4l2 -5v-4v-1v-4h0l-2 6l-2 4v3l-2 4v3l-2 5v5v1v4l-2 1l-2 31v-29v-1v-7l2 -1v-5l2 -6l-1 -3l-3 6v3l-2 9v5l-2 1v5v1v6l-2 1v32l2 1v25v1l-2 25v1l-2 8v1v3ZM782 309l6 -6l-1 -1h-1l-6 7ZM924 318h5l3 -2l8 -2l10 -6l6 -4h-1l-6 2l-15 6h-3l-4 2h-4l-6 2h-5h-1l-13 1l3 1ZM760 330l14 -15v-1h-1l-15 16ZM778 333l10 -3l7 -2l5 -2h4h1h5l1 -2h33l1 2l2 -2h8l1 2h9h1h27h1l15 -2l3 -1l-20 -3h-1l-8 -2h-1h-4l-1 -2h-6h-1l-24 -2l-1 2h-10h-1h-7l-1 2l-14 2l-4 2h-3l-4 2h-3l-6 2l-16 7ZM849 322l-17 -1l5 -1h12ZM914 331l7 -1l13 -6h-2l-11 4h-3l-5 2h-5l-2 1ZM752 359l15 -5l8 -2l5 -2h10l1 -2h7v2l-4 2h3h1h10l1 -2h19l1 -2h5l2 -1l-22 -2l1 -1h6l6 -4h27l1 2l2 -2h18h1h8l4 -2h5l9 -4h6l-4 -2h-15h-1l-19 -2l-6 -2h-7l-1 -2h-25l-1 2h-17l-1 2h-5l-8 2l-10 2l-3 2l-15 6l-8 4h2l3 -2l12 -4h3l4 -2h4h1l20 -2h1h18l-8 2l-20 2l-1 2h-4h-1l-8 2l-4 2h-3l-12 4l-3 2h-2l-14 7ZM886 338h-23l-1 -2h-10h34ZM548 531l2 -7v-1v-6l2 -1v-17l-2 -1l2 -2v-24l-2 -1v-13v-1v-4v-1l2 2l3 30l1 -1v-8l2 -1l-2 -3h2v8v1v3v15v1v12l-2 1v5v1v4l2 -3v-4l2 -4v-4v-1v-4l2 -6v-5l2 -1v-33l2 -1l-2 -1l2 -3v44v1l-2 12v1v6l2 -12l2 -1v-3v-1l2 -9v-1v-7l2 -1v-7v-1v-18l2 -1v-10l-2 -1l1 -19l1 1v5l2 1v10v1v9l2 1v7l-2 1v15v1v9l-2 1l1 8l1 -4v-1v-5l2 -1v-12l2 -1v-7v-1l2 -20h0v33v1v7l-2 1v9l-2 1l1 4l3 -8v-3v-1v-3l2 -1v-3v-1l2 -12v-1v-8l2 -1v-36l-2 -1v-9v-1v-34v-1v-9l2 -1v-6v-1v-5l-2 4v4v1v4l-2 1l-2 25v1v26h-2v-3v-1v-40l-2 3v13l-2 1v29h-2v-17v-1v-23l2 -1v-10l2 -3v-4l2 -1v-7v-1v-4l-2 4v4v1l-2 11l-2 4v4l-2 5l-2 9v1v6l-2 1v15v1v5v1l-2 -2v-9v-1v-24l2 -1v-12l4 -12v-4l2 -4v-5l-2 6l-2 5l-6 17v-2l2 -4v-4l4 -15v-3l4 -6l-1 -2l-5 10v2l-4 8l-8 24v4l-2 1v4v1v4l-2 6v6l-2 1v13v1v6v1l-1 2l-1 -6v-1v-20v-1v-7l2 -1v-5l2 -5v-9l4 -11v-2l-4 8l-4 9v3l-2 7l-2 8v1v5l-2 1v13l-2 1v27v1v8l4 8v4v1v4l2 6v5l2 1v14l-2 1l1 14l1 -3v-7l2 -2v1v3v15l-2 6v4ZM900 345l12 -3l8 -3l-3 -1l-4 2l-11 2h-1h-5l-1 2h-11l1 2ZM558 348v-1v-1l-2 1v1ZM740 349l2 -3h0l-4 2l1 2ZM728 362v-1v-1l-2 2ZM706 384l18 -18l-1 -2l-19 19v1ZM616 523l7 -3l18 -6l8 -2l5 -2h4h1h4l1 -2h14l1 -2h20l1 2h8h1h8l1 2h9h1h16h1l17 -2h1h4l1 -2h3l4 -2h6l3 -2h-18l-1 2h-12l-1 -2h-6l22 -2h1h4l9 -4h4l15 -4l17 -9l-3 -1l-3 2l-6 2h-3l-1 2h-3h-1h-3l-1 2h-13l-1 2h-21l-1 -2h-6h-1l-14 -2l-3 -1l31 1l1 -2h9h1h5l1 -2h5h1l8 -2l18 -6l16 -8l6 -4h-1l-15 6l-6 2h-3l-1 2h-3h-1h-7l-1 2l-16 -1l14 -1l9 -4h3l12 -4l3 -2h5l6 -4h-2l-10 4h-11l-1 2h-28l-1 -2h-5l28 -2h1h5l1 -2h4h1l7 -2l18 -6l17 -9l-1 -1l-7 4l-12 4h-3l-5 2h-6l-1 2h-8h-1h-25l-1 -2h-8h-1l-5 -1l13 -1l1 2h13l1 -2h10h1h5l1 -2h4l5 -2h4l4 -2h3l13 -5l-3 -1l-8 4h-4l-6 2h-14v-2h10l1 -2h7l13 -5l-4 -1l-1 2h-11l-1 2h-16l-1 -2h-11l-5 -1l18 -1v-2h15h1h5l1 -2l11 -2l12 -4l13 -6h-2h-1h-3l-4 2h-4l-5 2h-5l-1 2h-13h-1h-2h-4l-3 -2v-2h-3h-1l-11 -2h0h32h1l13 -2l11 -3l-5 -1l-1 2h-10h-1h-17v-1l1 -1h1h3h1h11l1 -2h6l-1 -2h-15h-1l-11 -2h-1h-5l-4 -2h6h1l19 1l-8 -3l-7 -2h-1h-4l-5 -2l-10 -2h-1h-8v-2h19l1 2h10l-10 -4h-3l-8 -4h-5l-2 -1l3 -1l1 2h7l15 4h2l-6 -4l-6 -4l-10 -2h-1h-4l-1 -2l-14 -1l7 -1h1h2h6l-3 -2h-4h-1h-23h-1h-5l-5 2h-4l-4 2h-3l-9 4l-3 2l-8 2l-8 4h-2l-3 1l3 1l17 -6l7 -2h1h4l1 -2h6h1l17 -2l1 2h10h-10l-1 2h-10h-1h-5l-5 2h-4l-1 2h-)quill"
    R"quill(7l-18 6l-3 2h-2l-5 4h-4v2l18 -6h3l9 -4h4h1h7l1 -2h25l1 2h6l5 1l-8 1l-1 -2h-13l-1 2h-8h-1l-11 2h-1l-11 2l-4 2h-3l-9 4l-10 4h-2l-6 4h0h2l3 -2l12 -4h7l4 -2h4l6 -2h8l1 -2h22h1h11l1 2h7h1l5 1l-28 -1l-1 2h-9h-1l-12 2h-1l-15 4l-18 6l-3 2h-2l-19 8l-22 14l-3 3l2 1l2 -2l13 -8h2l3 -2h2l3 -2h5l9 -4h3l12 -4l9 -2h1l11 -2h1h9l1 -2h27l1 2h8h1h7l1 2h7h1l5 1l-49 -1l-1 2h-8h-1h-6l-6 2h-5l-4 2h35h1h7l1 2h5h1l4 1l-8 1l-1 -2h-23h-1l-25 2h-1h-4l-6 2h-5l-4 2h5h1h6l1 -2l3 1l-2 1l-19 4l-15 6l-3 2h-5l-11 6l-1 1v1l20 -8h3l4 -2l8 -2l5 -2h4h1h5l1 -2h28h1h11l1 2h7h1l4 1l-13 1l-1 -2h-12l-1 2h-14h-1h-7l-1 2h-10l-9 4h-4l-4 2h-3l-6 2l-8 4h-2l-18 8l-12 9l1 1l12 -6h2l3 -2h2l3 -2l12 -4l8 -2l9 -2l12 -2h1h10l1 -2h29l1 2h9h1h8l1 2h7h-44l-1 2h-9h-1l-12 2h-1h-4l-1 2h-4h-1l-11 2l-3 2h11l1 -2h13h1h8l-13 2h-1h-6l-1 2h-5h-1h-5l-1 2h-4h-1h-3l-4 2l-6 2l-23 10l-17 10l-7 6h2l2 -2l12 -6h2l5 -2h2l5 -4h5l8 -4h3l1 -2h7l5 -2l9 -2l9 -2l18 -2h1h12l-4 2h-9h-1h-6l-6 2h-5l-1 2h-4l-4 2l-23 4v2h3l1 -2h17l1 -2l31 1v1h-23l-1 2h-7h-1h-4l-1 2h-5h-1l-7 2l-15 4l-12 6h-2l-19 11l2 1l7 -4l10 -4l17 -8l6 -2h4l1 -2h5l5 -2h8h1h22h1h13l1 2h5h-40l-1 2h-9l-9 2l-8 2l-4 2l-7 1l7 1l1 -2h15l1 -2l28 1l-27 1l-1 2h-7h-1h-5l-1 2h-4h-1l-7 2l-9 4h-2l-3 2h-2l-5 2h-2l-11 7ZM738 379l2 -1h-3l-3 1ZM558 410v-10v-1l2 -11l1 -2l1 5l-2 1v14ZM692 399l8 -9v-2l-10 10v1ZM538 549l2 -6v-1v-4l2 -1v-5v-1l2 -24l-2 -1v-6l-2 -4v-3l-2 -1v-5v-1v-5l-2 -1l1 -4l1 2v4l2 1v3l2 5v4l1 3l1 -2v-14v-1v-6l-2 -1v-10l-2 -5l-2 -13v-1v-31v-1v-7l2 -1v-10l2 -1l-1 -2l-3 6v10l-2 6v5l-2 1v7v1v14l-2 1l2 2l-1 13l-1 -5v-7l-2 -1v-16l2 -1v-15l2 -1l-2 -1l2 -1v-8l4 -9l-1 -3l-3 6v7l-2 4l-2 9v1l-2 12v1l-2 19l2 1v17l2 1v4v1l2 11l4 9v3v1l2 10v1v7l2 1v19l-2 1v8v1v4ZM682 410l6 -7v-1l-8 7v1ZM743 414v-2h6h1h8l1 -2l9 1l-1 1h-11l-1 2h-11ZM792 414l-22 -2l1 -2l3 2h15h3ZM672 421l6 -7v-2l-8 8v1ZM662 431l6 -6v-1l-6 6v1ZM854 433l2 -1h-3l-3 1ZM656 438l4 -3l-1 -1l-3 3v1ZM646 451l8 -10v-1l-10 10v1ZM567 450l-1 -5l2 -1v3ZM592 519l32 -41l20 -25v-1l-6 5l-22 29l-4 5l-4 4l-14 20l-2 3v1ZM474 636v-29l-2 -6v-6v-1v-3l-2 -1v-3l-2 -6l1 -1l1 2v2l2 3l2 6l2 7l2 9v1l1 26l1 -3v-7l2 -1v-27v-1l-2 -14v-1v-4l-2 -5v-3l-2 -1l1 -1l3 4l4 12v8l2 1v6v1v6h2v-16l-2 -1v-14l-2 -1v-4l-2 -5l1 -3l1 2l2 6v4l4 8v14l2 1l-1 19l1 -5v-9l2 -1v-17l-2 -1v-15l-2 -5l-2 -11l-12 -33l-2 -8l-1 -3l-1 4l2 1v6v1v5l2 5l2 11l2 1l-1 4l-1 -3l-2 -6l-6 -18v-13l-2 -2v19l2 1v8l2 5v3h-2l-2 -6v-3v-1v-3l-2 -4v-5l-2 -1v-11v-1l-1 -11l-1 31l2 1v12l4 8v7l2 1l-1 2l-1 -2l-8 -21l-2 -21v17v1v7l2 1v5v1v6l2 1v4v1v3l4 8v5l-2 -2v-2l-4 -8v-3l-2 -3v-3l-2 -9v-4l-2 -1v-7v17v1v6l2 1v4v1l2 11l10 27l4 9v5h-2v1l2 5v6l2 6v6ZM584 530l8 -9l-1 -1l-7 9v1ZM544 624l15 -8h2l5 -2l12 -4h3l4 -2l7 -2l8 -2h5l1 -2h4h1l19 -4l5 -2h4l5 -2h4l4 -2h4l4 -1h-16l5 -1h5l1 -2l15 -2l1 -2h4l7 -2h-4h-1h-9l-1 2h-11v-2h6h1h5l1 -2h5h1l5 -1l-11 -1l-1 2h-13h-1h-10l2 -2h8h1h6l1 -2h6h1l23 -4l10 -3l-32 -1h-1h-9l-1 -2h-9h0l3 -2h30l1 -2h14l1 -2h4h1h4l1 -2l13 -2l18 -7l-2 -1l-8 4h-5l-6 2l-26 2v-2l9 -1l-2 -1l-17 -2l-1 -1h22l-7 -1h-1l-20 -2h0l1 -2h4l1 2h15h1h19h1h5l1 -2h9l1 -2h3l11 -3l-20 1h-1h-27l-2 -1l18 -1l-2 -2h-5h-1l-8)quill"
    R"quill( -2h-1h-5l-1 -2l-26 2h-1h-8l4 -2l22 -1l-3 -1h-33h-1l-10 2l-24 8l-19 10l-14 10h1l4 -2l24 -12h3l4 -2h3l9 -2h4l1 -2h6h1h31h1l4 1l-23 1l-1 2h-11l-1 2h-4l-12 4h-3l-3 2l-6 1l5 1h0l-3 2h-6l-26 12l-2 2h1l6 -2l10 -4l3 -2h3l15 -4h5l6 -2h6l1 -2h6h1h11l-4 2h-12l-1 2l-11 2h-1h-3l-1 2h-3h-1h-3l-9 4l-3 2h-5l-27 14l-10 8h1l4 -2l17 -8l5 -2l3 -2l12 -4h3l9 -2l11 -2h1l19 -2l1 2h-6l-6 2h-5l-1 2l-11 2l-12 4l-3 2h-5l-5 4h-2h0h3l12 -4h10l1 -2l2 1l-5 3h-3l-10 4h-3l-18 6l-5 2l-5 2l-6 2l-1 2h0l9 -2l3 -2h3l14 -6h8l9 -4l4 1l-6 3h-3l-9 2l-11 6l-8 2l-17 8h3l6 -2l9 -4h3l4 -2h3h1l9 -2h1h5l1 -2h6v2h-4l-5 2l-11 2l-16 6h-2l-3 2h-2l-14 8l-10 6ZM386 863l2 -2l10 -19l2 -2l8 -17l2 -2l10 -20l2 -2l8 -17l2 -2l4 -9l2 -2l12 -24l4 -7l4 -7l2 -2l2 -7l2 -2l4 -7l4 -7l4 -7l4 -7l10 -19l10 -15l12 -21l4 -5l4 -8l14 -20l2 -3l24 -36l2 -3l14 -20l6 -7l2 -3l-1 -2l-3 4l-2 4l-14 17l-10 16l-2 3l-14 21l-2 3l-20 31l-4 8l-4 5l-2 5l-8 11l-2 5l-10 15l-8 16l-14 21l-14 27l-2 2l-2 7l-8 11l-4 9l-6 11l-4 9l-2 2l-30 59l-2 2l-4 8v1ZM470 655v-4v-1v-19v-1l-2 -14v-1v-3l2 -1l-2 -1v-7l-2 -4v-3l-4 -12l-4 -8l-6 -15v-8l-2 -1v-5v-1v-10l-2 -2v11v1l2 17v1v5l2 1v8l4 8l-1 2l-7 -20v-4l-2 -1v-9l-2 -1v-9l-2 2v3l2 1v13v1l2 11v1v3l2 1v3v1v3l2 6l14 30v3l4 12v17ZM446 693v-19v-1v-6l-2 -1v-5l-2 -5l1 -2l3 5v3l2 9l2 17l2 -16l-2 -1v-16l-4 -12v-1l2 4l6 15v17h2v-20l-2 -1v-7v-1l-2 -11l-4 -8v-2l4 5l4 15v3l2 1v6h0v-10v-1l-2 -11l-4 -12v-3v-1v-2l4 6l6 18l1 14l1 -15l-2 -1v-6v-1v-4l-2 -1v-3l-2 -4v-6l-4 -9l-2 -2v1v3v2l-2 -5l-2 -5l-2 -5l-2 -5l-4 -9l-4 -17v8v1v5l2 1l2 14v1l2 7l2 5v3v1v2l-2 -4v-2l-4 -9l-2 -7l-2 -9l-3 -19l-1 16l2 1v18l2 1v4l4 12v7l4 7l-1 2l-5 -11l-6 -15l-3 -16l-1 9l2 1v6l2 6v6v1l2 11l2 4v3l4 7l-1 2l-9 -20v-3l-2 -4l-2 -13v7v1v4l2 1l2 11l2 3v6l4 6l8 27l2 9v1l2 13ZM708 567l8 -1l6 -3l-3 -1l-5 2l-11 2l-3 1ZM434 716v-15v-1l-2 -9v-1v2h-2l-6 -14l-4 -6l-6 -5l-6 -14l-4 -10l-1 -1l-1 1l2 1v3l2 4v3l2 6l4 7l4 7l2 3l10 14l4 15l2 10ZM500 694l24 -12l8 -2l11 -4h3l15 -6l3 -2l15 -4l19 -10h-2l-10 4l-9 4h-3l-4 2h-10l-5 2h-4l-5 2h-3l-15 6l-3 2h-2l-14 6l-11 8h0h2l9 -6l13 -4v2l-12 4l-2 2l-10 6ZM526 671l9 -3l15 -4l4 -2h6l3 -2l7 -2l3 -2h3h-4h-1h-4l-5 2l-11 2l-16 6l-5 2l-6 3ZM436 688l-4 -13l-6 -9v1l6 10v2l2 5l2 4ZM576 675l6 -3l18 -3l-2 -1h-3h-6l-15 7ZM792 1030l13 -10l25 -26v-1l6 -6v-2l2 -3h2l14 -14l10 -7v-1l12 -14l12 -16h1l1 1l-4 6l-10 17l-22 22h2l4 -2l14 -10l21 -24h1v1l-14 21l-8 8v1l-4 4l1 1l11 -8l12 -12l6 -9l4 -8l2 -2v-2l8 -15v-2l2 -3l4 -12v-12h2v-3v-6l-2 -4v10l-2 2l-2 7l-2 5l-2 6l-4 6v2l-6 13l2 1v2h-2l-4 4h-2h0l12 -23v-5l2 -3l6 -15v-5l2 -5l-1 -16l1 1l2 3v5l1 1h1v-6l-6 -10l-2 -2v-10l-2 -3l-4 -7l-6 -4l-2 -4l-18 -12l-16 -6l-8 -2h-1h-5l8 2l5 4l6 2l4 2h2l17 12l4 5l-2 1l-4 -4l-13 -10l-15 -8l-18 -6h-3h-1h-4l8 4h2l20 10l12 8l10 11l-1 1l-4 -4l-4 -2l-13 -10l-9 -4l-9 -6h-2l-16 -8h-2l8 6l8 4l3 2l15 10l10 10l2 3l-1 1l-5 -6l-12 -8l-8 -4h-4l-9 -2l-9 -6h-2l-6 -2h-3l-12 -4h-4l-12 -6l-14 -2l-4 -2l-8 -2h-1h-6l-2 -2h6l6 2h5l4 2h4l8 2h3l13 6h7l8 4l2 -1l-19 -9l-37 -12l-31 -6h0h17l1 2h12l12 4h3l21 8l5 2l5 2l12 6l12 6l8 4h1l-17 -12l-30 -14l-7 -2l-8 -4l-9 -2l-9 -4h-4l-6 -4h-5h-1h-4l-6 -2h-6l)quill"
    R"quill(-1 -2h-9h0l1 -2h4l1 2h10h1h5l1 2h9l5 2h4l21 8l12 6l2 -1l-10 -7l-17 -8l-18 -6h-9l-10 -4h7l5 2h4h1h3l4 2h3l9 4l6 2h2l-8 -4h-2l-10 -6l-9 -2h-3l-4 -2l-11 -2l-1 -2h-4l-6 -2h-7h-1h-12l-2 -2h0h15h1h7l1 2h5h1h5l5 2h4l11 4h4l4 2h1l-9 -6h-1l2 2h-1l-9 -4h-3l-3 -2h-3l-27 -8l-23 -4h0h19l9 4h8l8 4l6 2l3 2l12 4l2 -1l-16 -9l-8 -2l-7 -4h-5l-9 -4l-7 -2l-8 -2h-5l-1 -2h-12l1 -2h11l1 2h13l30 10l5 4l1 -1l-20 -11l-15 -6l-11 -2h-1l-12 -2h-1l-14 -1l7 -1h1h12h1h5l1 2h4h1l7 2l9 4l2 -1l-8 -5h-2l-9 -4l-11 -2l-9 -2l-25 -2l4 -2h18l1 2h5h1h4l4 2h3l3 2h5l-9 -4l-9 -4l-13 -2h-1h-4l-1 -2h-19l7 -2h14l12 4h3l3 2l1 -1l-2 -3l-6 -2l-9 -4h-11l-1 -2h-14l-1 2h-6v-2h7l1 -2h8h1h13l-8 -4l-2 -2l-12 -4h-19l-4 2h-3l8 -4h13l1 -1l-2 -1l-19 -2l-6 2h-3l10 -4l16 1l-4 -3l-5 -2h-13l-1 2h-4l-6 2l-2 2h-2l-6 4l-1 -1l2 -3l8 -4h6l3 -2l11 -2l-4 -2h-10l-9 4l-5 4l-11 6l-8 6h-1l11 -10l7 -4h-3l-13 6l-13 12h-1v-2l10 -10h-2l-12 9v3l-8 8v-2l6 -7v-1l-3 2l-10 10l-2 -2l-6 6h-4l-4 4l-9 4h4l11 -4h1l-7 6l-17 8l-9 2l-3 2l-8 2l-5 4h-4l-1 2h0l27 -8l10 -6l1 1l-12 9l-3 2l-4 2l-3 2h2l3 -2h5l-2 2l-10 6v1v9l-2 -1v-5l-6 4v7l-1 1l-1 -6l-6 4v13v1v5l-2 1v3l-2 6h2l4 -8l4 -14l1 -1l1 6l-2 1v4v1l-2 6h1l1 -1l4 -6l4 -18l2 -1v8v1v5l-2 1v4l-2 6l1 1l1 -2l8 -16l2 -10l2 1l-2 9l-6 18h1l1 -1l8 -16l2 -6l4 -9v-4h2v3l-2 6l-2 10l-6 16l1 1l1 -2l2 -5l2 -2l2 -5l4 -7v-3l2 -6l2 -2v8l-6 17v3l8 -15l4 -15h1l1 4l-2 6v4l-4 5v5l-8 14v2l6 -8v-4l4 -4v-6l2 -2l2 -10l2 -3l2 -7l2 -4v-8l1 -2l1 5l-2 1v10v1v9l4 -10l2 -12h2l-2 1v9l-2 4v10l-6 15l-2 5v5l-2 3v5h0l2 -6l2 -2v-5l2 -1v-6l2 -4l2 -1v9l-2 1v4l-2 3v4v2v1l2 -2v-2l2 -4v-2l4 -5v-9l2 -4v10l-2 5v2h2v-4l4 -7v-3l2 -4v-3l2 -4l-2 -2l4 -7v-3h1l-1 9l-2 1v24l2 1v5v-15l2 -1l2 -19l2 -3v9v1v8l-2 1l1 11l1 -5l2 -13v-1l4 -15l4 -12l3 -6l1 2l-2 3l-2 6l-2 4v4v1v5l-2 1l1 8l3 -8v-3l4 -12l2 -5v-2l3 -2h1l-4 10v4l4 -8l1 -2l1 1l-4 6v4l-6 10l1 3l3 -5l2 -3v1l-6 12l-2 9v3l-2 1l1 2l5 -11l3 -3l1 1l-12 26v1l2 -1l5 -11l1 3l-14 27v3l-4 7l1 2l7 -15l2 -1v2l-8 16l-2 3l-2 7h0l16 -24v2l-8 17v2h2l10 -19v-2l2 -6l4 -4l-2 10l-8 17v1l2 -2l8 -15v-2l6 -15v-2l10 -20l2 -7l5 -7l1 2h0l2 -2l-2 -2l2 -3l8 -15v1l-8 17v4l-2 6l-2 8l-2 6v2l4 -8l2 -3l-2 -2l4 -6l4 -5v-2l4 -8l3 -2l1 1l-4 4l-4 7l-2 2l-2 6l-2 2l2 1l-2 6l-12 25v2l6 -8l4 -10l6 -6v1l-2 2l-6 12v2l-4 6l-10 15h2l8 -9l18 -25l-2 7l-2 2l-4 7l-2 3l-4 9h0l2 -2l2 -4l8 -9l11 -17l1 2l-2 2l-2 2l-6 12l-6 8l-2 5l-8 9h1l9 -9l8 -11l4 -2l4 -6l2 -3l4 -5v2v2l-4 4l-4 6l-4 5l-6 8l-8 9v1v1l5 -6h3l7 -6l10 -10l-1 3l-6 7h-1l-3 4l-8 4l-4 4h3l7 -4l5 -2l14 -10l1 1l-3 3l-17 11l9 1l4 -4l9 -2l16 -8h0l-3 4h-2l-15 8h-3l-3 1l11 1v2h-18h-1h-7l-1 2h-4h-1h-3l-9 4l-17 8l-4 2l1 2h-5v2h-5l-5 3l5 1h1l-2 2h-8h-1l-5 1l6 1v2h-7l-3 -2l-8 3l8 1h0h-7h-3h-1l-5 1l6 1h0l-18 1l4 1l2 1l-1 1l-10 -2h-1l-6 2h-10h4v2h-5l-6 -2h-7l3 2h0h-7l-1 -2h-5h-1h-1l2 2h-1h-1h-5l-1 -2h-19l-1 2h-4h-1l-7 2l-9 2l-3 2h-2v2l-3 -2l-9 4l-3 2h-4l-5 1l5 1h0h-10l-7 3l5 1h0l-9 2h-1h-7l-6 4l-8 2l-3 2l-6 2l-18 10l-11 9l-6 12l-2 6v1v15l2 9l4 7l4 3h2l-6 -7l-2 -5l4 7l6 6l-2 -1v1l1 1l4 2l3 2l8 4h-3v2h3l16 1l-1 1h-8v2h16l15 -6h3l5 -2l4 -2l4 -2l3 -4l3 -1v1l-8 6l-11 6h-3l-7 4l-9 1l8 1l15 -6l8 -4l7 -4)quill"
    R"quill(l10 -10l2 -3v-2l2 -2v-3h0l2 1v3l-2 2l-2 4l-12 14l-11 6l-15 4h5h1h4l6 -4h2l2 -2l7 -2l17 -18l4 -8v-4l2 -2l2 -5l1 -1l1 3l-4 4v5l-4 6l4 2l4 -4l-2 -3l2 1l2 -4v-4h2l2 -2l2 -10l2 1v5l-2 1v4l-2 3v5h-2l-4 8l3 1l1 -2l4 -7l4 -3v-4l-2 -2h1l2 2l2 -2l1 -2v-6l-2 -3l-4 -9l1 -4l1 3v3l2 3v3l5 6l3 -4v-9l-2 -1v-5v-1v-6l2 5v4l2 3v9l2 1v3l2 1v-3l-2 -5v-2l2 4l4 2v3l-2 1v3h2l1 -3l2 2l3 -3l-2 -5h2l2 -5l2 -3v6l-2 4v2l2 2l4 -7v-3l2 -9v-4l2 -1l-1 -14l1 1v5l2 1v20l-4 8l1 3l3 -7l2 -6v-3l2 -1v-5v-3h2v11l-2 1v4l-2 9l-4 5l1 1l7 -10l4 -13l1 -3l1 1l-2 12l-2 9l-4 4l1 2l5 -6l6 -13v-2l2 -3l1 -6l1 9l-2 1v3l-2 6l-4 7l1 2l9 -13l6 -10l1 -5l1 5l-2 1v3l-2 6v2l-6 8l1 1l3 -1l8 -10l8 -15v1v1v3l-2 6l-6 10l-2 2v3l4 -3l12 -16l4 -7h1l-1 6l-10 18h0l3 -2l9 -10l2 -4l4 -3l3 -7l1 2l-4 8v2l-6 8l-6 7l2 1l14 -14l4 -7l6 -11v2l-2 6l-6 12l-8 10l1 2l7 -7l15 -19l1 2l-6 12l-8 9l-2 3h0l5 -2l9 -10l10 -12h2l-2 3l-6 11l-8 9v1l4 -2l14 -13l6 -9h2l-4 9l-10 13l-6 5l1 1l8 -6l15 -14l10 -16l2 1l-10 16l-4 7l-14 13l2 1l10 -6l14 -14l8 -13l4 -6l1 -1l1 3l-10 17l-4 5l-18 16l3 1l10 -6l13 -13l14 -18l7 -15l1 4l-4 4v5l-8 14l-12 13l-11 10l-4 2l-3 2h1l9 -2l8 -6l16 -15l10 -17l4 -7l2 -1v3l-6 13l-10 14l-7 8l-18 12l-9 4h-3l-3 2h4l15 -4l3 -2h2l2 -2l5 -2l1 -2l8 -7l-2 -2l2 -2l6 -7l12 -16l6 -14l6 -18v4v1v3l-2 3v5l-2 3v3l-2 3l-2 5l-8 16l-14 16h1l9 -7l14 -18l10 -19v-6l4 -5v-11v11v1v5l-2 4v3l-8 18l-8 10l1 2l9 -11l10 -16v-2l2 -3v-3l4 -9v-8l1 -2l1 14l-6 18v2l2 -4l6 -10l2 -7l1 -1l1 2l-2 1v3l-2 9l-12 22v1l4 -5l6 -8l12 -21v3l-10 24l-8 11l-22 20l-4 2l-4 4l-9 4l-2 2l-11 4h5l9 -2l3 -2h2l17 -12l20 -20l8 -12l2 -3l6 -10l6 -14l2 -1v3l-2 6v2l-10 19l-10 14l-23 22l-7 4l-10 5l3 1l15 -8l9 -6l13 -11l4 -5l10 -12v-4l4 -3l8 -17l2 -2l4 -10l3 -4l1 1l-2 3l-2 6l-6 13l-18 30l-16 15l-14 10h2l7 -2l15 -10l18 -16v2l-22 19l2 1l9 -6l5 -2l2 -3l6 -6l14 -21l4 -7l4 -6v-4l2 -2l2 -6v-3l2 -3l2 -7h1l1 1l-2 1v4l-2 4v3l-2 6l-2 5l-4 10l-6 6l-6 12l-19 20l-7 6l-22 10l-9 4h-3h-1h-4l-6 2h-8l-3 2h18h1h6l1 -2h4h1l11 -2l23 -10l2 -2l7 -4l11 -8l1 2l-4 4l-15 10l-14 6h-2l-3 2l-15 6h-9l-6 2h-17h-1l-10 -1l5 3h5h1h28l27 -8h0l-1 2l-6 2l-6 2h-3l-2 2v2l11 -2h1h3l1 -2h3l6 -2l16 -6l16 -10l8 -8l3 -2v1l-3 5l-19 14l-17 8h-2l-8 4h-3l-5 2h-6l-1 2h-4l-1 -2h-17l-1 2l-2 -2l-13 1l5 1h7l1 2h34l1 -2h5h1l7 -2l9 -2l5 -4h2l18 -10l15 -12l12 -12l10 -13l2 -1v1l-6 7l-8 12l-14 14l-13 10l-16 10h-2l-3 2h-2l-3 2l-9 2l-8 2h10h1l8 -2l12 -4l20 -10l16 -12l21 -21l10 -12l6 -12l3 -5l-5 12l-2 2l-4 9l-6 10l-22 23l-13 10l-17 8h-2l-8 4l-6 2l-4 2h-14l-1 2l-1 -2l-18 1l16 1h1h19l1 -2h4l8 -2h3l16 -8l2 -2l11 -6l14 -10l17 -18l17 -22l1 1l-8 14l-12 15l-10 12l-16 12l-12 8h-2l-7 4h-2l-3 2l-9 2h-3l-4 2h-5l-1 2h-9h-1l-6 1l16 1l1 -2h5h1l9 -2l4 -2h3l6 -2l9 -4h2l17 -12l13 -10l14 -15l10 -13l4 -7l3 -3l1 1l-8 14l-6 9l-4 5l-4 6l-21 19l-12 6l-2 2l-13 6l-9 2l-7 2h-1h-5l-1 2h-4l-5 1l6 1l1 -2h12l1 -2h7l12 -4l4 -4l9 -4l19 -14l29 -30l12 -16l2 -6v-2l3 -4l1 2l-4 6v2l-8 16l-12 18l-12 12h0l8 -6l14 -13l8 -11l10 -13l8 -17v1l-4 11v2l-14 23l-14 18l-2 1l-2 3l-6 4l1 1l4 -2l17 -16l10 -12l6 -10l9 -12l1 2l-10 18l-12 17l-12 12l1 1l7 -6l18 -19l6 -11l4 -3l9 -19h1v2l-2 4v2l-4 7)quill"
    R"quill(v2l-4 4l-2 7l-14 18l-6 7l-1 3l7 -6l16 -19l13 -21l1 1l-10 19l-4 3l-2 6l-12 16v1l8 -8l16 -21v-3l10 -15l4 -6v-2l2 -3v3l-12 25l-4 6v2l-10 14l-4 4l1 2l13 -14l4 -5l2 -5l6 -7l4 -6l4 -5h1l1 1l-8 14l-10 16l1 1l3 -3v-1l10 -11l12 -15l6 -13l3 -1l-1 3l-6 9l-2 7l-16 22l-6 7v2l2 -2l10 -10l12 -13l4 -8l4 -4l6 -12l2 -1v1l-4 8l-4 9l-12 19l-10 12l-12 11l-13 10l-5 7l1 1l3 -4l15 -12l13 -12v-1l14 -15l17 -22l1 3l-4 6l-10 15l-6 9l-38 37l-16 19l-14 13ZM430 684v-2l-2 -3l-3 -7l-1 2l2 6l2 4ZM401 692l-1 -4v-2l2 2ZM496 705l17 -5l12 -8l11 -3l-4 -1l-1 2h-3h-1h-3l-1 2h-3l-6 2l-16 8l-4 3ZM650 694h-4l-6 -2l-20 1l7 -3h10h1h4l6 2l2 2ZM484 716l7 -4h6l9 -4l3 -2h5l12 -6h-3l-16 6h-3l-9 2l-13 8ZM406 711l-2 -3l1 -2l-5 2l1 2h2l2 2ZM264 987l4 -4l2 -7l2 -2l1 2h1v-4l1 -2l1 3l-2 6v4h0l6 -13l1 -10l1 11l-2 6v2h0l4 -12v-5l2 -1l-1 -9l3 5v9l-2 1v4v2v1l2 -2v-6l2 -1v-13l-2 -1l1 -13l1 4l2 19h2v-8l-2 -1v-7v-1v-12v-1v-6l2 -1v-6h2v2l6 -12l2 -3l2 -4l8 -7l-1 -2h-3l2 -2l4 2l2 -2l-2 -2l-3 -2l-1 -1l2 -1l3 2l4 2l1 -1l-8 -7h-2v-2h3l8 6l1 -1l-2 -5l-6 -4h2l8 4v-1l-6 -7h-1l-1 -1v-1l6 2l6 8h2l-2 -3l-7 -7l-11 -6h4l12 6v-3l-6 -5l-9 -4l-11 -1l10 -1l1 2h4v-2l-7 -2h5h1h4l7 4h1l-3 -4h-2l-2 -2l-7 -2h3l9 4l4 3v2l4 3l1 2l1 -1l-2 -2l-8 -10l2 -1l10 10l3 8l1 -3l-2 -1v-4l-2 -6l-8 -8h-3v-2l10 6l5 4l4 9h0v-5l-4 -9l-4 -4l-8 -7h2l4 2l10 9l2 3v4h2v-3l-4 -8v-2l-4 -4l1 -1l1 -1l-2 -4l-6 -5h3l7 7l6 15v-3l-2 -4v-4l-2 -6l-2 -3v-4l-4 -5l2 1l2 2l2 3l4 7v2l3 4l1 -2l-4 -9v-2l-2 -3v-4l4 6l4 8v-3l-2 -3v-5l-4 -6l-8 -8v-1h1l15 15l2 10h2v-4l-2 -6l-4 -3v-4l-6 -9l2 1l4 3l6 11v-2v-3l-6 -12l-4 -5v-2l-4 -4h0l12 13l5 9l1 -3l-6 -12l-8 -10l1 -1l7 6l6 7l2 7l2 8h0v-5v-1v-4l-2 -3v-2l-6 -11l-8 -8h2l10 10l4 8v-2v-3l-4 -8l-6 -7l-10 -8h2h2l8 8l5 6l1 -2l-6 -8l-5 -6l-7 -4l-4 -2l-2 -1l2 -1l6 4h2l5 4l1 -2l-8 -8h0l5 2l10 10l1 -1l-2 -2l-6 -7l1 -2l9 11l6 13v-2l-2 -9l-6 -8v-3l4 5l6 8l4 11v5l1 1l1 -5l-2 -1v-5l-6 -16l1 -1l1 2l6 10l3 10l1 -11l-2 -1v-4l-2 -6v-2l-4 -4h1l3 3l4 5v2l2 6l2 8v-18l-4 -10h2l4 8l2 10l2 2v-6v-1v-7l-4 -9l1 -1l3 3l2 4l1 11l1 -4v-7v-1v-5l-2 -5l2 1l2 6v3l2 2v-1v-2v-6l-2 -4v-3h2v2l2 6v3h2l-2 -10l-2 -6l-4 -9h0l4 4l4 8v-3v-1v-3l-4 -5v-2l-2 -2v-2l-4 -6h1l8 6l1 -2l-6 -6l-6 -4l-1 -2l-6 2l-23 12l-2 2h2h2h0l-1 2h-3l-1 -2l-2 2l-5 2l1 2l3 -2v2h-7l-5 3l3 1h0h-7l-6 4h6v2h-10l-2 1l4 1v2l-7 -2l-6 2l-5 4h11l1 1l-8 1l-6 2l-2 2h-3l-5 5l1 1l8 -4h3l-8 4l-10 7l1 1l5 -2h3l9 -4h2l-2 2l-8 2l-6 2l-8 9l1 1l6 -6l7 -4v1l-10 9l-4 7l-2 3h1l9 -10l7 -4l1 1l-3 3l-11 10v1l1 1l7 -6l8 -4v1l-14 11l-8 13l-2 2l1 1l7 -8l7 -6l1 1v1l-10 9l-8 9v2v2l9 -10l3 -2v1v1h-1l-13 15l-2 4v1l8 -8l3 -2h1l-16 18v2v2l9 -10h3l-16 15v2v3l5 -6l5 -4h0l-1 2h-1l-6 7l-4 4l-2 2l1 1l4 -4l4 -2l1 1l-8 8l-6 8l-2 4h2l6 -7l5 -4l1 1l-14 12l-2 6v5l4 -9l6 -5h2l-8 8l-2 5l-2 3l-2 4h2l4 -7l6 -5h0l-8 12l-2 7h0l2 -3l4 -7l7 -7h1v1l-8 11l-2 6h0l2 -4l7 -6l1 1l-8 9l-2 6h2l7 -8h1l-6 6l-2 6h0l4 -4l3 -2h1l-6 8v2l4 -4l2 1l-6 6l-2 17l-1 2l-1 -9l2 -1v-3l-2 -1l-4 5v3h4l-2 2l-4 8v7l-2 4v4l-2 5l-2 9l-2 9h2v-3l6 -7v2v3l-6 9l2 4l4 -5v-2l1 -1h1l-2 5l-2 5v1ZM480 725l23 -11l3 -2h-1l-3 2l-7 2l-12 6l-5 3ZM570 718v-1l2 -3v1ZM572 724v-2l5 -6l1 1ZM553 746)quill"
    R"quill(l-1 -5l2 -1v-3l1 -1l1 2ZM462 748l10 -6h7l3 -2h-5l-9 2l-8 6ZM519 746l-1 -1l2 -3v3ZM495 750h-1v-6l2 2ZM537 756l-1 -4l2 -4v-3l2 -1v7l-2 1v3ZM588 758h-2l6 -10l3 -4h1ZM513 750l-1 -1v-3l2 1ZM482 749l2 -1h-2l-2 1ZM592 769l4 -10l-1 -3l-5 11v2ZM1160 766l2 -1l-3 -3l-15 -4h-4l9 2l11 6ZM1148 767v-1l-8 -4l-6 -2h-3l-1 -2l-4 1l2 1h3l11 6ZM1138 770l-2 -2h-2l-4 -4h-2l-3 -2h-6l-1 -2h-4h-1h-1v2h3l9 2l12 6ZM573 794l-1 -1l2 -6l4 -7l2 -5l4 -10l4 -5v3l-6 11v2l-6 11v2ZM1124 771l-10 -5h-2l-6 -4h-4h-1h-5l22 8l2 2ZM731 766l-21 -3l6 -1l1 2h10h3l2 2ZM1110 774v-2h-3l-2 -2l-6 -2l-5 -4h-6l-1 -2l-3 1l1 1l11 4l13 6ZM460 792l6 -15v-3l2 -5v-5l-3 2l-1 -1l-2 14v1l-2 9v1v2ZM603 770l-1 -1l3 -5l1 3ZM1100 776l-8 -6l-6 -2l-9 -4l-5 1l11 3l5 4h2l8 4ZM1086 776l-13 -8h-6l-1 -2l-6 1l7 1l11 6h2l5 2ZM1076 779l-13 -7l-6 -2h-3l-1 -2h-3h-1h-3l2 2h3l9 2l3 2h2l9 6ZM575 778l-1 -2l5 -8l1 2l-2 2ZM454 793l6 -15l-1 -8l-3 3v3v1v5l-2 1v4l-2 6ZM1062 780l-15 -8l-13 -2l1 2h3l9 2l12 6ZM625 776l-1 -3l3 -1l1 1ZM1144 774l-3 -2h-1l2 2ZM1050 784l-2 -2l-9 -6h-2l-9 -4l-4 1l11 3l12 6l2 2ZM1156 795l-2 -2l2 -1h2l-1 -2l-8 -4l-13 -10l-5 -2l-3 1l2 3l6 2l19 16ZM536 792l4 -10l1 -6h-1l-4 8v2v4v2ZM634 780l-2 -1l3 -3l1 1ZM902 1095l4 -1l6 -6l12 -20l4 -6v-1l-2 1v-2l10 -22l-1 -2l-3 4l-2 2h0l8 -15v-5l2 -3v-3l2 -1l-2 -4l8 -20l-1 -1l-3 4h0l4 -15l2 -8v-1v-7l2 -1v-6v-1v-6l2 -1v-7v-1l2 -25v-1l1 -6l1 19l-2 1v15v1v10l-2 1v8v1v7l-2 1v6v1v5l-2 1l1 3l1 -2l2 -6l2 -7v-4l2 -4v-5l2 -6v-5l2 -1v-6v-1v-6l2 -1v-4v8v1v9l-2 1v8v1v6l-2 1v7v1v5l-2 4v2v2v6l-2 1l-2 19l-6 14l1 4l1 -2l2 -5l2 -3l4 -15v-3l4 -11l2 -8l2 -4v-9l2 -4v-4l2 -1v-4l1 -3l1 5l-2 1v5v1v5l-2 1v5v1l-4 19v1l-2 7v1v3l-4 9l-2 3l-2 7l-4 8l-2 6l-2 2l-10 17v2l-8 17h1l3 -3l12 -17v-2l4 -7v-2l6 -9l4 -12l2 -5l10 -30l4 -14l2 -4v-3l2 -6l1 -2l-1 6l-2 5v4l-2 4l-2 16l-6 18v7l-14 27l-6 9v5l-6 10l-2 2l1 1l11 -11l6 -9v-2l2 -5l2 -7l8 -15l2 -6v-4l2 -5l2 -6v-3l2 -5v-7l2 -1h0v2v9l-2 1l1 6l7 -19l2 -11l2 -1v-4v-1v-4l2 -1v-5l2 -6v-5v-1v-6l2 -1v-5v8v1l-2 15v1l-2 10l-2 5v9l-2 4v4l-2 4v3l-8 15v6l-4 6l-12 24l-2 6l-6 11l-4 5l2 1l12 -12l2 -5l2 -3v5l-2 3v4l-4 5v1l6 -5l2 -4l2 -3l2 -5l10 -24l4 -9v-3l2 -6v-7l-2 4l-2 5l-1 1l-3 3v7l-4 9v2l-9 13l-1 -3l6 -10l2 -7l2 -5l2 -5l4 -10l2 -1l6 -9v-2l2 -3v-3l2 -4l4 -9v-3h1l-1 6l-2 3v3l-2 5v3l-2 4l-2 13v1v5l-2 4v4h0l4 -7v4l-6 13l-2 2v2l-8 11l-2 9l-6 10l-4 5h0l8 -5l12 -16v-4l6 -11v-2l4 -8l2 -10l2 -3v-3l2 -7v-4l4 -7v-4l4 -12v-5l2 -9v-5l2 -1v-4v-1v-4l2 -5v5v1v6l-2 1v7v1v4l-2 1v4l-2 6v6v1v3l-4 9v5l-2 9v3l6 -11l1 -1h1l4 -6v-6l4 -13v-4l4 -12v-3l-2 5l-2 1h0l4 -10l2 -20l2 -4l2 5v4l-4 7v8l2 3l-2 7v8l-2 3v3l-2 1v5v1v4l-2 1v4v1l-2 7l-6 16v2l-2 2l-4 9v3l-4 8v2l-2 6l-4 5l-4 10l-6 6l1 1l5 -3l10 -15v-2l6 -10v-7l4 -9l2 -8l10 -21v-5l4 -10v-7l2 -1v-5l2 -4v-11l2 -1l-2 -8l4 -9l-1 -10l-3 6l-1 2l-1 -3l2 -1v-3l2 -2v-6l2 -1v-4v-1v-8v-1v-10v-1v-7l-2 -1l2 -1v-10l-2 -1v-11v-1v-7v5l2 1v6v1v8l2 1v12l2 4v6l-2 1v9l2 1v5l-2 1v16v1l-2 14v1l-2 15v1v5l-2 1l2 6l-2 4l-2 9v1v3l-2 1v3l-2 9v3l2 -4l2 -4v3l-6 13l-6 18l1 4l5 -7v-4l4 -4v-2l4 -7v-4l4 -6v-4l2 -5v-10l2 -6v-6l2 -1v-10l2 -1v-7v-1l2 -18v-1l2 -45l-2 -1v-23l-2 -1v-12l-2 -1)quill"
    R"quill(v-5v-1v-4l-2 -5l-2 -8l-4 -10v-6l-4 -8v-2l-8 -14l-4 -4h0l2 2l2 -1l-4 -6l-7 -7l-5 -4h-9l-7 3l6 1l2 2h-1h-3h-1h-8l-1 1l6 1v2h-7l-1 -2h-6l-2 2l3 2h3l2 1l-1 1h-4l-5 -2h-12l1 2h2h2v2h-4l-6 -4h-5h0l2 3v1l-14 -8h-3l-6 -4l-9 -2l-11 -2l-1 2h-7l-8 4l-9 9l-4 6l2 1l3 -4l6 -4h1h2v2l2 -1v-3l8 -2h3l4 2l7 6l6 11l6 18v8l2 1v8l2 1v5v1l2 14v1v8l2 1v13v1v42v1v15l-2 1v21l-2 1v7v1l-2 19l-2 1l-2 14l-2 5v4v1l-4 19l-2 4v3l-4 12l-10 20l-8 10h-3h-3v-2h2v-2l-3 -6l-1 1l2 3l2 3l-2 1l-2 -3v-3v-7l-2 -1v-3l-2 -7v15l2 5h-2v-2v-4l-2 -4v-3l-2 -4l-2 -7h0v14l2 1l-2 5l4 10l2 3l8 7ZM1090 778l-2 -2l-2 1l2 1ZM446 796l2 -3l2 -6v-4l2 -1l-2 -4l-2 13l-2 4v1ZM1152 803l-12 -13l-9 -6l-11 -6h-4l20 14l12 10l2 2ZM572 786l2 -6v2ZM1142 808v-2l-19 -18l-14 -8l-3 1l3 1l14 10l18 16ZM534 786v-2v-2l-2 2v2ZM572 813l14 -29l-1 -2l-7 12v5l-6 12v2ZM1132 811l-1 -1l-19 -18l-13 -8l-3 -2l-2 1l1 1l7 4l12 8l16 16ZM625 794l1 -5l5 -5l1 1ZM638 790h0l4 -6h1l-1 2ZM1056 798l-12 -10l-9 -4h-3l16 10l4 4h2ZM1054 786l-2 -2l-2 1l2 1ZM498 794l4 -5v-2v-1l-6 7l1 1ZM1086 787l-2 -1h-2l2 2ZM530 800l2 -4v-5v-1v-4l-2 4v8l-2 1v1ZM648 792v-2l3 -2h1ZM1026 836l-4 -15l-6 -12v-2l-8 -12l-2 -3l-4 -4h1h1l8 8l6 13v2l4 8l2 6l2 7ZM1124 815v-1l-11 -10l-5 -4l-4 -4l-4 -2l-6 -4l-3 -2l-1 1l4 3l6 6l4 2l10 8l5 4l4 4ZM300 793l2 -2v-1l-2 2v1ZM955 858l-1 -2l-2 -12v-1v-4l-2 -1v-4v-1l-2 -11l-4 -9l-2 -3v-2l-6 -12l-4 -5l1 -1l9 12l2 4l2 5v3l4 7v7l2 1v4v1v4l2 1v16l2 1ZM442 795v-2v-3l-2 3v2ZM657 794l-1 -2l2 -2v2ZM1013 816l-7 -13l-4 -6l-6 -6v-1h1l7 7l4 6l4 10ZM1118 821v-1l-16 -14l-12 -8l-9 -6l-3 -2l-2 1l15 11l5 2l17 14l4 4ZM522 802l4 -8l-1 -2l-3 6v4ZM634 796l-2 -1l4 -3h0ZM884 797l4 -5h0l-4 2v2v1ZM470 795v-1v-2l-2 2v1ZM642 798v-2l3 -2h1ZM998 806l-2 -4l-8 -7v-1l8 7l2 3ZM536 800l2 -4v-2l-2 5v1ZM960 836l-8 -23l-8 -15l-2 -3v-1l4 4l8 12v2l2 3l2 9l2 8v1ZM1068 820v-1l-13 -13l-11 -10l-3 -2l-1 1l8 7l19 18ZM462 800l2 -3l-1 -1l-3 3v1ZM653 798h-1l3 -2h1ZM664 800v-1l3 -3l1 1ZM560 800v-2l1 -2l1 2ZM750 798l-6 -1l6 -1ZM454 802l2 -3v-1l-2 3v1ZM985 802l-5 -4h1l3 2ZM635 804l-1 -1l5 -5l1 2ZM985 820l-5 -11l-10 -10l1 -1l9 8l4 9l2 3ZM965 820l-7 -13l-4 -5v-2l6 7l4 8ZM1066 806l-7 -6h-1l6 6ZM640 816l-2 -1l2 -1l13 -6l8 -6h1l-10 8ZM968 806l-2 -3v-1l2 3ZM436 812l4 -3l2 -7l-2 1v3l-4 5v1ZM444 810l4 -6l-1 -2l-5 7v1ZM450 808l4 -4l-1 -2l-5 6ZM659 816l-3 -1l10 -5l3 -4h3l-3 4h-3ZM1070 836l-8 -13l-17 -17l-1 1l10 9l14 18l2 2ZM1106 823l-4 -5l-12 -12l-2 1l11 9l6 8ZM773 810h-5h-1h-4l-5 -2h6h1h5l3 2ZM1080 821v-1l-12 -12h-2l14 14ZM918 843l-6 -9l-8 -8h-1l-3 -4h2l4 2l4 4l2 -1l-12 -9l-9 -6h-2l-6 -2h-3l-1 -2l-11 1l7 1l21 12l4 2l18 20ZM912 820l-7 -6l-10 -4h-3l-1 -2l-5 1l8 3l14 6l2 2ZM1086 816l-6 -6h-2l7 6ZM786 814l-8 -2l-2 -1l5 -1l5 2ZM428 816l2 -2v-2l-2 3v1ZM673 816l-3 -1l5 -3l1 1ZM683 816h-3l4 -2v1ZM864 848l-13 -10l-20 -10l-7 -2l-5 -4h-5l-3 -2h-2l-9 -5l3 -1l3 2h3l3 2h3l6 2l3 2h2l3 2h2l3 2l5 2l9 4l8 5l8 8ZM1164 855l-8 -17l-4 -4l-4 -5l-8 -9v-1l-2 -1l2 5l8 11l2 4l12 16l2 2ZM368 825v-3l-2 -2v2v3ZM805 826l-10 -4l-3 -2l12 4l2 1ZM1092 823l-1 -3l-1 1l2 3ZM1084 826l-3 -4l-1 1l4 3ZM1072 825l-2 -3v2l1 2ZM839 854l-11 -6l-2 )quill"
    R"quill(-2l-24 -12h-2l-5 -4h-2l-3 -2h-2l-3 -2l-7 -2h1l9 4h3l6 2l15 6h2l17 10l9 7ZM1072 851l-8 -13l-6 -8l-6 -6v1l14 17l4 7l2 3ZM851 850l-14 -10l-24 -12h-2l-1 -1l2 -1l3 2l3 2h2l16 6l10 8l6 5ZM1154 856v-3l-10 -14l-10 -11v1l10 14l4 7l6 6ZM548 834v-1v-1l-2 1l1 1ZM340 836l-2 -1l1 -1l1 1ZM828 858l-8 -4h-2l-2 -2l-9 -4l-8 -6h-2l-3 -2l-14 -4h3h1l7 2l3 2l12 4l16 8l3 2l3 3ZM904 842l-4 -3l1 -1l3 4ZM1080 880l-6 -15l-4 -5l-6 -11l-9 -11l-1 2l2 2l8 12l4 8l8 12l2 5l2 1ZM1124 845l-5 -7l-1 1l6 7ZM1108 849l-1 -3l-1 2l2 2ZM911 850l-3 -1l1 -1h1ZM1128 850l-1 -2l-1 1l2 1ZM366 856l4 -5l-1 -1l-3 5v1ZM922 858l-4 -8v2v3l2 3ZM1138 914l-8 -16v-2l-2 -3v-2l-6 -12l-2 -2l-4 -8v-2l-2 -2l2 -1l-2 -4l-5 -10l-1 2l6 9v7l6 12l2 2v2l10 19v2l4 8l1 1ZM812 860l-6 -2l-10 -6h2l5 2h2l2 2l3 2l2 2ZM1074 854l-1 -2l-1 1l2 1ZM1132 854l-2 -2v1l1 1ZM911 862h-1l-2 -2v-3l-2 -3l1 -2l1 1v2l2 3ZM1098 897l-2 -7l-10 -19l-2 -2l-6 -12l-3 -3l3 7l2 2l8 16l2 2v4l6 12ZM1104 857l-1 -3l-1 2l2 2ZM1134 857l-2 -3v2l1 2ZM364 861l2 -3l-1 -2l-3 4v1ZM1108 861l-3 -3l-1 1l3 3ZM1124 864l-2 -2v1l1 1ZM655 868l-1 -1v-5l2 3ZM1027 934l-1 -8l2 -1v-12l-2 -1l2 -1v-19l-2 -1v-6v-1v-13l-2 -1v-8l2 2v11l2 1v7v1v9l2 1v22v1v5v1l-2 8ZM1080 898v-2l-10 -22l-8 -12v2l4 8l2 2l4 9l2 2l6 13ZM681 872l-1 -7l2 -1v3v1ZM910 882v-4v-1l1 -13l1 3v5v1v8ZM1018 884l-2 -1v-16l-2 -1l2 -2l2 5v7v1ZM1160 943l-6 -20l-6 -13l-2 -5l-6 -11v-4l-2 -2l-4 -7l-10 -17v2l4 8l4 7l6 10v2l4 7v2l4 7v2l8 17l4 13v2ZM406 868h-2l3 -2l1 1ZM469 890l-1 -1l-6 -13l-2 -2v-3l-4 -4v-1l8 10l4 6l2 6ZM668 870h-2l1 -4l1 1ZM332 870l-2 -2v1v1ZM1118 917l-2 -6l-4 -7v-2l-8 -17v-2l-2 -2l-7 -13l-1 2l12 22v2l2 2v4l8 16l2 2ZM315 872h-2l-1 -1l1 -1l3 2ZM477 892h-1l-2 -2v-4l-4 -9l-4 -5l1 -2l5 6l4 8ZM456 880l-4 -6l1 -2l3 4v3ZM427 892l-1 -3v-2v-7l4 -8v2l-2 4v5v1ZM619 880l-1 -4l2 -4v6ZM436 888l-2 -1v-9l2 -4ZM481 880l-3 -5v-1l4 5ZM629 884h-1v-1l2 -3v-3l1 -3l1 2l-2 1v4ZM678 882v-2l2 -6v5ZM924 878l-1 -4l-1 4ZM1130 922l-4 -11v-2l-6 -9v-2l-12 -24v2l4 7l2 7l8 15v2l4 7v2l2 6ZM605 888l-1 -3l2 -1v-4l2 -4v2v1l-2 7ZM666 878v-2h1l1 1ZM450 888l-2 -3v-7l2 4ZM328 881l-1 -3l-1 2l2 2ZM592 890v-2l3 -8l-1 6ZM669 884l-1 -2l2 -2v2ZM1088 936l-6 -11l-6 -18l-13 -27l-1 3l2 3l12 24l2 7l2 5l2 5l4 8l1 1ZM617 888l-1 -1v-1l2 -4v3ZM680 944l-2 -1l8 -6v-1l10 -10l8 -12l12 -20v-2l4 -10v3v1v3l-8 14v3l-4 7l-12 18l-6 5ZM355 968h-12h-1h-4l-7 -4l-6 -4l-7 -8l-6 -11v-5h2v5l6 10l8 7h4l-5 -2l-1 -1l1 -1h4h1h5l1 -2l11 -2l9 -4h6l6 -4v-2l-4 2h-4l-5 2h-3l2 -2l14 -4l-2 -2l-6 2l-4 -1l8 -3l2 -1l-2 -1l1 -2l5 10l6 6l3 2h2l1 -13l-2 -1v-5l-2 -5v-4l-2 -5v-3l-4 -13l-3 -1l-3 3l-4 11h-2l2 -3l-1 -1h-2h-4l-1 2l-2 -1l10 -3v-2l-5 2h-7l1 -2l10 -2l1 -1v-1l-2 2h-5h-1h-7l-1 2l-4 -1l8 -3h4l2 -1l-3 -1l-1 2h-4l-6 2l-4 -1l18 -5h-4h-1h-4l-5 2h-4l-14 6l-8 7l-4 7v-1l4 -9l10 -8l15 -8h3l4 -2h4l2 -2h8l1 -2h8v2h-10l-1 2h5l4 2h2l5 2l4 6l6 13l2 13v1v14l-2 7v-19v-1v-6l-2 -6v-4l-4 -11l-4 -4l-5 -5l-1 2l6 8l6 15v3v1l2 10v1v17l-2 5l-6 6l-6 4h-2l-3 2l-11 2ZM577 898l-1 -3l5 -9l-1 8ZM1018 896v-10l2 4ZM381 888h-5h5ZM624 894v-2l2 -4v3ZM891 926l-1 -3l8 -16l4 -9v-7l2 -3v5v1l-2 10l-2 3l-2 5l-2 2l-2 7ZM387 900l)quill"
    R"quill(-6 -6l-6 -4h-7h8l5 2l5 4l2 3ZM438 900l-2 -4l1 -6l1 4v1ZM602 896h-1l-1 -1l4 -5v2ZM589 898l-1 -3l3 -3l1 2ZM870 904v-4l3 -8l1 3l-2 4v3ZM855 912h-1v-1l2 -5l2 -8l3 -4l1 2l-4 6v6ZM882 896v-1l1 -1l1 1ZM890 900v-4l1 -2l1 2ZM705 934l-1 -1l6 -5l2 -4l8 -11l2 -3l4 -7l2 -6l3 -3l-3 11l-4 4l-2 5l-4 6ZM307 898l-1 -1l1 -1ZM477 902v-6l1 1v3ZM565 900l-1 -2l2 -2v2ZM455 904v-8l1 5ZM844 910h-2l6 -14v4ZM1116 991l-2 -10l-2 -3v-6l-4 -6v-3l-2 -3v-3l-2 -3v-5l-4 -5l-2 -10l-8 -15l-4 -12l-2 -6l-3 -3l-1 3l8 13v2l6 12v5l2 3l2 5l4 9v3l2 3l4 16l2 3l4 14l2 2ZM464 906v-5l1 -1l1 4ZM483 902v-2l1 1ZM680 914l2 -4l2 -3l5 -7l1 2l-2 2ZM270 905v-2v-3l-2 3v2ZM390 904l-2 -2l1 -2l1 2ZM573 906h-1l3 -6l1 3ZM879 906l-1 -2l3 -4l-1 4ZM562 906v-1l2 -3v1ZM834 908v-3l2 -3v1ZM887 908h-1v-1l3 -5l1 2ZM1084 955v-4l-4 -9l-2 -3l-2 -11l-12 -26v3l2 3v3l2 3l4 15l2 3l6 20l2 2ZM867 912h-1v-1l3 -7l1 3ZM1023 926l-1 -5l2 -1v-5v-1l1 -10l1 11l-2 1v6ZM814 922h-2l10 -16v1l-2 2v2l-2 2l-2 6v2ZM875 914l-1 -1l4 -5v2ZM392 912l-2 -2h1h1ZM719 934l-1 -1l8 -8l12 -15v1l-6 10ZM832 914h-2l3 -4l1 3ZM884 975l18 -15l12 -17l8 -22v-6l2 -1l-1 -4l-3 7l-2 3v2l-4 7v3l-4 4l-4 9l-6 11l-6 8l-12 11ZM879 926l-1 -2l4 -7v-2l3 -3l1 3l-4 5v4ZM677 920l-1 -2l3 -2h1ZM849 924l-1 -1l2 -3l3 -4l1 1ZM452 922l-2 -1l2 -3ZM1138 991l-2 -14l-4 -10v-3l-6 -15l-2 -5l-4 -9l-9 -17l-1 2l8 17l2 8l4 5l2 8l4 8l6 20l2 3v2ZM390 952v-1l2 -3v-7l2 -1v-22l2 5v16l-2 1v7ZM440 922h-2l1 -4l1 1ZM830 924h-1l-1 -2l3 -4l1 2ZM1104 927l-1 -5l-1 3l2 2ZM1122 928v-2l-2 -4v2v3l2 1ZM580 955l4 -1h4l9 -4h2l5 -4h2l13 -10l9 -9v-3h0l-11 12l-7 4l-9 4l-4 2l-13 6l-14 3ZM634 929l2 -1l-1 -2l-1 2v1ZM1124 982v-3l-2 -7v-3l-6 -15l-11 -26l-1 2l2 1v6l2 3l6 16l2 3l2 8l2 5l4 10ZM444 935l2 -4l-4 -3l-2 4l4 4ZM458 936l4 -4l-1 -2l-3 3v3ZM1076 964l-2 -10l-4 -12l-2 -3l-2 -8l-1 -1l-1 3l2 3l2 3l8 25ZM1152 998v-4l-4 -11v-3l-6 -15v-3l-4 -9l-2 -11l-7 -12l1 5l2 3l12 36v3l2 3l4 12v6ZM268 940v-4l3 -6l1 4ZM582 959l11 -1l9 -4l14 -6l9 -8l5 -4l2 -5v-1l-13 12l-10 6l-17 8h-3l-5 2h-5l-3 1ZM410 934h-2l1 -2h1ZM440 941l2 -3l-4 -4l-2 6l2 2ZM450 938l2 -3l-4 -1v3l1 1ZM995 938h-1l1 -4ZM510 946l12 -6l4 -4h-2l-3 2l-13 8ZM536 942l3 -2l5 -4h-2l-6 4l-4 2ZM1164 999v-4l-2 -4v-3l-4 -9v-3l-2 -4v-3l-6 -18l-2 -3v-5l-4 -7v2v1v3l16 45v3l2 9ZM488 948l5 -2l7 -8v-1v-1l-14 12ZM394 956h-2l4 -4l6 -14v4l-4 10ZM409 942l-1 -1l1 -3l1 1ZM476 952l5 -4l11 -10h-3l-9 10l-6 4ZM498 947l8 -3l6 -6h-2l-4 4l-6 2l-4 3ZM520 945l4 -1l8 -6h-2l-2 2l-12 5ZM456 942v-1v-1l-2 1v1ZM446 943l2 -1l-1 -2h-2l-1 2l1 2ZM446 964l9 -4l4 -4l9 -11l2 -3h-2l-4 6l-11 10l-1 -1l6 -6l2 -2l-1 -1l-15 16ZM466 955l10 -9l2 -3l-1 -1l-5 5l-8 8ZM382 946l-2 -2l1 -2l1 2ZM542 944l2 -1l-3 -1l-5 2ZM548 947l3 -1l7 -3l-3 -1l-3 2l-8 3ZM432 951l2 -3l4 -2l-2 -2h-2l-2 1v3l-2 2l1 2ZM902 971l8 -10l6 -9l4 -6l-1 -2l-5 9l-14 18ZM554 950h6l5 -2h4l5 -3l-10 1l-3 2h-7l-2 2ZM440 950l2 -1l1 -3l-3 1v2v1ZM340 958v-2h4l8 -4h8l8 -4h-3h-1h-4l-12 4h-4l-3 2h-1l-4 2h-4v2l1 -2l3 2l1 -2l2 2ZM1070 967l-4 -14l-2 -5v3l2 6l2 3v5l2 2ZM327 952h-3v-1l3 -1ZM450 953l2 -1l-1 -2l-1 2v1ZM430 954l2 -1l-2 -1h-1l-1 2ZM382 978l9 -2l4 -2l7 2l1 -2h3l3)quill"
    R"quill( -2l5 2l1 -2h6l7 -4l4 -4l2 -5l-1 -1l-2 2l-6 6l-11 6v-1l1 -1l5 -4l8 -7l-1 -1h-2l-8 8h-3l-5 4l-10 4h-1l2 -2l10 -4l10 -11l-2 -1l-9 10l-10 6l-1 -1l6 -4l10 -12v-1l-2 -2l-2 2v3l-2 1l-7 8l-4 2l-3 4l-11 4h-2l-1 2ZM436 956l2 -2h0l-4 1l1 1ZM430 969l10 -5l10 -9v-1h-1l-3 2l-9 8l-9 5ZM1108 999l-2 -9v-3l-10 -27l-3 -6l-1 2l2 1v3l6 18l4 11l2 8l2 2ZM352 956l4 -1l-4 -1l-4 2ZM440 959l4 -4l-1 -1l-5 5ZM360 957l4 -1h-4l-4 1ZM338 962h5l5 -2h5l3 -2h-4h-1l-11 2h-1h-3l-2 2ZM1088 965l-2 -6l-2 -1v1l2 6ZM434 963l4 -3h-1l-5 3ZM346 964h5l1 -2l8 -1l-5 -1l-5 2h-5l-1 2ZM828 963l2 -1l-1 -2l-1 2v1ZM1072 996l-2 -9v-3l-9 -22l-1 3l2 3l2 6l6 20l2 2ZM294 1009l-1 -1l3 -1l-2 -3l4 -1l-2 -2l1 -1h3v-1l-2 -2l1 -1h1h2v-2l2 -1l-2 -2l2 -1l2 -3l4 -5v-4l2 -2v-1v-2l-5 -5l-3 4l-2 2v-1l2 -5l2 -2l-2 -2l-14 22h1h1l2 1l-5 3l-5 2h6v2l-2 1l2 1l-3 2h-5l-2 2h3v2h-5l5 2l9 6ZM999 976l-1 -3l3 -7l1 5l-2 1v3ZM828 971l4 -4v-1l-6 5ZM1033 978l-1 -2l5 -6h1ZM1096 996v-3l-2 -7v-3l-4 -12l-2 -1v3l2 6v3l2 3l4 11ZM822 976l4 -3l-1 -1l-5 4ZM702 1003l13 -3l7 -2h3l6 -2l16 -10l13 -10l-3 -2l-3 4l-3 2l-9 6l-12 6l-32 11ZM272 981v-5v5ZM818 979l2 -2v-1l-4 2ZM708 1023l9 -1h1h5l1 -2h4l4 -2h3l22 -10l21 -14l18 -16h-2l-22 16l-23 12h-1l18 -12l18 -15l-1 -1l-3 4l-18 12l-16 8l-12 4h-4l-5 2h-6h-1l-21 2h-1h-24l7 2h3l1 2l37 1l-4 1h-6l-2 2l-3 2h-17l-1 -2h-11l-1 -2h-4h-1l-7 -2l-9 -2l-3 -2h-3l7 4h5l5 4l6 2h3h1l16 2l1 2ZM714 1007l8 -3h3l9 -2l3 -2h2l3 -2h2l16 -10l8 -7l-1 -1l-6 4l-31 16l-24 7ZM270 986l2 -2l-1 -2l-3 3v1ZM979 988h-1l2 -6v2ZM1068 1006l-6 -18l-3 -6l1 5l2 3l4 14v2ZM1013 988l-1 -2l2 -2v1ZM320 989l2 -3l-1 -2l-3 4v1ZM430 999l-6 -7l-7 -4l-6 -2h-3h-1h-3v2h3l6 2h2l11 6l3 4ZM808 1022l9 -6l6 -4l31 -25l-3 -1l-1 2l-1 -2l-2 2l-18 12l-23 22ZM826 1018l12 -6l3 -4l11 -6l5 -6l8 -4l5 -5l-2 -1l-7 4l-14 10l-9 4l-14 14ZM844 1014l18 -10l4 -4l7 -4l9 -8h-2l-8 4l-3 4l-7 2l-9 6l-11 9l1 1ZM316 994l2 -3v-1l-2 3v1ZM418 1070l2 -3v-7l2 -1v-14l-2 -4v-3l-4 -5v-1l4 3l4 10v4l2 1v10l-2 1v3l-2 3l1 1l3 -5v-3l2 -1v-12l-2 -1v-5l-2 -5v-2l-4 -5l-4 -4l1 -1l9 8l2 5l2 4l1 1h1v-6l-2 -4l-2 -2l-2 -3l-8 -8l2 -1l6 4l6 7l1 1h1l-2 -5l-12 -11h2l10 6l6 8l2 8v-3l-2 -9l-7 -6l-3 -4l-4 -2l-6 -5l1 -1l11 6l6 8v-2v-2l-5 -6l-7 -6l-12 -6h4h3l2 2l5 2l7 6l1 -1l-12 -11l-9 -4l-6 -2h-19h-1l-6 2l-13 6l-6 5l1 1l5 -4l8 -4l3 -2l7 1l-10 3l-12 8l-2 -2l-2 4v2h0l3 -2l10 -6h3h4l-11 6l-5 5l1 1l6 -6h7v2l-6 4h1l2 -2h4l2 2h0l-7 1l6 1l9 6l15 14l4 8l2 18l2 -2v-4v-1v-3v-1v-3l2 3v3v1v11l2 -6v-13l-2 -4l1 -1l1 2l4 7v12l-4 6v1ZM772 1024l8 -6l26 -27l-1 -1l-3 2l-10 12l-5 2l-9 10l-5 2l-3 6ZM878 1010l15 -12l5 -7l-1 -1l-16 14l-4 2l-3 4ZM1034 994v-2l1 -2l1 2ZM506 1066l2 -7v-1v-3v-8l-2 -1v-3l-2 -4l-2 -3h0l6 6l2 4v3l2 1l-1 14l1 -2v-7l2 -3l-2 -1v-6v-3l-4 -6l-6 -6v-2l7 4l7 10v5v1v3v9l2 -3v-12l-2 -1v-3l-2 -4l-2 -5l-7 -6l-5 -3l4 -1l8 6h2l-2 -2l-4 -4l-12 -7l4 -1l3 2l9 4l4 4v-1l-6 -7l-6 -4h-4l-4 -4l8 2l5 4l1 -1l-5 -5l-5 -2h-5h-1h-4l-4 -2h6l5 2h4h1v-1l-3 -3l-8 -2l-13 -1l5 -1h1h2h5l6 2l8 6l2 2l1 -1l-10 -9l-11 -4l-8 -2h-1h-15h-1h-3v2h19l8 2h-6h-1h-5h-1h-13l-3 2h0h12h1l6 1l-14 1l-8 4l-2 1l1 1l8 -4h12l3 2l-16 2l-6 5l1 1l3 -2h3l1 -2h13l3 2h-1)quill"
    R"quill(3l-3 2l-4 2l-2 1l1 1l5 -2h6l-6 2l-3 2v2l6 -2h5l1 -2l5 1l-2 1l-7 2l-7 4h0h2l4 -2l6 1l-3 1h-3l-6 4h8h1h5h0l-4 1l10 3l11 6l5 6l4 9v5h2l1 4l1 -6l-2 -1v-5l-2 -5l1 -1l3 5l2 6v1v5v1v4ZM946 1021l8 -23v-4v-1l-1 -1l-1 4l-4 8v7l-4 7v3ZM312 1002l4 -6l-1 -2l-5 6v2ZM362 998l4 -3l-1 -1l-2 2l-3 2ZM858 1014l3 -2l6 -2l3 -4l4 -2l8 -8v-1v-1l-3 2l-9 8h-2l-12 10ZM890 1009l4 -3l12 -9l-2 -1l-3 4l-15 9ZM1064 1021l-7 -23l-1 6l4 8l2 7l2 2ZM812 1002l2 -2l-1 -2l-3 3v1ZM1010 1006h-2l2 -1v-3l2 -4v2ZM356 1004l4 -3l-1 -1l-3 3v1ZM716 1043l10 -1l1 -2h3l4 -2h6l23 -12l9 -7l2 -4l14 -15h-2l-3 2l-16 14l-15 10l-5 2l-5 4l-4 2l-5 2l-6 2h-3l-1 2h-4h-1h-6l-1 2h-17l-1 -2l-15 1l2 1h10l1 2ZM762 1042l9 -4l8 -6l15 -17l10 -14v-1l-6 6l-2 2l-16 15l2 1l-2 3l-5 5l-15 10ZM574 1004l-1 -2l-1 1l1 1ZM802 1013l8 -8l-1 -1l-9 8v1ZM610 1021l-19 -17l-1 1v1l14 12l4 4ZM602 1007l-2 -3v1l1 3ZM620 1007l-1 -1h-1l1 2ZM630 1027l-11 -7l-15 -12h-2l7 8l4 2l3 2l12 8ZM674 1037l-7 -3h-3l-3 -2h-6l-3 -2l-10 -4l-20 -10l-10 -8h-2l7 6l21 12h2l8 4l18 6h3l1 2ZM662 1028l-10 -4h-2l-7 -4h-2l-11 -6l-8 -6h-2l11 8l11 6h2l3 2h2l9 4ZM743 1010l-3 -1l2 -1h2h0ZM208 1127l10 -18l2 -2l2 -5l2 -2l2 -5l4 -7l10 -18l14 -17l19 -27l1 3l-2 2l-8 12l-8 17l-4 13l-12 15l-12 20h0l2 -2l2 -4l6 -6v2l-6 8v2l16 -14l14 -9v-1v-2v-2l-2 5l-2 1h0l2 -6l2 -5v-2l6 -12v-3l-4 7v4l-2 1v-1l8 -17v-2l4 -7v-2l12 -24l2 -2v-3l-2 -2l-5 -2l1 3l-4 1l-2 1v2l-4 5v3l-8 10l-4 7l-8 7l-6 9l-8 8l-10 12l-2 5l-3 7l-1 -2l2 -1v-3l2 -9v-1l-2 2v3l-4 10v3l-4 15l-2 3l-4 12l-4 5v2ZM636 1010l-1 -2l-1 1l1 1ZM346 1019v-9v9ZM372 1011l2 -1h-2l-2 1ZM352 1022v-5l2 -4l-1 -3l-3 5v7ZM590 1012v-2l-2 1l2 1ZM696 1032l15 -2l13 -4h-5h-1h-9l-1 2h-20l-1 -2h-7h-1l-8 -2l-6 -2h-3l-3 -2l-12 -4l-9 -6l-2 1l3 3l5 2l2 2l15 6l15 6h3h1l12 2h1ZM735 1014l-3 -1l4 -1h2ZM596 1016l-4 -4l-2 1l4 3ZM716 1036l12 -2l13 -6h2l8 -6l11 -8h-2l-32 16l-8 2l-3 2l-15 2ZM1062 1041l-2 -11l-2 -3l-5 -13l-1 4l2 3l4 8l2 10l2 2ZM842 1027l4 -1l11 -4h2l6 -4l4 -2l1 -1l-3 -1l-12 6l-19 7ZM826 1026h4l8 -4h3l11 -6h-3l-8 4l-6 2h-3l-10 4ZM976 1080l8 -8l8 -12v-2l8 -14v-6l2 -3l2 -8l2 -3v-3l2 -4v-1l-4 4l-2 5l-2 2v2l-4 6l-2 9l-6 12l-4 4v5l-2 2l-2 6l-4 6v1ZM856 1028h6l3 -2l7 -2l12 -7l-3 -1l-5 4l-26 8ZM356 1024v-6v6ZM866 1030l14 -2l18 -9l-3 -1l-6 4l-18 6h-7l-2 2ZM820 1022l2 -2h-1l-3 2ZM370 1038l-2 -6l2 -4v-4l2 -1l-1 -3l-1 1l-4 4v9l2 4ZM406 1071l-2 -1l2 -30l-10 -10l-10 -6l-4 -2h-2l4 3l4 4l-1 1l-10 -8h-1l8 10h0l-3 -2l-4 -4l-1 2l2 2l8 10h0l-4 -2l-6 -6v1l4 7l8 7l-1 1l-2 -2l-9 -8v1l9 11h1h2l6 6h-1l-7 -6v1v1l6 5v2v1l-15 -16l-1 2l19 18l9 8ZM1014 1034l4 -9v-3l-2 7l-2 3v2ZM882 1031l11 -3l9 -4h-2l-7 2h-3l-7 4l-7 1ZM720 1049l15 -3l4 -2h3l8 -4h2l10 -4l8 -6l8 -5l-2 -1l-1 2h-3l-5 4l-11 4l-4 4h-2l-5 2l-12 4l-9 2h-1h-5l-1 2l-19 1ZM998 1089l8 -3l6 -7l4 -6v-2l4 -8v-3l2 -7v-3l2 -3v-2l2 -3l2 -9v-3l2 -1l-1 -3l-1 2v2l-8 14l-2 2l-6 11l-2 10l-8 13v2l-8 7ZM918 1086l8 -9l8 -14l2 -5l2 -3v-2l2 -3v-2l2 -3l4 -12v-5h-2v4l-2 2l-8 17l-6 9h1l1 2l-10 21l-2 2v1ZM1056 1048l-2 -6l-2 -6l-2 -7l-1 -1l-1 4l2 1v3l2 3l2 7v2ZM674 1032l-5 -2h-3l-1 -2l-3 1l9 3ZM518 1033l-3 -3l-1 1l3 3ZM894 1032l4 -1l-4 -1l-4 2Z)quill"
    R"quill(M438 1051v-10v-1v-4l-3 -4l1 8v1v10ZM498 1072l-4 -12v-16l-2 -4l-11 -4l-7 -2h-1h-5l7 4h0l-8 -2h-1l4 2v2h-3l-3 -2h-2l12 6h-3l-9 -2h0l1 2l6 2l5 2l4 2l-15 -4l-1 1l2 1l9 4h2l3 2l6 2l6 1l-2 1l-15 -4h-3l-7 -4l-1 1l2 1l6 4l8 2l3 2h8l1 2h-2h-1h-4l-11 -4h-3h0l17 6h3l2 2l1 1l-8 -3h-3l-10 -4h-1v2l3 2l9 2l10 2l2 2h-3h-1l-15 -4h-1h-2h0l15 6h4h1ZM522 1043l-2 -1v-7l-2 -1v2v1l2 6ZM454 1051v-13l-2 3v5v1v1v3ZM992 1084l6 -6l6 -7l4 -8v-3l2 -3l4 -12l-1 -5l-9 18v2l-4 6l-4 11l-6 7ZM376 1044l-1 -2l-1 1l2 1ZM432 1060l2 -6v-8l-2 -4v15l-2 1v2ZM1048 1049v-5l-2 -2v3l2 4ZM1048 1066l-4 -11v-2l-2 -1v5l2 3l2 5l1 1ZM1052 1057l-2 -5v3v2ZM270 1058v-2v-2l-2 2v2ZM399 1062l-1 -2l1 -6l1 2v1ZM878 1071l-2 -5l-2 -10v7v1l2 6l1 2ZM1013 1066l1 -3l3 -3h1ZM400 1066l-6 -5l1 -1l5 3v2ZM270 1082l2 -7l2 -9v-4l-2 7l-2 7v1v4l-2 1ZM890 1069v-5v5ZM1042 1072l-2 -8l-2 3l2 5ZM1080 1068l-2 -4v3l1 1ZM1104 1075v-3l-4 -4v1v2l2 3l2 2ZM1050 1074v-4h-2l2 4ZM1108 1102v-1l-2 -1l-16 -15l-9 -15l-1 2l4 6l8 12l8 8l4 4ZM1120 1073l-2 -3v2l1 2ZM894 1081l-2 -7v3l2 4ZM1122 1076l-2 -2v1l1 1ZM1038 1079v-2l-2 -1v1v2ZM1112 1086l-2 -4l-5 -6l-1 1l2 3l4 4l2 2ZM1070 1082l-1 -2l-1 1l2 1ZM1072 1085l-1 -3l-1 2l2 2ZM1152 1087l-2 -3v1l2 3ZM253 1088l1 -2l1 -2l1 1ZM1156 1102l-12 -8l-10 -10v1l4 6l8 7l6 4ZM1068 1092l-4 -6v1l2 3l2 2ZM1074 1088l-1 -2l-1 1l2 1ZM1142 1102l-9 -6l-9 -10v1l2 6l5 5l6 4h3ZM1124 1102l-8 -4l-11 -10h-1v1l9 9l9 4ZM1116 1090l-2 -2v1l1 1ZM1168 1103l-3 -3l-12 -12h-1l4 7l7 7l3 2ZM1090 1102l-4 -2l-11 -12l-1 1l2 4l7 7l5 2ZM1062 1095l-1 -1h-1l1 2ZM1078 1101l-8 -7h0l4 6l3 2ZM200 1145v-1v-2l-2 2l1 2Z"/></svg>)quill";
static const char* kLassoToolIcon =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M6 15c-2-1-3-3-2-6 1-4 5-6 10-5 5 1 8 4 6 8-2 3-7 5-11 4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-dasharray="3 2" stroke-linecap="round"/><path d="M9 14c-3-1-5 0-5 2 0 2 4 3 5 1 1-1 0-3-1-2-1 2 0 6 3 7" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>)";

static bool FitToolbarGroups(const Vec<int>& widths, int available, int gap, int overflowDx, int brandIdx,
                             Vec<bool>& visible) {
    VecReset(visible);
    int total = 0;
    int count = 0;
    for (int width : widths) {
        bool show = width > 0;
        VecAppend(visible, show);
        if (show) total += width + (count++ > 0 ? gap : 0);
    }
    if (total <= available) return false;

    auto remove = [&](int idx) {
        if (idx < 0 || !visible[idx]) return;
        visible[idx] = false;
        total -= widths[idx] + (count-- > 1 ? gap : 0);
    };
    remove(brandIdx);
    if (total <= available) return false;
    for (int i = len(widths) - 1; i >= 0 && total + overflowDx + (count > 0 ? gap : 0) > available; i--) remove(i);
    return true;
}

static bool FitScrolledToolbarGroups(const Vec<int>& widths, int available, int gap, int controlsDx, int brandIdx,
                                     int& first, Vec<bool>& visible, bool& before, bool& after) {
    before = after = false;
    if (!FitToolbarGroups(widths, available, gap, controlsDx, brandIdx, visible)) {
        first = 0;
        return false;
    }
    VecReset(visible);
    for (int i = 0; i < len(widths); i++) VecAppend(visible, false);
    first = std::clamp(first, 0, std::max(0, len(widths) - 1));
    int remaining = std::max(0, available - controlsDx - gap);
    bool showed = false;
    for (int i = 0; i < len(widths); i++) {
        if (i == brandIdx || widths[i] <= 0) continue;
        if (i < first) {
            before = true;
            continue;
        }
        int needed = widths[i] + (showed ? gap : 0);
        if (needed > remaining) {
            after = true;
            continue;
        }
        // Keep a contiguous run: arrows move through groups in displayed order.
        if (after) continue;
        visible[i] = true;
        remaining -= needed;
        showed = true;
    }
    return true;
}

static int LocationEditWidth(int preferred, int minimum, int cap) {
    return std::min(std::max(preferred, minimum), std::max(minimum, cap));
}

static int LocationEditCap(int available, int fixedDx, int reservedDx, int fieldCount) {
    int budget = std::max(0, available - fixedDx - reservedDx);
    return std::min(available / 4, budget / std::max(1, fieldCount));
}

#if IS_DEBUG
static void ToolbarInteractionTests();
static void ToolbarPaletteTests();

void ToolbarLayout_UnitTests() {
    Vec<int> widths;
    for (int width : {180, 120, 170, 100}) VecAppend(widths, width);
    Vec<bool> visible;
    utassert(!FitToolbarGroups(widths, 588, 6, 36, 0, visible));
    utassert(visible[0] && visible[1] && visible[2] && visible[3]);
    utassert(!FitToolbarGroups(widths, 402, 6, 36, 0, visible));
    utassert(!visible[0] && visible[1] && visible[2] && visible[3]);
    utassert(FitToolbarGroups(widths, 360, 6, 36, 0, visible));
    utassert(!visible[0] && visible[1] && visible[2] && !visible[3]);
    utassert(FitToolbarGroups(widths, 40, 6, 36, 0, visible));
    utassert(!visible[0] && !visible[1] && !visible[2] && !visible[3]);
    VecReset(widths);
    VecAppend(widths, 0);
    VecAppend(widths, 100);
    utassert(!FitToolbarGroups(widths, 100, 6, 36, -1, visible));
    utassert(!visible[0] && visible[1]);

    VecReset(widths);
    for (int width : {180, 120, 170, 100}) VecAppend(widths, width);
    int first = 0;
    bool before = false, after = false;
    utassert(FitScrolledToolbarGroups(widths, 300, 6, 108, 0, first, visible, before, after));
    utassert(!visible[0] && visible[1] && !visible[2] && !before && after);
    first = 0;
    utassert(FitScrolledToolbarGroups(widths, 300, 6, 108, -1, first, visible, before, after));
    utassert(visible[0] && !visible[1] && !before && after);
    first = 2;
    utassert(FitScrolledToolbarGroups(widths, 300, 6, 108, 0, first, visible, before, after));
    utassert(!visible[1] && visible[2] && !visible[3] && before && after);
    first = 3;
    utassert(FitScrolledToolbarGroups(widths, 300, 6, 108, 0, first, visible, before, after));
    utassert(visible[3] && before && !after);
    utassert(!FitScrolledToolbarGroups(widths, 600, 6, 108, 0, first, visible, before, after));
    utassert(first == 0 && !before && !after && visible[0] && visible[3]);

    // Empty and one-digit labels stay compact; longer text grows until capped.
    utassert(LocationEditWidth(0, 24, 100) == 24);
    utassert(LocationEditWidth(18, 24, 100) == 24);
    utassert(LocationEditWidth(46, 24, 100) == 46);
    utassert(LocationEditWidth(140, 24, 100) == 100);
    utassert(LocationEditWidth(140, 24, 12) == 24);
    utassert(LocationEditWidth(92, 48, 60) == 60);

    // Reserve the navigation group's labels/buttons and the overflow command.
    utassert(LocationEditCap(600, 80, 36, 1) == 150);
    utassert(LocationEditCap(200, 140, 36, 1) == 24);
    utassert(LocationEditCap(200, 140, 36, 2) == 12);
    utassert(LocationEditCap(100, 140, 36, 1) == 0);
    utassert(LocationEditCap(1200, 160, 72, 1) == 300);
    ToolbarInteractionTests();
    ToolbarPaletteTests();
}
#endif

struct ToolbarLocationEdit : Edit {
    MainWindow* win = nullptr;
    int preferredDx = 0;
    int minimumDx = 0;
    int widthCap = Inf;

    int PreferredWidth() const { return LocationEditWidth(preferredDx, minimumDx, widthCap); }

    bool MeasureWidth() {
        int previous = PreferredWidth();
        int dpi = win->frameDpi > 0 ? win->frameDpi : DpiGet();
        int breathingRoom = UiScalePxForDpi(dpi, 8);
        minimumDx = GetPreferredWidth(StrL("00"), 0, Inf) + breathingRoom;
        preferredDx = GetPreferredWidth(GetTextTemp(), 0, Inf) + breathingRoom;
        return previous != PreferredWidth();
    }

    void OnTextChanged() {
        if (!MeasureWidth()) return;
        auto* tb = win->toolbarVirt;
        if (!tb || !tb->host->layout) return;
        tb->host->Relayout();
        tb->host->Invalidate(true);
    }

    Size GetIdealSize() override {
        Size size = Edit::GetIdealSize();
        size.dx = PreferredWidth();
        return size;
    }

    void SetBounds(Rect bounds) override {
        bool resizing = bounds.dx != lastBounds.dx && IsFocused();
        int start = 0, end = 0;
        if (resizing) EditGetSelection(this, start, end);
        Edit::SetBounds(bounds);
        if (!resizing) return;
        int newStart = 0, newEnd = 0;
        EditGetSelection(this, newStart, newEnd);
        if (start != newStart || end != newEnd) EditSelectText(this, start, end);
        SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
    }
};

struct ToolbarLocationField {
    ToolbarLocationEdit* edit = nullptr;
    HBox* group = nullptr;
};

struct ToolbarHiddenLayout {
    ILayout* layout = nullptr;
    Visibility visibility = Visibility::Visible;
};

enum class ToolbarLineKind {
    Main,
    Annotations
};

// Hide complete groups, including native fields, while retaining document availability.
struct ToolbarLine : HBox {
    ToolbarVirt* tb = nullptr;
    Vec<ToolbarHiddenLayout> hidden;
    Vec<ToolbarLocationField> locationFields;
    int brandIdx = -1;
    int findGroupIdx = -1;
    ToolbarLineKind lineKind;
    VirtIconButton* previousButton = nullptr;
    VirtIconButton* nextButton = nullptr;
    int firstGroup = 0;
    int wheelRemainder = 0;
    bool canScrollBefore = false;
    bool canScrollAfter = false;

    explicit ToolbarLine(ToolbarVirt* toolbar, ToolbarLineKind kind = ToolbarLineKind::Main)
        : tb(toolbar), lineKind(kind) {
        alignCross = CrossAxisAlign::CrossCenter;
    }

    VirtIconButton* OverflowButton() const {
        return lineKind == ToolbarLineKind::Main ? tb->overflowButton : tb->annotationOverflowButton;
    }

    Vec<VirtCtrl*>& OverflowItems() const {
        return lineKind == ToolbarLineKind::Main ? tb->overflowItems : tb->annotationOverflowItems;
    }

    bool Scroll(int direction) {
        if ((direction < 0 && !canScrollBefore) || (direction > 0 && !canScrollAfter)) return false;
        Restore();
        int next = firstGroup + direction;
        int count = len(children) - 3;
        while (next >= 0 && next < count && (next == brandIdx || children[next].layout->MinIntrinsicWidth(0) == 0))
            next += direction;
        firstGroup = std::clamp(next, 0, std::max(0, count - 1));
        tb->host->Relayout();
        tb->host->Invalidate(true);
        return true;
    }

    void Restore() {
        for (int i = len(hidden) - 1; i >= 0; i--) hidden[i].layout->SetVisibility(hidden[i].visibility);
        VecReset(hidden);
        VecReset(OverflowItems());
    }

    void HideLayout(ILayout* item) {
        VecAppend(hidden, ToolbarHiddenLayout{item, item->GetVisibility()});
        if (VirtCtrl* ctrl = item->AsVirtCtrl()) {
            if (ctrl->id && ctrl->GetVisibility() == Visibility::Visible) VecAppend(OverflowItems(), ctrl);
        }
        for (int i = 0; i < item->LayoutChildCount(); i++) HideLayout(item->LayoutChildAt(i));
        item->SetVisibility(Visibility::Collapse);
    }

    int MinIntrinsicWidth(int) override { return OverflowButton() ? OverflowButton()->MinIntrinsicWidth(0) : 0; }

    int MinIntrinsicHeight(int) override {
        int height = tb->rowDy - UiScalePx(12);
        for (auto& child : children) height = std::max(height, child.layout->MinIntrinsicHeight(Inf));
        return height;
    }

    void SizeLocationFields(int available, int overflowDx) {
        for (auto& field : locationFields) {
            if (IsCollapsed(field.edit)) continue;
            int fieldsDx = 0;
            int count = 0;
            for (auto& other : locationFields) {
                if (other.group != field.group || IsCollapsed(other.edit)) continue;
                fieldsDx += other.edit->PreferredWidth();
                count++;
            }
            int fixedDx = field.group->MinIntrinsicWidth(0) - fieldsDx;
            // Long labels share the remaining group budget, with room for other commands.
            field.edit->widthCap = LocationEditCap(available, fixedDx, overflowDx + gap, count);
        }
    }

    Size Layout(Constraints bc) override {
        Restore();
        if (lineKind == ToolbarLineKind::Main && tb->findSlot) {
            tb->findSlot->SetVisibility(tb->findExpanded ? Visibility::Visible : Visibility::Collapse);
            tb->findSlot->dx = tb->findExpanded ? tb->findPreferredWidth : 0;
        }
        if (lineKind == ToolbarLineKind::Main && tb->findButton && tb->findExpanded &&
            tb->findButton->GetVisibility() == Visibility::Visible) {
            VecAppend(hidden, ToolbarHiddenLayout{tb->findButton, Visibility::Visible});
            tb->findButton->SetVisibility(Visibility::Collapse);
        }
        auto* overflowButton = OverflowButton();
        overflowButton->SetVisibility(Visibility::Collapse);
        previousButton->SetVisibility(Visibility::Collapse);
        nextButton->SetVisibility(Visibility::Collapse);
        int available = bc.HasBoundedWidth() ? bc.max.dx : Inf;
        int pickerDx = overflowButton->MinIntrinsicWidth(0) + gap;
        available = std::max(0, available - pickerDx);
        int overflowDx = previousButton->MinIntrinsicWidth(0) + nextButton->MinIntrinsicWidth(0) + gap;
        SizeLocationFields(available, overflowDx);
        Vec<int> widths;
        for (int i = 0; i < len(children) - 3; i++) {
            auto* child = children[i].layout;
            VecAppend(widths, IsCollapsed(child) ? 0 : child->MinIntrinsicWidth(0));
        }
        bool showScrollButtons = true;
        if (lineKind == ToolbarLineKind::Main && tb->findExpanded && findGroupIdx >= 0 && findGroupIdx < len(widths)) {
            int withoutFind = widths[findGroupIdx] - tb->findSlot->dx;
            // Give active search priority; Open stays available in the toolbar picker.
            if (withoutFind + tb->findMinWidth + overflowDx + gap > available) {
                ILayout* group = children[findGroupIdx].layout;
                for (int i = 0; i < group->LayoutChildCount(); i++) {
                    auto* item = group->LayoutChildAt(i)->AsVirtCtrl();
                    if (item && item->id == CmdOpenFile && item->IsVisible()) HideLayout(item);
                }
                withoutFind = group->MinIntrinsicWidth(0) - tb->findSlot->dx;
            }
            // Keep active search usable on narrow windows; the picker and wheel
            // still provide access to the other groups while the arrows are hidden.
            if (withoutFind + tb->findMinWidth + overflowDx + gap > available) {
                overflowDx = 0;
                showScrollButtons = false;
            }
            int searchDx = std::min(tb->findPreferredWidth,
                                    std::max(tb->findMinWidth, available - withoutFind - overflowDx - gap));
            tb->findSlot->dx = searchDx;
            widths[findGroupIdx] = withoutFind + searchDx;
        }
        Vec<bool> visible;
        int removableBrand = tb->findExpanded && findGroupIdx == brandIdx ? -1 : brandIdx;
        bool overflow = FitScrolledToolbarGroups(widths, available, gap, overflowDx, removableBrand, firstGroup,
                                                 visible, canScrollBefore, canScrollAfter);
        for (int i = 0; i < len(widths); i++) {
            if (widths[i] > 0 && !visible[i]) HideLayout(children[i].layout);
        }
        overflowButton->SetVisibility(Visibility::Visible);
        previousButton->SetVisibility(overflow && showScrollButtons ? Visibility::Visible : Visibility::Collapse);
        nextButton->SetVisibility(overflow && showScrollButtons ? Visibility::Visible : Visibility::Collapse);
        previousButton->SetIsEnabled(canScrollBefore);
        nextButton->SetIsEnabled(canScrollAfter);
        Size size = HBox::Layout(bc);
        size.dy = bc.ConstrainHeight(MinIntrinsicHeight(available));
        return size;
    }

    void SetBounds(Rect r) override {
        HBox::SetBounds(r);
        auto* overflowButton = OverflowButton();
        if (overflowButton->GetVisibility() == Visibility::Visible) {
            Rect button = overflowButton->lastBounds;
            button.x = rtl ? r.x : r.Right() - button.dx;
            overflowButton->SetBounds(button);
            for (VirtIconButton* nav : {nextButton, previousButton}) {
                Rect next = nav->lastBounds;
                next.x = rtl ? button.Right() + gap : button.x - gap - next.dx;
                nav->SetBounds(next);
                button = next;
            }
        }
    }
};

static ToolbarButtonInfo gToolbarButtons[] = {
    {kEnhancedIconOpen, CmdOpenFile, TrN("Open")},
    {kEnhancedIconSearch, CmdFindFirst, TrN("Find")},
    {nullptr, 0, {}},
    {kEnhancedIconPrevious, CmdGoToPrevPage, TrN("Previous Page")},
    {nullptr, PageInfoId, {}},
    {kEnhancedIconNext, CmdGoToNextPage, TrN("Next Page")},
    {nullptr, 0, {}},
    {gIconZoomOut, CmdZoomOut, TrN("Zoom Out")},
    {gIconZoomIn, CmdZoomIn, TrN("Zoom In")},
    {kEnhancedIconFitWidth, CmdZoomFitWidthAndContinuous, TrN("Fit Width")},
    {kEnhancedIconPage, CmdSinglePageView, TrN("Reading layout")},
    {nullptr, 0, {}},
    {gIconRotateLeft, CmdRotateLeft, TrN("Rotate Left")},
    {gIconRotateRight, CmdRotateRight, TrN("Rotate Right")},
    {nullptr, 0, {}},
    {kHandQuillIcon, CmdToggleEditPDF, TrN("Edit PDF")},
    {kHandToolIcon, CmdHandTool, TrN("Hand tool: drag to pan")},
    {kLassoToolIcon, CmdAnnotationLasso, TrN("Lasso: select, move, resize or delete annotations")},
    {kEnhancedIconInk, CmdCreateAnnotInk, TrN("Pen: tools, colors and thickness")},
    {kEnhancedIconHighlight, CmdAnnotationHighlightBrush, TrN("Text highlighter")},
    {kEnhancedIconUnderline, CmdCreateAnnotUnderline, TrN("Underline")},
    {kEnhancedIconStrikeOut, CmdCreateAnnotStrikeOut, TrN("Strike Out")},
    {kEnhancedIconEraser, CmdInkEraser, TrN("Stroke eraser")},
    {gIconLaserPointer, CmdToggleLaserPointer, TrN("Laser pointer: styles and colors")},
    {nullptr, 0, {}},
    {kEnhancedIconBookmark, CmdToggleBookmarks, TrN("Bookmarks")},
    {kEnhancedIconStar, CmdCommandPaletteFavorites, TrN("Favorites")},
    {kEnhancedIconCommand, CmdCommandPalette, TrN("Command Palette")},
    {kEnhancedIconPrint, CmdPrint, TrN("Print")},
    {nullptr, 0, {}},
    {kEnhancedIconSettings, CmdOptions, TrN("Settings")},
    {kEnhancedIconSun, CmdThemeLight, TrN("Day mode")},
    {kEnhancedIconMoon, CmdThemeDark, TrN("Night mode")},
    {kEnhancedIconGrid, CmdChangeTheme, TrN("Theme presets")},
    {kEnhancedIconInvert, CmdInvertColors, TrN("Invert document colors")},
    // Available to custom layouts without widening the default toolbar.
    {gIconDictionary, CmdDictionaryLookup, TrN("Dictionary")},
    {gIconLearning, CmdVocabularyHome, TrN("Vocabulary practice")},
    {gIconStudyExport, CmdExportStudyNotes, TrN("Export highlights and notes")},
    {gIconPresentation, CmdTogglePresentationMode, TrN("Presentation")},
    {gIconHome, CmdGoToHomePage, TrN("Home")},
    {gIconNavigateBack, CmdNavigateBack, TrN("Back")},
    {gIconNavigateForward, CmdNavigateForward, TrN("Forward")},
    {gIconSpeak, CmdToggleReadAloud, TrN("Read Aloud")},
    {kEnhancedIconPage, CmdZoomFitPageAndSinglePage, TrN("Fit a Single Page")},
    {kEnhancedIconInvert, CmdToggleLightDarkTheme, TrN("Light / Dark")},
};
// unicode chars: https://www.compart.com/en/unicode/U+25BC

constexpr int kButtonsCount = dimof(gToolbarButtons);

static ToolbarButtonInfo gPdfAnnotationButtons[] = {
    {kHandToolIcon, CmdHandTool, TrN("Hand tool: drag to pan")},
    {kLassoToolIcon, CmdAnnotationLasso, TrN("Lasso: select, move, resize or delete annotations")},
    {kEnhancedIconHighlight, CmdAnnotationHighlightBrush, TrN("Highlighter: select text to highlight it")},
    {kEnhancedIconInk, CmdCreateAnnotInk, TrN("Ink")},
    {gIconAnnotHighlight, CmdCreateAnnotHighlight, TrN("Highlight Selection")},
    {kEnhancedIconUnderline, CmdCreateAnnotUnderline, TrN("Underline")},
    {gIconAnnotSquiggly, CmdCreateAnnotSquiggly, TrN("Squiggly")},
    {kEnhancedIconStrikeOut, CmdCreateAnnotStrikeOut, TrN("Strike Out")},
    {nullptr, 0, {}},
    {gIconAnnotText, CmdCreateAnnotText, TrN("Text")},
    {gIconAnnotFreeText, CmdCreateAnnotFreeText, TrN("Free Text")},
    {nullptr, 0, {}},
    {gIconAnnotLine, CmdCreateAnnotLine, TrN("Line")},
    {gIconAnnotPolyLine, CmdCreateAnnotPolyLine, TrN("Polyline")},
    {gIconAnnotSquare, CmdCreateAnnotSquare, TrN("Square")},
    {gIconAnnotCircle, CmdCreateAnnotCircle, TrN("Circle")},
    {gIconAnnotPolygon, CmdCreateAnnotPolygon, TrN("Polygon")},
    {nullptr, 0, {}},
    {gIconAnnotRedact, CmdCreateAnnotRedact, TrN("Redact")},
    {gIconApplyRedactions, CmdApplyRedactions, TrN("Apply Redactions")},
    {gIconAnnotStamp, CmdCreateAnnotStamp, TrN("Stamp")},
    {gIconAnnotCaret, CmdCreateAnnotCaret, TrN("Caret")},
    {gIconAnnotFileAttachment, CmdCreateAnnotFileAttachment, TrN("File Attachment")},
    {nullptr, 0, {}},
    {gIconUndo, CmdUndo, TrN("Undo")},
    {gIconRedo, CmdRedo, TrN("Redo")},
    {nullptr, 0, {}},
    {gIconFindAnnotation, CmdFindAnnotation, TrN("Find Annotation")},
    {nullptr, 0, {}},
    // the tooltip names the file, see ToolbarUpdateStateForWindow. Hovering it
    // opens a drop-down with the other two ways to end an editing session
    {gIconSave, CmdSaveAnnotations, TrN("Save changes to existing PDF")},
};

constexpr int kPdfAnnotationButtonsCount = dimof(gPdfAnnotationButtons);

// The built-in buttons actually on the toolbar, which is gToolbarButtons unless
// ToolbarCustomLayout asks for a different set / order (issue #5095). A layout
// can repeat a button, so allow for more than the default count.
constexpr int kMaxLayoutButtons = 64;
static ToolbarButtonInfo gLayoutButtons[kMaxLayoutButtons];
static int gLayoutButtonsCount = 0;
static Str gLayoutParsedFrom;
static bool gLayoutParsed = false;

// 128 should be more than enough
// we use static array so that we don't have to generate
// code for Vec<ToolbarButtonInfo>
constexpr int kMaxCustomButtons = 127;
// +1 to ensure there's always space for WarningsMsgId button
static ToolbarButtonInfo gCustomButtons[kMaxCustomButtons + 1];
static int gCustomButtonsCount = 0;

// Light theme ControlBackgroundColor is white, which is what the old themed
// rebar/toolbar painted. Other themes use their control background.
static Color TbBgColor() {
    return ThemeControlBackgroundColor();
}

Color TbTextColor() {
    if (IsCurrentThemeDefault() && !ThemeColorizeControls()) {
        return SysControlTextColor();
    }
    return ThemeWindowTextColor();
}

static Color TbDisabledColor() {
    if (ThemeUsesHighContrastColors()) return ThemeWindowTextDisabledColor();
    if (IsCurrentThemeDefault() && !ThemeColorizeControls()) {
        return SysDisabledTextColor();
    }
    Color fg = TbTextColor(), bg = TbBgColor();
    return MkRgb((GetRValue(fg) * 3 + GetRValue(bg)) / 4, (GetGValue(fg) * 3 + GetGValue(bg)) / 4,
                 (GetBValue(fg) * 3 + GetBValue(bg)) / 4);
}

static Color TbHoverColor() {
    return ThemeHotBackgroundColor();
}

// A ground a shade off the normal one, for telling two areas of a drop-down
// apart. Well short of the hover highlight, which is 20 units off: this is a
// cue, not something lit up.
static Color TbSubtleBgColor() {
    return AccentColor(TbBgColor(), 8);
}

static Color TbSelectedColor() {
    return ThemeHotBackgroundColor();
}

static Color TbEdgeColor() {
    return ThemeEdgeColor();
}

// Old Win32 toolbar: TBMETRICS.cyPad defaults to 6, then we added UiScalePx(2).
// TB_SETBUTTONSIZE cannot go below image + 2*cyPad, so that was the bar height.
static int ToolbarCyPad() {
    return UiScalePx(4);
}

static int ToolbarRowDy(int iconSize) {
    return std::max(iconSize + (2 * ToolbarCyPad()), PlatformFontLineHeight(GetAppFont()) * 2 + UiScalePx(4)) +
           UiScalePx(12);
}

static bool HasToolbarButtonContent(const ToolbarButtonInfo& tbi) {
    return tbi.icon || tbi.isText || !str::IsEmptyOrWhiteSpace(tbi.svgIcon);
}

static VirtHost* ToolbarHost(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    return tb ? tb->host : nullptr;
}

static VirtCtrl* ToolbarItemAt(MainWindow* win, int idx) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || idx < 0 || idx >= len(tb->items)) {
        return nullptr;
    }
    return tb->items[idx];
}

static VirtCtrl* PdfAnnotationToolbarItemAt(MainWindow* win, int idx) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || idx < 0 || idx >= len(tb->annotationItems)) {
        return nullptr;
    }
    return tb->annotationItems[idx];
}

// Includes disabled items (those are not hit-testable), so a click on a gray
// button is not treated as empty toolbar and does not start a window drag.
VirtCtrl* ToolbarItemFromPoint(MainWindow* win, Point pt) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return nullptr;
    }
    for (VirtCtrl* button : {tb->overflowButton, tb->annotationOverflowButton}) {
        if (button && button->IsVisible() && button->BoundsInWindow().Contains(pt)) return button;
    }
    for (ToolbarLine* line : {tb->mainRow, tb->annotationLine}) {
        if (!line || IsCollapsed(line) || (line == tb->annotationLine && IsCollapsed(tb->annotationRow))) continue;
        for (VirtCtrl* button : {line->previousButton, line->nextButton}) {
            if (button && button->IsVisible() && button->BoundsInWindow().Contains(pt)) return button;
        }
    }
    for (VirtCtrl* button : tb->pinnedItems) {
        if (button && button->IsVisible() && button->BoundsInWindow().Contains(pt)) return button;
    }
    for (VirtCtrl* w : tb->items) {
        if (!w || w->GetVisibility() != Visibility::Visible) {
            continue;
        }
        if (w->BoundsInWindow().Contains(pt)) {
            return w;
        }
    }
    for (VirtCtrl* w : tb->annotationItems) {
        if (!w || w->GetVisibility() != Visibility::Visible) {
            continue;
        }
        if (w->BoundsInWindow().Contains(pt)) {
            return w;
        }
    }
    if (tb->pageTotal && tb->pageTotal->GetVisibility() == Visibility::Visible &&
        tb->pageTotal->BoundsInWindow().Contains(pt)) {
        return tb->pageTotal;
    }
    if (tb->chapterTotal && tb->chapterTotal->GetVisibility() == Visibility::Visible &&
        tb->chapterTotal->BoundsInWindow().Contains(pt)) {
        return tb->chapterTotal;
    }
    return nullptr;
}

static void SetToolbarButtonEnabledByIdx(MainWindow* win, int idx, bool isEnabled) {
    VirtCtrl* w = ToolbarItemAt(win, idx);
    if (!w || w->IsEnabled() == isEnabled) {
        return;
    }
    w->SetIsEnabled(isEnabled);
    w->Invalidate();
}

static void SetPdfAnnotationButtonToolTipByIdx(MainWindow* win, int idx, Str tip) {
    VirtCtrl* w = PdfAnnotationToolbarItemAt(win, idx);
    if (w) {
        w->SetTooltip(tip);
    }
}

static void SetPdfAnnotationButtonEnabledByIdx(MainWindow* win, int idx, bool isEnabled) {
    VirtCtrl* w = PdfAnnotationToolbarItemAt(win, idx);
    if (!w || w->IsEnabled() == isEnabled) {
        return;
    }
    w->SetIsEnabled(isEnabled);
    w->Invalidate();
}

// true if the row has to be laid out again
static Str ToolbarItemName(int cmd) {
    return cmd == PageInfoId ? StrL("PageInfo") : GetCommandName(cmd);
}

static bool ToolbarItemHidden(int cmd) {
    if (!gSettings || !cmd) return false;
    Str name = ToolbarItemName(cmd);
    if (!len(name)) return false;
    StrVec names;
    Split(&names, gSettings->toolbarHiddenItems, StrL(" "), true);
    for (Str item : names)
        if (str::EqI(item, name)) return true;
    return false;
}

static void SetToolbarItemHidden(int cmd, bool hidden) {
    Str name = ToolbarItemName(cmd);
    if (!gSettings || len(name) == 0) return;
    StrVec names;
    Split(&names, gSettings->toolbarHiddenItems, StrL(" "), true);
    str::Builder saved;
    for (Str item : names) {
        if (str::EqI(item, name)) continue;
        if (len(saved)) saved.Append(StrL(" "));
        saved.Append(item);
    }
    if (hidden) {
        if (len(saved)) saved.Append(StrL(" "));
        saved.Append(name);
    }
    str::ReplaceWithCopy(&gSettings->toolbarHiddenItems, ToStrTemp(saved));
}

static bool SetPdfAnnotationButtonHiddenByIdx(MainWindow* win, int idx, bool isHidden) {
    VirtCtrl* w = PdfAnnotationToolbarItemAt(win, idx);
    if (!w) {
        return false;
    }
    Visibility want = isHidden ? Visibility::Collapse : Visibility::Visible;
    if (w->GetVisibility() == want) {
        return false;
    }
    w->SetVisibility(want);
    return true;
}

// hiding the page box hides the whole group (label + edit + " / N")
static bool SetToolbarButtonHiddenByIdx(MainWindow* win, int idx, bool isHidden) {
    VirtCtrl* w = ToolbarItemAt(win, idx);
    if (!w) {
        return false;
    }
    Visibility want = isHidden ? Visibility::Collapse : Visibility::Visible;
    if (w->GetVisibility() == want) {
        return false;
    }
    w->SetVisibility(want);
    ToolbarVirt* tb = win->toolbarVirt;
    if (w->id == CmdZoomIn && tb && tb->zoomEdit) tb->zoomEdit->SetVisibility(want);
    if (w->id == PageInfoId && tb) {
        if (tb->pageLabel) {
            tb->pageLabel->SetVisibility(want);
        }
        if (win->pageEdit) {
            win->pageEdit->SetVisibility(want);
        }
        if (tb->pageTotal) {
            tb->pageTotal->SetVisibility(want);
        }
        // chapter widgets stay collapsed unless the doc has chapters;
        // UpdateToolbarPageText() narrows this further right after
        if (win->chapterEdit) {
            win->chapterEdit->SetVisibility(want);
        }
        if (tb->chapterTotal) {
            tb->chapterTotal->SetVisibility(want);
        }
    }
    return true;
}

static void SetToolbarButtonCheckedByIdx(MainWindow* win, int idx, bool isChecked) {
    // a custom button with ToolbarText is a VirtButton, which has no
    // checked state (and is not a VirtIconButton)
    auto* ib = AsVirtIconButton(ToolbarItemAt(win, idx));
    if (!ib || ib->isSelected == isChecked) {
        return;
    }
    ib->isSelected = isChecked;
    ib->Invalidate();
}

// Work out which built-in buttons the toolbar has, and in which order. Empty
// ToolbarCustomLayout (the default) means the standard layout; otherwise the
// setting lists the buttons the user wants: a command name puts that button
// there, `|` a separator, `PageInfo` the page number box, and leaving a button
// out is how you hide it (issue #5095).
static void PopulateToolbarLayout() {
    Str setting = gSettings->toolbarCustomLayout;
    if (gLayoutParsed && str::Eq(setting, gLayoutParsedFrom)) {
        return;
    }
    str::Free(gLayoutParsedFrom);
    gLayoutParsedFrom = str::Dup(setting);
    gLayoutParsed = true;
    gLayoutButtonsCount = 0;

    auto addButton = [](const ToolbarButtonInfo& tbi) {
        if (gLayoutButtonsCount < kMaxLayoutButtons) {
            gLayoutButtons[gLayoutButtonsCount++] = tbi;
        }
    };
    auto useDefaultLayout = [&addButton]() {
        for (const ToolbarButtonInfo& tbi : gToolbarButtons) {
            if (tbi.cmdId == CmdGoToHomePage) {
                break;
            }
            addButton(tbi);
        }
    };

    if (str::IsEmptyOrWhiteSpace(setting)) {
        useDefaultLayout();
        return;
    }

    // commas and semicolons are a natural way to write a list, so accept them
    TempStr normalized = str::ReplaceTemp(setting, StrL(","), StrL(" "));
    normalized = str::ReplaceTemp(normalized, StrL(";"), StrL(" "));
    StrVec names;
    Split(&names, normalized, StrL(" "), true);
    for (Str name : names) {
        Str tok = name;
        str::TrimWSInPlace(tok, str::TrimOpt::Both);
        if (len(tok) == 0) {
            continue;
        }
        if (str::Eq(tok, StrL("|")) || str::EqI(tok, StrL("Separator"))) {
            addButton({nullptr, 0, {}});
            continue;
        }
        if (str::EqI(tok, StrL("PageInfo"))) {
            addButton({nullptr, PageInfoId, {}});
            continue;
        }
        int cmdId = GetCommandIdByName(tok);
        const ToolbarButtonInfo* found = nullptr;
        for (int i = 0; i < kButtonsCount && cmdId != CmdNone; i++) {
            if (gToolbarButtons[i].cmdId == cmdId) {
                found = &gToolbarButtons[i];
                break;
            }
        }
        if (!found) {
            logf("ToolbarCustomLayout: no built-in toolbar button for '%s'\n", tok);
            continue;
        }
        addButton(*found);
    }
    if (gLayoutButtonsCount == 0) {
        logf("ToolbarCustomLayout: nothing usable in '%s', using the standard layout\n", setting);
        useDefaultLayout();
    }
}

static int TotalButtonsCount() {
    return gLayoutButtonsCount + gCustomButtonsCount;
}

static ToolbarButtonInfo& GetToolbarButtonInfoByIdx(int idx) {
    if (idx < gLayoutButtonsCount) return gLayoutButtons[idx];
    return gCustomButtons[idx - gLayoutButtonsCount];
}

static int OriginalCommandId(int cmdId) {
    CustomCommand* cmd = FindCustomCommand(cmdId);
    return cmd ? cmd->origId : cmdId;
}

void SetToolbarButtonCheckedState(MainWindow* win, int cmdId, bool isChecked) {
    int originalCmdId = OriginalCommandId(cmdId);
    int n = TotalButtonsCount();
    for (int i = 0; i < n; i++) {
        const ToolbarButtonInfo& tbi = GetToolbarButtonInfoByIdx(i);
        if (OriginalCommandId(tbi.cmdId) == originalCmdId) {
            SetToolbarButtonCheckedByIdx(win, i, isChecked);
        }
    }
}

// some commands are only avialble in certain contexts
// we remove toolbar buttons for un-availalbe commands
static bool IsCmdAvailable(MainWindow* win, int cmdId, AppCommandCtx* ctx) {
    switch (cmdId) {
        case CmdZoomFitWidthAndContinuous:
        case CmdZoomFitPageAndSinglePage:
        case CmdRotateLeft:
        case CmdRotateRight:
            return !IsBrowserDocController(win->ctrl);
        case CmdFindFirst:
            // CHM has its own (WebView2/IE) find bar even though NeedsFindUI()
            // is false for it; show the Search button so it's reachable
            return NeedsFindUI(win) || IsBrowserDocController(win->ctrl);
        case CmdFindNext:
        case CmdFindPrev:
        case CmdFindToggleMatchCase:
        case CmdFindToggleMatchWholeWord:
            return NeedsFindUI(win);
        case CmdToggleReadAloud:
            // opt-in: the button and its drop-down only show if asked for
            return gSettings->toolbarShowReadAloud;
        case PageInfoId:
            return true;
    }
    // Toolbar buttons stay visible (but disabled) when no document is open, so
    // decide visibility as if a document were loaded; otherwise the no-document
    // gate in GetCommandVisibility would remove them. Document-type-specific
    // removals (e.g. for CHM/image collections) still apply when a real document
    // is loaded, and the enabled state is handled separately in IsCmdEnabled.
    bool savedLoaded = ctx->isDocLoaded;
    ctx->isDocLoaded = true;
    bool remove, disable;
    GetCommandIdState(ctx, cmdId, &remove, &disable);
    ctx->isDocLoaded = savedLoaded;
    return !remove;
}

static bool IsCmdEnabled(MainWindow* win, int cmdId, AppCommandCtx* ctx) {
    switch (cmdId) {
        case CmdCreateAnnotUnderline:
        case CmdCreateAnnotStrikeOut:
            return ctx->isDocLoaded && ctx->supportsAnnots;
        case CmdNextTab:
        case CmdPrevTab:
        case CmdNextTabSmart:
        case CmdPrevTabSmart:
            return SettingsUseTabs();
        case PageInfoId:
            return true;
    }

    bool remove, disable;
    GetCommandIdState(ctx, cmdId, &remove, &disable);
    if (remove || disable) {
        return false;
    }
    switch (cmdId) {
        case CmdOpenFile:
        case CmdOpenFileNoHistory:
            if (!CanAccessDisk()) {
                return false;
            }
            break;
        case CmdPrint:
            if (!HasPermission(Perm::PrinterAccess)) {
                return false;
            }
            break;
    }

    // if no file is open, only enable buttons for commands that don't require a document
    // (custom toolbar buttons use a custom command id, the original command decides)
    // https://github.com/sumatrapdfreader/sumatrapdf/issues/5657
    if (!win->IsDocLoaded()) {
        return CmdWorksWithoutDocument(OriginalCommandId(cmdId));
    }

    switch (cmdId) {
        case CmdOpenFile:
        case CmdOpenFileNoHistory:
            // opening different files isn't allowed in plugin mode
            return !gPluginMode;

#ifndef DISABLE_DOCUMENT_RESTRICTIONS
        case CmdPrint:
            return !win->AsFixed() || win->AsFixed()->GetEngine()->AllowsPrinting();
#endif

        case CmdFindFirst:
            return NeedsFindUI(win) || IsBrowserDocController(win->ctrl);

        case CmdFindNext:
        case CmdFindPrev: {
            // Need non-empty find text (findEdit is the active bar or floating window edit).
            if (CbGetTextLen(win->findEdit) == 0) {
                return false;
            }
            // When we already know there are zero matches, disable next/prev.
            // Unknown count (scan pending / not started) still allows searching.
            if (win->ctrl && win->ctrl->CanFindInPage()) {
                if (win->browserFindTotal == 0) {
                    return false;
                }
                return true;
            }
            if (win->findCountValid && len(win->findCountPositions) == 0) {
                return false;
            }
            return true;
        }

        case CmdGoToNextPage:
            return win->ctrl->CurrentPageNo() < win->ctrl->PageCount();
        case CmdGoToPrevPage:
            return win->ctrl->CurrentPageNo() > 1;

        case CmdNavigateBack:
            return win->ctrl->CanNavigate(-1);
        case CmdNavigateForward:
            return win->ctrl->CanNavigate(1);

        default:
            return true;
    }
}

static TempStr ToolbarTipTemp(int cmdId, Str tip, bool translate) {
    TempStr s = translate ? trans::GetTranslation(tip) : TempStr(tip);
    TempStr accelStr = AppendAccelKeyToMenuStringTemp({}, cmdId);
    if (accelStr) {
        Str accel = accelStr.len > 1 ? Str(accelStr.s + 1, accelStr.len - 1) : accelStr;
        s = str::JoinTemp(s, fmt(" (%s)", accel));
    }
    return s;
}

void UpdateToolbarButtonsToolTipsForWindow(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return;
    }
    for (int i = 0; i < gLayoutButtonsCount; i++) {
        const ToolbarButtonInfo& bi = gLayoutButtons[i];
        if (len(bi.toolTip) == 0 || bi.isText) {
            continue;
        }
        VirtCtrl* w = ToolbarItemAt(win, i);
        if (w) {
            w->SetTooltip(ToolbarTipTemp(bi.cmdId, bi.toolTip, true));
        }
    }
    for (int i = 0; i < kPdfAnnotationButtonsCount; i++) {
        const ToolbarButtonInfo& bi = gPdfAnnotationButtons[i];
        if (len(bi.toolTip) == 0) {
            continue;
        }
        VirtCtrl* w = PdfAnnotationToolbarItemAt(win, i);
        if (w) {
            w->SetTooltip(ToolbarTipTemp(bi.cmdId, bi.toolTip, true));
        }
    }
}

static void SetToolbarButtonImageByIdx(MainWindow* win, int idx, const char* icon) {
    VirtCtrl* w = ToolbarItemAt(win, idx);
    if (!w) {
        return;
    }
    auto* ib = AsVirtIconButton(w);
    if (!ib) {
        return;
    }
    ToolbarVirt* tb = win->toolbarVirt;
    int sz = tb ? tb->iconSize : UiScalePx(gSettings->toolbarSize);
    Pixmap* px = GetCachedPixmapForSvg(Str(icon), sz, sz, TbTextColor());
    Pixmap* pxOff = GetCachedPixmapForSvg(Str(icon), sz, sz, TbDisabledColor());
    if (ib->pixmap == px && ib->pixmapDisabled == pxOff) {
        return;
    }
    ib->pixmap = px;
    ib->pixmapDisabled = pxOff;
    ib->Invalidate();
}

static void SetToolbarButtonToolTipByIdx(MainWindow* win, int idx, int cmdId, Str s) {
    VirtCtrl* w = ToolbarItemAt(win, idx);
    if (!w) {
        return;
    }
    w->SetTooltip(ToolbarTipTemp(cmdId, s, false));
}

static void SetPdfAnnotationsToolbarVisible(MainWindow* win, bool visible) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || len(tb->annotationGroups) == 0) {
        return;
    }
    Visibility want = visible ? Visibility::Visible : Visibility::Collapse;
    if (tb->annotationExpanded == visible) {
        return;
    }
    tb->mainRow->Restore();
    tb->annotationExpanded = visible;
    for (auto* group : tb->annotationGroups) group->SetVisibility(want);
    SetToolbarButtonCheckedState(win, CmdToggleEditPDF, visible);
    ToolbarSetHeight(win, tb->rowDy);
    tb->host->Relayout();
    tb->host->Invalidate(true);
    if (visible) {
        StartLoadingAnnotationsForUi(win->CurrentTab());
        RefreshAnnotFilterAnnotations(win);
    }
    ScheduleUiUpdate(win, kUiForceRelayout | kUiToolbarDirty);
}

// TODO: this is called too often
// TODO: also set checked state instead of calling SetToolbarButtonCheckedState() all over
void ToolbarUpdateStateForWindow(MainWindow* win, bool setButtonsVisibility) {
    if (win->toolbarVirt && win->toolbarVirt->mainRow) win->toolbarVirt->mainRow->Restore();
    if (win->toolbarVirt && win->toolbarVirt->annotationLine) win->toolbarVirt->annotationLine->Restore();
    int n = TotalButtonsCount();
    bool visibilityChanged = false;
    // One command ctx for the whole pass. Building it per button used to call
    // HasToc() (and page hit-testing) once per toolbar item during load.
    auto* ctx = NewBuildMenuCtx(win->CurrentTab(), Point{0, 0});
    AutoCall delCtx(DeleteBuildMenuCtx, ctx);
    for (int i = 0; i < n; i++) {
        auto& tb = GetToolbarButtonInfoByIdx(i);
        int cmdId = tb.cmdId;
        // cmdId 0 is a separator; GetCommandVisibility treats 0 as Hide, but
        // separators are always drawn. Which ones to drop is decided below,
        // by position, not by command availability.
        if (setButtonsVisibility && cmdId != WarningMsgId && cmdId != 0) {
            bool hide = !IsCmdAvailable(win, cmdId, ctx) || ToolbarItemHidden(cmdId);
            visibilityChanged |= SetToolbarButtonHiddenByIdx(win, i, hide);
        }
        if (!HasToolbarButtonContent(tb)) {
            continue;
        }
        bool isEnabled = IsCmdEnabled(win, cmdId, ctx);
        SetToolbarButtonEnabledByIdx(win, i, isEnabled);

        if (cmdId == CmdToggleReadAloud || cmdId == CmdPauseReadAloud) {
            bool speaking = TtsIsSpeaking();
            SetToolbarButtonImageByIdx(win, i, speaking ? gIconPauseSpeaking : gIconSpeak);
            // tooltip reflects what clicking the button will do
            Str tip = Tr("Read Aloud");
            if (speaking) {
                tip = Tr("Pause Reading");
            } else if (CanContinueReadAloud(win->CurrentTab())) {
                tip = Tr("Continue Reading");
            }
            SetToolbarButtonToolTipByIdx(win, i, cmdId, tip);
        }
    }

    if (win->toolbarVirt) {
        auto* presets = gSettings->pinnedAnnotationTools;
        for (int i = 0; presets && i < len(win->toolbarVirt->pinnedItems) && i < len(*presets); i++)
            win->toolbarVirt->pinnedItems[i]->SetIsEnabled(
                IsCmdEnabled(win, PinnedToolCommand((*presets)[i]->tool), ctx));
        if (Edit* zoom = win->toolbarVirt->zoomEdit) {
            zoom->SetIsEnabled(win->IsDocLoaded());
            if (GetFocus() != zoom->hwnd && win->IsDocLoaded())
                zoom->SetText(fmt("%.1f%%", win->ctrl->GetZoomVirtual(true)));
        }
    }
    SetToolbarButtonCheckedState(win, CmdCreateAnnotInk, IsPlacingInkAnnotation(win) && win->inkEraseMode == 0);
    SetToolbarButtonCheckedState(win, CmdInkEraser, win->inkEraseMode == 1);
    SetToolbarButtonCheckedState(win, CmdToggleLaserPointer, IsLaserPointerActive(win));
    SetToolbarButtonCheckedState(win, CmdHandTool, win->handTool);
    SetToolbarButtonCheckedState(win, CmdAnnotationLasso, win->annotationLasso.active);

    bool showPdfAnnotationsToolbar = win->pdfAnnotationsToolbarEnabled && ctx->isPdf && ctx->supportsAnnots;
    SetPdfAnnotationsToolbarVisible(win, showPdfAnnotationsToolbar);
    // a placement mode (ink, shape, highlighter...) owns the page until it ends
    bool annotButtonsEnabled = showPdfAnnotationsToolbar;
    bool annotVisibilityChanged = false;
    for (int i = 0; i < kPdfAnnotationButtonsCount; i++) {
        const ToolbarButtonInfo& bi = gPdfAnnotationButtons[i];
        if (!HasToolbarButtonContent(bi)) {
            continue;
        }
        CommandVisibility v = GetCommandVisibility(bi.cmdId, *ctx, CommandSurface::Toolbar);
        bool remove = CommandShouldRemove(v) || ToolbarItemHidden(bi.cmdId);
        annotVisibilityChanged |= SetPdfAnnotationButtonHiddenByIdx(win, i, remove);
        SetPdfAnnotationButtonEnabledByIdx(win, i, annotButtonsEnabled && !CommandShouldDisable(v) && !remove);
        if (bi.cmdId == CmdSaveAnnotations) {
            // name the file it writes to, like the annotation list's Save button
            WindowTab* tab = win->CurrentTab();
            TempStr base = tab ? path::GetBaseNameTemp(tab->filePath) : TempStr{};
            Str tip = Tr("Save changes to existing PDF");
            if (len(base) > 0) {
                tip = fmt(Tr("Save changes to %s").s, base);
            }
            SetPdfAnnotationButtonToolTipByIdx(win, i, ToolbarTipTemp(bi.cmdId, tip, false));
        }
    }

    if (setButtonsVisibility) {
        // drop a separator that would sit next to another, or at either end
        // (Read Aloud is hidden by default, which would otherwise leave ||)
        bool prevVisibleNonSep = false;
        int lastSep = -1;
        for (int i = 0; i < n; i++) {
            const ToolbarButtonInfo& bi = GetToolbarButtonInfoByIdx(i);
            VirtCtrl* w = ToolbarItemAt(win, i);
            if (!w) {
                continue;
            }
            if (bi.cmdId == 0) {
                bool hide = !prevVisibleNonSep;
                visibilityChanged |= SetToolbarButtonHiddenByIdx(win, i, hide);
                prevVisibleNonSep = false;
                if (!hide) {
                    lastSep = i;
                }
                continue;
            }
            // the page box counts as visible content: a separator right after
            // it is not a leading one (ToolbarCustomLayout = PageInfo | ...)
            if (w->GetVisibility() == Visibility::Visible) {
                prevVisibleNonSep = true;
                lastSep = -1;
            }
        }
        if (lastSep >= 0) {
            visibilityChanged |= SetToolbarButtonHiddenByIdx(win, lastSep, true);
        }
    }

    if (visibilityChanged || annotVisibilityChanged || (win->toolbarVirt && win->toolbarVirt->mainRow)) {
        VirtHost* host = ToolbarHost(win);
        if (host) {
            if (host->vroot) {
                host->vroot->RequestLayout();
            }
            host->Relayout();
            host->Invalidate(true);
        }
    }

    // reposition the floating find bar over the search icon (and hide it if the
    // current document doesn't support find) when toolbar buttons change
    if (setButtonsVisibility) {
        UpdateToolbarFindText(win);
    }

    // update dirty (unsaved annotations) flag and tooltip on each tab
    if (win->tabsCtrl) {
        int nTabs = win->TabCount();
        for (int i = 0; i < nTabs; i++) {
            WindowTab* tab = win->GetTab(i);
            bool dirty = false;
            if (tab && tab->AsFixed()) {
                dirty = EngineHasUnsavedAnnotations(tab->AsFixed()->GetEngine());
            }
            // update tooltip before SetTabDirty (which rebuilds tooltips via LayoutTabs).
            // Must use MakeTabTooltipTemp (path+size); path-only overwrote size here.
            TabInfo* ti = win->tabsCtrl->GetTab(i);
            if (ti && tab && tab->filePath && (ti->isDirty != dirty || len(ti->tooltip) == 0)) {
                TempStr tooltip = MakeTabTooltipTemp(tab->filePath, dirty);
                str::ReplaceWithCopy(&ti->tooltip, tooltip);
            }
            win->tabsCtrl->SetTabDirty(i, dirty);
        }
    }
}

void SetToolbarButtonEnableState(MainWindow* win, int cmdId, bool isEnabled) {
    int originalCmdId = OriginalCommandId(cmdId);
    int n = TotalButtonsCount();
    for (int i = 0; i < n; i++) {
        const ToolbarButtonInfo& tbi = GetToolbarButtonInfoByIdx(i);
        if (OriginalCommandId(tbi.cmdId) == originalCmdId) {
            SetToolbarButtonEnabledByIdx(win, i, isEnabled);
        }
    }
    for (int i = 0; i < kPdfAnnotationButtonsCount; i++) {
        if (gPdfAnnotationButtons[i].cmdId == originalCmdId) {
            SetPdfAnnotationButtonEnabledByIdx(win, i, isEnabled);
        }
    }
}

static void SetPdfAnnotationsToolbarEnabled(MainWindow* win, bool enabled) {
    if (!win) {
        return;
    }
    AppCommandCtx ctx = NewAppCommandCtx(win);
    if (!ctx.isPdf || !ctx.supportsAnnots) {
        return;
    }
    if (win->pdfAnnotationsToolbarEnabled == enabled) {
        return;
    }
    if (win->pdfAnnotationsToolbarEnabled) {
        FinishInkAnnotationPlacement(win);
        // a half-placed line / shape / stamp is editing UI too: its notification
        // and cross cursor would outlive the mode it belongs to
        CancelAnnotationPlacement(win);
    }
    win->pdfAnnotationsToolbarEnabled = enabled;
    ToolbarUpdateStateForWindow(win, true);
    if (enabled) {
        RemoveNotificationsForGroup(win->hwndCanvas, kNotifAnnotation);
        UpdateAnnotationHoverOverlay(win);
    } else {
        // leaving the mode leaves no editing UI behind: without this the
        // selection marker and its resize handles stay painted on the page
        WindowTab* tab = win->CurrentTab();
        if (tab && tab->selectedAnnotation) {
            SetSelectedAnnotation(tab, nullptr);
        }
        HideAnnotationHoverOverlay(win);
        HideAnnotEditToolbar(win);
    }
    ScheduleRepaint(win, 0);
}

void TogglePdfAnnotationsToolbar(MainWindow* win) {
    if (!win) {
        return;
    }
    SetPdfAnnotationsToolbarEnabled(win, !win->pdfAnnotationsToolbarEnabled);
}

void EnablePdfAnnotationsToolbar(MainWindow* win) {
    SetPdfAnnotationsToolbarEnabled(win, true);
}

// toolbar mode for this window: Fullscreen.Toolbar in fullscreen, else Toolbar
static int ToolbarModeForWindow(MainWindow* win) {
    if (win->isFullScreen) {
        return FullscreenToolbarModeFromPrefs();
    }
    return ToolbarModeFromPrefs();
}

bool ShouldShowToolbar(MainWindow* win) {
    if (win->presentation || win->isQuickLook) {
        return false;
    }
    int mode = ToolbarModeForWindow(win);
    return mode == kToolbarShow;
}

bool ShouldOverlayToolbar(MainWindow* win) {
    if (win->presentation || win->isQuickLook) {
        return false;
    }
    if (ToolbarModeForWindow(win) != kToolbarOverlay) {
        return false;
    }
    // don't float the overlay toolbar over the home / about page (only the
    // pinned "show" mode shows a toolbar there)
    if (win->IsCurrentTabAbout()) {
        return false;
    }
    return true;
}

// natural width of the toolbar content (buttons + page box); the find bar
// floats separately so the page-total label is the rightmost element
static int ToolbarNaturalWidth(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    VirtHost* host = ToolbarHost(win);
    if (!host || !host->layout) {
        return 0;
    }
    tb->mainRow->Restore();
    int dx = tb->mainRow->HBox::MinIntrinsicWidth(tb->rowDy);
    host->Relayout();
    if (dx <= 0) {
        dx = tb->rowDy * 8;
    }
    return dx + UiScalePx(12);
}

// when the overlay toolbar sits at the bottom, lift it above the horizontal
// scrollbar so it doesn't cover it. The height is reserved even when the
// scrollbar isn't currently visible, so the toolbar's position is stable.
static int OverlayToolbarBottomScrollbarOffset() {
    if (ScrollbarsAreHidden()) {
        return 0;
    }
    if (ScrollbarsUseOverlay()) {
        // smart/overlay: the thick overlay scrollbar height (see OverlayScrollbarCreate)
        return UiScalePx(16);
    }
    return UiHScrollbarDy();
}

// rectangle (frame-client coords) the overlay toolbar occupies when shown
static Rect OverlayToolbarRect(MainWindow* win) {
    Rect canvas = ToolbarCanvasRectInFrame(win);
    int natW = ToolbarNaturalWidth(win);
    if (natW <= 0 || natW > canvas.dx) {
        natW = canvas.dx;
    }
    int h = ToolbarHost(win)->ScreenRect().dy;
    int x = canvas.x + ((canvas.dx - natW) / 2);
    int y = canvas.y;
    if (ToolbarAtBottom()) {
        y = canvas.y + canvas.dy - h - OverlayToolbarBottomScrollbarOffset();
    }
    return {x, y, natW, h};
}

// position/show the floating overlay toolbar; called on relayout and mouse move
void PositionOverlayToolbar(MainWindow* win) {
    VirtHost* host = ToolbarHost(win);
    if (!win->isToolbarOverlay || !host) {
        return;
    }
    Rect r = OverlayToolbarRect(win);
    host->SetPos(r, win->toolbarOverlayShown);
    if (!win->toolbarOverlayShown) {
        ToolbarRepaintUncovered(win, r);
    }
}

// whether the cursor is currently in the reveal band or over the toolbar
static bool OverlayToolbarShouldShowForCursor(MainWindow* win) {
    Point pt = UiCursorScreenPos();
    Point ptFrame = ToolbarScreenToFrame(win, pt);

    Rect tb = OverlayToolbarRect(win);
    // reveal band: spans the full canvas width so the toolbar also appears when
    // the mouse is to the left or right of it, and extends a bit past the
    // toolbar (toward the page) so it shows before the cursor reaches it
    Rect canvas = ToolbarCanvasRectInFrame(win);
    int my = UiScalePx(16);
    int bandY = ToolbarAtBottom() ? (tb.y - my) : tb.y;
    Rect band(canvas.x, bandY, canvas.dx, tb.dy + my);
    bool inBand = band.Contains(Point(ptFrame.x, ptFrame.y));

    // also keep shown while the cursor is over the toolbar window itself
    return inBand || ToolbarHost(win)->ContainsScreenPoint(pt);
}

// the overlay toolbar must not vanish while it owns the keyboard focus (e.g.
// the user is typing a page number into the page box after Ctrl+G)
static bool OverlayToolbarHasFocus(MainWindow* win) {
    VirtHost* host = ToolbarHost(win);
    return host && host->HasFocus();
}

static void CancelOverlayHide(MainWindow* win) {
    if (win->toolbarOverlayHidePending) {
        ToolbarHost(win)->KillTimer(kHideOverlayToolbarTimerId);
        win->toolbarOverlayHidePending = false;
    }
}

static void ScheduleOverlayHide(MainWindow* win) {
    if (win->toolbarOverlayHidePending) {
        return; // already scheduled; don't keep pushing it out on every move
    }
    win->toolbarOverlayHidePending = true;
    ToolbarHost(win)->SetTimer(kHideOverlayToolbarTimerId, kDelayToolbarHide);
}

static void SetOverlayShown(MainWindow* win, bool shown) {
    if (shown == win->toolbarOverlayShown) {
        return;
    }
    win->toolbarOverlayShown = shown;
    PositionOverlayToolbar(win);
}

// re-evaluate overlay toolbar visibility based on the cursor's screen position
void UpdateOverlayToolbarForMouse(MainWindow* win) {
    if (!win->isToolbarOverlay || !ToolbarHost(win)) {
        return;
    }
    bool show = OverlayToolbarShouldShowForCursor(win) || OverlayToolbarHasFocus(win);
    if (show) {
        CancelOverlayHide(win);
        SetOverlayShown(win, true);
    } else if (win->toolbarOverlayShown) {
        // don't hide immediately; give the user kDelayToolbarHide to come back
        ScheduleOverlayHide(win);
    }
}

// reveal the overlay toolbar right now, without waiting for the cursor to enter
// the reveal band. Used by commands that drive the toolbar from the keyboard
// (Ctrl+G): the toolbar stays up while it has the focus and auto-hides once the
// focus and the cursor are away from it.
void RevealOverlayToolbar(MainWindow* win) {
    if (!win->isToolbarOverlay || !ToolbarHost(win)) {
        return;
    }
    CancelOverlayHide(win);
    SetOverlayShown(win, true);
}

// the delayed-hide timer fired on the toolbar's own host
static void OnHoverDropdownTimer(MainWindow* win, int timerId);

static void OnToolbarTimer(MainWindow* win, int timerId) {
    if (timerId == kOpenHoverDropdownTimerId || timerId == kCloseHoverDropdownTimerId) {
        OnHoverDropdownTimer(win, timerId);
        return;
    }
    if (timerId != kHideOverlayToolbarTimerId) {
        return;
    }
    win->toolbarOverlayHidePending = false;
    ToolbarHost(win)->KillTimer(kHideOverlayToolbarTimerId);
    if (!win->isToolbarOverlay) {
        return;
    }
    // if the cursor came back near the top while the timer was pending, keep
    // the toolbar shown; otherwise hide it now
    if (OverlayToolbarShouldShowForCursor(win) || OverlayToolbarHasFocus(win)) {
        SetOverlayShown(win, true);
    } else {
        SetOverlayShown(win, false);
    }
}

void ShowOrHideToolbar(MainWindow* win) {
    bool show = ShouldShowToolbar(win);
    bool overlay = ShouldOverlayToolbar(win);
    if (show == win->isToolbarVisible && overlay == win->isToolbarOverlay) {
        return;
    }
    bool enteredOverlay = overlay && !win->isToolbarOverlay;
    win->isToolbarVisible = show;
    win->isToolbarOverlay = overlay;
    if (!overlay) {
        CancelOverlayHide(win);
        win->toolbarOverlayShown = false;
    }
    if (enteredOverlay) {
        // reveal immediately on entering overlay mode (e.g. via F8) so the
        // change is visible; it auto-hides after kDelayToolbarHide
        win->toolbarOverlayShown = true;
    }
    if (!show && !overlay) {
        // Move the focus out of the toolbar
        if ((win->findEdit && win->findEdit->IsFocused()) || (win->pageEdit && win->pageEdit->IsFocused()) ||
            (win->chapterEdit && win->chapterEdit->IsFocused())) {
            ToolbarFocusFrame(win);
        }
        if (win->hwndToolbar) {
            ShowWindow(win->hwndToolbar, SW_HIDE);
        }
    }
    // overlay <-> hide does not flip isToolbarVisible, so RelayoutFrame would
    // skip without this (sidebar stays at the overlay y, toolbar HWND stays)
    ScheduleUiUpdate(win, kUiForceRelayout | kUiRelayout);
    if (enteredOverlay) {
        ScheduleOverlayHide(win);
    }
}

void UpdateFindbox(MainWindow* win) {
    VirtHost* host = ToolbarHost(win);
    if (host) {
        host->Invalidate(true);
        if (ToolbarFrameIsVisible(win)) {
            host->Repaint();
        }
    }
    ToolbarUpdateFindEditCursor(win);
}

// the find UI is now a floating Chrome-style bar (see FindBar.cpp). When the
// toolbar moves/resizes we keep the bar centered over the search icon.
void UpdateToolbarFindText(MainWindow* win) {
    FindBarReposition(win);
}

static void UpdateZoomHoverDropdown(MainWindow* win);

void UpdateToolbarState(MainWindow* win) {
    if (!win->IsDocLoaded()) {
        return;
    }
    // the zoom buttons' strip may be up: the zoom just moved under it
    UpdateZoomHoverDropdown(win);
    DisplayMode dm = win->ctrl->GetDisplayMode();
    float zoomVirtual = win->ctrl->GetZoomVirtual();
    {
        bool isChecked = dm == DisplayMode::Continuous && zoomVirtual == kZoomFitWidth;
        SetToolbarButtonCheckedState(win, CmdZoomFitWidthAndContinuous, isChecked);
    }
    {
        bool isChecked = dm == DisplayMode::SinglePage && zoomVirtual == kZoomFitPage;
        SetToolbarButtonCheckedState(win, CmdZoomFitPageAndSinglePage, isChecked);
        if (!isChecked) {
            win->CurrentTab()->prevZoomVirtual = kInvalidZoom;
        }
    }
}

void UpdateToolbarPageText(MainWindow* win, int pageCount, bool updateOnly) {
    VirtHost* host = ToolbarHost(win);
    if (!host) {
        return;
    }
    ToolbarVirt* tb = win->toolbarVirt;
    bool hadOverflow = len(tb->overflowItems) > 0;
    if (tb->mainRow) tb->mainRow->Restore();
    if (!tb->pageTotal) {
        return;
    }

    bool hasChapters = ShowChapterUi(win->ctrl);
    if (tb->pageLabel) {
        bool compact = !hasChapters && str::IsEmptyOrWhiteSpace(gSettings->toolbarCustomLayout);
        tb->pageLabel->SetText(compact ? Str{} : hasChapters ? Tr("Chapter:") : Tr("Page:"));
        tb->pageLabel->padding =
            compact ? Insets{} : Insets{0, UiScalePx(kTextPaddingRight + kButtonSpacingX), 0, UiScalePx(4)};
    }
    Visibility chapterVis = hasChapters && !ToolbarItemHidden(PageInfoId) ? Visibility::Visible : Visibility::Collapse;
    bool chapterVisChanged = false;
    if (win->chapterEdit && win->chapterEdit->GetVisibility() != chapterVis) {
        win->chapterEdit->SetVisibility(chapterVis);
        chapterVisChanged = true;
    }
    if (tb->chapterTotal && tb->chapterTotal->GetVisibility() != chapterVis) {
        tb->chapterTotal->SetVisibility(chapterVis);
        chapterVisChanged = true;
    }
    if (tb->pageLabel2 && tb->pageLabel2->GetVisibility() != chapterVis) {
        tb->pageLabel2->SetVisibility(chapterVis);
        chapterVisChanged = true;
    }
    if (chapterVisChanged) {
        host->Relayout();
    }

    TempStr txt;
    if (-1 == pageCount || !pageCount) {
        txt = StrL(" ");
    } else if (hasChapters) {
        int chapter = win->ctrl->CurrentLocation().chapter;
        txt = fmt(" / %d", win->ctrl->ChapterPageCount(chapter));
        if (tb->chapterTotal) {
            tb->chapterTotal->SetText(fmt(" / %d", win->ctrl->ChapterCount()));
        }
    } else if (!win->ctrl || !win->ctrl->HasPageLabels()) {
        txt = fmt(" / %d", pageCount);
    } else {
        int logical = pageCount;
        DisplayModel* dm = win->ctrl->AsFixed();
        if (dm) {
            logical = dm->LogicalPageCount();
        }
        if (logical > 0 && logical != pageCount) {
            txt = fmt(" / %d (%d / %d)", logical, win->ctrl->CurrentPageNo(), pageCount);
        } else {
            txt = fmt("%d / %d", win->ctrl->CurrentPageNo(), pageCount);
        }
    }
    if (!hadOverflow && updateOnly && tb->pageTotal->s && txt && str::Eq(tb->pageTotal->s, txt)) {
        return;
    }
    tb->pageTotal->SetText(txt);
    host->Relayout();
    host->Invalidate(true);
}

static TempStr ShortcutToolbarToolTipTemp(Shortcut* shortcut) {
    if (!str::IsEmptyOrWhiteSpace(shortcut->name)) {
        return shortcut->name;
    }
    CustomCommand* cmd = FindCustomCommand(shortcut->cmdId);
    if (cmd && cmd->name) {
        return cmd->name;
    }
    int origId = cmd ? cmd->origId : shortcut->cmdId;
    if (origId > 0 && origId < CmdLast) {
        Str desc = GetCommandDescription(origId);
        if (desc) {
            return desc;
        }
    }
    return shortcut->cmd;
}

static TempStr CustomCommandToolbarToolTipTemp(CustomCommand* cmd, Str fallback) {
    if (cmd && !str::IsEmptyOrWhiteSpace(cmd->name)) {
        return cmd->name;
    }
    if (!str::IsEmptyOrWhiteSpace(fallback)) {
        return fallback;
    }
    return StrL("External Viewer");
}

static void PopulateCustomToolbarButtons() {
    gCustomButtonsCount = 0;
    for (Shortcut* shortcut : *gSettings->shortcuts) {
        if (gCustomButtonsCount >= kMaxCustomButtons) {
            break;
        }
        if (!str::IsEmptyOrWhiteSpace(shortcut->toolbarSvgIcon)) {
            ToolbarButtonInfo tbi;
            tbi.cmdId = shortcut->cmdId;
            tbi.svgIcon = shortcut->toolbarSvgIcon;
            tbi.toolTip = ShortcutToolbarToolTipTemp(shortcut);
            gCustomButtons[gCustomButtonsCount++] = tbi;
            continue;
        }
        if (!str::IsEmptyOrWhiteSpace(shortcut->toolbarText)) {
            ToolbarButtonInfo tbi;
            tbi.cmdId = shortcut->cmdId;
            tbi.toolTip = shortcut->toolbarText;
            tbi.isText = true;
            gCustomButtons[gCustomButtonsCount++] = tbi;
        }
    }

    // add toolbar buttons from custom commands with toolbar settings (e.g. ExternalViewers).
    // gFirstCustomCommand is a prepend-only list, so walking it directly yields
    // the commands in reverse creation order and the buttons would show up in
    // the reverse of the order the user listed them in (#5869)
    Vec<CustomCommand*> customCmds;
    for (auto* cc = gFirstCustomCommand; cc; cc = cc->next) {
        VecAppend(customCmds, cc);
    }
    VecReverse(customCmds);
    for (CustomCommand* cc : customCmds) {
        if (gCustomButtonsCount >= kMaxCustomButtons) {
            break;
        }
        Str svgIcon = GetCommandStringArg(cc, kCmdArgToolbarSvgIcon, {});
        Str tbText = GetCommandStringArg(cc, kCmdArgToolbarText, {});
        if (!str::IsEmptyOrWhiteSpace(svgIcon)) {
            ToolbarButtonInfo tbi;
            tbi.cmdId = cc->id;
            tbi.svgIcon = svgIcon;
            tbi.toolTip = CustomCommandToolbarToolTipTemp(cc, tbText);
            gCustomButtons[gCustomButtonsCount++] = tbi;
            continue;
        }
        if (str::IsEmptyOrWhiteSpace(tbText)) {
            continue;
        }
        ToolbarButtonInfo tbi;
        tbi.cmdId = cc->id;
        tbi.toolTip = tbText;
        tbi.isText = true;
        gCustomButtons[gCustomButtonsCount++] = tbi;
    }
}

int ToolbarIconSize(int dpi) {
    if (dpi <= 0) dpi = DpiGet();
    return RoundUp(UiScalePxForDpi(dpi, gSettings->toolbarSize), 4);
}

static void ApplyToolbarItemColors(VirtCtrl* w) {
    Color hover = TbHoverColor();
    Color sel = TbSelectedColor();
    if (auto* ib = AsVirtIconButton(w)) {
        if (w->id == CmdThemeLight) ib->isSelected = IsLightColor(ThemeWindowBackgroundColor());
        if (w->id == CmdThemeDark) ib->isSelected = !IsLightColor(ThemeWindowBackgroundColor());
        if (w->id == CmdInvertColors) ib->isSelected = GetInvertPageColors();
        ib->cornerRadius = UiScalePx(8);
        ib->backgroundColor = w->id == CmdOpenFile ? ThemeBrandColor() : kColorTransparent;
        if (w->id == CmdOpenFile) {
            hover = AccentColor(ThemeBrandColor(), 12);
        }
        ib->SetColor(kColIconBtnBgHover, hover);
        ib->SetColor(kColIconBtnBgSelected, sel);
        ib->SetColor(kColIconBtnChevron, w->id == CmdOpenFile ? ThemeBrandTextColor() : TbTextColor());
        ib->SetColor(kColIconBtnChevronDisabled, TbDisabledColor());
        return;
    }
    if (auto* b = AsVirtButton(w)) {
        b->cornerRadius = UiScalePx(8);
        // a toolbar button is a label that highlights on hover, not a box
        b->SetColor(kColBtnBg, kColorTransparent);
        b->SetColor(kColBtnBorder, kColorTransparent);
        b->SetColor(kColBtnBgHover, hover);
        b->SetColor(kColBtnText, TbTextColor());
        b->SetColor(kColBtnTextDisabled, TbDisabledColor());
        return;
    }
    if (auto* t = AsVirtText(w)) {
        t->SetColor(kColText, TbTextColor());
        return;
    }
    if (auto* line = AsVirtLine(w)) {
        line->SetColor(kColLineFg, TbEdgeColor());
    }
}

static void RefreshToolbarIcons(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (!tb) {
        return;
    }
    int sz = tb->iconSize;
    Color fg = TbTextColor();
    Color dis = TbDisabledColor();
    for (int i = 0; i < len(tb->items); i++) {
        VirtCtrl* w = tb->items[i];
        ApplyToolbarItemColors(w);
        auto* ib = AsVirtIconButton(w);
        if (!ib) {
            continue;
        }
        const ToolbarButtonInfo& bi = GetToolbarButtonInfoByIdx(i);
        if (!HasToolbarButtonContent(bi)) {
            continue;
        }
        Str svg = bi.svgIcon ? bi.svgIcon : Str(bi.icon);
        ib->pixmap = GetCachedPixmapForSvg(svg, sz, sz, w->id == CmdOpenFile ? ThemeBrandTextColor() : fg, TbBgColor());
        ib->pixmapDisabled = GetCachedPixmapForSvg(svg, sz, sz, dis, TbBgColor());
    }
    for (int i = 0; i < len(tb->annotationItems); i++) {
        VirtCtrl* w = tb->annotationItems[i];
        ApplyToolbarItemColors(w);
        auto* ib = AsVirtIconButton(w);
        if (!ib) {
            continue;
        }
        const ToolbarButtonInfo& bi = gPdfAnnotationButtons[i];
        if (!HasToolbarButtonContent(bi)) {
            continue;
        }
        ib->pixmap = GetCachedPixmapForSvg(Str(bi.icon), sz, sz, fg, TbBgColor());
        ib->pixmapDisabled = GetCachedPixmapForSvg(Str(bi.icon), sz, sz, dis, TbBgColor());
    }
    if (tb->pageLabel) {
        tb->pageLabel->SetColor(kColText, TbTextColor());
    }
    if (tb->pageLabel2) {
        tb->pageLabel2->SetColor(kColText, TbTextColor());
    }
    if (tb->pageTotal) {
        tb->pageTotal->SetColor(kColText, TbTextColor());
    }
    if (win->pageEdit) {
        win->pageEdit->SetColors(TbTextColor(), ThemeWindowControlBackgroundColor());
    }
    if (tb->chapterTotal) {
        tb->chapterTotal->SetColor(kColText, TbTextColor());
    }
    if (win->chapterEdit) {
        win->chapterEdit->SetColors(TbTextColor(), ThemeWindowControlBackgroundColor());
    }
}

void UpdateToolbarAfterThemeChange(MainWindow* win) {
    RefreshToolbarIcons(win);
    if (win->toolbarVirt && win->toolbarVirt->zoomEdit) {
        win->toolbarVirt->zoomEdit->SetColors(TbTextColor(), TbBgColor());
    }
    ToolbarUpdateStateForWindow(win, true);
    UpdateToolbarPageText(win, win->ctrl ? win->ctrl->PageCount() : -1);
    VirtHost* host = ToolbarHost(win);
    if (host) {
        host->bgColor = TbBgColor();
        host->Invalidate(true);
    }
    UpdateAnnotFilterToolbar(win);
}

// bounds of a button in the toolbar's client coords, empty if it has none
static VirtCtrl* ToolbarItemForCmd(MainWindow* win, int cmdId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return nullptr;
    }
    if (cmdId == ToolbarOverflowId) return tb->overflowButton;
    if (cmdId == ToolbarAnnotOverflowId) return tb->annotationOverflowButton;
    for (VirtCtrl* w : tb->items) {
        if (w && w->id == cmdId && w->GetVisibility() == Visibility::Visible) {
            return w;
        }
    }
    for (VirtCtrl* w : tb->annotationItems) {
        if (w && w->id == cmdId && w->GetVisibility() == Visibility::Visible) {
            return w;
        }
    }
    return nullptr;
}

static Rect ToolbarButtonRect(MainWindow* win, int cmdId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return {};
    }
    if (cmdId == ToolbarOverflowId && tb->overflowButton && tb->overflowButton->IsVisible()) {
        return tb->overflowButton->BoundsInWindow();
    }
    if (cmdId == ToolbarAnnotOverflowId && tb->annotationOverflowButton && tb->annotationOverflowButton->IsVisible()) {
        return tb->annotationOverflowButton->BoundsInWindow();
    }
    for (VirtCtrl* w : tb->items) {
        if (w && w->id == cmdId && w->GetVisibility() == Visibility::Visible) {
            return w->BoundsInWindow();
        }
    }
    for (VirtCtrl* w : tb->annotationItems) {
        if (w && w->id == cmdId && w->GetVisibility() == Visibility::Visible) {
            return w->BoundsInWindow();
        }
    }
    for (VirtCtrl* w : tb->overflowItems) {
        if (w->id == cmdId && tb->overflowButton) return tb->overflowButton->BoundsInWindow();
    }
    for (VirtCtrl* w : tb->annotationOverflowItems) {
        if (w->id == cmdId && tb->annotationOverflowButton) return tb->annotationOverflowButton->BoundsInWindow();
    }
    return {};
}

void ToolbarSetFindExpanded(MainWindow* win, bool expanded, int minWidth, int preferredWidth) {
    auto* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) return;
    tb->findExpanded = expanded;
    if (minWidth > 0) tb->findMinWidth = minWidth;
    if (preferredWidth > 0) tb->findPreferredWidth = preferredWidth;
    if (expanded) RevealToolbarTool(win, CmdFindFirst);
    tb->host->Relayout();
    tb->host->Invalidate(true);
}

Rect ToolbarFindScreenRect(MainWindow* win) {
    auto* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || !tb->findExpanded || !tb->findSlot || !tb->host->IsVisible() || IsCollapsed(tb->findSlot)) return {};
    return tb->host->ToScreen(tb->findSlot->lastBounds);
}

// screen-coordinates rect of a toolbar button, used to position the FindBar.
// returns an empty rect when the toolbar isn't visible (e.g. fullscreen /
// presentation) so the caller can fall back to a different anchor.
Rect GetToolbarButtonScreenRect(MainWindow* win, int cmdId) {
    VirtHost* host = ToolbarHost(win);
    if (!host || !host->IsVisible()) {
        return {};
    }
    Rect r = ToolbarButtonRect(win, cmdId);
    if (r.IsEmpty()) {
        return {};
    }
    return host->ToScreen(r);
}

// Dump of the toolbar's buttons for -dbg-control tests (tests/issue-5869.ts).
// One line per button: its command id, its rect and the string the toolbar
// shows as its tooltip. Also reports how many tools the toolbar's tooltip
// control ended up with: the toolbar registers one tool per button keyed by
// command id, so duplicate command ids silently collapse into one tooltip.
static TempStr HoverDropdownStateTemp(MainWindow* win);

TempStr ToolbarButtonsResultTemp(int* exitCodeOut) {
    str::Builder out;
    MainWindow* win = len(gWindows) == 0 ? nullptr : gWindows[0];
    if (!win || !ToolbarHost(win)) {
        *exitCodeOut = 1;
        out.Append(StrL("ERROR no-toolbar\n"));
        return ToStrTemp(out);
    }
    ToolbarVirt* tb = win->toolbarVirt;
    int n = tb ? len(tb->items) : 0;
    int nTools = 0;
    for (int i = 0; i < n; i++) {
        if (tb->items[i] && tb->items[i]->tooltip) {
            nTools++;
        }
    }
    out.Append(fmt("buttons=%d tooltipTools=%d\n", n, nTools));
    int toolIdx = 0;
    for (int i = 0; i < n; i++) {
        VirtCtrl* w = tb->items[i];
        Rect r = w ? w->BoundsInWindow() : Rect{};
        bool hidden = !w || w->GetVisibility() != Visibility::Visible;
        // Match the old Win32 dump: TBIF_TEXT. Built-in buttons store the
        // tooltip (with accelerator); custom ones stored the raw name.
        Str text{};
        if (auto* b = AsVirtButton(w)) {
            text = b->s;
        } else if (i >= gLayoutButtonsCount) {
            text = GetToolbarButtonInfoByIdx(i).toolTip;
        } else if (w && w->tooltip) {
            text = w->tooltip;
        }
        out.Append(fmt("idx=%d cmd=%d hidden=%d rect=%d,%d,%d,%d text=%s\n", i, w ? w->id : 0, hidden ? 1 : 0, r.x, r.y,
                       r.x + r.dx, r.y + r.dy, text));
        if (w && w->tooltip) {
            out.Append(fmt("tool=%d uid=%d rect=%d,%d,%d,%d\n", toolIdx, w->id, r.x, r.y, r.x + r.dx, r.y + r.dy));
            toolIdx++;
        }
    }
    int nAnnotations = len(tb->annotationItems);
    bool annotationsVisible = tb->annotationExpanded;
    out.Append(fmt("annotationButtons=%d visible=%d\n", nAnnotations, annotationsVisible ? 1 : 0));
    for (int i = 0; i < nAnnotations; i++) {
        VirtCtrl* w = tb->annotationItems[i];
        const ToolbarButtonInfo& bi = gPdfAnnotationButtons[i];
        if (!w && bi.cmdId) w = ToolbarItemForCmd(win, bi.cmdId);
        Rect r = w ? w->BoundsInWindow() : Rect{};
        bool hidden = !annotationsVisible || !w || w->GetVisibility() != Visibility::Visible;
        Str tip = w ? w->tooltip : Str{};
        out.Append(fmt("annotation-idx=%d cmd=%d hidden=%d enabled=%d rect=%d,%d,%d,%d text=%s tip=%s\n", i,
                       w ? w->id : 0, hidden ? 1 : 0, w && w->IsEnabled() ? 1 : 0, r.x, r.y, r.x + r.dx, r.y + r.dy,
                       bi.toolTip, tip));
    }
    out.Append(AnnotFilterToolbarStateTemp(win));
    out.Append(HoverDropdownStateTemp(win));
    *exitCodeOut = 0;
    return ToStrTemp(out);
}

// A drop-down menu is modal: the click that dismisses it is delivered to the
// toolbar after the menu closes, and when it lands on the split button that
// opened the menu it would open it right back up. So the button ignores a click
// that arrives on the heels of its menu closing.
static u64 gToolbarDropdownClosedAt = 0;

static bool ToolbarDropdownJustClosed() {
    return GetTickCount64() - gToolbarDropdownClosedAt < 200;
}

// called when a toolbar drop-down menu was dismissed
void ToolbarNoteDropdownClosed() {
    gToolbarDropdownClosedAt = GetTickCount64();
}

static bool ShowToolbarButtonDropdown(MainWindow*, int cmdId);
static bool IsAnnotColorCmd(int cmdId);

static void OnToolbarButtonClicked(MainWindow* win, VirtMouseEvent* ev) {
    VirtCtrl* w = ev->target;
    if (!w || !win) {
        return;
    }
    int cmdId = w->id;
    if (cmdId == PageInfoId || cmdId == 0) {
        return;
    }
    if (cmdId != CmdFindFirst && IsFindBarVisible(win)) CollapseFindBar(win);
    if (ToolbarDropdownJustClosed() && (cmdId == CmdToggleReadAloud || cmdId == CmdPauseReadAloud)) {
        ev->didHandle = true;
        return;
    }
    if (w->IsEnabled() && ev->button == 0 &&
        (cmdId == CmdCreateAnnotInk || cmdId == CmdAnnotationHighlightBrush || cmdId == CmdCreateAnnotUnderline ||
         cmdId == CmdCreateAnnotStrikeOut)) {
        if (win->toolbarVirt && win->toolbarVirt->hoverCmdId == cmdId) {
            HideToolbarHoverDropdown(win);
            ev->didHandle = true;
            return;
        }
        bool ink = cmdId == CmdCreateAnnotInk;
        bool started = ink ? IsPlacingInkAnnotation(win) : IsPlacingHighlighterAnnotation(win);
        if (!started || !ink) {
            HwndSendCommand(win->hwndFrame, cmdId);
        }
        ShowToolbarButtonDropdown(win, cmdId);
        ev->didHandle = true;
        return;
    }
    if (w->IsEnabled() && ev->button == 0 && cmdId == CmdToggleLaserPointer) {
        if (win->toolbarVirt && win->toolbarVirt->hoverCmdId == cmdId) {
            HideToolbarHoverDropdown(win);
        } else {
            if (!IsLaserPointerActive(win)) {
                HwndSendCommand(win->hwndFrame, cmdId);
            }
            ShowToolbarButtonDropdown(win, cmdId);
        }
        ev->didHandle = true;
        return;
    }
    if (w->IsEnabled() && ev->button == 0 && cmdId == CmdSinglePageView) {
        ShowToolbarButtonDropdown(win, cmdId);
        ev->didHandle = true;
        return;
    }
    // right-click: the drop-down, not the button's command
    if (ev->button == 1) {
        if (ShowPinToolMenu(win, cmdId)) {
            ev->didHandle = true;
            return;
        }
        if (ShowToolbarButtonDropdown(win, cmdId)) {
            ev->didHandle = true;
            return;
        }
    }
    if (!w->IsEnabled()) {
        return;
    }
    if (auto* ib = AsVirtIconButton(w)) {
        if (ib->hasDropdown) {
            int dropDx = ib->DropdownDx();
            if (dropDx > 0 && ev->pt.x >= w->bounds.dx - dropDx) {
                ShowTtsVoiceMenu(win, GetToolbarButtonScreenRect(win, cmdId));
                ev->didHandle = true;
                return;
            }
        }
    }
    // save: the hover menu's rows end the session; they no longer apply.
    // an annotation button: picking the tool is done, its colors are in the way
    if (cmdId == CmdSaveAnnotations || IsAnnotColorCmd(cmdId)) {
        HideToolbarHoverDropdown(win);
        // not again for as long as the mouse stays on the button
        if (ToolbarVirt* tb = win->toolbarVirt) {
            tb->hoverPendingCmdId = cmdId;
        }
    }
    ToolbarPostCommand(win, cmdId);
    ev->didHandle = true;
}

//--- hover drop-down

// A row of NewToolbarHoverMenu(): an icon on the left, text on the right, and a
// background that lights up under the mouse, like a menu item.
constexpr int kHoverRowPadY = 6;
constexpr int kHoverRowPadX = 10;
constexpr int kHoverRowIconGapX = 8;
// between the label and the shortcut that sits at the right edge, as in a menu
constexpr int kHoverRowShortcutGapX = 24;
constexpr int kHoverMenuBorder = 1;
// around a label in the single-row strip; less than a menu row's, it is a row
// of them and the gaps add up
constexpr int kHoverCellPadX = 8;
// the mouse crosses a seam going from the button to the drop-down; don't close
// on the frame where it is over neither
constexpr int kCloseHoverDropdownDelayMs = 150;

struct ToolbarHoverRow : VirtCtrl {
    Pixmap* pixmap = nullptr; // not owned, from GetCachedPixmapForSvg()
    Str text;                 // owned
    Str shortcut;             // owned; empty when the command has no key
    PlatformFont* font = nullptr;
    int iconSize = 0;
    bool isCurrent = false;

    ToolbarHoverRow() = default;
    ~ToolbarHoverRow() override {
        str::Free(text);
        str::Free(shortcut);
    }

    int ShortcutDx() {
        if (len(shortcut) == 0) {
            return 0;
        }
        return PlatformFontMeasureText(font, shortcut).dx + UiScalePx(kHoverRowShortcutGapX);
    }

    Size GetIdealSize() override {
        Size ts = PlatformFontMeasureText(font, text);
        int dx = (2 * UiScalePx(kHoverRowPadX)) + iconSize + UiScalePx(kHoverRowIconGapX) + ts.dx + ShortcutDx();
        int dy = std::max(ts.dy, iconSize) + (2 * UiScalePx(kHoverRowPadY));
        return {dx, dy};
    }

    void Paint(VirtPaintCtx& ctx) override {
        bool enabled = IsEnabled();
        Rect r = ctx.bounds;
        if (isCurrent || (enabled && (HasFlag(vwfHovered) || HasFlag(vwfFocused)))) {
            ctx.gfx->FillRoundedRect(r, UiScalePx(6), TbHoverColor(), isCurrent ? ThemeBrandColor() : TbHoverColor());
        }
        int x = r.x + UiScalePx(kHoverRowPadX);
        if (pixmap) {
            int y = r.y + ((r.dy - pixmap->height) / 2);
            ctx.gfx->DrawPixmap(pixmap, {x, y, pixmap->width, pixmap->height});
        }
        x += iconSize + UiScalePx(kHoverRowIconGapX);
        int right = r.Right() - UiScalePx(kHoverRowPadX);
        Color col = enabled ? TbTextColor() : TbDisabledColor();
        if (shortcut) {
            // right-aligned and dimmer, the way a menu shows its accelerator
            Rect sr{x, r.y, right - x, r.dy};
            ctx.gfx->DrawText(shortcut, sr, gfxTextRight | gfxTextVCenter, font, TbDisabledColor());
            right -= ShortcutDx();
        }
        Rect tr{x, r.y, right - x, r.dy};
        ctx.gfx->DrawText(text, tr, gfxTextVCenter | gfxTextEllipsis, font, col);
    }

    void OnMouseEnter() { Invalidate(); }
    void OnMouseLeave() { Invalidate(); }
};

// A cell of NewToolbarHoverStrip(): a label in a row of them, no icon. The one
// in use is boxed rather than ticked; a tick per cell would double the width of
// a strip whose whole point is to be compact.
struct ToolbarHoverCell : VirtCtrl {
    Str text; // owned
    PlatformFont* font = nullptr;
    bool isCurrent = false;

    ToolbarHoverCell() = default;
    ~ToolbarHoverCell() override { str::Free(text); }

    Size GetIdealSize() override {
        Size ts = PlatformFontMeasureText(font, text);
        return {ts.dx + (2 * UiScalePx(kHoverCellPadX)), ts.dy + (2 * UiScalePx(kHoverRowPadY))};
    }

    void Paint(VirtPaintCtx& ctx) override {
        bool enabled = IsEnabled();
        Rect r = ctx.bounds;
        if (enabled && (HasFlag(vwfHovered) || HasFlag(vwfFocused))) {
            ctx.gfx->FillRect(r, TbHoverColor());
        }
        if (isCurrent) {
            ctx.gfx->DrawRect(r, TbTextColor(), UiScalePx(1));
        }
        Color col = enabled ? TbTextColor() : TbDisabledColor();
        ctx.gfx->DrawText(text, r, gfxTextCenter | gfxTextVCenter, font, col);
    }

    void OnMouseEnter() { Invalidate(); }
    void OnMouseLeave() { Invalidate(); }
};

static void PostedHideHoverDropdown(MainWindow* win) {
    if (IsMainWindowValidAndNotClosing(win)) {
        HideToolbarHoverDropdown(win);
    }
}

static void OnHoverRowClicked(MainWindow* win, VirtMouseEvent* ev) {
    VirtCtrl* w = ev ? ev->target : nullptr;
    if (!w || !w->IsEnabled()) {
        return;
    }
    int cmdId = w->id;
    // the click is being handled by the drop-down's own window, so it can only
    // be torn down once that returns
    uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "HideToolbarHoverDropdown");
    ToolbarPostCommand(win, cmdId);
}

static void OnPaletteKey(MainWindow* win, VirtKeyEvent* ev) {
    if (ev->vkey == VK_ESCAPE) {
        uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "Hide toolbar settings");
        FocusToolbar(win);
        ev->didHandle = true;
        return;
    }
    if (auto* slider = AsVirtSlider(ev->target)) {
        int value = slider->value;
        if (ev->vkey == VK_LEFT || ev->vkey == VK_DOWN)
            value--;
        else if (ev->vkey == VK_RIGHT || ev->vkey == VK_UP)
            value++;
        else if (ev->vkey == VK_HOME)
            value = slider->minVal;
        else if (ev->vkey == VK_END)
            value = slider->maxVal;
        else
            return;
        slider->SetValue(value, true);
        if (slider->onValueCommitted.IsValid()) slider->onValueCommitted.Call();
        ev->didHandle = true;
        return;
    }
    if (ev->vkey != VK_SPACE && ev->vkey != VK_RETURN) return;
    VirtMouseEvent click;
    click.target = ev->target;
    click.hit = ev->target;
    if (ev->target->onClick.IsValid()) ev->target->onClick.Call(&click);
    ev->didHandle = true;
}

// Remember a row/cell for HoverDropdownStateTemp(). `text` has to be the ctrl's
// own copy: what the caller built the item from is often temp-allocated and
// gone by the time the dump is asked for.
static void RecordHoverItem(ToolbarVirt* tb, VirtCtrl* w, Str text, const ToolbarHoverMenuItem& it) {
    ToolbarHoverItemState st;
    st.ctrl = w;
    st.text = text;
    st.cmdId = it.cmdId;
    st.isCurrent = it.isCurrent;
    VecAppend(tb->hoverItems, st);
}

ILayout* NewToolbarHoverMenu(MainWindow* win, const Vec<ToolbarHoverMenuItem>& items) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return nullptr;
    }
    int iconSize = tb->iconSize;
    Color fg = TbTextColor();
    Color dis = TbDisabledColor();
    Color bg = TbBgColor();
    auto* vbox = new VBox();
    vbox->alignCross = CrossAxisAlign::Stretch;
    for (const ToolbarHoverMenuItem& it : items) {
        auto* row = new ToolbarHoverRow();
        row->id = it.cmdId;
        row->font = tb->platformFont;
        row->isCurrent = it.isCurrent;
        row->iconSize = iconSize;
        str::ReplaceWithCopy(&row->text, it.text);
        str::ReplaceWithCopy(&row->shortcut, ShortcutsForCmdTemp(it.cmdId, 1));
        if (it.svgIcon) {
            row->pixmap = GetCachedPixmapForSvg(it.svgIcon, iconSize, iconSize, it.enabled ? fg : dis, bg);
        }
        row->SetIsEnabled(it.enabled);
        row->onClick = MkFunc1(OnHoverRowClicked, win);
        vbox->AddChild(row);
        RecordHoverItem(tb, row, row->text, it);
    }
    int b = UiScalePx(kHoverMenuBorder);
    return new Padding(vbox, Insets{b, b, b, b});
}

struct ZoomPickerScroll : ScrollBox {
    Size maximum;

    ZoomPickerScroll(ILayout* child, Size maximum) : ScrollBox(child), maximum(maximum) {}

    Size Layout(Constraints bc) override {
        bc.max.dx = std::min(bc.max.dx, maximum.dx);
        bc.max.dy = std::min(bc.max.dy, maximum.dy);
        return ScrollBox::Layout(bc);
    }
    void SetBounds(Rect r) override {
        ScrollBox::SetBounds(r);
        // ScrollBox owns an ILayout tree rather than VirtCtrl children. Give its
        // interactive descendants the host for capture, focus and invalidation.
        Vec<VirtCtrl*> controls;
        CollectPaletteControls(child, controls);
        for (VirtCtrl* ctrl : controls) ctrl->SetRoot(root);
    }
};

ILayout* NewToolbarHoverStrip(MainWindow* win, const Vec<ToolbarHoverMenuItem>& items) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return nullptr;
    }
    // a cell per item, made in the order they came in so that hoverItems (and
    // the -dbg-control dump built from it) stays in that order whatever row a
    // cell ends up in
    Vec<VirtCtrl*> cells;
    for (const ToolbarHoverMenuItem& it : items) {
        auto* cell = new ToolbarHoverCell();
        cell->id = it.cmdId;
        cell->font = tb->platformFont;
        cell->isCurrent = it.isCurrent;
        str::ReplaceWithCopy(&cell->text, it.text);
        cell->SetIsEnabled(it.enabled);
        cell->onClick = MkFunc1(OnHoverRowClicked, win);
        VecAppend(cells, (VirtCtrl*)cell);
        RecordHoverItem(tb, cell, cell->text, it);
        tb->hoverItems[len(tb->hoverItems) - 1].isStripCell = true;
    }

    int b = UiScalePx(kHoverMenuBorder);
    Rect frame = HwndClientRect(win->hwndFrame);
    Rect work = PlatformWindowWorkArea(win->hwndFrame);
    constexpr int kPickerMaxColumns = 6;
    int availableWidth = std::max(1, std::min(frame.dx, work.dx) - 2 * UiScalePx(16));
    int availableHeight = std::max(1, std::min(frame.dy, work.dy) - UiScalePx(120));
    int cellWidth = 1;
    for (VirtCtrl* cell : cells) {
        cellWidth = std::max(cellWidth, cell->GetIdealSize().dx);
    }
    int columns = limitValue((availableWidth - 2 * b) / cellWidth, 1, kPickerMaxColumns);
    auto* table = new Table();
    table->SetSize((len(cells) + columns - 1) / columns, columns);
    for (int i = 0; i < len(cells); i++) {
        auto& slot = table->SetCell(i / columns, i % columns, cells[i]);
        slot.alignH = CrossAxisAlign::Stretch;
        slot.alignV = CrossAxisAlign::Stretch;
    }
    auto* scroll = new ZoomPickerScroll(table, Size{availableWidth - 2 * b, availableHeight - 2 * b});
    scroll->syncScrollbar = false;
    scroll->lineDy = PlatformFontLineHeight(tb->platformFont) + UiScalePx(8);
    return new Padding(scroll, Insets{b, b, b, b});
}

// The rows/cells of the drop-down that is up, in screen coordinates, for
// -dbg-control tests (tests/toolbar-hover-dropdown.ts).
static TempStr HoverDropdownStateTemp(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    VirtHost* host = tb ? tb->hoverHost : nullptr;
    if (!host) {
        return StrL("dropdown cmd=0 items=0\n");
    }
    str::Builder out;
    int n = len(tb->hoverItems);
    out.Append(fmt("dropdown cmd=%d items=%d\n", tb->hoverCmdId, n));
    for (int i = 0; i < n; i++) {
        ToolbarHoverItemState& st = tb->hoverItems[i];
        Rect r = st.ctrl ? host->ToScreen(st.ctrl->BoundsInWindow()) : Rect{};
        out.Append(fmt("dropdown-item idx=%d cmd=%d current=%d rect=%d,%d,%d,%d text=%s\n", i, st.cmdId,
                       st.isCurrent ? 1 : 0, r.x, r.y, r.x + r.dx, r.y + r.dy, st.text));
    }
    return ToStrTemp(out);
}

// The toolbar sees no mouse moves once the cursor is inside the drop-down, so
// the drop-down has to say when the cursor leaves it.
static void OnHoverDropdownMouseLeave(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (tb && tb->host && tb->hoverCmdId != 0 && !tb->hoverSticky) {
        tb->host->SetTimer(kCloseHoverDropdownTimerId, kCloseHoverDropdownDelayMs);
    }
}

static void OnHoverDropdownMouseMove(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (tb && tb->host && tb->hoverCmdId != 0) {
        tb->host->KillTimer(kCloseHoverDropdownTimerId);
    }
}

// The pyramid's right half - the values above the middle - gets its own
// ground, so which way is bigger can be seen rather than read. The rows are
// staggered, so the two halves meet along a staircase, and each band runs to
// the right edge, covering the empty space beside a short row as well.
static void PaintHoverDropdownRightHalf(MainWindow* win, VirtHostPaintEvent* ev) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return;
    }
    Vec<ToolbarHoverItemState>& items = tb->hoverItems;
    int n = len(items);
    constexpr int kMaxRows = 32;
    int rowTop[kMaxRows];
    int rowBottom[kMaxRows];
    int rowSplit[kMaxRows];
    int nRows = 0;
    int prevTop = INT_MIN;
    while (nRows < kMaxRows) {
        // the topmost row below the last one found
        int y = INT_MAX;
        for (int i = 0; i < n; i++) {
            // only a strip has halves; a menu's rows are all one ground
            VirtCtrl* c = items[i].isStripCell ? items[i].ctrl : nullptr;
            if (c) {
                int cy = c->BoundsInWindow().y;
                if (cy > prevTop && cy < y) {
                    y = cy;
                }
            }
        }
        if (y == INT_MAX) {
            break;
        }
        int bottom = y;
        int right = INT_MIN;
        int split = INT_MAX;
        for (int i = 0; i < n; i++) {
            VirtCtrl* c = items[i].ctrl;
            if (!c) {
                continue;
            }
            Rect r = c->BoundsInWindow();
            if (r.y != y) {
                continue;
            }
            bottom = std::max(bottom, r.y + r.dy);
            right = std::max(right, r.x + r.dx);
            if (items[i].isRightHalf) {
                split = std::min(split, r.x);
            }
        }
        if (split == INT_MAX) {
            // nothing of the larger side in this row: only the space past its
            // end is on that side
            split = right;
        }
        rowTop[nRows] = y;
        rowBottom[nRows] = bottom;
        rowSplit[nRows] = split;
        nRows++;
        prevTop = y;
    }
    Rect cr = ev->clientRect;
    Color col = TbSubtleBgColor();
    for (int i = 0; i < nRows; i++) {
        // the first and last bands take in the border, so no strip of the
        // other ground is left above or below them
        int y = (i == 0) ? cr.y : rowTop[i];
        int bottom = (i == nRows - 1) ? cr.y + cr.dy : rowBottom[i];
        int x = rowSplit[i];
        ev->gfx->FillRect(Rect{x, y, (cr.x + cr.dx) - x, bottom - y}, col);
    }
}

static void PaintHoverDropdownBg(MainWindow* win, VirtHostPaintEvent* ev) {
    ev->gfx->FillRect(ev->clientRect, TbBgColor());
    PaintHoverDropdownRightHalf(win, ev);
}

static void PaintHoverFocus(MainWindow*, VirtHostPaintEvent* ev) {
    VirtCtrl* focused = ev->host->vroot ? ev->host->vroot->focused : nullptr;
    if (focused && focused->IsVisible() && ev->host->HasFocus()) ev->gfx->DrawFocusRect(focused->BoundsInWindow());
    int diameter = UiCornerDiameter(DpiGetForHwnd(ev->host->native), 6);
    ev->gfx->FillRoundedRect(ev->clientRect, diameter, kColorTransparent, ThemeEdgeColor());
}

// The button a drop-down is up for goes without its tooltip: the bubble would
// sit on top of the drop-down, and WM_SETCURSOR would keep bringing it back.
static void TakeHoverButtonTooltip(MainWindow* win, int cmdId) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (VirtCtrl* btn = ToolbarItemForCmd(win, cmdId)) {
        str::ReplaceWithCopy(&tb->hoverSavedTip, btn->tooltip);
        btn->SetTooltip({});
    }
}

static void GiveHoverButtonTooltipBack(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (tb->hoverCmdId == 0) {
        return;
    }
    if (VirtCtrl* btn = ToolbarItemForCmd(win, tb->hoverCmdId)) {
        btn->SetTooltip(tb->hoverSavedTip);
    }
    str::Free(tb->hoverSavedTip);
    tb->hoverSavedTip = {};
}

// SetWindowPos / DestroyWindow of the drop-down can deliver a mouse move
// back into this code. That move must not open a second window.
static bool gInOpenHover = false;

struct HoverOpenScope {
    HoverOpenScope() { gInOpenHover = true; }
    ~HoverOpenScope() { gInOpenHover = false; }
};

void HideToolbarHoverDropdown(MainWindow* win) {
    if (gInOpenHover) {
        return;
    }
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return;
    }
    HoverOpenScope openScope;
    VecReset(tb->hoverItems);
    GiveHoverButtonTooltipBack(win);
    // A second dismissal after a tool command must preserve its hover suppression.
    if (tb->hoverCmdId != 0) tb->hoverPendingCmdId = 0;
    tb->hoverCmdId = 0;
    tb->hoverSticky = false;
    tb->hoverMoveTick = 0;
    if (tb->host) {
        tb->host->KillTimer(kOpenHoverDropdownTimerId);
        tb->host->KillTimer(kCloseHoverDropdownTimerId);
    }
    if (tb->hoverHost) {
        VirtHost* h = tb->hoverHost;
        tb->hoverHost = nullptr;
        delete h;
    }
}

// Geometry, not WindowFromPoint(): the toolbar only gets a mouse move while
// the cursor is over it, so who is on top does not come into it.
static bool HostHasPoint(VirtHost* host, Point pt) {
    return host && host->IsVisible() && host->ScreenRect().Contains(pt);
}

bool ToolbarHoverDropdownContainsScreenPoint(MainWindow* win, Point pt) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    return HostHasPoint(tb ? tb->hoverHost : nullptr, pt);
}

static ToolbarHoverReg* FindHoverReg(ToolbarVirt* tb, int cmdId) {
    if (!tb || cmdId == 0) {
        return nullptr;
    }
    for (ToolbarHoverReg& reg : tb->hoverRegs) {
        if (reg.cmdId == cmdId) {
            return &reg;
        }
    }
    return nullptr;
}

void SetToolbarHoverDropdown(MainWindow* win, int cmdId, const Func1<ToolbarHoverBuildEvent*>& build, int groupId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return;
    }
    if (ToolbarHoverReg* reg = FindHoverReg(tb, cmdId)) {
        reg->build = build;
        reg->groupId = groupId;
        return;
    }
    ToolbarHoverReg reg;
    reg.cmdId = cmdId;
    reg.groupId = groupId;
    reg.build = build;
    VecAppend(tb->hoverRegs, reg);
}

static void CollectPaletteControls(ILayout* layout, Vec<VirtCtrl*>& out) {
    if (!layout || IsCollapsed(layout)) return;
    if (auto* ctrl = layout->AsVirtCtrl()) VecAppend(out, ctrl);
    for (int i = 0; i < layout->LayoutChildCount(); i++) CollectPaletteControls(layout->LayoutChildAt(i), out);
}

static int ToolbarPaletteWidth(PlatformFont* font, int availableDx) {
    int width = UiScalePx(320);
    if (font) width = std::max(width, 8 * PlatformFontLineHeight(font));
    return std::max(1, std::min(width, availableDx));
}

// Preserve the natural height of every control when a palette exceeds the monitor.
struct ToolbarPaletteScroll : ScrollBox {
    Size limit;
    ToolbarPaletteScroll(ILayout* content, Size available, PlatformFont* font) : ScrollBox(content), limit(available) {
        limit.dx = ToolbarPaletteWidth(font, available.dx);
    }
    Size Layout(Constraints bc) override {
        bc.max.dx = std::min(bc.max.dx, limit.dx);
        bc.max.dy = std::min(bc.max.dy, limit.dy);
        bc.min.dx = std::min(bc.min.dx, bc.max.dx);
        bc.min.dy = std::min(bc.min.dy, bc.max.dy);
        return ScrollBox::Layout(bc);
    }
    void SetBounds(Rect r) override {
        ScrollBox::SetBounds(r);
        Vec<VirtCtrl*> controls;
        CollectPaletteControls(child, controls);
        for (VirtCtrl* ctrl : controls) ctrl->SetRoot(root);
    }
};

static void PaletteNativeMsg(ScrollBox* scroll, VirtHostNativeMsg* ev) {
    if (ev->msg == WM_MOUSEWHEEL) {
        VirtMouseEvent wheel;
        wheel.wheelDelta = GET_WHEEL_DELTA_WPARAM(ev->wp);
        scroll->OnMouseWheel(&wheel);
        ev->didHandle = true;
        return;
    }
    if (ev->msg == WM_VSCROLL) {
        scroll->OnVScroll(ev->wp);
        ev->didHandle = true;
        return;
    }
    if (ev->msg != WM_KEYDOWN) return;
    auto* root = ev->host->vroot;
    if (ev->wp == VK_TAB && root && !IsCtrlPressed() && !IsAltPressed()) {
        Vec<VirtCtrl*> all, stops;
        CollectPaletteControls(scroll->child, all);
        for (VirtCtrl* ctrl : all)
            if (ctrl->IsHitTestable() && ctrl->HasFlag(vwfFocusable) && !ctrl->HasFlag(vwfSkipTabStop))
                VecAppend(stops, ctrl);
        if (len(stops)) {
            int index = VecFind(stops, root->focused);
            index = IsShiftPressed() ? (index <= 0 ? len(stops) - 1 : index - 1) : (index + 1) % len(stops);
            root->SetFocus(stops[index]);
        }
        if (root->focused) {
            Rect focused = root->focused->BoundsInWindow();
            Rect view = scroll->BoundsInWindow();
            if (focused.y < view.y)
                scroll->ScrollBy(focused.y - view.y);
            else if (focused.Bottom() > view.Bottom())
                scroll->ScrollBy(focused.Bottom() - view.Bottom());
        }
        ev->didHandle = true;
    } else if (ev->wp == VK_NEXT || ev->wp == VK_PRIOR) {
        scroll->ScrollPage(ev->wp == VK_NEXT ? 1 : -1);
        ev->didHandle = true;
    }
}

static void NoteHoverButtonMove(ToolbarVirt* tb) {
    tb->hoverMoveTick = GetTickCount64();
}

static void OpenHoverDropdown(MainWindow* win, int cmdId) {
    if (gInOpenHover) {
        return;
    }
    HoverOpenScope openScope;
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    ToolbarHoverReg* reg = FindHoverReg(tb, cmdId);
    if (!reg || !reg->build.IsValid() || !tb->host) {
        return;
    }
    Rect anchor = GetToolbarButtonScreenRect(win, cmdId);
    if (anchor.IsEmpty()) {
        return;
    }
    ToolbarHoverBuildEvent ev;
    ev.win = win;
    ev.cmdId = cmdId;
    VecReset(tb->hoverItems);
    reg->build.Call(&ev);
    if (!ev.layout) {
        return;
    }

    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = WStrL(L"SumatraToolbarHoverMenu");
    args.isPopup = true;
    args.visible = false;
    args.noActivate = true;
    args.userData = win;
    args.bgColor = TbBgColor();
    args.isRtl = IsUIRtl();
    args.initialSize = {100, 100};
    VirtHost* host = VirtHost::Create(args);
    if (!host) {
        delete ev.layout;
        return;
    }
    host->onPaintBackground = MkFunc1(PaintHoverDropdownBg, win);
    host->onPaint = MkFunc1(PaintHoverFocus, win);
    host->onMouseMove = MkFunc0(OnHoverDropdownMouseMove, win);
    host->onMouseLeave = MkFunc0(OnHoverDropdownMouseLeave, win);
    Rect work = PlatformWindowWorkArea(win->hwndFrame);
    auto* paletteScroll = new ToolbarPaletteScroll(
        ev.layout, {std::max(1, work.dx - UiScalePx(16)), std::max(1, work.dy - UiScalePx(16))}, tb->platformFont);
    paletteScroll->lineDy = PlatformFontLineHeight(tb->platformFont) + UiScalePx(8);
    ev.layout = paletteScroll;
    host->onNativeMsg = MkFunc1(PaletteNativeMsg, (ScrollBox*)paletteScroll);
    Size sz = host->SetLayoutSizedToContent(ev.layout);
    Vec<VirtCtrl*> controls;
    CollectPaletteControls(ev.layout, controls);
    for (VirtCtrl* control : controls) {
        if (!control->onClick.IsValid() && !AsVirtSlider(control)) continue;
        control->SetFlag(vwfFocusable, true);
        control->onKeyDown = MkFunc1(OnPaletteKey, win);
    }

    // under the button, left edges aligned, kept on the monitor. A build that
    // asked for it instead hangs off the middle of the button, so it opens
    // around where the mouse already is
    int x = anchor.x;
    if (ev.centerOnButton) {
        x = anchor.x + ((anchor.dx - sz.dx) / 2);
    }
    Rect r{x, anchor.Bottom(), sz.dx, sz.dy};
    if (cmdId == CmdZoomIn || cmdId == CmdZoomOut) {
        Rect frame = HwndMapLtrClientRectToScreen(win->hwndFrame, HwndClientRect(win->hwndFrame));
        Rect bounds = frame.Intersect(PlatformWindowWorkArea(win->hwndFrame));
        r.x = limitValue(r.x, bounds.x, std::max(bounds.x, bounds.Right() - r.dx));
        r.y = limitValue(r.y, bounds.y, std::max(bounds.y, bounds.Bottom() - r.dy));
    }
    r = ShiftRectToWorkArea(r, win->hwndFrame, true);

    // Live before the show. SetWindowPos can deliver a mouse move; gInOpenHover
    // makes that move a no-op so it cannot open a second window.
    tb->hoverHost = host;
    tb->hoverCmdId = cmdId;
    tb->hoverPendingCmdId = 0;
    NoteHoverButtonMove(tb);
    host->SetPos(r, true);

    tb->host->KillTimer(kOpenHoverDropdownTimerId);
    tb->host->KillTimer(kCloseHoverDropdownTimerId);

    TakeHoverButtonTooltip(win, cmdId);
    if (tb->host->vroot) {
        tb->host->vroot->HideTooltip();
    }
}

// Open the drop-down this button has, if any. One already up is left as it is.
static bool ShowToolbarButtonDropdown(MainWindow* win, int cmdId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return false;
    }

    if (auto* ib = AsVirtIconButton(ToolbarItemForCmd(win, cmdId))) {
        if (ib->hasDropdown) {
            ShowTtsVoiceMenu(win, GetToolbarButtonScreenRect(win, cmdId));
            return true;
        }
    }

    ToolbarHoverReg* to = FindHoverReg(tb, cmdId);
    if (!to) {
        return false;
    }
    if (tb->hoverCmdId == cmdId) {
        if (tb->host) {
            tb->host->KillTimer(kCloseHoverDropdownTimerId);
        }
        tb->hoverSticky = true;
        return true;
    }
    if (tb->hoverCmdId != 0) {
        ToolbarHoverReg* from = FindHoverReg(tb, tb->hoverCmdId);
        int group = from ? from->groupId : 0;
        if (group != 0 && to->groupId == group) {
            if (tb->host) {
                tb->host->KillTimer(kCloseHoverDropdownTimerId);
            }
            GiveHoverButtonTooltipBack(win);
            tb->hoverCmdId = cmdId;
            TakeHoverButtonTooltip(win, cmdId);
            tb->hoverSticky = true;
            return true;
        }
        HideToolbarHoverDropdown(win);
    }
    OpenHoverDropdown(win, cmdId);
    tb->hoverSticky = true;
    return true;
}

// The mouse moved over the toolbar (or left it): open, keep or close the
// drop-down of whatever button it is resting on. clientPt is the move; null
// on leave, which uses the real cursor so the drop-down stays up in it.
static void ToolbarHoverDropdownOnMouseMove(MainWindow* win, const Point* clientPt) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (gInOpenHover || !tb || !tb->host || len(tb->hoverRegs) == 0) {
        return;
    }
    Point ptScreen = UiCursorScreenPos();
    bool overMenu = ToolbarHoverDropdownContainsScreenPoint(win, ptScreen);
    int cmdId = 0;
    VirtCtrl* w = nullptr;
    if (clientPt) {
        w = ToolbarItemFromPoint(win, *clientPt);
    } else if (HostHasPoint(tb->host, ptScreen)) {
        // a disabled button still gets its drop-down, the way it still gets its
        // tooltip: the rows say what could be done and why they are greyed
        w = ToolbarItemFromPoint(win, tb->host->FromScreen(ptScreen));
    }
    if (w && FindHoverReg(tb, w->id) && (w->id != CmdCreateAnnotInk || tb->hoverCmdId == w->id)) {
        cmdId = w->id;
    }

    if (tb->hoverCmdId != 0) {
        // one is open: keep it while the mouse is on its button or in it
        if (overMenu || cmdId == tb->hoverCmdId) {
            tb->host->KillTimer(kCloseHoverDropdownTimerId);
            if (cmdId == tb->hoverCmdId) {
                NoteHoverButtonMove(tb);
            }
            return;
        }
        if (cmdId != 0) {
            ToolbarHoverReg* from = FindHoverReg(tb, tb->hoverCmdId);
            ToolbarHoverReg* to = FindHoverReg(tb, cmdId);
            int group = from ? from->groupId : 0;
            if (group != 0 && to && to->groupId == group) {
                // both buttons share this drop-down, so it stays put: the two
                // sit side by side and sliding it between them would be a
                // twitch, not a new drop-down
                tb->host->KillTimer(kCloseHoverDropdownTimerId);
                GiveHoverButtonTooltipBack(win);
                tb->hoverCmdId = cmdId;
                TakeHoverButtonTooltip(win, cmdId);
                NoteHoverButtonMove(tb);
                return;
            }
            if (tb->hoverSticky) return;
            // moved straight onto another button that has one: swap to it
            // without the delay, the way a menu bar follows the mouse
            HideToolbarHoverDropdown(win);
            OpenHoverDropdown(win, cmdId);
            return;
        }
        if (tb->hoverSticky) return;
        tb->host->SetTimer(kCloseHoverDropdownTimerId, kCloseHoverDropdownDelayMs);
        return;
    }
    if (cmdId == tb->hoverPendingCmdId) {
        return;
    }
    tb->hoverPendingCmdId = cmdId;
    tb->host->KillTimer(kOpenHoverDropdownTimerId);
    if (cmdId != 0) {
        tb->host->SetTimer(kOpenHoverDropdownTimerId, UiTooltipDelayMs());
    }
}

// Cursor is on this drop-down, or on a button that shares it.
static bool CursorKeepsHoverMenu(MainWindow* win, Point pt) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || tb->hoverCmdId == 0) {
        return false;
    }
    if (ToolbarHoverDropdownContainsScreenPoint(win, pt)) {
        return true;
    }
    if (GetToolbarButtonScreenRect(win, tb->hoverCmdId).Contains(pt)) {
        return true;
    }
    ToolbarHoverReg* from = FindHoverReg(tb, tb->hoverCmdId);
    int group = from ? from->groupId : 0;
    if (group == 0) {
        return false;
    }
    for (ToolbarHoverReg& reg : tb->hoverRegs) {
        if (reg.groupId != group) {
            continue;
        }
        if (GetToolbarButtonScreenRect(win, reg.cmdId).Contains(pt)) {
            return true;
        }
    }
    return false;
}

static void OnHoverDropdownTimer(MainWindow* win, int timerId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || !tb->host) {
        return;
    }
    if (timerId == kOpenHoverDropdownTimerId) {
        tb->host->KillTimer(kOpenHoverDropdownTimerId);
        int cmdId = tb->hoverPendingCmdId;
        tb->hoverPendingCmdId = 0;
        if (cmdId != 0 && cmdId != CmdCreateAnnotInk && !tb->hoverHost) {
            OpenHoverDropdown(win, cmdId);
        }
        return;
    }
    tb->host->KillTimer(kCloseHoverDropdownTimerId);
    if (tb->hoverSticky) {
        return;
    }
    if (CursorKeepsHoverMenu(win, UiCursorScreenPos())) {
        return;
    }
    // Posted moves are not the real cursor. A fresh one on the button keeps
    // the menu across a cursor yank between them.
    if (tb->hoverMoveTick != 0 && GetTickCount64() - tb->hoverMoveTick < (u64)kCloseHoverDropdownDelayMs) {
        tb->host->SetTimer(kCloseHoverDropdownTimerId, kCloseHoverDropdownDelayMs);
        return;
    }
    HideToolbarHoverDropdown(win);
}

struct ZoomHoverLevel {
    float zoom;
    int cmdId;
};

static void ZoomHoverLevels(Vec<ZoomHoverLevel>& out) {
    static Vec<ZoomHoverLevel> cached;
    if (len(cached) > 0) {
        for (const auto& level : cached) {
            VecAppend(out, level);
        }
        return;
    }
    Vec<float> levels;
    CollectZoomPickerLevels(levels);
    for (float zoom : levels) {
        int cmdId = CmdIdFromVirtualZoom(zoom);
        if (cmdId == CmdZoomCustom) {
            auto* cmd = CreateCommandFromDefinition(fmt("CmdZoomCustom %.2f", zoom));
            if (!cmd) {
                continue;
            }
            cmdId = cmd->id;
        }
        VecAppend(cached, ZoomHoverLevel{zoom, cmdId});
        VecAppend(out, ZoomHoverLevel{zoom, cmdId});
    }
}

// which of the levels the document is at, exact match only, -1 when it is at
// none of them (a zoom typed into Custom Zoom, or a fit mode not listed)
static int ZoomHoverCurrentIdx(MainWindow* win, const Vec<ZoomHoverLevel>& levels) {
    DocController* ctrl = win ? win->ctrl : nullptr;
    if (!ctrl) {
        return -1;
    }
    float current = ctrl->GetZoomVirtual(false);
    // the same fuzz DisplayModel::GetNextZoomStep uses to match a level
    constexpr float kZoomFuzz = 0.01f;
    for (int i = 0; i < len(levels); i++) {
        float zl = levels[i].zoom;
        if (current + kZoomFuzz >= zl && current - kZoomFuzz <= zl) {
            return i;
        }
    }
    return -1;
}

// Compact presets; custom entry and document zoom retain the full supported range.
static void BuildZoomHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    DocController* ctrl = win ? win->ctrl : nullptr;
    if (!ctrl) {
        return;
    }
    Vec<ZoomHoverLevel> levels;
    ZoomHoverLevels(levels);
    int currentIdx = ZoomHoverCurrentIdx(win, levels);

    Vec<ToolbarHoverMenuItem> items;
    for (int i = 0; i < len(levels); i++) {
        ToolbarHoverMenuItem it;
        it.text = ZoomLevelStrExact(levels[i].zoom);
        it.cmdId = levels[i].cmdId;
        it.isCurrent = i == currentIdx;
        VecAppend(items, it);
    }

    ev->layout = NewToolbarHoverStrip(win, items);
    ev->centerOnButton = true;
}

// The zoom moved (the buttons step it, and their strip stays up while they are
// clicked): box the level it landed on, if it landed on one. The strip stays
// where it is; sliding it out from under the mouse mid-click would be worse
// than the mark being off-centre.
static void UpdateZoomHoverDropdown(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || !tb->hoverHost) {
        return;
    }
    if (tb->hoverCmdId != CmdZoomIn && tb->hoverCmdId != CmdZoomOut) {
        return;
    }
    Vec<ZoomHoverLevel> levels;
    ZoomHoverLevels(levels);
    int currentIdx = ZoomHoverCurrentIdx(win, levels);
    for (int i = 0; i < len(tb->hoverItems); i++) {
        ToolbarHoverItemState& st = tb->hoverItems[i];
        if (!st.isStripCell || !st.ctrl) {
            continue;
        }
        bool isCurrent = i == currentIdx;
        auto* cell = (ToolbarHoverCell*)st.ctrl;
        if (cell->isCurrent == isCurrent) {
            continue;
        }
        cell->isCurrent = isCurrent;
        st.isCurrent = isCurrent;
        cell->Invalidate();
    }
}

// The Save button's drop-down: the three ways to end an editing session.
static void BuildLayoutHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    if (!win->ctrl) {
        return;
    }
    DisplayMode mode = win->ctrl->GetDisplayMode();
    Vec<ToolbarHoverMenuItem> items;
    VecAppend(items, {Str(kEnhancedIconPage), Tr("Single Page"), CmdSinglePageView, true, IsSingle(mode)});
    VecAppend(items, {Str(kEnhancedIconFitWidth), Tr("Facing"), CmdFacingView, true, IsFacing(mode)});
    VecAppend(items, {Str(kEnhancedIconFitWidth), Tr("Book View"), CmdBookView, true, IsBookView(mode)});
    VecAppend(items, {Str(kEnhancedIconFitWidth), Tr("Continuous scrolling"), CmdToggleContinuousView, true,
                      IsContinuous(mode)});
    ev->layout = NewToolbarHoverMenu(win, items);
    ev->centerOnButton = true;
}

static void BuildSaveHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    auto* ctx = NewBuildMenuCtx(tab, Point{0, 0});
    AutoCall delCtx(DeleteBuildMenuCtx, ctx);
    bool dirty = ctx->hasUnsavedAnnotations;

    TempStr base = tab ? path::GetBaseNameTemp(tab->filePath) : TempStr{};
    Str saveText = Tr("Save changes to existing PDF");
    if (len(base) > 0) {
        saveText = fmt(Tr("Save changes to %s").s, base);
    }

    Vec<ToolbarHoverMenuItem> items;
    VecAppend(items, {Str(gIconSave), saveText, CmdSaveAnnotations, dirty});
    VecAppend(items, {Str(gIconSaveToNewFile), Tr("Save changes to a new PDF"), CmdSaveAnnotationsNewFile, dirty});
    VecAppend(items, {Str(gIconTrash), Tr("Discard changes"), CmdDiscardChanges, dirty});
    ev->layout = NewToolbarHoverMenu(win, items);
}

//--- the annotation buttons' color drop-down

// Every annotation button that creates something with a color offers the colors
// in Annotations.PresetColors: picking one becomes the color of new annotations
// of that type, and the pencil opens the color dialog on the whole set.
constexpr int kAnnotSwatchDx = 22;
// ring space around the circle, where the mark on the color in use goes
constexpr int kAnnotSwatchPad = 5;
constexpr int kAnnotColorsPad = 10;

// the buttons that offer the preset colors
// Redact is left out: its color is the box that covers the text, not a choice
static const int kAnnotColorCmds[] = {
    CmdAnnotationHighlightBrush, CmdCreateAnnotHighlight, CmdCreateAnnotUnderline, CmdCreateAnnotSquiggly,
    CmdCreateAnnotStrikeOut,     CmdCreateAnnotText,      CmdCreateAnnotFreeText,  CmdCreateAnnotLine,
    CmdCreateAnnotPolyLine,      CmdCreateAnnotSquare,    CmdCreateAnnotCircle,    CmdCreateAnnotPolygon,
    CmdCreateAnnotInk,           CmdCreateAnnotStamp,     CmdCreateAnnotCaret,     CmdCreateAnnotFileAttachment,
};

static bool IsAnnotColorCmd(int cmdId) {
    for (int id : kAnnotColorCmds) {
        if (id == cmdId) {
            return true;
        }
    }
    return false;
}

static ParsedColor* AnnotPresetColorSetting(int cmdId) {
    if (!gSettings) {
        return nullptr;
    }
    Annotations& a = gSettings->annotations;
    switch (cmdId) {
        // the highlighter makes highlight annotations
        case CmdAnnotationHighlightBrush:
        case CmdCreateAnnotHighlight:
            return &a.highlightColor;
        case CmdCreateAnnotUnderline:
            return &a.underlineColor;
        case CmdCreateAnnotSquiggly:
            return &a.squigglyColor;
        case CmdCreateAnnotStrikeOut:
            return &a.strikeOutColor;
        case CmdCreateAnnotText:
            return &a.textIconColor;
        case CmdCreateAnnotFreeText:
            // the text's color; the box behind it is FreeTextBackgroundColor
            return &a.freeTextColor;
        case CmdCreateAnnotLine:
            return &a.lineColor;
        case CmdCreateAnnotPolyLine:
            return &a.polyLineColor;
        case CmdCreateAnnotSquare:
            return &a.squareColor;
        case CmdCreateAnnotCircle:
            return &a.circleColor;
        case CmdCreateAnnotPolygon:
            return &a.polygonColor;
        case CmdCreateAnnotInk:
            return nullptr;
        case CmdCreateAnnotStamp:
            return &a.stampColor;
        case CmdCreateAnnotCaret:
            return &a.caretColor;
        case CmdCreateAnnotFileAttachment:
            return &a.fileAttachmentColor;
    }
    return nullptr;
}

// What an annotation is made in when its setting is empty: MuPDF's defaults,
// which are also what Acrobat, PDF-XChange and Foxit use
static Color AnnotDefaultColor(int cmdId) {
    switch (cmdId) {
        case CmdCreateAnnotText:
        case CmdCreateAnnotFileAttachment:
            return MkRgb(0xff, 0xff, 0);
        case CmdCreateAnnotFreeText:
            return MkRgb(0, 0, 0);
        case CmdCreateAnnotCaret:
            return MkRgb(0, 0, 0xff);
        case CmdCreateAnnotLine:
        case CmdCreateAnnotPolyLine:
        case CmdCreateAnnotSquare:
        case CmdCreateAnnotCircle:
        case CmdCreateAnnotPolygon:
        case CmdCreateAnnotStamp:
            return MkRgb(0xff, 0, 0);
        case CmdCreateAnnotInk:
            // 40% yellow, Annotations.InkColor's default
            return 0x6600ffff;
    }
    return kColorUnset;
}

// the color the button's next annotation is made in
static Color AnnotCurrentColor(MainWindow* win, int cmdId) {
    if (cmdId == CmdCreateAnnotInk) return InkPenColor(win);
    ParsedColor* setting = AnnotPresetColorSetting(cmdId);
    Color col = setting ? GetParsedColor(*setting, kColorUnset) : kColorUnset;
    return col != kColorUnset ? col : AnnotDefaultColor(cmdId);
}

// The colors a button offers. Ink has its own, translucent ones: they are
// exactly what it paints. cmdId 0 is not a button, and gets the presets
static Str* AnnotPresetColorList(int cmdId) {
    if (!gSettings) {
        return nullptr;
    }
    Annotations& a = gSettings->annotations;
    return (cmdId == CmdCreateAnnotInk) ? &a.inkColors : &a.presetColors;
}

static void AnnotPresetColors(int cmdId, Vec<Color>& out) {
    if (Str* list = AnnotPresetColorList(cmdId)) {
        ParseColorList(*list, out, 0);
    }
}

static void SetAnnotPresetColor(MainWindow* win, int cmdId, Color col) {
    if (cmdId == CmdCreateAnnotInk) {
        SetInkPenColor(win, col);
        return;
    }
    ParsedColor* setting = AnnotPresetColorSetting(cmdId);
    if (!setting) {
        return;
    }
    SetColorText(*setting, SerializeColorTemp(col));
    ScheduleSaveSettings();
}

// A color as a filled circle, the way a highlighter's colors are shown. The one
// in use is ringed, and so is the one under the mouse.
struct ToolbarColorSwatch : VirtCtrl {
    Color col = kColorUnset;
    Str text; // owned; the color as text, for the -dbg-control dump
    bool isCurrent = false;
    bool isNone = false; // no color at all, drawn as an empty circle with a slash

    ToolbarColorSwatch() { cursor = CursorId::Hand; }
    ~ToolbarColorSwatch() override { str::Free(text); }

    Size GetIdealSize() override {
        int dx = UiScalePx(kAnnotSwatchDx) + (2 * UiScalePx(kAnnotSwatchPad));
        return {dx, dx};
    }

    void Paint(VirtPaintCtx& ctx) override {
        Rect r = ctx.bounds;
        int d = std::min(r.dx, r.dy);
        Rect ring{r.x + ((r.dx - d) / 2), r.y + ((r.dy - d) / 2), d, d};
        int t = UiScalePx(1);
        if (isCurrent || HasFlag(vwfHovered) || HasFlag(vwfFocused)) {
            // a filled disc with a smaller one of the background punched out of
            // it: Gfx fills ellipses but doesn't outline them
            ctx.gfx->FillEllipse(ring, TbTextColor());
            Rect hole = ring;
            hole.Inflate(-t, -t);
            ctx.gfx->FillEllipse(hole, TbBgColor());
        }
        Rect circle = ring;
        int p = UiScalePx(kAnnotSwatchPad);
        circle.Inflate(-p, -p);
        ctx.gfx->FillEllipse(circle, TbEdgeColor());
        circle.Inflate(-t, -t);
        if (isNone) {
            ctx.gfx->FillEllipse(circle, TbBgColor());
            // a slash from the lower left to the upper right, inset so it
            // stays inside the circle
            int inset = (int)((float)circle.dx * 0.15f);
            Point p1{circle.x + inset, circle.Bottom() - inset};
            Point p2{circle.Right() - inset, circle.y + inset};
            ctx.gfx->DrawLineAA(p1, p2, TbTextColor(), (float)t + 0.5f);
            return;
        }
        u8 a = GetAlpha(col);
        ctx.gfx->FillEllipse(circle, col & 0xffffff, a == 0 ? 255 : a);
    }
};

// whether the button's annotation can be made out of the text selected right now
static bool CanCreateAnnotFromSelection(MainWindow* win, int cmdId) {
    switch (cmdId) {
        case CmdCreateAnnotHighlight:
        case CmdCreateAnnotUnderline:
        case CmdCreateAnnotSquiggly:
        case CmdCreateAnnotStrikeOut:
            break;
        default:
            return false;
    }
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (!tab || !win->showSelection || !tab->selectionOnPage) {
        return false;
    }
    DisplayModel* dm = win->AsFixed();
    return dm && dm->textSelection && dm->textSelection->result.len > 0;
}

static void OnAnnotColorClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* sw = ev ? (ToolbarColorSwatch*)ev->target : nullptr;
    if (!sw) {
        return;
    }
    SetAnnotPresetColor(win, sw->id, sw->col);
    if (CanCreateAnnotFromSelection(win, sw->id)) {
        // with text selected, picking a color is also a request to mark it up
        ToolbarPostCommand(win, sw->id);
    }
    uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "HideToolbarHoverDropdown");
}

// which button's color the generic color dialog is editing; a valid onPick
// instead means it was opened for something that is not a toolbar button
struct AnnotColorsTarget {
    MainWindow* win = nullptr;
    InkPenStyle style = InkPenStyle::Ballpoint;
    int cmdId = 0;
    Func1<Color> onPick;
};

static void AnnotColorsPicked(AnnotColorsTarget* target, ChangeColorsArgs* args) {
    Str* list = AnnotPresetColorList(target->cmdId);
    if (args->colorsChanged && list) {
        str::ReplaceWithCopy(list, SerializeColorList(args->colors));
        ScheduleSaveSettings();
    }
    if (args->didSelect && args->color != kColorUnset) {
        if (target->onPick.IsValid()) {
            target->onPick.Call(args->color);
        } else {
            if (target->cmdId == CmdCreateAnnotInk)
                SetInkPenColor(target->style, args->color);
            else
                SetAnnotPresetColor(target->win, target->cmdId, args->color);
        }
    }
    delete target;
}

static void OnAnnotColorsEditClicked(MainWindow* win, VirtMouseEvent* ev) {
    VirtCtrl* w = ev ? ev->target : nullptr;
    if (!w) {
        return;
    }
    int cmdId = w->id;
    uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "HideToolbarHoverDropdown");

    auto* target = new AnnotColorsTarget();
    target->win = win;
    target->style = win->inkPenStyle;
    target->cmdId = cmdId;

    auto* args = new ChangeColorsArgs();
    args->win = win;
    args->title = Tr("Annotation Colors");
    args->color = AnnotCurrentColor(win, cmdId);
    args->withOpacity = true;
    AnnotPresetColors(cmdId, args->colors);
    args->onClose = MkFunc1(AnnotColorsPicked, target);
    ShowChangeColorsDialog(args);
}

// alpha 0 and 0xff both mean opaque, so a palette color matches an
// annotation's even when only one of the two spells the alpha out
static bool SameColorAndAlpha(Color a, Color b) {
    u8 aa = GetAlpha(a);
    u8 ab = GetAlpha(b);
    if (aa == 0) {
        aa = 0xff;
    }
    if (ab == 0) {
        ab = 0xff;
    }
    return ((a & 0xffffff) == (b & 0xffffff)) && (aa == ab);
}

// the color a button makes annotations in is always one of the presets, so
// its drop-down can show it; one set some other way joins the list
static void EnsureAnnotPresetColor(int cmdId, Color col) {
    Str* list = AnnotPresetColorList(cmdId);
    if (!list || col == kColorUnset) {
        return;
    }
    Vec<Color> colors;
    AnnotPresetColors(cmdId, colors);
    for (Color c : colors) {
        if (SameColorAndAlpha(c, col)) {
            return;
        }
    }
    VecAppend(colors, col);
    str::ReplaceWithCopy(list, SerializeColorList(colors));
    ScheduleSaveSettings();
}

// The drop-down's content: the preset colors as swatches with the one in use
// ringed, and a button that opens the color dialog on the whole set. cmdId is 0
// when this is not a toolbar button's drop-down, and nothing is recorded then.
// swatchesOut, when given, collects the swatches in the order they are laid
// out, for the -dbg-control dump
static ILayout* MakeAnnotColorsPanel(MainWindow* win, Str label, Color current, int cmdId, bool withNone,
                                     Vec<ToolbarColorSwatch*>* swatchesOut, const Func1<VirtMouseEvent*>& onSwatch,
                                     const Func1<VirtMouseEvent*>& onEdit, ILayout* extra = nullptr, Str title = {},
                                     VirtIconButton** editOut = nullptr) {
    ToolbarVirt* tb = win->toolbarVirt;
    Vec<Color> colors;
    AnnotPresetColors(cmdId, colors);

    auto* row = new Wrap();
    row->alignCross = CrossAxisAlign::CrossCenter;
    if (withNone) {
        // for a color that can be left out, like a shape's interior
        auto* sw = new ToolbarColorSwatch();
        sw->id = cmdId;
        sw->col = kColorUnset;
        sw->isNone = true;
        sw->isCurrent = (current == kColorUnset);
        str::ReplaceWithCopy(&sw->text, StrL("none"));
        // the named-color menu calls it that, untranslated like the other color names
        sw->SetTooltip(StrL("Transparent"));
        sw->onClick = onSwatch;
        row->AddChild(sw);
        if (swatchesOut) {
            VecAppend(*swatchesOut, sw);
        }
    }
    for (Color col : colors) {
        auto* sw = new ToolbarColorSwatch();
        sw->id = cmdId;
        sw->col = col;
        sw->isCurrent = SameColorAndAlpha(col, current);
        str::ReplaceWithCopy(&sw->text, SerializeColorTemp(col));
        sw->onClick = onSwatch;
        row->AddChild(sw);
        if (swatchesOut) {
            VecAppend(*swatchesOut, sw);
        }
        if (cmdId != 0) {
            RecordHoverItem(tb, sw, sw->text, {{}, sw->text, cmdId, true, sw->isCurrent});
        }
    }

    auto* edit = new VirtIconButton();
    int iconSize = tb->iconSize;
    int pad = UiScalePx(kAnnotSwatchPad);
    edit->id = cmdId;
    edit->padding = {pad, pad, pad, pad};
    edit->pixmap = GetCachedPixmapForSvg(Str(kEnhancedIconEdit), iconSize, iconSize, TbTextColor(), TbBgColor());
    edit->SetTooltip(Tr("Edit colors"));
    edit->onClick = onEdit;
    if (editOut) *editOut = edit;
    if (cmdId != CmdCreateAnnotInk) row->AddChild(edit);

    auto* labelText = NewVirtText({
        .s = label,
        .font = tb->platformFont,
        .textColor = TbTextColor(),
        .isRtl = IsUIRtl(),
    });
    // indented by the swatch's padding so the text lines up with the first
    // color, and 0.25rem above the swatches
    Insets labelInsets{.bottom = UiScalePx(4)};
    if (IsUIRtl()) {
        labelInsets.right = pad;
    } else {
        labelInsets.left = pad;
    }

    ILayout* labelRow = labelText;
    if (cmdId == CmdCreateAnnotInk) {
        auto* hbox = new HBox();
        hbox->alignCross = CrossAxisAlign::CrossCenter;
        hbox->AddChild(labelText, 1);
        hbox->AddChild(edit);
        labelRow = hbox;
    }
    if (len(title) > 0) {
        // what the button is, as its tooltip says, since the drop-down takes
        // the tooltip's place; on the far end of the label's row
        auto* hbox = new HBox();
        hbox->alignMain = MainAxisAlign::SpaceBetween;
        hbox->alignCross = CrossAxisAlign::CrossCenter;
        hbox->AddChild(labelText);
        Insets gap{};
        if (IsUIRtl()) {
            gap.right = UiScalePx(16);
        } else {
            gap.left = UiScalePx(16);
        }
        hbox->AddChild(new Padding(NewVirtText({
                                       .s = title,
                                       .font = tb->platformFont,
                                       .textColor = TbTextColor(),
                                       .isRtl = IsUIRtl(),
                                   }),
                                   gap));
        labelRow = hbox;
    }

    auto* vbox = new VBox();
    vbox->alignCross = CrossAxisAlign::Stretch;
    vbox->AddChild(new Padding(labelRow, labelInsets));
    vbox->AddChild(row);
    if (extra) {
        vbox->AddChild(extra);
    }
    int b = UiScalePx(kHoverMenuBorder);
    int p = UiScalePx(kAnnotColorsPad);
    return new Padding(vbox, Insets{b + p, b + p, b + p, b + p});
}

//--- the ink button's drop-down also sets how thick the stroke is

// Annotations.InkBorderWidth is in PDF points, which is about a pixel at 100%
constexpr float kInkThicknessMin = 0.1f;
constexpr int kInkThicknessMax = 16;
constexpr int kInkPreviewDy = 28;
constexpr int kInkSliderDx = 190;
// how far the preview's wave swings, as a part of the room left by the stroke
constexpr float kInkPreviewWave = 0.42f;

static float InkThickness(MainWindow* win) {
    float v = InkPenWidth(win);
    return limitValue(v, gSettings->penMinWidth, gSettings->penMaxWidth);
}

// What the ink button will lay down: the color in use, drawn as thick as the
// slider is set to. It follows the slider while it's being dragged.
struct InkStrokePreview : VirtCtrl {
    Color col = kColRed;
    float thickness = kInkThicknessMin;

    Size GetIdealSize() override { return {UiScalePx(kInkSliderDx), UiScalePx(kInkPreviewDy)}; }

    void Paint(VirtPaintCtx& ctx) override {
        Rect r = ctx.bounds;
        float w = thickness * (float)UiScalePx(100) / 100.f;
        // the stroke has to fit the preview whatever the thickness
        w = std::min(w, (float)r.dy / 2.f);
        w = std::max(w, 1.f);
        int inset = (int)(w / 2.f) + UiScalePx(2);
        int x0 = r.x + inset;
        int x1 = r.Right() - inset;
        if (x1 <= x0) {
            return;
        }
        float midY = (float)r.y + ((float)r.dy / 2.f);
        float amp = (((float)r.dy / 2.f) - (float)inset) * kInkPreviewWave;
        u8 a = GetAlpha(col);
        Color c = col & 0xffffff;
        // one period of a sine, as a run of short anti-aliased segments, with a
        // disc at every joint: the segments are butt-capped and a thick curve
        // would be notched without them
        constexpr int kSegs = 48;
        int d = (int)w;
        u8 alpha = (a == 0) ? 255 : a;
        Point prev{};
        for (int i = 0; i <= kSegs; i++) {
            float u = (float)i / (float)kSegs;
            int x = x0 + (int)(u * (float)(x1 - x0));
            int y = (int)(midY + (amp * sinf(u * 2.f * 3.14159265f)));
            Point pt{x, y};
            if (i > 0) {
                ctx.gfx->DrawLineAA(prev, pt, c, w, alpha);
            }
            if (d > 2) {
                ctx.gfx->FillEllipse(Rect{pt.x - (d / 2), pt.y - (d / 2), d, d}, c, alpha);
            }
            prev = pt;
        }
    }
};

// Dragging it is the width of the next ink annotation, and of the stroke the
// preview shows. The preview is a sibling in the same drop-down, so it lives
// exactly as long as the slider does.
struct InkThicknessSlider : VirtSlider {
    MainWindow* win = nullptr;
    InkStrokePreview* preview = nullptr;
    // the width as a number, under the slider; a sibling like the preview
    VirtText* valueText = nullptr;
    Str text; // owned; what the -dbg-control dump shows for the slider
    // where the width goes when let go. Without one it's the setting the ink
    // button makes its strokes with
    Func1<float> onThickness;
    Func1<int> onInteger;
    bool fractional = false;
    float minimum = 0.1f;
    float step = 0.1f;
    float Width() { return fractional ? minimum + (float)value * step : (float)value; }
    // committed by letting go of a drag, so the mouse capture is about to be
    // released; the color popup that holds the mouse takes it back
    bool releasingMouse = false;

    ~InkThicknessSlider() override { str::Free(text); }

    void OnChanged() {
        if (preview) {
            preview->thickness = Width();
            preview->Invalidate();
        }
        if (valueText) {
            valueText->SetText(fractional ? fmt("%.2f", Width()) : fmt("%d", value));
            valueText->Invalidate();
        }
        if (!onThickness.IsValid() && !onInteger.IsValid() && win) {
            SetInkPenWidth(win, Width());
        }
    }
    // rewriting an annotation is too slow to do on every step of a drag, so
    // the width lands when the slider is let go
    void OnCommitted() {
        // a mouse-up commits while still adjusting, a wheel step doesn't
        releasingMouse = IsAdjusting();
        OnChanged();
        if (onInteger.IsValid()) {
            onInteger.Call(value);
        } else if (onThickness.IsValid()) {
            onThickness.Call(Width());
        } else {
            ScheduleSaveSettings();
        }
    }
};

// sliderOut gets the slider, for the caller to record once the colors are in.
// thickness < 0 starts the slider at Annotations.InkBorderWidth and leaves the
// width there; otherwise it starts there and onThickness gets it
static ILayout* MakeInkThicknessPanel(MainWindow* win, Color current, float thickness, const Func1<float>& onThickness,
                                      Str label, float minThickness, InkThicknessSlider** sliderOut) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (thickness < 0) {
        thickness = InkThickness(win);
    }
    float minWidth = minThickness > 0 ? std::max(gSettings->penMinWidth, minThickness) : 0.f;
    float maxWidth = std::max(minWidth, gSettings->penMaxWidth);
    float step = limitValue(gSettings->penWidthStep, 0.1f, 16.f);
    thickness = limitValue(thickness, minWidth, maxWidth);

    auto* preview = new InkStrokePreview();
    preview->col = (current == kColorUnset) ? kColRed : current;
    preview->thickness = thickness;

    auto* slider = new InkThicknessSlider();
    slider->win = win;
    slider->fractional = true;
    slider->minimum = minWidth;
    slider->step = step;
    slider->minVal = 0;
    slider->maxVal = std::max(0, (int)floorf((maxWidth - minWidth) / step + 0.001f));
    slider->value = limitValue((int)roundf((thickness - minWidth) / step), 0, slider->maxVal);
    slider->idealDx = UiScalePx(kInkSliderDx);
    slider->preview = preview;
    slider->onThickness = onThickness;
    slider->onValueChanged = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnChanged>(slider);
    slider->onValueCommitted = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnCommitted>(slider);
    str::ReplaceWithCopy(&slider->text, fmt("thickness=%.2f", thickness));

    auto* ends = new HBox();
    ends->alignMain = MainAxisAlign::Homogeneous;
    ends->alignCross = CrossAxisAlign::CrossCenter;
    auto mkLabel = [tb](Str s) {
        return NewVirtText({
            .s = s,
            .font = tb->platformFont,
            .textColor = TbDisabledColor(),
            .isRtl = IsUIRtl(),
            .padding = {.right = UiScalePx(2), .left = UiScalePx(2)},
        });
    };
    // the width as a number, centered between the ends. Padded out to the
    // widest value so it keeps its place when a drag adds a digit.
    TempStr valueStr = fmt("%.2f", thickness);
    int widestDx = PlatformFontMeasureText(tb->platformFont, fmt("%.2f", maxWidth)).dx;
    int extraDx = std::max(widestDx - PlatformFontMeasureText(tb->platformFont, valueStr).dx, 0);
    auto* valueText = NewVirtText({
        .s = valueStr,
        .font = tb->platformFont,
        .textColor = TbTextColor(),
        .align = VirtTextAlign::Center,
        .isRtl = IsUIRtl(),
        .padding = {.right = extraDx - (extraDx / 2), .left = extraDx / 2},
    });
    slider->valueText = valueText;

    ends->AddChild(mkLabel(Tr("Thin")));
    ends->AddChild(valueText);
    auto* thick = mkLabel(Tr("Thick"));
    thick->align = VirtTextAlign::Right;
    ends->AddChild(thick);

    auto* vbox = new VBox();
    vbox->alignCross = CrossAxisAlign::Stretch;
    int gap = UiScalePx(6);
    vbox->AddChild(new Padding(preview, Insets{gap, 0, gap, 0}));
    vbox->AddChild(NewVirtText({
        .s = label,
        .font = tb->platformFont,
        .textColor = TbTextColor(),
        .isRtl = IsUIRtl(),
    }));
    vbox->AddChild(slider);
    vbox->AddChild(ends);
    *sliderOut = slider;
    return vbox;
}

struct PaletteIconButton : VirtButton {
    Pixmap* pixmap = nullptr;
    bool isCurrent = false;
    PaletteIconButton(Str text, PlatformFont* font) : VirtButton(text, font) {
        name = s;
        SetTooltip(s);
    }
    Size GetIdealSize() override {
        int pad = UiScalePx(6);
        return {pixmap ? pixmap->width + 2 * pad : 2 * pad, pixmap ? pixmap->height + 2 * pad : 2 * pad};
    }
    void Paint(VirtPaintCtx& ctx) override {
        if (isCurrent || HasFlag(vwfHovered) || HasFlag(vwfFocused)) {
            ctx.gfx->FillRoundedRect(ctx.bounds, UiScalePx(6), TbHoverColor(),
                                     isCurrent || HasFlag(vwfFocused) ? ThemeBrandColor() : TbHoverColor());
        }
        if (pixmap) {
            ctx.gfx->DrawPixmap(pixmap,
                                {ctx.bounds.x + (ctx.bounds.dx - pixmap->width) / 2,
                                 ctx.bounds.y + (ctx.bounds.dy - pixmap->height) / 2, pixmap->width, pixmap->height});
        }
    }
};

static PaletteIconButton* NewPaletteButton(MainWindow* win, Str svg, Str text) {
    ToolbarVirt* tb = win->toolbarVirt;
    auto* button = new PaletteIconButton(text, tb->platformFont);
    button->pixmap = GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, TbTextColor(), TbBgColor());
    return button;
}

struct InkPenTile : VirtButton {
    Pixmap* pixmap = nullptr;
    bool isCurrent = false;
    bool showLabel = true;
    int imagePad = 0;
    int preferredDx = 0;
    int index = 0;
    Color color = kColorUnset;
    InkPenTile() : VirtButton({}) {}
    Size GetIdealSize() override {
        int imageDx = pixmap ? pixmap->width : UiScalePx(40);
        int imageDy = pixmap ? pixmap->height : UiScalePx(64);
        Size label = showLabel ? PlatformFontMeasureText(font, s) : Size{};
        return {std::max(imageDx, label.dx) + 2 * imagePad, imageDy + label.dy + 2 * imagePad};
    }
    void Paint(VirtPaintCtx& ctx) override {
        Rect r = ctx.bounds;
        if (isCurrent || HasFlag(vwfHovered) || HasFlag(vwfFocused)) {
            ctx.gfx->FillRoundedRect(r, UiScalePx(10), TbHoverColor(),
                                     isCurrent || HasFlag(vwfFocused) ? ThemeBrandColor() : TbHoverColor());
        }
        if (pixmap) {
            ctx.gfx->DrawPixmap(pixmap,
                                {r.x + (r.dx - pixmap->width) / 2, r.y + imagePad, pixmap->width, pixmap->height});
        }
        if (!showLabel) return;
        int labelDy = PlatformFontLineHeight(font);
        Rect label{r.x, r.Bottom() - labelDy - imagePad, r.dx, labelDy};
        ctx.gfx->DrawText(s, label, gfxTextCenter | gfxTextVCenter, font, TbTextColor());
    }
};

static void OnHidePenSettings(MainWindow* win, VirtMouseEvent* ev) {
    uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "Hide pen settings");
    ev->didHandle = true;
}

static ILayout* NewPaletteHeader(MainWindow* win, Str title) {
    auto* header = new HBox();
    header->gap = UiScalePx(8);
    header->alignCross = CrossAxisAlign::CrossCenter;
    header->rtl = IsUIRtl();
    header->AddChild(NewVirtText({.s = title,
                                  .font = win->toolbarVirt->platformFont,
                                  .textColor = TbTextColor(),
                                  .isRtl = IsUIRtl(),
                                  .ellipsis = true}),
                     1);
    auto* close = NewPaletteButton(win, Str(gIconClose), Tr("Hide settings"));
    close->onClick = MkFunc1(OnHidePenSettings, win);
    header->AddChild(close);
    return header;
}

// Original upright instruments: color barrels, metal nibs and distinct tips.
static TempStr InkPenSvg(int index, Color color) {
    const char* shapes[] = {
        R"(<path d="M23 89V42l4-18 5-10 5 10 4 18v47Z" fill="#bac4cf"/><path d="M23 89V55h18v34Z" fill="%s"/><path d="M29 25l3-11 3 11v8h-6Z" fill="%s"/><path d="M25 44h14M27 60v21" fill="none"/><path d="M37 43v12" fill="none" stroke-width="3"/>)",
        R"(<path d="M22 89V57l-2-16L32 8l12 33-2 16v32Z" fill="#bac4cf"/><path d="M22 89V57h20v32Z" fill="%s"/><path d="M32 8v31" fill="none"/><circle cx="32" cy="42" r="3" fill="%s"/><path d="M23 52h18M26 65v17" fill="none"/>)",
        R"(<path d="M23 89V53h18v36Z" fill="%s"/><path d="M24 52C17 36 24 18 32 8c-2 14 14 17 8 44Z" fill="%s"/><path d="M24 48h16v9H24Z" fill="#bac4cf"/><path d="M29 24c-4 9-4 14-1 19M27 63v19" fill="none"/>)",
        R"(<path d="M22 89V37l10-27 10 27v52Z" fill="%s"/><path d="m22 37 10-27 10 27-7-4-3 5-3-5Z" fill="#e9caa1"/><path d="m29 18 3-8 3 8Z" fill="%s"/><path d="M29 42v47M35 42v47" fill="none"/>)",
        R"(<path d="M19 89V47l4-11V20l18-7v23l4 11v42Z" fill="#bac4cf"/><path d="M23 20l18-7v16l-18 7Z" fill="%s"/><path d="M19 89V54h26v35Z" fill="%s"/><path d="M23 46h18M24 63v18" fill="none"/>)",
    };
    TempStr barrel = SerializeColorTemp(color & 0xffffff);
    TempStr shape = fmt(shapes[std::clamp(index, 0, 4)], barrel, barrel);
    return fmt(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="96" viewBox="0 0 64 96"><g stroke="currentColor" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round">%s</g></svg>)",
        shape);
}

struct InkPenRow : HBox {
    int preferredWidth = 0;
    InkPenRow() {
        alignMain = MainAxisAlign::Homogeneous;
        alignCross = CrossAxisAlign::CrossStart;
        rtl = IsUIRtl();
    }
    int MinIntrinsicWidth(int) override { return preferredWidth; }
    int MinIntrinsicHeight(int width) override {
        return Layout(ExpandHeight(width == Inf ? preferredWidth : width)).dy;
    }
    Size Layout(Constraints bc) override {
        int width = bc.ConstrainWidth(bc.HasBoundedWidth() ? bc.max.dx : preferredWidth);
        int count = ChildrenCount();
        if (!count) return bc.Constrain({});
        gap = std::min(UiScalePx(2), width / (4 * count));
        int slot = std::max(0, (width - gap * (count - 1)) / count);
        int pad = std::min(UiScalePx(6), slot / 6);
        bool labelsFit = true;
        for (auto& item : children) {
            auto* tile = (InkPenTile*)item.layout;
            labelsFit &= PlatformFontMeasureText(tile->font, tile->s).dx + 2 * pad <= slot;
        }
        for (auto& item : children) {
            auto* tile = (InkPenTile*)item.layout;
            tile->showLabel = labelsFit;
            tile->imagePad = pad;
            int dx = std::max(1, std::min(tile->preferredDx, slot - 2 * pad));
            tile->pixmap = GetCachedPixmapForSvg(InkPenSvg(tile->index, tile->color), dx, std::max(1, dx * 3 / 2),
                                                 TbTextColor(), TbBgColor());
        }
        return HBox::Layout(bc.TightenWidth(width));
    }
};

static ILayout* BuildInkPenTypes(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    Str labels[] = {Tr("Ballpoint"), Tr("Fountain"), Tr("Brush"), Tr("Pencil"), Tr("Highlighter")};
    int cmds[] = {CmdInkPen, CmdInkFountain, CmdInkBrush, CmdInkPencil, CmdInkHighlighter};
    InkPenStyle styles[] = {InkPenStyle::Ballpoint, InkPenStyle::Fountain, InkPenStyle::Brush, InkPenStyle::Pencil,
                            InkPenStyle::Highlighter};
    auto* row = new InkPenRow();
    int preferredSlot = 0;
    for (int i = 0; i < dimof(cmds); i++) {
        auto* tile = new InkPenTile();
        tile->id = cmds[i];
        tile->font = tb->platformFont;
        tile->isCurrent = win->inkEraseMode == 0 && win->inkPenStyle == styles[i];
        tile->SetText(labels[i]);
        tile->name = tile->s;
        tile->SetTooltip(labels[i]);
        tile->index = i;
        tile->color = InkPenColor(styles[i]);
        tile->preferredDx = std::max(UiScalePx(28), tb->iconSize);
        tile->imagePad = UiScalePx(6);
        tile->pixmap = GetCachedPixmapForSvg(InkPenSvg(i, tile->color), tile->preferredDx, tile->preferredDx * 3 / 2,
                                             TbTextColor(), TbBgColor());
        preferredSlot = std::max(preferredSlot, tile->GetIdealSize().dx);
        tile->onClick = MkFunc1(OnHoverRowClicked, win);
        row->AddChild(tile);
        RecordHoverItem(tb, tile, tile->s, {{}, labels[i], cmds[i], true, tile->isCurrent});
    }
    row->preferredWidth = preferredSlot * dimof(cmds) + UiScalePx(2) * (dimof(cmds) - 1);
    return new Padding(row, Insets{UiScalePx(8), UiScalePx(8), UiScalePx(8), UiScalePx(8)});
}

static void OnLaserColorClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* sw = ev ? (ToolbarColorSwatch*)ev->target : nullptr;
    if (sw) {
        SetLaserPointerColor(win, sw->col);
        uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "Hide laser settings");
        ev->didHandle = true;
    }
}

static void OnLaserCustomColorPicked(MainWindow* win, ChangeColorsArgs* args) {
    if (IsMainWindowValidAndNotClosing(win) && args->didSelect && args->color != kColorUnset) {
        SetLaserPointerColor(win, args->color);
    }
}

static void OnLaserCustomColor(MainWindow* win, VirtMouseEvent* ev) {
    uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "Hide laser settings");
    auto* args = new ChangeColorsArgs();
    args->win = win;
    args->title = Tr("Laser pointer color");
    args->color = win->laserPointerColor;
    args->onClose = MkFunc1(OnLaserCustomColorPicked, win);
    ShowChangeColorsDialog(args);
    ev->didHandle = true;
}

static void OnLaserLifetime(float seconds) {
    gSettings->laserLifetimeSeconds = limitValue(seconds, 0.1f, 120.f);
    ScheduleSaveSettings();
}

static void OnLaserWidth(float width) {
    gSettings->laserWidth = NormalizeLaserWidth(width);
    ScheduleSaveSettings();
    for (MainWindow* win : gWindows) {
        if (win->laserPointerActive) InvalidateRect(win->hwndCanvas, nullptr, FALSE);
    }
}

static void StepLaserWidth(InkThicknessSlider* slider, int direction, VirtMouseEvent* ev) {
    slider->SetValue(slider->value + direction, false);
    slider->OnCommitted();
    slider->Invalidate();
    ev->didHandle = true;
}

static void LaserWidthThinner(InkThicknessSlider* slider, VirtMouseEvent* ev) {
    StepLaserWidth(slider, -1, ev);
}
static void LaserWidthThicker(InkThicknessSlider* slider, VirtMouseEvent* ev) {
    StepLaserWidth(slider, 1, ev);
}

struct PaletteNote : VirtText {
    PaletteNote(Str text, PlatformFont* font) : VirtText(text, font) { isRtl = IsUIRtl(); }
    Size Layout(Constraints bc) override {
        int width = bc.HasBoundedWidth() ? std::max(1, bc.max.dx) : -1;
        return bc.Constrain(PlatformFontMeasureText(font, s, width));
    }
    int MinIntrinsicHeight(int width) override { return Layout(ExpandHeight(width)).dy; }
    void Paint(VirtPaintCtx& ctx) override {
        ctx.gfx->DrawText(s, ctx.content, gfxTextWrap | (isRtl ? gfxTextRtl : 0), font, TbDisabledColor());
    }
};

static void BuildLaserHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    ToolbarVirt* tb = win->toolbarVirt;
    auto* panel = new VBox();
    panel->alignCross = CrossAxisAlign::Stretch;
    panel->AddChild(NewPaletteHeader(win, Tr("Laser pointer")));
    Vec<ToolbarHoverMenuItem> modes;
    VecAppend(modes, {Str(gIconLaserSolid), Tr("Solid line"), CmdLaserSolid, true,
                      win->laserPointerMode == LaserPointerMode::Solid});
    VecAppend(modes, {Str(gIconLaserHollow), Tr("Hollow line"), CmdLaserHollow, true,
                      win->laserPointerMode == LaserPointerMode::Hollow});
    VecAppend(modes, {Str(gIconLaserDot), Tr("Single dot"), CmdLaserDot, true,
                      win->laserPointerMode == LaserPointerMode::Dot});
    auto* modeRow = new Wrap();
    modeRow->colGap = UiScalePx(4);
    modeRow->rtl = IsUIRtl();
    for (const auto& mode : modes) {
        auto* button = new VirtIconButton();
        button->id = mode.cmdId;
        button->label = mode.text;
        button->labelFont = tb->platformFont;
        button->pixmap = GetCachedPixmapForSvg(mode.svgIcon, tb->iconSize, tb->iconSize, TbTextColor(), TbBgColor());
        button->isSelected = mode.isCurrent;
        button->cornerRadius = UiScalePx(6);
        button->padding = {UiScalePx(4), UiScalePx(6), UiScalePx(4), UiScalePx(6)};
        button->SetTooltip(mode.text);
        button->onClick = MkFunc1(OnHoverRowClicked, win);
        modeRow->AddChild(button);
        RecordHoverItem(tb, button, button->tooltip, mode);
    }
    panel->AddChild(modeRow);
    auto* width = new InkThicknessSlider();
    width->fractional = true;
    width->minimum = 0.1f;
    width->step = 0.1f;
    width->minVal = 0;
    width->maxVal = 319;
    width->value = limitValue((int)roundf((NormalizeLaserWidth(gSettings->laserWidth) - 0.1f) / 0.1f), 0, 319);
    width->idealDx = UiScalePx(kInkSliderDx);
    width->onThickness = MkFunc1Void(OnLaserWidth);
    width->onValueChanged = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnChanged>(width);
    width->onValueCommitted = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnCommitted>(width);
    str::ReplaceWithCopy(&width->text, StrL("laser-width-px"));
    width->valueText =
        NewVirtText({.s = fmt("%.2f", width->Width()), .font = tb->platformFont, .textColor = TbTextColor()});
    width->SetTooltip(Tr("Laser width: 0.1 to 32 pixels. Independent of pen thickness."));
    auto* widthRow = new Wrap();
    widthRow->colGap = UiScalePx(8);
    widthRow->alignCross = CrossAxisAlign::CrossCenter;
    auto* thinner = new VirtButton(StrL("-"), tb->platformFont);
    thinner->SetTooltip(Tr("Decrease laser width by 0.1 px"));
    thinner->onClick = MkFunc1(LaserWidthThinner, width);
    auto* thicker = new VirtButton(StrL("+"), tb->platformFont);
    thicker->SetTooltip(Tr("Increase laser width by 0.1 px"));
    thicker->onClick = MkFunc1(LaserWidthThicker, width);
    widthRow->AddChild(NewVirtText({.s = Tr("Width (px)"), .font = tb->platformFont, .textColor = TbTextColor()}));
    widthRow->AddChild(thinner);
    widthRow->AddChild(width->valueText);
    widthRow->AddChild(thicker);
    panel->AddChild(widthRow);
    panel->AddChild(width);
    auto* lifetime = new InkThicknessSlider();
    lifetime->fractional = true;
    lifetime->minimum = 0.1f;
    lifetime->step = 0.1f;
    lifetime->minVal = 0;
    lifetime->maxVal = 1199;
    lifetime->value = limitValue((int)roundf((gSettings->laserLifetimeSeconds - 0.1f) / 0.1f), 0, 1199);
    lifetime->idealDx = UiScalePx(kInkSliderDx);
    lifetime->onThickness = MkFunc1Void(OnLaserLifetime);
    lifetime->onValueChanged = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnChanged>(lifetime);
    lifetime->onValueCommitted = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnCommitted>(lifetime);
    str::ReplaceWithCopy(&lifetime->text, Str("laser-lifetime-seconds"));
    lifetime->valueText =
        NewVirtText({.s = fmt("%.2f", lifetime->Width()), .font = tb->platformFont, .textColor = TbTextColor()});
    auto* timeRow = new Wrap();
    timeRow->colGap = UiScalePx(8);
    timeRow->alignCross = CrossAxisAlign::CrossCenter;
    timeRow->AddChild(
        NewVirtText({.s = Tr("Disappear after (s)"), .font = tb->platformFont, .textColor = TbTextColor()}));
    timeRow->AddChild(lifetime->valueText);
    panel->AddChild(timeRow);
    panel->AddChild(lifetime);
    panel->AddChild(NewVirtText({.s = Tr("Color"), .font = tb->platformFont, .textColor = TbTextColor()}));
    auto* colors = new Wrap();
    colors->alignCross = CrossAxisAlign::CrossCenter;
    Color palette[] = {MkRgb(244, 67, 54),  MkRgb(255, 193, 7),  MkRgb(76, 175, 80),
                       MkRgb(33, 150, 243), MkRgb(156, 39, 176), MkRgb(255, 255, 255)};
    bool custom = true;
    for (Color col : palette) {
        auto* sw = new ToolbarColorSwatch();
        sw->id = CmdToggleLaserPointer;
        sw->col = col;
        sw->isCurrent = SameColorAndAlpha(col, win->laserPointerColor);
        custom = custom && !sw->isCurrent;
        str::ReplaceWithCopy(&sw->text, SerializeColorTemp(col));
        sw->SetTooltip(sw->text);
        sw->onClick = MkFunc1(OnLaserColorClicked, win);
        colors->AddChild(sw);
        RecordHoverItem(tb, sw, sw->text, {{}, sw->text, CmdToggleLaserPointer, true, sw->isCurrent});
    }
    if (custom) {
        auto* sw = new ToolbarColorSwatch();
        sw->id = CmdToggleLaserPointer;
        sw->col = win->laserPointerColor;
        sw->isCurrent = true;
        str::ReplaceWithCopy(&sw->text, SerializeColorTemp(sw->col));
        sw->SetTooltip(sw->text);
        sw->onClick = MkFunc1(OnLaserColorClicked, win);
        colors->AddChild(sw);
        RecordHoverItem(tb, sw, sw->text, {{}, sw->text, CmdToggleLaserPointer, true, true});
    }
    panel->AddChild(colors);
    auto* customButton = new VirtButton(Tr("Custom color..."), tb->platformFont);
    customButton->padding = {UiScalePx(6), UiScalePx(10), UiScalePx(6), UiScalePx(10)};
    customButton->cornerRadius = UiScalePx(6);
    customButton->onClick = MkFunc1(OnLaserCustomColor, win);
    colors->AddChild(customButton);
    panel->AddChild(new PaletteNote(Tr("Timed from pen lift. Esc stops drawing."), tb->platformFont));
    ev->layout = new Padding(panel, Insets{UiScalePx(10), UiScalePx(10), UiScalePx(10), UiScalePx(10)});
    ev->centerOnButton = true;
}

static void PinToolClick(MainWindow*, VirtMouseEvent*);

static void BuildAnnotColorsHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    ParsedColor* setting = AnnotPresetColorSetting(ev->cmdId);
    if (!tb || (!setting && ev->cmdId != CmdCreateAnnotInk)) {
        return;
    }
    Color current = AnnotCurrentColor(win, ev->cmdId);
    EnsureAnnotPresetColor(ev->cmdId, current);
    // ink is the one annotation whose width is a choice too
    InkThicknessSlider* slider = nullptr;
    ILayout* extra = (ev->cmdId == CmdCreateAnnotInk)
                         ? MakeInkThicknessPanel(win, current, -1, {}, Tr("Thickness"), kInkThicknessMin, &slider)
                         : nullptr;
    // a note's color fills its icon, behind the note
    Str label = (ev->cmdId == CmdCreateAnnotText) ? Tr("Background Color") : Tr("Color");
    // the button still has its tooltip; it's taken once the drop-down is up
    VirtCtrl* btn = ToolbarItemForCmd(win, ev->cmdId);
    Str title = ev->cmdId != CmdCreateAnnotInk && btn ? btn->tooltip : Str{};
    ev->layout = MakeAnnotColorsPanel(win, label, current, ev->cmdId, false, nullptr, MkFunc1(OnAnnotColorClicked, win),
                                      MkFunc1(OnAnnotColorsEditClicked, win), extra, title);
    if (slider) {
        RecordHoverItem(tb, slider, slider->text, {{}, slider->text, ev->cmdId, true, false});
    }
    if (ev->cmdId == CmdCreateAnnotInk) {
        auto* panel = new VBox();
        panel->alignCross = CrossAxisAlign::Stretch;
        panel->AddChild(
            new Padding(NewPaletteHeader(win, Tr("Pen types")), Insets{UiScalePx(8), UiScalePx(8), 0, UiScalePx(8)}));
        panel->AddChild(BuildInkPenTypes(win));
        panel->AddChild(ev->layout);
        Vec<ToolbarHoverMenuItem> tools;
        VecAppend(tools, {Str(kEnhancedIconEraser), Tr("Stroke eraser"), CmdInkEraser, true, win->inkEraseMode == 1});
        VecAppend(tools, {Str(kEnhancedIconEraser), Tr("Erase highlights only"), CmdHighlightEraser, true,
                          win->inkEraseMode == 2});
        VecAppend(tools, {{}, Tr("Ignore touch while writing"), CmdTogglePenOnly, true, win->penOnly});
        auto* toolRow = new Wrap();
        toolRow->colGap = UiScalePx(4);
        toolRow->rtl = IsUIRtl();
        for (const auto& tool : tools) {
            auto* button = new VirtButton(tool.text, tb->platformFont);
            button->id = tool.cmdId;
            button->cornerRadius = UiScalePx(6);
            button->padding = {UiScalePx(4), UiScalePx(6), UiScalePx(4), UiScalePx(6)};
            button->SetTooltip(tool.text);
            button->onClick = MkFunc1(OnHoverRowClicked, win);
            if (tool.isCurrent) button->SetColor(kColBtnBg, TbHoverColor());
            toolRow->AddChild(button);
            RecordHoverItem(tb, button, button->tooltip, tool);
        }
        panel->AddChild(toolRow);
        ev->layout = panel;
    }
    auto* withPin = new VBox();
    withPin->alignCross = CrossAxisAlign::Stretch;
    withPin->AddChild(ev->layout);
    bool isPinned = PinnedToolIndex(win, ev->cmdId) >= 0;
    auto* pin = NewPaletteButton(win, Str(gIconPin), isPinned ? Tr("Unpin this tool") : Tr("Pin this tool"));
    pin->isCurrent = isPinned;
    pin->id = ev->cmdId;
    pin->onClick = MkFunc1(PinToolClick, win);
    auto* pinAlign = new Align(pin);
    pinAlign->HAlign = IsUIRtl() ? AlignStart : AlignEnd;
    withPin->AddChild(pinAlign);
    ev->layout = withPin;
    ev->centerOnButton = true;
}

//--- the same drop-down, opened from a chip of the annotation edit toolbar

// There is no toolbar button to hover here, so the popup keeps the mouse and
// the first click outside it dismisses it, the way a menu does.
struct AnnotColorPopup {
    VirtHost* host = nullptr;
    MainWindow* win = nullptr;
    Color current = kColorUnset;
    Func1<Color> onPick;
    Color picked = kColorUnset;
    bool hasPick = false;
    bool openDialog = false;
    // non-owning, for tests; the layout tree owns them
    Vec<ToolbarColorSwatch*> swatches;
    InkThicknessSlider* slider = nullptr;
    VirtIconButton* edit = nullptr;
};

static AnnotColorPopup* gAnnotColorPopup = nullptr;

static void ShowAnnotColorsDialog(MainWindow* win, Color current, const Func1<Color>& onPick);
static void ShowAnnotPopupHost(AnnotColorPopup* p, ILayout* layout, Rect anchor);

static void PostedCloseAnnotColorPopup(MainWindow* win) {
    AnnotColorPopup* p = gAnnotColorPopup;
    if (!p) {
        return;
    }
    gAnnotColorPopup = nullptr;
    Func1<Color> onPick = p->onPick;
    bool hasPick = p->hasPick;
    bool openDialog = p->openDialog;
    Color col = p->picked;
    Color current = p->current;
    delete p->host;
    delete p;
    if (hasPick) {
        onPick.Call(col);
    }
    if (openDialog) {
        // only now: destroying the popup activates its owner, which would put
        // the dialog behind the main window if it were already up
        ShowAnnotColorsDialog(win, current, onPick);
    }
}

// the click is handled by the popup's own window, so the window can only be
// torn down once that returns
static void CloseAnnotColorPopup(AnnotColorPopup* p) {
    if (::GetCapture() == p->host->native) {
        ::ReleaseCapture();
    }
    uitask::Post(MkFunc0(PostedCloseAnnotColorPopup, p->win), "CloseAnnotColorPopup");
}

static void OnAnnotColorPopupSwatch(AnnotColorPopup* p, VirtMouseEvent* ev) {
    auto* sw = ev ? (ToolbarColorSwatch*)ev->target : nullptr;
    if (!sw || p != gAnnotColorPopup) {
        return;
    }
    p->picked = sw->col;
    p->hasPick = true;
    CloseAnnotColorPopup(p);
}

static void ShowAnnotColorsDialog(MainWindow* win, Color current, const Func1<Color>& onPick) {
    auto* target = new AnnotColorsTarget();
    target->onPick = onPick;

    auto* args = new ChangeColorsArgs();
    args->win = win;
    args->title = Tr("Annotation Colors");
    args->color = current;
    args->withOpacity = true;
    AnnotPresetColors(0, args->colors);
    args->onClose = MkFunc1(AnnotColorsPicked, target);
    ShowChangeColorsDialog(args);
}

static void OnAnnotColorPopupEdit(AnnotColorPopup* p, VirtMouseEvent*) {
    if (p != gAnnotColorPopup) {
        return;
    }
    p->openDialog = true;
    CloseAnnotColorPopup(p);
}

static void PaintAnnotColorPopupBg(MainWindow*, VirtHostPaintEvent* ev) {
    ev->gfx->FillRect(ev->clientRect, TbBgColor());
    ev->gfx->DrawRect(ev->clientRect, ThemeEdgeColor(), UiScalePx(kHoverMenuBorder));
}

static void PostedReclaimAnnotColorPopupCapture(MainWindow*) {
    AnnotColorPopup* p = gAnnotColorPopup;
    if (p && p->host) {
        ::SetCapture(p->host->native);
    }
}

static void AnnotColorPopupNativeMsg(AnnotColorPopup* p, VirtHostNativeMsg* ev) {
    if (p != gAnnotColorPopup) {
        return;
    }
    switch (ev->msg) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            // the mouse is captured, so clicks meant for another window come
            // here too: they dismiss the popup and go no further
            Point pt{GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp)};
            if (p->host->ClientRect().Contains(pt)) {
                return;
            }
            CloseAnnotColorPopup(p);
            ev->didHandle = true;
            ev->res = 0;
            break;
        }
        case WM_CAPTURECHANGED:
            if ((HWND)ev->lp == p->host->native) {
                break;
            }
            // the slider lets go of the mouse when its drag ends; the popup
            // takes it back instead of treating that as a click elsewhere
            if (!ev->lp && p->slider && p->slider->releasingMouse) {
                p->slider->releasingMouse = false;
                uitask::Post(MkFunc0(PostedReclaimAnnotColorPopupCapture, p->win), "ReclaimAnnotColorPopupCapture");
                break;
            }
            CloseAnnotColorPopup(p);
            break;
    }
}

void ShowAnnotColorPopup(MainWindow* win, Rect anchor, Color current, bool withNone, Str label,
                         const Func1<Color>& onPick, float thickness, const Func1<float>& onThickness,
                         Str thicknessLabel, float minThickness) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || gAnnotColorPopup) {
        return;
    }
    auto* p = new AnnotColorPopup();
    p->win = win;
    p->current = current;
    p->onPick = onPick;
    // an ink annotation's stroke is as much a choice as its color, so its
    // popup has the same Thickness slider the ink button's drop-down has
    InkThicknessSlider* slider = nullptr;
    ILayout* extra =
        (thickness >= 0)
            ? MakeInkThicknessPanel(win, current, thickness, onThickness,
                                    len(thicknessLabel) > 0 ? thicknessLabel : Tr("Thickness"), minThickness, &slider)
            : nullptr;
    ILayout* layout =
        MakeAnnotColorsPanel(win, label, current, 0, withNone, &p->swatches, MkFunc1(OnAnnotColorPopupSwatch, p),
                             MkFunc1(OnAnnotColorPopupEdit, p), extra, {}, &p->edit);
    p->slider = slider;
    ShowAnnotPopupHost(p, layout, anchor);
}

// A number picked with a slider: a label, the slider, and the value under it.
// onValue gets the value when the slider is let go.
void ShowAnnotSliderPopup(MainWindow* win, Rect anchor, Str label, int value, int minVal, int maxVal,
                          const Func1<int>& onValue) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || gAnnotColorPopup) {
        return;
    }
    auto* p = new AnnotColorPopup();
    p->win = win;
    value = limitValue(value, minVal, maxVal);

    auto* slider = new InkThicknessSlider();
    slider->minVal = minVal;
    slider->maxVal = maxVal;
    slider->value = value;
    slider->idealDx = UiScalePx(kInkSliderDx);
    slider->onInteger = onValue;
    slider->onValueChanged = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnChanged>(slider);
    slider->onValueCommitted = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnCommitted>(slider);
    str::ReplaceWithCopy(&slider->text, fmt("thickness=%d", value));

    auto mkEnd = [tb](int v) {
        return NewVirtText({
            .s = fmt("%d", v),
            .font = tb->platformFont,
            .textColor = TbDisabledColor(),
            .isRtl = IsUIRtl(),
        });
    };
    // centered between the ends, padded out to the widest value so it keeps
    // its place when a drag adds a digit
    TempStr valueStr = fmt("%d", value);
    int widestDx = PlatformFontMeasureText(tb->platformFont, fmt("%d", maxVal)).dx;
    int extraDx = std::max(widestDx - PlatformFontMeasureText(tb->platformFont, valueStr).dx, 0);
    auto* valueText = NewVirtText({
        .s = valueStr,
        .font = tb->platformFont,
        .textColor = TbTextColor(),
        .align = VirtTextAlign::Center,
        .isRtl = IsUIRtl(),
        .padding = {.right = extraDx - (extraDx / 2), .left = extraDx / 2},
    });
    slider->valueText = valueText;

    auto* ends = new HBox();
    ends->alignMain = MainAxisAlign::SpaceBetween;
    ends->alignCross = CrossAxisAlign::CrossCenter;
    ends->AddChild(mkEnd(minVal));
    ends->AddChild(valueText);
    ends->AddChild(mkEnd(maxVal));

    auto* vbox = new VBox();
    vbox->alignCross = CrossAxisAlign::Stretch;
    vbox->AddChild(new Padding(NewVirtText({
                                   .s = label,
                                   .font = tb->platformFont,
                                   .textColor = TbTextColor(),
                                   .isRtl = IsUIRtl(),
                               }),
                               Insets{.bottom = UiScalePx(4)}));
    vbox->AddChild(slider);
    vbox->AddChild(ends);
    int b = UiScalePx(kHoverMenuBorder);
    int pad = UiScalePx(kAnnotColorsPad);
    ILayout* layout = new Padding(vbox, Insets{b + pad, b + pad, b + pad, b + pad});
    p->slider = slider;
    ShowAnnotPopupHost(p, layout, anchor);
}

static void ShowAnnotPopupHost(AnnotColorPopup* p, ILayout* layout, Rect anchor) {
    MainWindow* win = p->win;
    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = WStrL(L"SumatraAnnotColorPopup");
    args.isPopup = true;
    args.visible = false;
    args.noActivate = true;
    args.userData = win;
    args.bgColor = TbBgColor();
    args.isRtl = IsUIRtl();
    args.initialSize = {100, 100};
    VirtHost* host = VirtHost::Create(args);
    if (!host) {
        delete layout;
        delete p;
        return;
    }
    p->host = host;
    host->onPaintBackground = MkFunc1(PaintAnnotColorPopupBg, win);
    host->onNativeMsg = MkFunc1(AnnotColorPopupNativeMsg, p);
    Size sz = host->SetLayoutSizedToContent(layout);

    // under the chip, centered on it, kept on the monitor
    Rect r{anchor.x + ((anchor.dx - sz.dx) / 2), anchor.Bottom(), sz.dx, sz.dy};
    r = ShiftRectToWorkArea(r, win->hwndFrame, true);
    host->SetPos(r, true);
    gAnnotColorPopup = p;
    ::SetCapture(host->native);
}

// for tests: the swatches of the drop-down that is up, if any
TempStr AnnotColorPopupStateTemp() {
    AnnotColorPopup* p = gAnnotColorPopup;
    if (!p || !p->host) {
        return fmt("annotColorPopup visible=0 n=0 thickness= swatches=\n");
    }
    Rect r = p->host->ScreenRect();
    str::Builder swatches;
    for (int i = 0; i < len(p->swatches); i++) {
        if (i > 0) {
            swatches.AppendChar(';');
        }
        ToolbarColorSwatch* sw = p->swatches[i];
        Rect sr = sw->BoundsInWindow();
        swatches.Append(
            fmt("%s:%d,%d,%d,%d:%d", sw->text, r.x + sr.x, r.y + sr.y, sr.dx, sr.dy, sw->isCurrent ? 1 : 0));
    }
    Str thickness = StrL("");
    if (p->slider) {
        Rect sr = p->slider->BoundsInWindow();
        thickness = fmt("%g:%d,%d,%d,%d", p->slider->Width(), r.x + sr.x, r.y + sr.y, sr.dx, sr.dy);
    }
    Rect er{};
    if (p->edit) {
        er = p->edit->BoundsInWindow();
        er.x += r.x;
        er.y += r.y;
    }
    return fmt("annotColorPopup visible=1 n=%d placed=%d,%d,%d,%d edit=%d,%d,%d,%d thickness=%s swatches=%s\n",
               len(p->swatches), r.x, r.y, r.dx, r.dy, er.x, er.y, er.dx, er.dy, thickness, ToStrTemp(swatches));
}

static void OnToolbarMouseMove(MainWindow* win, Point pt) {
    UpdateOverlayToolbarForMouse(win);
    ToolbarHoverDropdownOnMouseMove(win, &pt);
}

static void OnToolbarMouseLeave(MainWindow* win) {
    UpdateOverlayToolbarForMouse(win);
    ToolbarHoverDropdownOnMouseMove(win, nullptr);
}

static void PaintToolbarSeparator(VirtCustom*, VirtPaintCtx* ctx) {
    Rect r = ctx->bounds;
    int inset = UiScalePx(6);
    int dy = r.dy - (2 * inset);
    if (dy <= 0) {
        return;
    }
    int x = r.x + (r.dx / 2);
    ctx->gfx->FillRect({x, r.y + inset, 1, dy}, ThemeEdgeColor());
}

static VirtCtrl* MakeToolbarSeparator(int rowDy) {
    auto* sep = new VirtCustom();
    sep->idealSize = {UiScalePx(8), rowDy - UiScalePx(12)};
    sep->onPaint = MkFunc1(PaintToolbarSeparator, sep);
    sep->SetFlag(vwfNoHitTest, true);
    return sep;
}

// (re)build the tree of virtual controls the toolbar is made of, one per button
static bool IsAppearanceCmd(int cmdId) {
    return cmdId == CmdOptions || cmdId == CmdThemeLight || cmdId == CmdThemeDark || cmdId == CmdChangeTheme ||
           cmdId == CmdInvertColors;
}

static void PaintToolbarBrand(VirtCustom*, VirtPaintCtx* ctx) {
    Rect r = ctx->bounds;
    int sz = UiScalePx(std::max(28, limitValue(gSettings->toolbarSize, 8, 64)));
    HICON icon = (HICON)LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(GetAppIconID()), IMAGE_ICON, sz, sz, 0);
    Pixmap* badge = icon ? PixmapFromHICON(icon) : nullptr;
    if (badge) {
        ctx->gfx->DrawPixmap(badge, {r.x, r.y + (r.dy - sz) / 2, sz, sz});
    }
    delete badge;
    if (icon) {
        DestroyIcon(icon);
    }
    PlatformFont* font = GetAppFont();
    int lineDy = PlatformFontLineHeight(font);
    Rect title{r.x + sz + UiScalePx(6), r.y + (r.dy - lineDy * 2) / 2, r.dx - sz - UiScalePx(6), lineDy};
    ctx->gfx->DrawText(StrL("SumatraPDF Enhanced"), title, gfxTextEllipsis, font, TbTextColor());
    title.y += lineDy;
    ctx->gfx->DrawText(Tr("Focused reading"), title, gfxTextEllipsis, GetAppFont(), TbDisabledColor());
}

static void ZoomEntryChar(MainWindow* win, Edit::CharEvent* ev) {
    if (ev->c != VK_RETURN && ev->c != VK_ESCAPE) return;
    ev->didHandle = true;
    auto* edit = win->toolbarVirt ? win->toolbarVirt->zoomEdit : nullptr;
    if (ev->c == VK_RETURN && edit && win->IsDocLoaded()) {
        TempStr text = edit->GetTextTemp();
        str::RemoveCharsInPlace(text, StrL("% "));
        char* end = nullptr;
        float zoom = (float)strtod(text.s, &end);
        if (text.len > 0 && end == text.s + text.len && zoom >= kZoomMin && zoom <= kZoomMax) {
            SmartZoom(win, zoom, nullptr, true);
        }
    }
    HwndSetFocus(win->hwndFrame);
}

static Edit* CreateZoomEntry(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    auto* edit = new Edit();
    Edit::CreateArgs args;
    args.parent = win->hwndToolbar;
    args.font = tb->platformFont;
    args.withFrame = true;
    args.noTheme = true;
    args.selectAllOnFocus = true;
    args.alignRight = true;
    args.cueText = StrL("Zoom %");
    args.text = StrL("100%");
    edit->SetColors(TbTextColor(), ThemeWindowControlBackgroundColor());
    edit->Create(args);
    edit->SetIdealWidthChars(7);
    edit->SetMaxWidthChars(7);
    edit->idealDy = std::max(tb->iconSize, PlatformFontLineHeight(tb->platformFont) + UiScalePx(4));
    edit->mapRtlX = true;
    edit->onChar = MkFunc1(ZoomEntryChar, win);
    return edit;
}

static Str PinnedToolName(MainWindow* win, int cmd) {
    if (cmd == CmdAnnotationHighlightBrush) return StrL("highlight");
    if (cmd == CmdCreateAnnotUnderline) return StrL("underline");
    if (cmd == CmdCreateAnnotStrikeOut) return StrL("strikeout");
    if (cmd != CmdCreateAnnotInk) return GetCommandName(cmd);
    switch (win->inkPenStyle) {
        case InkPenStyle::Fountain:
            return StrL("fountain");
        case InkPenStyle::Brush:
            return StrL("brush");
        case InkPenStyle::Pencil:
            return StrL("pencil");
        case InkPenStyle::Highlighter:
            return StrL("marker");
        default:
            return StrL("ballpoint");
    }
}

static int PinnedToolCommand(Str tool) {
    if (str::Eq(tool, StrL("highlight"))) return CmdAnnotationHighlightBrush;
    if (str::Eq(tool, StrL("underline"))) return CmdCreateAnnotUnderline;
    if (str::Eq(tool, StrL("strikeout"))) return CmdCreateAnnotStrikeOut;
    if (str::Eq(tool, StrL("fountain"))) return CmdInkFountain;
    if (str::Eq(tool, StrL("brush"))) return CmdInkBrush;
    if (str::Eq(tool, StrL("pencil"))) return CmdInkPencil;
    if (str::Eq(tool, StrL("marker"))) return CmdInkHighlighter;
    if (str::Eq(tool, StrL("ballpoint"))) return CmdInkPen;
    return GetCommandIdByName(tool);
}

static bool IsPenPresetCommand(int cmd) {
    return cmd == CmdInkPen || cmd == CmdInkFountain || cmd == CmdInkBrush || cmd == CmdInkPencil ||
           cmd == CmdInkHighlighter;
}

static bool IsPinnableTool(int cmd) {
    for (const auto& button : gPdfAnnotationButtons)
        if (button.cmdId == cmd && cmd != 0) return true;
    return cmd == CmdInkEraser;
}

static void RefreshPinnedBars() {
    for (MainWindow* window : gWindows) {
        ReCreateToolbar(window);
        ToolbarUpdateStateForWindow(window, true);
        ScheduleUiUpdate(window, kUiForceRelayout | kUiToolbarDirty);
    }
}

static void RemovePinnedTool(int index) {
    auto* presets = gSettings->pinnedAnnotationTools;
    if (!presets || index < 0 || index >= len(*presets)) return;
    auto* preset = (*presets)[index];
    str::Free(preset->tool);
    str::Free(preset->color);
    free(preset);
    VecRemoveAt(*presets, index);
    ScheduleSaveSettings();
    uitask::Post(MkFunc0Void(RefreshPinnedBars), "Refresh pinned tools");
}

static int PinnedToolIndex(MainWindow* win, int cmd) {
    auto* presets = gSettings->pinnedAnnotationTools;
    if (!presets) return -1;
    Str tool = PinnedToolName(win, cmd);
    bool presetColor = cmd == CmdCreateAnnotInk || cmd == CmdAnnotationHighlightBrush ||
                       cmd == CmdCreateAnnotUnderline || cmd == CmdCreateAnnotStrikeOut;
    Color color = presetColor ? AnnotCurrentColor(win, cmd) : kColorUnset;
    Str colorName = presetColor ? SerializeColorTemp(color) : StrL("");
    float width = cmd == CmdCreateAnnotInk ? InkPenWidth(win) : 0;
    for (int i = 0; i < len(*presets); i++) {
        auto* preset = (*presets)[i];
        if (str::Eq(preset->tool, tool) && str::Eq(preset->color, colorName) &&
            (cmd != CmdCreateAnnotInk || fabsf(preset->width - width) < 0.001f)) {
            return i;
        }
    }
    return -1;
}

static void PinToolClick(MainWindow* win, VirtMouseEvent* ev) {
    int cmd = ev->target->id;
    int index = PinnedToolIndex(win, cmd);
    if (index >= 0) {
        RemovePinnedTool(index);
        ev->didHandle = true;
        return;
    }
    auto*& presets = gSettings->pinnedAnnotationTools;
    if (!presets) presets = new Vec<PinnedAnnotationTool*>();
    if (len(*presets) >= 32) return;
    Str tool = PinnedToolName(win, cmd);
    bool presetColor = cmd == CmdCreateAnnotInk || cmd == CmdAnnotationHighlightBrush ||
                       cmd == CmdCreateAnnotUnderline || cmd == CmdCreateAnnotStrikeOut;
    Color color = presetColor ? AnnotCurrentColor(win, cmd) : kColorUnset;
    Str colorName = presetColor ? SerializeColorTemp(color) : StrL("");
    float width = cmd == CmdCreateAnnotInk ? InkPenWidth(win) : 0;
    auto* preset = AllocStruct<PinnedAnnotationTool>();
    str::ReplaceWithCopy(&preset->tool, tool);
    str::ReplaceWithCopy(&preset->color, colorName);
    preset->width = width;
    VecAppend(*presets, preset);
    ScheduleSaveSettings();
    uitask::Post(MkFunc0Void(RefreshPinnedBars), "Refresh pinned tools");
    ev->didHandle = true;
}

struct PinnedToolButton : VirtIconButton {
    int index = 0;
    Color inkColor = kColBlack;
    bool hasColor = true;
    void Paint(VirtPaintCtx& ctx) override {
        VirtIconButton::Paint(ctx);
        if (!hasColor) return;
        int sz = UiScalePx(9);
        Rect dot{ctx.bounds.Right() - sz - UiScalePx(2), ctx.bounds.Bottom() - sz - UiScalePx(2), sz, sz};
        ctx.gfx->FillEllipse(dot, TbTextColor());
        dot.Inflate(-UiScalePx(1), -UiScalePx(1));
        ctx.gfx->FillEllipse(dot, inkColor);
    }
};

static void PinnedToolClick(MainWindow* win, VirtMouseEvent* ev) {
    int index = ((PinnedToolButton*)ev->target)->index;
    auto* presets = gSettings->pinnedAnnotationTools;
    if (!presets || index < 0 || index >= len(*presets)) return;
    if (ev->button == 1) {
        RemovePinnedTool(index);
        ev->didHandle = true;
        return;
    }
    auto* preset = (*presets)[index];
    int cmd = PinnedToolCommand(preset->tool);
    Color color = ParseColor(preset->color);
    if (cmd == CmdAnnotationHighlightBrush || cmd == CmdCreateAnnotUnderline || cmd == CmdCreateAnnotStrikeOut) {
        SetAnnotPresetColor(win, cmd, color);
        HwndSendCommand(win->hwndFrame, cmd);
    } else if (IsPenPresetCommand(cmd)) {
        HandlePenToolCommand(win, cmd);
        SetInkPenColor(win, color);
        SetInkPenWidth(win, preset->width);
        StartAnnotationPlacement(win, CmdCreateAnnotInk);
    } else if (cmd > CmdNone) {
        HwndSendCommand(win->hwndFrame, cmd);
    }
    HideToolbarHoverDropdown(win);
    ToolbarUpdateStateForWindow(win, true);
    HwndSetFocus(win->hwndFrame);
    ev->didHandle = true;
}

static void OnToolbarKey(MainWindow*, VirtKeyEvent*);

static ILayout* BuildPinnedTools(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    auto* row = new HBox();
    row->gap = UiScalePx(3);
    auto* presets = gSettings->pinnedAnnotationTools;
    if (!presets) return row;
    for (int i = 0; i < std::min(len(*presets), 32); i++) {
        auto* preset = (*presets)[i];
        auto* button = new PinnedToolButton();
        button->index = i;
        button->id = CmdLast + 2000 + i;
        button->inkColor = ParseColor(preset->color);
        button->hasColor = len(preset->color) > 0;
        button->padding = {UiScalePx(4), UiScalePx(6), UiScalePx(4), UiScalePx(6)};
        button->cornerRadius = UiScalePx(6);
        int cmd = PinnedToolCommand(preset->tool);
        const char* svg = kEnhancedIconInk;
        for (const auto& info : gPdfAnnotationButtons)
            if (info.cmdId == cmd && info.icon) svg = info.icon;
        if (cmd == CmdInkEraser) svg = kEnhancedIconEraser;
        Str penSvg = Str(svg);
        int penCommands[] = {CmdInkPen, CmdInkFountain, CmdInkBrush, CmdInkPencil, CmdInkHighlighter};
        for (int pen = 0; pen < dimof(penCommands); pen++) {
            if (cmd == penCommands[pen]) penSvg = InkPenSvg(pen, button->inkColor);
        }
        button->pixmap = GetCachedPixmapForSvg(penSvg, tb->iconSize, tb->iconSize, TbTextColor(), TbBgColor());
        button->pixmapDisabled =
            GetCachedPixmapForSvg(penSvg, tb->iconSize, tb->iconSize, TbDisabledColor(), TbBgColor());
        Str name = GetCommandDescription(cmd);
        button->SetTooltip(button->hasColor
                               ? fmt("%s - %s - %.2f pt. Right-click to unpin.", name, preset->color, preset->width)
                               : fmt("%s. Right-click to unpin.", name));
        button->onClick = MkFunc1(PinnedToolClick, win);
        button->onKeyDown = MkFunc1(OnToolbarKey, win);
        button->SetFlag(vwfFocusable, true);
        row->AddChild(button);
        VecAppend(tb->pinnedItems, (VirtCtrl*)button);
    }
    return row;
}

static bool ShowPinToolMenu(MainWindow* win, int cmd) {
    if (!IsPinnableTool(cmd)) return false;
    Rect anchor = GetToolbarButtonScreenRect(win, cmd);
    HideToolbarHoverDropdown(win);
    HMENU menu = CreatePopupMenu();
    bool isPinned = PinnedToolIndex(win, cmd) >= 0;
    AppendMenuW(menu, MF_STRING | (isPinned ? MF_CHECKED : 0), 1,
                CWStrTemp(ToWStrTemp(isPinned ? Tr("Unpin this tool") : Tr("Pin this tool"))));
    if (IsAnnotColorCmd(cmd)) AppendMenuW(menu, MF_STRING, 2, ToWStrTemp(Tr("Tools, colors and settings")).s);
    MarkMenuOwnerDraw(menu);
    int picked =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, anchor.x, anchor.Bottom(), 0, win->hwndFrame, nullptr);
    FreeMenuOwnerDrawInfoData(menu);
    DestroyMenu(menu);
    if (picked == 1) {
        VirtCtrl target;
        target.id = cmd;
        VirtMouseEvent event;
        event.target = &target;
        PinToolClick(win, &event);
    } else if (picked == 2) {
        ShowToolbarButtonDropdown(win, cmd);
    }
    ToolbarNoteDropdownClosed();
    return true;
}

static void ShowToolbarOverflow(MainWindow* win, int anchorId);
static void OnToolbarKey(MainWindow* win, VirtKeyEvent* ev);

static void OnOverflowClicked(MainWindow* win, VirtMouseEvent* ev) {
    if (!ToolbarDropdownJustClosed()) ShowToolbarOverflow(win, ev->target->id);
    ev->didHandle = true;
}

static void OnToolbarScrollClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* tb = win->toolbarVirt;
    auto* line = ev->target->userData ? tb->annotationLine : tb->mainRow;
    HideToolbarHoverDropdown(win);
    line->Scroll(ev->target == line->previousButton ? -1 : 1);
    ev->didHandle = true;
}

void RevealToolbarTool(MainWindow* win, int cmd) {
    auto* tb = win->toolbarVirt;
    if (!tb) return;
    auto contains = [&](auto&& self, ILayout* item) -> bool {
        if (auto* ctrl = item->AsVirtCtrl()) {
            if (ctrl->id == cmd) return true;
            int index = VecFind(tb->pinnedItems, ctrl);
            if (index >= 0 && gSettings) {
                auto* presets = gSettings->pinnedAnnotationTools;
                if (presets && index < len(*presets) && (*presets)[index] &&
                    PinnedToolCommand((*presets)[index]->tool) == cmd)
                    return true;
            }
        }
        for (int i = 0; i < item->LayoutChildCount(); i++)
            if (self(self, item->LayoutChildAt(i))) return true;
        return false;
    };
    for (ToolbarLine* line : {tb->mainRow, tb->annotationLine}) {
        if (!line || (line == tb->annotationLine && IsCollapsed(tb->annotationRow))) continue;
        for (int i = 0; i < len(line->children) - 3; i++) {
            auto* group = line->children[i].layout;
            if (!contains(contains, group)) continue;
            if (IsCollapsed(group)) {
                line->firstGroup = i;
                tb->host->Relayout();
                tb->host->Invalidate(true);
            }
            return;
        }
    }
}

bool FocusToolbar(MainWindow* win, bool backwards) {
    auto* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || !tb->host->IsVisible() || !tb->host->vroot) return false;
    Vec<TabStop> stops;
    CollectTabStops(tb->host->layout, stops);
    for (int j = 0; j < len(stops); j++) {
        auto& stop = stops[backwards ? len(stops) - 1 - j : j];
        if (!stop.vwnd) continue;
        tb->host->vroot->SetFocus(stop.vwnd);
        HwndSetFocusForce(tb->host->native);
        return true;
    }
    return false;
}

static void OnToolbarKey(MainWindow* win, VirtKeyEvent* ev) {
    if (ev->vkey == VK_ESCAPE) {
        HideToolbarHoverDropdown(win);
        HwndSetFocus(win->hwndFrame);
        ev->didHandle = true;
        return;
    }
    if (ev->vkey == VK_LEFT || ev->vkey == VK_RIGHT) {
        auto* root = win->toolbarVirt->host->vroot;
        if (root) root->TabNavigate((ev->vkey == VK_LEFT) != IsUIRtl());
        ev->didHandle = true;
        return;
    }
    bool palette = ev->vkey == VK_DOWN || (ev->vkey == VK_F10 && ev->isShift);
    if (palette && ev->target->id != ToolbarOverflowId && ev->target->id != ToolbarAnnotOverflowId) {
        if (ShowToolbarButtonDropdown(win, ev->target->id)) {
            auto* hover = win->toolbarVirt->hoverHost;
            if (hover && hover->vroot) hover->vroot->TabNavigate(false);
            ev->didHandle = true;
        }
        return;
    }
    if (ev->vkey != VK_RETURN && ev->vkey != VK_SPACE && !palette) return;
    VirtMouseEvent click;
    click.target = ev->target;
    click.hit = ev->target;
    if (ev->target->onClick.IsValid()) ev->target->onClick.Call(&click);
    ev->didHandle = true;
}

enum class ToolbarMenuActionKind {
    Palette,
    Pinned,
    Visibility,
    Restore
};
struct ToolbarMenuAction {
    ToolbarMenuActionKind kind = ToolbarMenuActionKind::Palette;
    int id = 0;
};

static void AppendToolbarMenu(HMENU menu, int cmdId, Str text, bool enabled, bool checked = false) {
    UINT flags = MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED) | (checked ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, flags, cmdId, CWStrTemp(ToWStrTemp(text)));
}

static void ShowToolbarOverflow(MainWindow* win, int anchorId) {
    auto* tb = win->toolbarVirt;
    Rect anchor = GetToolbarButtonScreenRect(win, anchorId);
    if (!tb || anchor.IsEmpty()) return;
    Vec<VirtCtrl*> hidden = anchorId == ToolbarAnnotOverflowId ? tb->annotationItems : tb->items;
    if (anchorId != ToolbarAnnotOverflowId && tb->annotationExpanded)
        for (VirtCtrl* annotation : tb->annotationItems) VecAppend(hidden, annotation);
    if (anchorId != ToolbarAnnotOverflowId)
        for (VirtCtrl* pinned : tb->pinnedItems) VecAppend(hidden, pinned);
    CollapseFindBar(win);
    HideToolbarHoverDropdown(win);
    HMENU menu = CreatePopupMenu();
    Vec<ToolbarMenuAction> actions;
    constexpr int kActionFirst = 60000;
    auto addPalette = [&](HMENU sub, int cmdId, bool enabled) {
        int localId = kActionFirst + len(actions);
        VecAppend(actions, ToolbarMenuAction{ToolbarMenuActionKind::Palette, cmdId});
        AppendToolbarMenu(sub, localId, Tr("Tools, colors and settings"), enabled);
    };
    auto* ctx = NewBuildMenuCtx(win->CurrentTab(), Point{0, 0});
    AutoCall delCtx(DeleteBuildMenuCtx, ctx);
    Vec<int> added;
    auto addItem = [&](VirtCtrl* ctrl) {
        if (!ctrl || !ctrl->id || ctrl->id == WarningMsgId) return;
        int pinned = VecFind(tb->pinnedItems, ctrl);
        if (pinned >= 0) {
            int localId = kActionFirst + len(actions);
            VecAppend(actions, ToolbarMenuAction{ToolbarMenuActionKind::Pinned, pinned});
            AppendToolbarMenu(menu, localId, ctrl->tooltip, ctrl->IsEnabled());
            return;
        }
        int cmdId = ctrl->id == PageInfoId ? CmdGoToPage : ctrl->id;
        if (VecFind(added, cmdId) >= 0) return;
        VecAppend(added, cmdId);
        auto* icon = AsVirtIconButton(ctrl);
        bool checked = icon && icon->isSelected;
        bool enabled = IsCmdEnabled(win, cmdId, ctx) && ctrl->IsEnabled();
        Str label = GetCommandDescription(cmdId);
        if (auto* custom = FindCustomCommand(cmdId)) label = custom->name;
        if (len(label) == 0) label = ctrl->tooltip;
        if (len(label) == 0) return;
        if (cmdId == CmdSinglePageView || cmdId == CmdZoomIn || cmdId == CmdZoomOut || IsAnnotColorCmd(cmdId) ||
            cmdId == CmdToggleLaserPointer || cmdId == CmdSaveAnnotations) {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(menu, MF_POPUP | (enabled ? 0 : MF_GRAYED), (UINT_PTR)sub, CWStrTemp(ToWStrTemp(label)));
            if (cmdId == CmdSinglePageView) {
                DisplayMode mode = win->ctrl ? win->ctrl->GetDisplayMode() : DisplayMode::SinglePage;
                AppendToolbarMenu(sub, CmdSinglePageView, Tr("Single Page"), enabled, IsSingle(mode));
                AppendToolbarMenu(sub, CmdFacingView, Tr("Facing"), enabled, IsFacing(mode));
                AppendToolbarMenu(sub, CmdBookView, Tr("Book View"), enabled, IsBookView(mode));
                AppendToolbarMenu(sub, CmdToggleContinuousView, Tr("Continuous scrolling"), enabled,
                                  IsContinuous(mode));
                return;
            }
            AppendToolbarMenu(sub, cmdId, label, enabled, checked);
            if (cmdId == CmdZoomIn || cmdId == CmdZoomOut) {
                Vec<ZoomHoverLevel> levels;
                ZoomHoverLevels(levels);
                int current = ZoomHoverCurrentIdx(win, levels);
                for (int i = 0; i < len(levels); i++) {
                    Str text = levels[i].zoom == kZoomFitPage    ? Tr("Fit page")
                               : levels[i].zoom == kZoomFitWidth ? Tr("Fit width")
                                                                 : fmt("%.0f%%", levels[i].zoom);
                    AppendToolbarMenu(sub, levels[i].cmdId, text, enabled, i == current);
                }
                AppendToolbarMenu(sub, CmdZoomCustom, Tr("Custom zoom"), enabled);
                return;
            }
            if (cmdId == CmdCreateAnnotInk) {
                int pens[] = {CmdInkPen, CmdInkFountain, CmdInkBrush, CmdInkPencil, CmdInkHighlighter};
                InkPenStyle styles[] = {InkPenStyle::Ballpoint, InkPenStyle::Fountain, InkPenStyle::Brush,
                                        InkPenStyle::Pencil, InkPenStyle::Highlighter};
                for (int i = 0; i < dimof(pens); i++)
                    AppendToolbarMenu(sub, pens[i], GetCommandDescription(pens[i]), enabled,
                                      win->inkEraseMode == 0 && win->inkPenStyle == styles[i]);
            }
            addPalette(sub, cmdId, enabled);
            return;
        }
        if (cmdId == CmdThemeLight) checked = IsLightColor(ThemeWindowBackgroundColor());
        if (cmdId == CmdThemeDark) checked = !IsLightColor(ThemeWindowBackgroundColor());
        if (cmdId == CmdInvertColors) checked = GetInvertPageColors();
        if (cmdId == CmdToggleBookmarks) checked = IsSidebarViewShown(win, SidebarView::Bookmarks);
        AppendToolbarMenu(menu, cmdId, label, enabled, checked);
    };
    for (VirtCtrl* ctrl : hidden) addItem(ctrl);
    if (GetMenuItemCount(menu) > 0) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU visibility = CreatePopupMenu();
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)visibility, CWStrTemp(ToWStrTemp(Tr("Show or hide toolbar items"))));
    Vec<int> listed;
    auto addVisibility = [&](const ToolbarButtonInfo& item) {
        int cmd = item.cmdId;
        if (!cmd || cmd == WarningMsgId || VecFind(listed, cmd) >= 0) return;
        Str label = cmd == PageInfoId ? Tr("Page number") : GetCommandDescription(cmd);
        if (len(label) == 0) label = item.toolTip;
        if (len(label) == 0) return;
        VecAppend(listed, cmd);
        int localId = kActionFirst + len(actions);
        VecAppend(actions, ToolbarMenuAction{ToolbarMenuActionKind::Visibility, cmd});
        AppendToolbarMenu(visibility, localId, label, true, !ToolbarItemHidden(cmd));
    };
    for (int i = 0; i < TotalButtonsCount(); i++) addVisibility(GetToolbarButtonInfoByIdx(i));
    for (const auto& item : gPdfAnnotationButtons) addVisibility(item);
    AppendMenuW(visibility, MF_SEPARATOR, 0, nullptr);
    int restoreId = kActionFirst + len(actions);
    VecAppend(actions, ToolbarMenuAction{ToolbarMenuActionKind::Restore, 0});
    AppendToolbarMenu(visibility, restoreId, Tr("Reset hidden toolbar items"), true);
    int picked = TrackScrollMenu(win->hwndFrame, menu, {IsUIRtl() ? anchor.Right() : anchor.x, anchor.Bottom()},
                                 tb->platformFont, gSettings->tabListVisibleItems, UiScalePx(480), TbBgColor(),
                                 ThemeWindowTextColor(), AccentColor(TbBgColor(), 25), IsUIRtl(), IsUIRtl());
    DestroyMenu(menu);
    ToolbarNoteDropdownClosed();
    if (!picked || !IsMainWindowValidAndNotClosing(win)) return;
    if (picked >= kActionFirst && picked - kActionFirst < len(actions)) {
        auto action = actions[picked - kActionFirst];
        if (action.kind == ToolbarMenuActionKind::Palette) {
            RevealToolbarTool(win, action.id);
            ShowToolbarButtonDropdown(win, action.id);
            auto* hover = win->toolbarVirt->hoverHost;
            if (hover && hover->vroot) hover->vroot->TabNavigate(false);
        } else if (action.kind == ToolbarMenuActionKind::Visibility || action.kind == ToolbarMenuActionKind::Restore) {
            if (action.kind == ToolbarMenuActionKind::Restore)
                str::ReplaceWithCopy(&gSettings->toolbarHiddenItems, StrL(""));
            else
                SetToolbarItemHidden(action.id, !ToolbarItemHidden(action.id));
            ScheduleSaveSettings();
            uitask::Post(MkFunc0Void(RefreshPinnedBars), "Refresh toolbar visibility");
        } else if (action.id < len(win->toolbarVirt->pinnedItems)) {
            VirtMouseEvent ev;
            ev.target = win->toolbarVirt->pinnedItems[action.id];
            PinnedToolClick(win, &ev);
        }
        return;
    }
    ToolbarPostCommand(win, picked);
}

static void BuildToolbarLayout(MainWindow* win) {
    PopulateToolbarLayout();
    PopulateCustomToolbarButtons();

    ToolbarVirt* tb = win->toolbarVirt;
    VecReset(tb->items);
    VecReset(tb->annotationItems);
    VecReset(tb->annotationGroups);
    tb->annotationExpanded = false;
    tb->annotationLine = nullptr;
    tb->annotationOverflowButton = nullptr;
    VecReset(tb->pinnedItems);
    tb->zoomEdit = nullptr;
    tb->findSlot = nullptr;
    tb->findButton = nullptr;
    tb->annotationRow = nullptr;
    tb->pageLabel = nullptr;
    tb->pageLabel2 = nullptr;
    tb->pageTotal = nullptr;
    tb->chapterTotal = nullptr;
    win->pageEdit = nullptr;
    win->chapterEdit = nullptr;

    int cyPad = ToolbarCyPad();
    int iconPad = UiScalePx(6);
    tb->rowDy = ToolbarRowDy(tb->iconSize);
    Color fg = TbTextColor();
    Color dis = TbDisabledColor();

    auto* mainRow = new ToolbarLine(tb);
    tb->mainRow = mainRow;
    mainRow->gap = UiScalePx(kButtonSpacingX);
    mainRow->rtl = IsUIRtl();
    bool prettyLayout = str::IsEmptyOrWhiteSpace(gSettings->toolbarCustomLayout);
    if (prettyLayout) {
        auto* brand = new VirtCustom();
        int badgeDx = UiScalePx(std::max(28, limitValue(gSettings->toolbarSize, 8, 64)) + 6);
        int brandDx = PlatformFontMeasureText(GetAppFont(), StrL("SumatraPDF Enhanced")).dx + badgeDx;
        brand->idealSize = {std::max(UiScalePx(176), brandDx),
                            std::max(UiScalePx(36), PlatformFontLineHeight(GetAppFont()) * 2 + UiScalePx(4))};
        brand->SetFlag(vwfNoHitTest, true);
        brand->onPaint = MkFunc1(PaintToolbarBrand, brand);
        mainRow->brandIdx = 0;
        mainRow->AddChild(brand);
    }

    auto newGroup = [&]() {
        auto* group = new HBox();
        group->alignCross = CrossAxisAlign::CrossCenter;
        group->rtl = mainRow->rtl;
        return group;
    };
    HBox* group = newGroup();
    HBox* pinnedGroup = nullptr;
    HBox* appearance = newGroup();
    int n = TotalButtonsCount();
    for (int i = 0; i < n; i++) {
        const ToolbarButtonInfo& bi = GetToolbarButtonInfoByIdx(i);
        VirtCtrl* w = nullptr;
        bool noTranslate = i >= gLayoutButtonsCount;
        if (bi.cmdId == PageInfoId) {
            // Old toolbar: label HWND was text + kTextPaddingRight + kButtonSpacingX
            // (10dpi) so "Page:" and "/ N" were not flush against the edit.
            int pageGap = UiScalePx(kTextPaddingRight) + UiScalePx(kButtonSpacingX);
            auto* label = new VirtText(Tr("Page:"), tb->platformFont);
            label->isRtl = mainRow->rtl;
            label->SetColor(kColText, fg);
            label->padding = {0, pageGap, 0, UiScalePx(4)};
            label->id = PageInfoId;
            tb->pageLabel = label;
            group->AddChild(label);

            // chapter box: [chapterEdit] / N, hidden unless HasChapters()
            Edit* chapterEdit = ToolbarCreateChapterEdit(win, tb->platformFont, tb->iconSize);
            chapterEdit->SetVisibility(Visibility::Collapse);
            win->chapterEdit = chapterEdit;
            group->AddChild(chapterEdit);
            VecAppend(mainRow->locationFields, ToolbarLocationField{(ToolbarLocationEdit*)chapterEdit, group});

            auto* chapterTotal = new VirtText(StrL(" "), tb->platformFont);
            chapterTotal->isRtl = mainRow->rtl;
            chapterTotal->SetColor(kColText, fg);
            chapterTotal->padding = {0, UiScalePx(4), 0, pageGap};
            chapterTotal->id = PageInfoId;
            chapterTotal->SetVisibility(Visibility::Collapse);
            tb->chapterTotal = chapterTotal;
            group->AddChild(chapterTotal);

            // second "Page:" label, shown before pageEdit only for HasChapters() docs
            auto* label2 = new VirtText(Tr("Page:"), tb->platformFont);
            label2->isRtl = mainRow->rtl;
            label2->SetColor(kColText, fg);
            label2->padding = {0, pageGap, 0, UiScalePx(4)};
            label2->id = PageInfoId;
            label2->SetVisibility(Visibility::Collapse);
            tb->pageLabel2 = label2;
            group->AddChild(label2);

            Edit* pageEdit = ToolbarCreatePageEdit(win, tb->platformFont, tb->iconSize);
            win->pageEdit = pageEdit;
            group->AddChild(pageEdit);
            VecAppend(mainRow->locationFields, ToolbarLocationField{(ToolbarLocationEdit*)pageEdit, group});

            auto* total = new VirtText(StrL(" "), tb->platformFont);
            total->isRtl = mainRow->rtl;
            total->SetColor(kColText, fg);
            total->padding = {0, UiScalePx(4), 0, pageGap};
            total->id = PageInfoId;
            tb->pageTotal = total;
            group->AddChild(total);
            VecAppend(tb->items, label);
            continue;
        }
        if (bi.cmdId == 0 || !HasToolbarButtonContent(bi)) {
            w = MakeToolbarSeparator(tb->rowDy);
        } else if (bi.isText) {
            auto* b = new VirtButton(noTranslate ? bi.toolTip : trans::GetTranslation(bi.toolTip), tb->platformFont);
            b->isRtl = mainRow->rtl;
            b->textPadding = {cyPad, iconPad, cyPad, iconPad};
            w = b;
        } else {
            auto* ib = new VirtIconButton();
            ib->padding = {cyPad, iconPad, cyPad, iconPad};
            ib->hasDropdown = (bi.cmdId == CmdToggleReadAloud);
            if (bi.cmdId == CmdOpenFile) {
                ib->label = Tr("Open");
                ib->labelFont = tb->platformFont;
            }
            Str svg = bi.svgIcon ? bi.svgIcon : Str(bi.icon);
            ib->pixmap = GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize,
                                               bi.cmdId == CmdOpenFile ? ThemeBrandTextColor() : fg, TbBgColor());
            ib->pixmapDisabled = GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, dis, TbBgColor());
            w = ib;
        }
        w->id = bi.cmdId;
        if (bi.cmdId == CmdToggleEditPDF) pinnedGroup = group;
        ApplyToolbarItemColors(w);
        if (bi.toolTip) {
            bool translate = !noTranslate && !bi.isText;
            w->SetTooltip(ToolbarTipTemp(bi.cmdId, bi.toolTip, translate));
        }
        if (bi.cmdId != 0 && bi.cmdId != PageInfoId) {
            w->onClick = MkFunc1(OnToolbarButtonClicked, win);
            w->onKeyDown = MkFunc1(OnToolbarKey, win);
            w->SetFlag(vwfFocusable, true);
        }
        VecAppend(tb->items, w);
        if (prettyLayout && IsAppearanceCmd(bi.cmdId)) {
            appearance->AddChild(w);
        } else {
            group->AddChild(w);
        }
        if (bi.cmdId == CmdZoomIn) {
            tb->zoomEdit = CreateZoomEntry(win);
            group->AddChild(tb->zoomEdit);
        }
        if (bi.cmdId == CmdFindFirst && !tb->findSlot) {
            tb->findButton = w;
            mainRow->findGroupIdx = len(mainRow->children);
            if (auto* button = AsVirtIconButton(w)) button->cornerRadius = tb->iconSize;
            tb->findSlot = new Spacer(0, tb->rowDy - UiScalePx(12));
            tb->findSlot->SetVisibility(Visibility::Collapse);
            group->AddChild(tb->findSlot);
        }
        if (bi.cmdId == 0) {
            mainRow->AddChild(group);
            group = newGroup();
        }
    }
    mainRow->AddChild(group);
    if (gSettings->pinnedAnnotationTools && len(*gSettings->pinnedAnnotationTools) > 0) {
        auto* pinned = (HBox*)BuildPinnedTools(win);
        if (!pinnedGroup) {
            pinnedGroup = newGroup();
            mainRow->AddChild(pinnedGroup);
        }
        int insertAt = len(pinnedGroup->children);
        if (insertAt > 0) {
            auto* last = pinnedGroup->children[insertAt - 1].layout->AsVirtCtrl();
            if (last && last->id == 0) insertAt--;
        }
        for (auto& item : pinned->children) VecInsertAt(pinnedGroup->children, insertAt++, item);
        VecClear(pinned->children);
        delete pinned;
    }

    int annotationInsertAt = len(mainRow->children);
    for (int i = 0; i < len(mainRow->children); i++) {
        if (mainRow->children[i].layout == pinnedGroup) {
            annotationInsertAt = i + 1;
            break;
        }
    }
    auto* annotationGroup = newGroup();
    auto addAnnotationGroup = [&]() {
        if (len(annotationGroup->children) == 0) return;
        annotationGroup->SetVisibility(Visibility::Collapse);
        VecAppend(tb->annotationGroups, (ILayout*)annotationGroup);
        if (mainRow->findGroupIdx >= annotationInsertAt) mainRow->findGroupIdx++;
        VecInsertAt(mainRow->children, annotationInsertAt++, boxElementInfo{annotationGroup});
    };
    for (const ToolbarButtonInfo& bi : gPdfAnnotationButtons) {
        bool duplicate = false;
        for (VirtCtrl* existing : tb->items) {
            if (bi.cmdId && existing && existing->id == bi.cmdId) {
                duplicate = true;
                break;
            }
        }
        if (duplicate || (!bi.cmdId && len(annotationGroup->children) == 0)) {
            VecAppend(tb->annotationItems, (VirtCtrl*)nullptr);
            continue;
        }
        VirtCtrl* w = nullptr;
        if (!HasToolbarButtonContent(bi)) {
            w = MakeToolbarSeparator(tb->rowDy);
        } else {
            auto* ib = new VirtIconButton();
            ib->padding = {cyPad, iconPad, cyPad, iconPad};
            ib->pixmap = GetCachedPixmapForSvg(Str(bi.icon), tb->iconSize, tb->iconSize, fg, TbBgColor());
            ib->pixmapDisabled = GetCachedPixmapForSvg(Str(bi.icon), tb->iconSize, tb->iconSize, dis, TbBgColor());
            w = ib;
        }
        w->id = bi.cmdId;
        ApplyToolbarItemColors(w);
        if (bi.toolTip) {
            w->SetTooltip(ToolbarTipTemp(bi.cmdId, bi.toolTip, true));
        }
        if (bi.cmdId != 0) {
            w->onClick = MkFunc1(OnToolbarButtonClicked, win);
            w->onKeyDown = MkFunc1(OnToolbarKey, win);
            w->SetFlag(vwfFocusable, true);
        }
        VecAppend(tb->annotationItems, w);
        annotationGroup->AddChild(w);
        if (bi.cmdId == 0) {
            addAnnotationGroup();
            annotationGroup = newGroup();
        }
    }
    if (len(annotationGroup->children) > 0)
        addAnnotationGroup();
    else
        delete annotationGroup;

    mainRow->AddChild(appearance);
    auto addScrollButtons = [&](ToolbarLine* line, bool annotations) {
        for (int direction : {-1, 1}) {
            auto* button = new VirtIconButton();
            button->id = CmdLast + 30 + (annotations ? 2 : 0) + (direction > 0 ? 1 : 0);
            button->userData = annotations ? 1 : 0;
            button->padding = {cyPad, iconPad, cyPad, iconPad};
            Str svg = Str(direction < 0 ? kEnhancedIconPrevious : kEnhancedIconNext);
            button->pixmap = GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, fg, TbBgColor());
            button->pixmapDisabled = GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, dis, TbBgColor());
            button->SetTooltip(direction < 0 ? Tr("Scroll toolbar left") : Tr("Scroll toolbar right"));
            button->SetFlag(vwfFocusable, true);
            button->onClick = MkFunc1(OnToolbarScrollClicked, win);
            button->onKeyDown = MkFunc1(OnToolbarKey, win);
            ApplyToolbarItemColors(button);
            if (direction < 0)
                line->previousButton = button;
            else
                line->nextButton = button;
            line->AddChild(button);
        }
    };
    addScrollButtons(mainRow, false);
    auto* overflow = new VirtIconButton();
    overflow->id = ToolbarOverflowId;
    overflow->padding = {cyPad, iconPad, cyPad, iconPad};
    overflow->pixmap = GetCachedPixmapForSvg(Str(gIconChevronDownBold), tb->iconSize, tb->iconSize, fg, TbBgColor());
    overflow->SetTooltip(Tr("Toolbar commands and visibility"));
    overflow->SetFlag(vwfFocusable, true);
    overflow->onClick = MkFunc1(OnOverflowClicked, win);
    overflow->onKeyDown = MkFunc1(OnToolbarKey, win);
    ApplyToolbarItemColors(overflow);
    tb->overflowButton = overflow;
    mainRow->AddChild(overflow);

    SetToolbarHoverDropdown(win, CmdToggleLaserPointer, MkFunc1(BuildLaserHoverMenu, win));
    SetToolbarHoverDropdown(win, CmdSinglePageView, MkFunc1(BuildLayoutHoverMenu, win));
    SetToolbarHoverDropdown(win, CmdSaveAnnotations, MkFunc1(BuildSaveHoverMenu, win));
    // one strip for the two of them, so it doesn't jump when the mouse crosses
    // from one to the other
    SetToolbarHoverDropdown(win, CmdZoomIn, MkFunc1(BuildZoomHoverMenu, win), CmdZoomIn);
    SetToolbarHoverDropdown(win, CmdZoomOut, MkFunc1(BuildZoomHoverMenu, win), CmdZoomIn);
    // no shared group: each of them shows the color it is set to
    for (int cmdId : kAnnotColorCmds) {
        SetToolbarHoverDropdown(win, cmdId, MkFunc1(BuildAnnotColorsHoverMenu, win));
    }

    auto* root = new VBox();
    root->alignCross = CrossAxisAlign::Stretch;
    root->AddChild(new Padding(mainRow, Insets{UiScalePx(6), UiScalePx(8), UiScalePx(6), UiScalePx(8)}));
    tb->host->SetLayout(root);
}

static void PaintToolbarBackground(MainWindow* win, VirtHostPaintEvent* ev) {
    ev->gfx->FillRect(ev->clientRect, TbBgColor());
    ToolbarVirt* tb = win->toolbarVirt;
    auto groupBounds = [&](auto&& self, ILayout* layout, Rect& bounds) -> void {
        if (layout->GetVisibility() != Visibility::Visible) return;
        if (auto* control = layout->AsVirtCtrl()) {
            if (control->id == 0) return;
            Rect rect = control->BoundsInWindow();
            bounds = bounds.IsEmpty() ? rect : bounds.Union(rect);
            return;
        }
        for (int i = 0; i < layout->LayoutChildCount(); i++) self(self, layout->LayoutChildAt(i), bounds);
    };
    if (tb->mainRow) {
        for (auto& item : tb->mainRow->children) {
            if (item.layout->LayoutChildCount() == 0) continue;
            Rect bounds;
            groupBounds(groupBounds, item.layout, bounds);
            if (!bounds.IsEmpty())
                ev->gfx->FillRoundedRect(bounds, UiScalePx(9), ThemeHotBackgroundColor(), ThemeEdgeColor());
        }
    }
    VirtCtrl* focused = tb->host->vroot ? tb->host->vroot->focused : nullptr;
    if (focused && focused->IsVisible() && tb->host->HasFocus()) {
        ev->gfx->FillRoundedRect(focused->BoundsInWindow(), UiScalePx(8), kColorTransparent, ThemeBrandColor());
    }
}

// the default theme separates the toolbar from the canvas with a hairline.
// Use the document background, not ThemeEdgeColor: on Light that is #c0c0c0
// and reads as a dark strip against the page.
static void PaintToolbarEdge(MainWindow*, VirtHostPaintEvent* ev) {
    if (!IsCurrentThemeDefault() || ThemeColorizeControls()) {
        return;
    }
    Color canvasBg;
    ThemeDocumentColors(canvasBg);
    Rect rc = ev->clientRect;
    int y = ToolbarAtBottom() ? rc.y : (rc.Bottom() - 1);
    ev->gfx->FillRect({rc.x, y, rc.dx, 1}, canvasBg);
}

static void OnToolbarSize(MainWindow* win, Size size) {
    VirtHost* host = ToolbarHost(win);
    if (!host || !host->layout || size.dx <= 0) {
        return;
    }
    int height = host->layout->MinIntrinsicHeight(size.dx);
    if (height > 0 && height != size.dy) {
        ToolbarSetHeight(win, height);
        ScheduleUiUpdate(win, kUiForceRelayout);
    }
}

static const WStr kToolbarHostClass = WStrL(L"SUMATRA_VIRT_TOOLBAR");

void CreateToolbar(MainWindow* win) {
    if (win->frameDpi > 0) {
        DpiSet(win->frameDpi, win->frameDpi);
    }
    int iconSize = ToolbarIconSize();

    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = kToolbarHostClass;
    args.initialSize = {100, ToolbarRowDy(iconSize)};
    args.bgColor = TbBgColor();
    args.isRtl = IsUIRtl();
    args.visible = true;
    // the old Win32 toolbar did not take the keyboard focus; a generic child
    // would, and then accelerators (Ctrl+W, â€¦) never reached the frame
    args.noActivate = true;
    // in overlay mode the canvas is a lower-Z sibling and would otherwise
    // paint over the floating toolbar
    args.clipSiblings = true;
    args.userData = win;

    VirtHost* host = VirtHost::Create(args);
    if (!host) {
        return;
    }
    host->onPaintBackground = MkFunc1(PaintToolbarBackground, win);
    host->onPaint = MkFunc1(PaintToolbarEdge, win);
    host->onTimer = MkFunc1(OnToolbarTimer, win);
    host->onSizeChanged = MkFunc1(OnToolbarSize, win);
    host->onMouseMove = MkFunc1(OnToolbarMouseMove, win);
    host->onMouseLeave = MkFunc0(OnToolbarMouseLeave, win);
    ToolbarSetNativeHooks(win, host);

    auto* tb = new ToolbarVirt();
    tb->host = host;
    tb->iconSize = iconSize;
    tb->platformFont = GetAppFontForDpi(win->frameDpi > 0 ? win->frameDpi : DpiGet());
    win->toolbarVirt = tb;
    win->hwndToolbar = host->native;
    host->SetFont(tb->platformFont);

    BuildToolbarLayout(win);

    DocController* ctrl = win->ctrl;
    UpdateToolbarPageText(win, ctrl ? ctrl->PageCount() : -1);
    if (ctrl && win->pageEdit) {
        if (ShowChapterUi(ctrl)) {
            Location cur = ctrl->CurrentLocation();
            win->pageEdit->SetText(fmt("%d", cur.page));
            if (win->chapterEdit) {
                win->chapterEdit->SetText(fmt("%d", cur.chapter));
            }
        } else {
            TempStr label = ctrl->GetPageLabeTemp(ctrl->CurrentPageNo());
            win->pageEdit->SetText(label);
        }
        EditSetNumbersOnly(win->pageEdit, !ctrl->HasPageLabels());
    }
    UpdateToolbarFindText(win);
    ToolbarUpdateStateForWindow(win, true);
}

void DestroyToolbar(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (!tb) {
        win->hwndToolbar = nullptr;
        return;
    }
    HideToolbarHoverDropdown(win);
    DeleteAnnotFilterToolbar(win);
    win->pageEdit = nullptr;
    win->chapterEdit = nullptr;
    win->toolbarVirt = nullptr;
    win->hwndToolbar = nullptr;
    delete tb->host;
    delete tb;
}

void ReCreateToolbar(MainWindow* win) {
    DestroyToolbar(win);
    CreateToolbar(win);
}

// What the toolbar still needs Win32 for, now that VirtHost owns its window:
// the colors of the native page-number edit, dragging the frame by an empty
// part of the toolbar, eating the click that dismissed a drop-down menu, and
// reaching the frame and canvas windows (which are not VirtHosts yet).

//--- the frame and the canvas are still plain HWNDs

// canvas rectangle in frame-client coordinates
Rect ToolbarCanvasRectInFrame(MainWindow* win) {
    Rect rc = HwndWindowRect(win->hwndCanvas);
    Point tl = HwndScreenToClient(win->hwndFrame, rc.TL());
    return {tl, rc.Size()};
}

// a screen point in frame-client coordinates
Point ToolbarScreenToFrame(MainWindow* win, Point pt) {
    return HwndScreenToClient(win->hwndFrame, pt);
}

// repaint what the overlay toolbar was covering after it hides
void ToolbarRepaintUncovered(MainWindow* win, Rect rInFrame) {
    HwndInvalidate(win->hwndCanvas);
    HwndInvalidateRect(win->hwndFrame, rInFrame, false);
}

void ToolbarFocusFrame(MainWindow* win) {
    HwndSetFocus(win->hwndFrame);
}

bool ToolbarFrameIsVisible(MainWindow* win) {
    return HwndIsVisible(win->hwndFrame);
}

void ToolbarPostCommand(MainWindow* win, int cmdId) {
    RevealToolbarTool(win, cmdId);
    LPARAM commandPoint = 0;
    if (cmdId >= CmdCreateAnnotFirst && cmdId <= CmdCreateAnnotLast && !CommandUsesPlacementMode(cmdId)) {
        Rect canvas = HwndClientRect(win->hwndCanvas);
        Point pt{canvas.dx / 2, canvas.dy / 2};
        commandPoint = MAKELPARAM(pt.x, pt.y);
    }
    HwndPostCommand(win->hwndFrame, cmdId, commandPoint);
}

void ToolbarSetHeight(MainWindow* win, int dy) {
    HWND hwnd = win ? win->hwndToolbar : nullptr;
    if (!hwnd || dy <= 0) {
        return;
    }
    Rect r = ChildPosWithinParent(hwnd);
    if (r.dy == dy) {
        return;
    }
    SetWindowPos(hwnd, nullptr, 0, 0, r.dx, dy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

//--- the native page-number edit

// Enter in either the page or chapter edit navigates; chaptered docs read
// both boxes and go by Location, single-chapter docs keep the page-label path
static void OnLocationEditChar(MainWindow* win, Edit::CharEvent* ev) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    switch ((Key)ev->c) {
        case Key::Enter: {
            DocController* ctrl = win->ctrl;
            if (ShowChapterUi(ctrl)) {
                int chapter = win->chapterEdit ? ParseInt(win->chapterEdit->GetTextTemp()) : 1;
                int page = win->pageEdit ? ParseInt(win->pageEdit->GetTextTemp()) : 1;
                Location loc = ctrl->ClampLocation({chapter, page});
                ctrl->GoToLocation(loc, true);
            } else if (win->pageEdit) {
                TempStr s = win->pageEdit->GetTextTemp();
                int newPageNo = ctrl->GetPageByLabel(s);
                if (!ctrl->ValidPageNo(newPageNo)) {
                    ev->didHandle = true;
                    return;
                }
                ctrl->GoToPage(newPageNo, true);
            }
            HwndSetFocus(win->hwndFrame);
            // the overlay toolbar was kept up by the focus; now that
            // it's gone, let it hide again
            UpdateOverlayToolbarForMouse(win);
            ev->didHandle = true;
            return;
        }
        case Key::Escape:
            HwndSetFocus(win->hwndFrame);
            UpdateOverlayToolbarForMouse(win);
            ev->didHandle = true;
            return;
        case Key::Tab:
            AdvanceFocus(win);
            ev->didHandle = true;
            return;
        default:
            return;
    }
}

static int PageEditPadL() {
    return UiEdgeDx();
}

static int PageEditPadR() {
    return PageEditPadL() + UiScalePx(4);
}

static Edit* ToolbarCreateLocationEdit(MainWindow* win, PlatformFont* font, int iconDy) {
    Edit::CreateArgs args;
    args.parent = win->hwndToolbar;
    args.font = font;
    args.isRtl = IsUIRtl();
    // no WS_EX_CLIENTEDGE: a themed edit draws a blue bottom accent (Win11)
    args.withFrame = true;
    args.noTheme = true;
    args.numbersOnly = true;
    args.alignRight = true;
    args.selectAllOnFocus = true;
    // the box is as tall as the icons, so without this the digits would sit at
    // its top instead of on the same line as "Page:" and "/ N"
    args.centerTextVert = true;
    args.marginLeft = PageEditPadL();
    args.marginRight = PageEditPadR();
    auto* e = new ToolbarLocationEdit();
    e->win = win;
    e->SetColors(TbTextColor(), ThemeWindowControlBackgroundColor());
    e->Create(args);
    // the toolbar tree arranges itself right-to-left (HBox.rtl), so its bounds
    // are offsets from the physical left; don't let the RTL host mirror them
    e->mapRtlX = true;
    e->idealDy = std::max(iconDy, PlatformFontLineHeight(font) + UiScalePx(4));
    e->MeasureWidth();
    e->onTextChanged = MkMethod0<ToolbarLocationEdit, &ToolbarLocationEdit::OnTextChanged>(e);
    e->onChar = MkFunc1(OnLocationEditChar, win);
    return e;
}

Edit* ToolbarCreatePageEdit(MainWindow* win, PlatformFont* font, int iconDy) {
    return ToolbarCreateLocationEdit(win, font, iconDy);
}

Edit* ToolbarCreateChapterEdit(MainWindow* win, PlatformFont* font, int iconDy) {
    return ToolbarCreateLocationEdit(win, font, iconDy);
}

// no document: the find edit does nothing, so don't offer a text cursor
void ToolbarUpdateFindEditCursor(MainWindow* win) {
    LPWSTR cursorId = win->IsDocLoaded() ? nullptr : IDC_ARROW;
    if (win->findEdit) {
        win->findEdit->SetCursorId(cursorId);
    }
}

//--- the messages VirtHost doesn't model

// the native edit control asks its parent what colors to draw itself in
static bool OnCtlColor(MainWindow* win, VirtHostNativeMsg* ev) {
    LRESULT reflected = TryReflectMessages(win->hwndToolbar, ev->msg, ev->wp, ev->lp);
    if (reflected) {
        ev->res = reflected;
        return true;
    }
    if (ev->msg == WM_COMMAND) {
        return false;
    }
    HDC hdc = (HDC)ev->wp;
    SetTextColor(hdc, TbTextColor());
    SetBkColor(hdc, ThemeWindowControlBackgroundColor());
    if (IsCurrentThemeDefault() && !ThemeColorizeControls() && !ThemeUsesHighContrastColors()) {
        ev->res = (LRESULT)GetStockObject(WHITE_BRUSH);
    } else {
        ev->res = (LRESULT)win->brControlBgColor;
    }
    return true;
}

// with the tabs in the title bar the toolbar is part of the caption, so
// dragging an empty part of it moves the window and a double click maximizes it
static bool OnCaptionDrag(MainWindow* win, VirtHostNativeMsg* ev) {
    HWND hwnd = win->hwndToolbar;
    Point pt = {GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp)};
    HWND childAtPoint = ChildWindowFromPoint(hwnd, ToPOINT(pt));
    bool overChild = childAtPoint && childAtPoint != hwnd;
    // layout bounds are physical-left; WM_LBUTTONDOWN x is mirrored on RTL
    Point hitPt = pt;
    UnmirrorRtl(hwnd, hitPt);
    VirtCtrl* hit = ToolbarItemFromPoint(win, hitPt);
    if (overChild || (hit && hit->id != 0 && hit->id != PageInfoId)) {
        return false;
    }
    HWND hwndFrame = GetAncestor(hwnd, GA_ROOT);
    if (ev->msg == WM_LBUTTONDBLCLK) {
        WPARAM cmd = IsZoomed(hwndFrame) ? SC_RESTORE : SC_MAXIMIZE;
        PostMessageW(hwndFrame, WM_SYSCOMMAND, cmd, 0);
    } else {
        ReleaseCapture();
        SendMessageW(hwndFrame, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    ev->res = 0;
    return true;
}

static void OnToolbarNativeMsg(MainWindow* win, VirtHostNativeMsg* ev) {
    switch (ev->msg) {
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            if (GET_KEYSTATE_WPARAM(ev->wp) & MK_CONTROL) return;
            auto* tb = win->toolbarVirt;
            if (!tb || !tb->mainRow) return;
            Point pt =
                HwndMapWindowPoint(HWND_DESKTOP, win->hwndToolbar, Point{GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp)});
            auto* line =
                tb->annotationLine && tb->annotationLine->lastBounds.Contains(pt) ? tb->annotationLine : tb->mainRow;
            int delta = GET_WHEEL_DELTA_WPARAM(ev->wp);
            line->wheelRemainder += ev->msg == WM_MOUSEHWHEEL ? delta : -delta;
            while (std::abs(line->wheelRemainder) >= WHEEL_DELTA) {
                int direction = line->wheelRemainder > 0 ? 1 : -1;
                line->wheelRemainder -= direction * WHEEL_DELTA;
                if (line->Scroll(direction)) HideToolbarHoverDropdown(win);
            }
            ev->didHandle = true;
            ev->res = 0;
            return;
        }
        case WM_KEYDOWN:
            if (ev->wp == VK_TAB && !IsCtrlPressed() && !IsAltPressed()) {
                auto* tb = win->toolbarVirt;
                Vec<TabStop> stops;
                CollectTabStops(tb->host->layout, stops);
                VirtCtrl* focused = tb->host->vroot ? tb->host->vroot->focused : nullptr;
                VirtCtrl* edge = nullptr;
                for (auto& stop : stops) {
                    if (!stop.vwnd) continue;
                    if (!edge || !IsShiftPressed()) edge = stop.vwnd;
                }
                if (focused && focused == edge) {
                    AdvanceFocus(win);
                    ev->didHandle = true;
                }
            }
            return;
        case WM_COMMAND:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            ev->didHandle = OnCtlColor(win, ev);
            return;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (win->tabsInTitlebar) {
                ev->didHandle = OnCaptionDrag(win, ev);
            }
            return;
    }
}

void ToolbarSetNativeHooks(MainWindow* win, VirtHost* host) {
    host->onNativeMsg = MkFunc1(OnToolbarNativeMsg, win);
}

#if IS_DEBUG
static void ToolbarInteractionTests() {
    MainWindow win(nullptr);
    win.tabsCtrl = new TabsCtrl();
    ToolbarVirt toolbar;
    win.toolbarVirt = &toolbar;
    VirtHost::CreateArgs args;
    args.className = WStrL(L"SumatraToolbarInteractionTest");
    args.initialSize = {240, 40};
    args.isPopup = true;
    args.visible = false;
    auto* host = VirtHost::Create(args);
    utassert(host != nullptr);
    if (!host) {
        win.toolbarVirt = nullptr;
        return;
    }
    toolbar.host = host;
    win.hwndToolbar = host->native;
    toolbar.rowDy = 36;
    auto* line = new ToolbarLine(&toolbar);
    toolbar.mainRow = line;
    line->gap = 6;
    int commands[] = {CmdGoToPrevPage, CmdGoToNextPage, CmdHandTool, CmdAnnotationLasso};
    for (int cmd : commands) {
        auto* button = new VirtIconButton();
        button->id = cmd;
        button->padding = {12, 50, 12, 50};
        line->AddChild(button);
        VecAppend(toolbar.items, (VirtCtrl*)button);
    }
    auto* pinned = new VirtIconButton();
    pinned->id = CmdLast + 50;
    pinned->padding = {12, 50, 12, 50};
    line->AddChild(pinned);
    VecAppend(toolbar.pinnedItems, (VirtCtrl*)pinned);
    for (int direction : {-1, 1}) {
        auto* button = new VirtIconButton();
        button->id = CmdLast + 30 + (direction > 0 ? 1 : 0);
        button->padding = {12, 12, 12, 12};
        button->onClick = MkFunc1(OnToolbarScrollClicked, &win);
        button->onKeyDown = MkFunc1(OnToolbarKey, &win);
        button->SetFlag(vwfFocusable, true);
        line->AddChild(button);
        if (direction < 0)
            line->previousButton = button;
        else
            line->nextButton = button;
    }
    toolbar.overflowButton = new VirtIconButton();
    toolbar.overflowButton->id = ToolbarOverflowId;
    toolbar.overflowButton->padding = {12, 12, 12, 12};
    line->AddChild(toolbar.overflowButton);
    ToolbarSetNativeHooks(&win, host);
    host->SetLayout(line);
    auto center = [](VirtCtrl* button) {
        Rect rect = button->BoundsInWindow();
        return Point{rect.x + rect.dx / 2, rect.y + rect.dy / 2};
    };
    Point screen = HwndMapWindowPoint(host->native, HWND_DESKTOP, {10, 10});
    LPARAM wheelPoint = MAKELPARAM(screen.x, screen.y);
    utassert(line->firstGroup == 0 && line->canScrollAfter && !line->canScrollBefore);
    SendMessageW(host->native, WM_MOUSEHWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), wheelPoint);
    utassert(line->firstGroup == 0);
    SendMessageW(host->native, WM_MOUSEHWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), wheelPoint);
    utassert(line->firstGroup == 1 && line->canScrollBefore);
    SendMessageW(host->native, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), wheelPoint);
    utassert(line->firstGroup == 1);
    SendMessageW(host->native, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), wheelPoint);
    utassert(line->firstGroup == 0);

    // Caption dragging must leave these controls' clicks in the virtual input path.
    win.tabsInTitlebar = true;
    Point next = center(line->nextButton);
    bool hitNext = ToolbarItemFromPoint(&win, next) == line->nextButton;
    utassert(hitNext);
    if (hitNext) {
        SendMessageW(host->native, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(next.x, next.y));
        SendMessageW(host->native, WM_LBUTTONUP, 0, MAKELPARAM(next.x, next.y));
        utassert(line->firstGroup == 1);
    }
    host->vroot->SetFocus(line->nextButton);
    SendMessageW(host->native, WM_KEYDOWN, VK_RETURN, 0);
    utassert(line->firstGroup == 2);
    Point previous = center(line->previousButton);
    utassert(ToolbarItemFromPoint(&win, previous) == line->previousButton);
    host->vroot->SetFocus(line->previousButton);
    SendMessageW(host->native, WM_KEYDOWN, VK_SPACE, 0);
    utassert(line->firstGroup == 1);

    ToolbarPostCommand(&win, CmdAnnotationLasso);
    utassert(line->firstGroup == 3 && toolbar.items[3]->IsVisible());
    line->Scroll(1);
    utassert(line->firstGroup == 4 && !line->canScrollAfter && pinned->IsVisible());
    utassert(ToolbarItemFromPoint(&win, center(pinned)) == pinned);
    host->SetBounds({0, 0, 640, 40});
    host->Relayout();
    utassert(line->firstGroup == 0 && !line->canScrollAfter && !line->canScrollBefore);
    utassert(toolbar.overflowButton->GetVisibility() == Visibility::Visible);
    win.toolbarVirt = nullptr;
    win.hwndToolbar = nullptr;
    delete host;
}

// Use the production palette builders in hidden native hosts. No live app state.
static void CaptureToolbarPalette(VirtHost* host, Str name) {
    TempStr directory = GetEnvVariableTemp(StrL("SUMATRA_PALETTE_CAPTURES"));
    if (!len(directory)) return;
    Rect rect = host->ClientRect();
    Pixmap* image = AllocPixmapDIB(rect.dx, rect.dy);
    if (!image) return;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, image->hbmp);
    auto* gfx = GfxCreate(dc);
    gfx->FillRect(rect, TbBgColor());
    host->vroot->Paint(gfx, rect);
    gfx->FillRoundedRect(rect, UiCornerDiameter(DpiGetForHwnd(host->native), 6), kColorTransparent, ThemeEdgeColor());
    delete gfx;
    GdiFlush();
    SelectObject(dc, old);
    DeleteDC(dc);
    Str bitmap = PixmapToBmpFormat(image);
    utassert(file::WriteFile(path::JoinTemp(directory, name), bitmap));
    str::Free(bitmap);
    FreePixmap(image);
}

static void PaletteTestClick(int* count, VirtMouseEvent* ev) {
    (*count)++;
    ev->didHandle = true;
}

static void ToolbarPaletteTests() {
    auto savedCornerRadius = gUiCornerRadius;
    gUiCornerRadius = GetAppCornerRadius;
    defer {
        gUiCornerRadius = savedCornerRadius;
    };
    RenderCache* savedCache = gRenderCache;
    if (!savedCache) gRenderCache = new RenderCache();
    defer {
        if (!savedCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    Settings* saved = gSettings;
    gSettings = NewSettings({});
    if (!ThemeGetCount()) CreateThemeCommands();
    defer {
        DeleteSettings(gSettings);
        gSettings = saved;
        if (gSettings) SetCurrentThemeFromSettings();
        RefreshUiFonts();
    };
    {
        VirtHost::CreateArgs args;
        args.className = WStrL(L"SumatraPopupShapeTest");
        args.isPopup = true;
        args.visible = false;
        args.initialSize = {300, 180};
        auto* host = VirtHost::Create(args);
        utassert(host != nullptr);
        if (host) {
            for (int scale : {100, 150, 250}) {
                gSettings->interfaceScale = scale;
                gSettings->uIFontSize = scale == 100 ? 0 : 36;
                for (int radius : {6, 10}) {
                    host->ClipToRoundedRect(radius, {300, 180});
                    for (Size size : {Size{340, 220}, Size{220, 110}, Size{80, 12}}) {
                        host->SetBounds({0, 0, size.dx, size.dy});
                        HRGN actual = CreateRectRgn(0, 0, 0, 0);
                        utassert(GetWindowRgn(host->native, actual) != ERROR);
                        int diameter = std::min(2 * GetAppCornerRadius(DpiGetForHwnd(host->native), radius),
                                                std::min(size.dx, size.dy));
                        HRGN expected = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
                        utassert(EqualRgn(actual, expected));
                        DeleteObject(expected);
                        DeleteObject(actual);
                    }
                }
            }
            gSettings->interfaceScale = 100;
            gSettings->uIFontSize = 0;
            SendMessageW(host->native, WM_PAINT, 0, 0);
            Size size = HwndWindowRect(host->native).Size();
            HRGN actual = CreateRectRgn(0, 0, 0, 0);
            GetWindowRgn(host->native, actual);
            int diameter =
                std::min(2 * GetAppCornerRadius(DpiGetForHwnd(host->native), 10), std::min(size.dx, size.dy));
            HRGN expected = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
            utassert(EqualRgn(actual, expected));
            DeleteObject(expected);
            DeleteObject(actual);
            delete host;
        }
        HWND owner = CreateWindowExW(0, L"STATIC", L"Host owner test", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, nullptr,
                                     nullptr, GetModuleHandleW(nullptr), nullptr);
        utassert(owner != nullptr);
        if (owner) {
            args.parent = owner;
            host = VirtHost::Create(args);
            utassert(host != nullptr);
            if (host) {
                host->SetLayout(new VirtButton(StrL("Tool"), GetAppFont()));
                HWND native = host->native;
                DestroyWindow(owner);
                utassert(!IsWindow(native));
                utassert(host->native == nullptr);
                utassert(host->vroot && host->vroot->hwnd == nullptr);
                delete host;
            } else {
                DestroyWindow(owner);
            }
        }
        auto* bitmap = AllocPixmapDIB(64, 64);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, bitmap->hbmp);
        auto* gfx = new GfxHdc(dc);
        gfx->FillRect({0, 0, 64, 64}, kColWhite);
        Rect tightRect{12, 20, 28, 12};
        gfx->FillRoundedRect(tightRect, 200, kColBlack, kColorTransparent);
        delete gfx;
        GdiFlush();
        utassert(GetPixel(dc, tightRect.x + tightRect.dx / 2, tightRect.y + tightRect.dy / 2) == kColBlack);
        bool outsideClean = true;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                if (!tightRect.Contains(Point{x, y}) && GetPixel(dc, x, y) != kColWhite) outsideClean = false;
        utassert(outsideClean);
        SelectObject(dc, old);
        DeleteDC(dc);
        FreePixmap(bitmap);
        gSettings->interfaceScale = 100;
        gSettings->uIFontSize = 0;
        RefreshUiFonts();
    }
    auto*& presets = gSettings->pinnedAnnotationTools;
    if (!presets) presets = new Vec<PinnedAnnotationTool*>();
    Str names[] = {StrL("ballpoint"), StrL("fountain"), StrL("brush"), StrL("pencil"), StrL("marker")};
    for (Str name : names) {
        auto* preset = AllocStruct<PinnedAnnotationTool>();
        preset->tool = str::Dup(name);
        preset->color = str::Dup(StrL("#287ae0"));
        preset->width = 2.f;
        VecAppend(*presets, preset);
    }
    SetToolbarItemHidden(CmdCreateAnnotInk, true);
    SetToolbarItemHidden(PageInfoId, true);
    utassert(ToolbarItemHidden(CmdCreateAnnotInk) && ToolbarItemHidden(PageInfoId));
    utassert(!ToolbarItemHidden(CmdInkFountain) && len(*presets) == 5);
    SetToolbarItemHidden(CmdCreateAnnotInk, true);
    Str data = SerializeSettings(gSettings, {});
    Settings* roundTrip = NewSettings(data);
    str::Free(data);
    utassert(str::Eq(roundTrip->toolbarHiddenItems, gSettings->toolbarHiddenItems));
    utassert(roundTrip->pinnedAnnotationTools && len(*roundTrip->pinnedAnnotationTools) == 5);
    DeleteSettings(roundTrip);
    SetToolbarItemHidden(CmdCreateAnnotInk, false);
    utassert(!ToolbarItemHidden(CmdCreateAnnotInk) && ToolbarItemHidden(PageInfoId));
    SetToolbarItemHidden(PageInfoId, false);
    utassert(len(gSettings->toolbarHiddenItems) == 0);

    {
        MainWindow win(nullptr);
        win.tabsCtrl = new TabsCtrl();
        ToolbarVirt tb;
        win.toolbarVirt = &tb;
        VirtHost::CreateArgs args;
        args.className = WStrL(L"SumatraPenClickTest");
        args.isPopup = true;
        args.visible = false;
        args.initialSize = {200, 50};
        tb.host = VirtHost::Create(args);
        auto* row = new HBox();
        for (int command : {CmdCreateAnnotInk, CmdZoomIn, CmdCreateAnnotLine}) {
            auto* button = new VirtButton(StrL("Tool"), GetAppFont());
            button->id = command;
            row->AddChild(button);
            VecAppend(tb.items, button);
            SetToolbarHoverDropdown(&win, command, MkFunc1(BuildAnnotColorsHoverMenu, &win));
        }
        tb.host->SetLayoutSizedToContent(row);
        tb.host->Relayout();
        auto point = [](VirtCtrl* ctrl) {
            Rect bounds = ctrl->BoundsInWindow();
            return Point{bounds.x + bounds.dx / 2, bounds.y + bounds.dy / 2};
        };
        Point pen = point(tb.items[0]), zoom = point(tb.items[1]);
        Point line = point(tb.items[2]);
        ToolbarHoverDropdownOnMouseMove(&win, &pen);
        utassert(tb.hoverPendingCmdId == 0 && tb.hoverHost == nullptr);
        ToolbarHoverDropdownOnMouseMove(&win, &zoom);
        utassert(tb.hoverPendingCmdId == CmdZoomIn);
        ToolbarHoverDropdownOnMouseMove(&win, &pen);
        utassert(tb.hoverPendingCmdId == 0);
        tb.hoverCmdId = CmdCreateAnnotInk;
        tb.hoverSticky = true;
        ToolbarHoverDropdownOnMouseMove(&win, &zoom);
        utassert(tb.hoverCmdId == CmdCreateAnnotInk && tb.hoverSticky);
        HideToolbarHoverDropdown(&win);
        tb.hoverPendingCmdId = CmdCreateAnnotLine;
        HideToolbarHoverDropdown(&win);
        utassert(tb.hoverPendingCmdId == CmdCreateAnnotLine && tb.hoverHost == nullptr);
        ToolbarHoverDropdownOnMouseMove(&win, &line);
        utassert(tb.hoverPendingCmdId == CmdCreateAnnotLine);
        ToolbarHoverDropdownOnMouseMove(&win, &zoom);
        utassert(tb.hoverPendingCmdId == CmdZoomIn);
        ToolbarHoverDropdownOnMouseMove(&win, &pen);
        utassert(tb.hoverPendingCmdId == 0);
        delete tb.host;
        win.toolbarVirt = nullptr;
    }

    for (int variant = 0; variant < 3; variant++) {
        gSettings->uIFontSize = variant == 2 ? 28 : 18;
        gSettings->interfaceScale = variant == 2 ? 150 : 100;
        str::ReplaceWithCopy(&gSettings->theme, variant == 0 ? StrL("Sumatra Light") : StrL("Modern Green Dark"));
        SetCurrentThemeFromSettings();
        RefreshUiFonts();
        MainWindow win(nullptr);
        win.tabsCtrl = new TabsCtrl();
        ToolbarVirt tb;
        win.toolbarVirt = &tb;
        tb.platformFont = GetAppFont();
        tb.iconSize = UiScalePx(28);
        win.laserPointerColor = MkRgb(244, 67, 54);
        for (int width : {180, 320, 560}) {
            ILayout* pens = BuildInkPenTypes(&win);
            Vec<VirtCtrl*> buttons;
            CollectPaletteControls(pens, buttons);
            utassert(len(buttons) == 5);
            for (bool rtl : {false, true}) {
                ((InkPenRow*)pens->LayoutChildAt(0))->rtl = rtl;
                Size size = pens->Layout(ExpandHeight(width));
                pens->SetBounds({0, 0, size.dx, size.dy});
                int top = buttons[0]->BoundsInWindow().y;
                int selected = 0;
                int clicks = 0;
                for (VirtCtrl* button : buttons) {
                    Rect rect = button->BoundsInWindow();
                    utassert(rect.y == top);
                    utassert(rect.dx > 0 && rect.x >= 0 && rect.Right() <= width);
                    utassert(len(button->tooltip) > 0 && button->HasFlag(vwfFocusable));
                    auto* tile = (InkPenTile*)button;
                    utassert(str::Eq(tile->s, tile->tooltip) && str::Eq(tile->name, tile->s));
                    utassert(tile->pixmap && tile->pixmap->width <= rect.dx);
                    utassert(tile->showLabel == ((InkPenTile*)buttons[0])->showLabel);
                    if (tile->isCurrent) selected++;
                    button->onClick = MkFunc1(PaletteTestClick, &clicks);
                    for (int key : {VK_RETURN, VK_SPACE}) {
                        VirtKeyEvent ev;
                        ev.target = button;
                        ev.vkey = key;
                        utassert(button->OnKeyDown(ev) && ev.didHandle);
                    }
                }
                utassert(selected == 1 && clicks == 10);
            }
            delete pens;
            VecReset(tb.hoverItems);
        }
        for (int command : {CmdCreateAnnotInk, CmdCreateAnnotUnderline}) {
            auto* exact = AllocStruct<PinnedAnnotationTool>();
            exact->tool = str::Dup(PinnedToolName(&win, command));
            exact->color = str::Dup(SerializeColorTemp(AnnotCurrentColor(&win, command)));
            exact->width = command == CmdCreateAnnotInk ? InkPenWidth(&win) : 0;
            VecAppend(*presets, exact);
            for (bool pinned : {true, false}) {
                ToolbarHoverBuildEvent ev;
                ev.cmdId = command;
                BuildAnnotColorsHoverMenu(&win, &ev);
                Vec<VirtCtrl*> buttons;
                CollectPaletteControls(ev.layout, buttons);
                auto* pin = (PaletteIconButton*)buttons[len(buttons) - 1];
                utassert(pin->isCurrent == pinned);
                utassert(str::Eq(pin->s, pinned ? Tr("Unpin this tool") : Tr("Pin this tool")));
                utassert(str::Eq(pin->name, pin->s) && str::Eq(pin->tooltip, pin->s));
                utassert(pin->pixmap && pin->HasFlag(vwfFocusable));
                if (command == CmdCreateAnnotInk) {
                    bool foundHide = false;
                    for (VirtCtrl* button : buttons) {
                        auto* text = AsVirtButton(button);
                        if (!text || !str::Eq(text->s, Tr("Hide settings"))) continue;
                        foundHide = true;
                        auto* close = (PaletteIconButton*)button;
                        utassert(close->pixmap && str::Eq(close->name, close->tooltip));
                    }
                    utassert(foundHide);
                }
                delete ev.layout;
                VecReset(tb.hoverItems);
                if (pinned) VecPop(*presets);
            }
            str::Free(exact->tool);
            str::Free(exact->color);
            free(exact);
        }
        ILayout* pinned = BuildPinnedTools(&win);
        utassert(len(tb.pinnedItems) == 5);
        for (int i = 0; i < 5; i++) {
            auto* icon = AsVirtIconButton(tb.pinnedItems[i]);
            auto* expected = GetCachedPixmapForSvg(InkPenSvg(i, ParseColor((*presets)[i]->color)), tb.iconSize,
                                                   tb.iconSize, TbTextColor(), TbBgColor());
            utassert(icon && icon->pixmap && icon->pixmap == expected);
            if (i) utassert(icon->pixmap != AsVirtIconButton(tb.pinnedItems[i - 1])->pixmap);
        }
        delete pinned;
        VecReset(tb.pinnedItems);
        {
            auto* extra = AllocStruct<PinnedAnnotationTool>();
            extra->tool = str::Dup(GetCommandName(CmdCreateAnnotFreeText));
            VecAppend(*presets, extra);
            MainWindow groupWin(nullptr);
            groupWin.tabsCtrl = new TabsCtrl();
            ToolbarVirt groupTb;
            groupWin.toolbarVirt = &groupTb;
            groupTb.platformFont = tb.platformFont;
            groupTb.iconSize = tb.iconSize;
            VirtHost::CreateArgs args;
            args.className = WStrL(L"SumatraPinnedGroupTest");
            args.isPopup = true;
            args.visible = false;
            args.initialSize = {2000, 100};
            groupTb.host = VirtHost::Create(args);
            groupWin.hwndToolbar = groupTb.host->native;
            BuildToolbarLayout(&groupWin);
            auto contains = [&](auto&& self, ILayout* layout, VirtCtrl* control) -> bool {
                if (layout == control) return true;
                for (int i = 0; i < layout->LayoutChildCount(); i++) {
                    if (self(self, layout->LayoutChildAt(i), control)) return true;
                }
                return false;
            };
            ILayout* annotationGroup = nullptr;
            VirtCtrl* editTool = nullptr;
            for (VirtCtrl* control : groupTb.items) {
                if (control->id == CmdToggleEditPDF) editTool = control;
            }
            for (auto& item : groupTb.mainRow->children) {
                if (contains(contains, item.layout, editTool)) annotationGroup = item.layout;
            }
            utassert(annotationGroup != nullptr && len(groupTb.pinnedItems) == 6);
            for (VirtCtrl* control : groupTb.pinnedItems) {
                utassert(annotationGroup && contains(contains, annotationGroup, control));
            }
            utassert(groupTb.annotationLine == nullptr && groupTb.annotationRow == nullptr);
            utassert(len(groupTb.annotationGroups) > 0);
            int editGroupIndex = -1;
            int expandedGroupIndex = -1;
            for (int i = 0; i < len(groupTb.mainRow->children); i++) {
                if (groupTb.mainRow->children[i].layout == annotationGroup) editGroupIndex = i;
                if (groupTb.mainRow->children[i].layout == groupTb.annotationGroups[0]) expandedGroupIndex = i;
            }
            utassert(expandedGroupIndex == editGroupIndex + 1);
            for (VirtCtrl* control : groupTb.items) {
                if (!control || !control->id) continue;
                for (VirtCtrl* expanded : groupTb.annotationItems) {
                    utassert(!expanded || expanded->id != control->id);
                }
            }
            int collapsedHeight = groupTb.host->layout->MinIntrinsicHeight(2000);
            groupTb.mainRow->firstGroup = editGroupIndex;
            SetPdfAnnotationsToolbarVisible(&groupWin, true);
            utassert(groupTb.mainRow->firstGroup <= editGroupIndex);
            groupTb.mainRow->Restore();
            for (auto* group : groupTb.annotationGroups) {
                group->SetVisibility(Visibility::Visible);
                bool onMainRow = false;
                for (auto& child : groupTb.mainRow->children) onMainRow |= child.layout == group;
                utassert(onMainRow);
            }
            groupTb.annotationExpanded = true;
            groupTb.host->Relayout();
            utassert(groupTb.host->layout->MinIntrinsicHeight(2000) == collapsedHeight);
            int first = groupTb.mainRow->firstGroup;
            while (groupTb.mainRow->Scroll(1)) {
                utassert(groupTb.mainRow->firstGroup > first);
                first = groupTb.mainRow->firstGroup;
            }
            utassert(!groupTb.mainRow->canScrollAfter);
            while (groupTb.mainRow->Scroll(-1)) {
            }
            utassert(!groupTb.mainRow->canScrollBefore);
            CaptureToolbarPalette(groupTb.host, fmt("toolbar-pins-%d.bmp", variant));
            delete groupTb.host;
            groupTb.host = nullptr;
            groupWin.hwndToolbar = nullptr;
            groupWin.pageEdit = nullptr;
            groupWin.chapterEdit = nullptr;
            groupWin.toolbarVirt = nullptr;
            VecPop(*presets);
            str::Free(extra->tool);
            free(extra);
        }
        for (int command : {CmdCreateAnnotInk, CmdToggleLaserPointer}) {
            ToolbarHoverBuildEvent ev;
            ev.win = &win;
            ev.cmdId = command;
            if (command == CmdCreateAnnotInk)
                BuildAnnotColorsHoverMenu(&win, &ev);
            else
                BuildLaserHoverMenu(&win, &ev);
            utassert(ev.layout != nullptr);
            int availableWidth = variant == 0 ? 3840 : 2560;
            auto* scroll = new ToolbarPaletteScroll(ev.layout, {availableWidth, 240}, tb.platformFont);
            scroll->lineDy = PlatformFontLineHeight(tb.platformFont);
            VirtHost::CreateArgs args;
            args.className = WStrL(L"SumatraToolbarPaletteTest");
            args.isPopup = true;
            args.visible = false;
            args.initialSize = {100, 100};
            auto* host = VirtHost::Create(args);
            utassert(host != nullptr);
            tb.hoverHost = host;
            host->onNativeMsg = MkFunc1(PaletteNativeMsg, (ScrollBox*)scroll);
            Size size = host->SetLayoutSizedToContent(scroll);
            utassert(size.dx <= UiScalePx(560) && size.dy <= 240);
            utassert(size.dx <= std::max(UiScalePx(320), 8 * PlatformFontLineHeight(tb.platformFont)));
            host->SetBounds({0, 0, size.dx, size.dy});
            host->Relayout();
            Vec<VirtCtrl*> controls;
            CollectPaletteControls(ev.layout, controls);
            VirtCtrl* last = nullptr;
            for (VirtCtrl* ctrl : controls) {
                if (ctrl->onClick.IsValid() || AsVirtSlider(ctrl)) {
                    ctrl->SetFlag(vwfFocusable, true);
                    last = ctrl;
                }
                Rect bounds = ctrl->BoundsInWindow();
                utassert(bounds.x >= 0 && bounds.Right() <= host->ClientRect().Right() + 1);
            }
            utassert(last != nullptr);
            utassert(scroll->MaxScrollY() > 0);
            CaptureToolbarPalette(
                host, fmt("%s-%d-top.bmp", command == CmdCreateAnnotInk ? StrL("pen") : StrL("laser"), variant));
            SendMessageW(host->native, WM_VSCROLL, SB_BOTTOM, 0);
            utassert(scroll->scrollY == scroll->MaxScrollY());
            if (command == CmdCreateAnnotInk) {
                Rect pin = last->BoundsInWindow();
                utassert(pin.y >= 0 && pin.Bottom() <= host->ClientRect().Bottom());
            }
            CaptureToolbarPalette(
                host, fmt("%s-%d-bottom.bmp", command == CmdCreateAnnotInk ? StrL("pen") : StrL("laser"), variant));
            SendMessageW(host->native, WM_KEYDOWN, VK_PRIOR, 0);
            utassert(scroll->scrollY < scroll->MaxScrollY());
            int previousScroll = scroll->scrollY;
            SendMessageW(host->native, WM_KEYDOWN, VK_NEXT, 0);
            utassert(scroll->scrollY > previousScroll);
            scroll->ScrollTo(0);
            SendMessageW(host->native, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
            utassert(scroll->scrollY > 0);
            scroll->ScrollTo(0);
            for (int i = 0; i < len(controls) + 1 && host->vroot->focused != last; i++)
                SendMessageW(host->native, WM_KEYDOWN, VK_TAB, 0);
            utassert(host->vroot->focused == last);
            Rect focused = last->BoundsInWindow();
            utassert(focused.y >= 0 && focused.Bottom() <= host->ClientRect().Bottom());
            HRGN clip = CreateRectRgn(0, 0, 0, 0);
            utassert(GetWindowRgn(host->native, clip) != ERROR);
            utassert(!PtInRegion(clip, 0, 0) && PtInRegion(clip, size.dx / 2, size.dy / 2));
            int diameter = std::min(UiCornerDiameter(DpiGetForHwnd(host->native), 6), std::min(size.dx, size.dy));
            HRGN painted = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
            utassert(EqualRgn(clip, painted));
            DeleteObject(painted);
            host->SetBounds({0, 0, size.dx - 20, size.dy - 20});
            utassert(GetWindowRgn(host->native, clip) != ERROR);
            RECT box{};
            GetRgnBox(clip, &box);
            utassert(box.right <= size.dx - 19 && box.bottom <= size.dy - 19);
            DeleteObject(clip);
            {
                scroll->limit.dy = 1600;
                scroll->ScrollTo(0);
                size = host->SetLayoutSizedToContent(scroll);
                host->SetBounds({0, 0, size.dx, size.dy});
                utassert(scroll->BoundsInWindow().dx == host->ClientRect().dx);
                utassert(host->vroot->bounds.dx == host->ClientRect().dx);
                CaptureToolbarPalette(
                    host, fmt("%s-%d-full.bmp", command == CmdCreateAnnotInk ? StrL("pen") : StrL("laser"), variant));
            }
            delete host;
            tb.hoverHost = nullptr;
            VecReset(tb.hoverItems);
        }
        win.toolbarVirt = nullptr;
    }
}
#endif
