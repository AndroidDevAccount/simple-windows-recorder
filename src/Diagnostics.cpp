// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Diagnostics.h"
#include <exception>
#include <cstdio>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace studio
{
namespace { std::unique_ptr<juce::FileLogger> logger; }
#if JUCE_WINDOWS
namespace
{
wchar_t crashPath[2048]{};
void fatalReport(const char* detail) noexcept
{
    const auto file=CreateFileW(crashPath,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){DWORD written=0;WriteFile(file,detail,(DWORD)strlen(detail),&written,nullptr);FlushFileBuffers(file);CloseHandle(file);}
    MessageBoxW(nullptr,L"Take One encountered a fatal error and must close. A crash marker was saved beside the diagnostic log in AppData\\Roaming\\SimpleWindowsRecorder\\Logs. Existing source recordings are not overwritten. Restart the app and share Diagnostics plus crash.txt.",L"Take One — fatal error",MB_OK|MB_ICONERROR);
}
LONG WINAPI crashFilter(EXCEPTION_POINTERS* info)
{char message[160]{};std::snprintf(message,sizeof(message),"Unhandled Windows exception 0x%08lX at %p\r\n",info->ExceptionRecord->ExceptionCode,info->ExceptionRecord->ExceptionAddress);fatalReport(message);return EXCEPTION_EXECUTE_HANDLER;}
}
#endif
juce::File diagnosticFile()
{return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SimpleWindowsRecorder/Logs/take-one.log");}
void logEvent(const juce::String& text)
{juce::Logger::writeToLog(juce::Time::getCurrentTime().toISO8601(true)+" | "+text);}
void startDiagnostics()
{
    const auto file=diagnosticFile();file.getParentDirectory().createDirectory();
    logger=std::make_unique<juce::FileLogger>(file,"Take One diagnostics",2*1024*1024);juce::Logger::setCurrentLogger(logger.get());
#if JUCE_WINDOWS
    const auto path=file.getSiblingFile("crash.txt").getFullPathName();wcsncpy_s(crashPath,path.toWideCharPointer(),_TRUNCATE);
    SetUnhandledExceptionFilter(crashFilter);
    std::set_terminate([]{fatalReport("Unhandled C++ termination; restart required.\r\n");std::_Exit(1);});
#endif
    logEvent("START Take One 0.7.0 | "+juce::SystemStats::getOperatingSystemName()+" | previous log retained (bounded to 2 MiB on launch)");
}
void showDiagnostics(const juce::String& context)
{
    struct Panel:juce::Component,juce::Timer
    {
        juce::TextEditor text;juce::TextButton copy{"Copy report"},folder{"Open log folder"};juce::String details;
        explicit Panel(juce::String s):details(std::move(s))
        {text.setMultiLine(true);text.setReadOnly(true);text.setFont(juce::FontOptions("Consolas",13,juce::Font::plain));addAndMakeVisible(text);addAndMakeVisible(copy);addAndMakeVisible(folder);
        copy.onClick=[this]{juce::SystemClipboard::copyTextToClipboard(text.getText());};folder.onClick=[]{diagnosticFile().getParentDirectory().startAsProcess();};setSize(850,520);timerCallback();startTimer(1000);}
        void resized()override{text.setBounds(12,12,getWidth()-24,getHeight()-65);copy.setBounds(12,getHeight()-42,140,30);folder.setBounds(165,getHeight()-42,160,30);}
        void timerCallback()override{const auto tail=[](juce::String s,int n){return s.substring(std::max(0,s.length()-n));};const auto content=details+"\nLog: "+diagnosticFile().getFullPathName()+"\nContains local file/device names; review before sharing.\n\n"+tail(diagnosticFile().loadFileAsString(),120000)+"\nCrash markers (may be from a previous run):\n"+tail(diagnosticFile().getSiblingFile("crash.txt").loadFileAsString(),8000);if(content!=text.getText())text.setText(content,false);}
    };
    juce::DialogWindow::LaunchOptions options;options.content.setOwned(new Panel(context));options.dialogTitle="Take One — Diagnostics";options.dialogBackgroundColour=juce::Colour(0xff111b29);options.useNativeTitleBar=true;options.resizable=true;options.launchAsync();
}
}
