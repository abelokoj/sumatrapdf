/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#define kPdfPreviewClsid "{192FD271-B80E-56BB-A188-ACE1BA085359}"
#define kXpsPreviewClsid "{75498B4E-BE0E-5223-9544-B7E3007F77AB}"
#define kDjVuPreviewClsid "{8CA3FC6F-EC29-5A1D-8F90-D89A7E0B58C8}"
#define kEpubPreviewClsid "{24DFD89B-C01D-5ACC-A350-150C8890AA65}"
#define kFb2PreviewClsid "{0874D03A-07EC-55E9-8266-11B7BB0FF72F}"
#define kMobiPreviewClsid "{17ECF2C4-76AC-5D18-A0FA-33BDF9BDB166}"
#define kCbxPreviewClsid "{4D441475-6D7D-5BF9-9756-84C469928D26}"
#define kTgaPreviewClsid "{CDA8C2EE-B5AA-561F-AB8E-9FC554405196}"

bool InstallPreviewDll(Str dllPath, bool allUsers);
bool UninstallPreviewDll(Str dllPath = {});
void DisablePreviewInstallExts(Str cmdLine);
bool IsPreviewInstalled();

// opt-in file logging for the preview handler (PdfPreview.dll), controlled by a
// registry value so it can be toggled (via CmdTogglePdfPreviewLogging) without a
// rebuild. Files are written to the per-build data dir (keyed on the sibling
// SumatraPDF.exe's sha1) so they land next to the app's other logs/crash info.
#define kPdfPreviewLogPrefix "pdfpreview.log."

bool IsPdfPreviewLoggingEnabled();
void SetPdfPreviewLoggingEnabled(bool enable);
TempStr GetPdfPreviewLogDirTemp();
void StartPdfPreviewLoggingIfEnabled();
