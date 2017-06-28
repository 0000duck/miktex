/* Ghostscript.cpp:

   Copyright (C) 1996-2017 Christian Schenk

   This file is part of the MiKTeX DVI Library.

   The MiKTeX DVI Library is free software; you can redistribute it
   and/or modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2, or (at your option) any later version.

   The MiKTeX DVI Library is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the MiKTeX DVI Library; if not, write to the
   Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
   USA.  */

#include "StdAfx.h"

#include "internal.h"

#include "Ghostscript.h"

Ghostscript::Ghostscript()
{
}

Ghostscript::~Ghostscript()
{
  MIKTEX_ASSERT(pProcess == nullptr);
  MIKTEX_ASSERT(!chunkerThread.joinable());
  MIKTEX_ASSERT(!stderrReaderThread.joinable());
  MIKTEX_ASSERT(gsIn.Get() == nullptr);
  MIKTEX_ASSERT(gsOut.Get() == nullptr);
  MIKTEX_ASSERT(gsErr.Get() == nullptr);
}

void Ghostscript::Start()
{
  shared_ptr<Session> session = Session::Get();

  // find Ghostscript
  unsigned long version;
  PathName gsExe = session->GetGhostscript(&version);

  // check to see whether the version number is ok
  if (((version >> 16) & 0xffff) < 8)
  {
    MIKTEX_UNEXPECTED();
  }

  // make Ghostscript command line
  vector<string> arguments{ gsExe.GetFileNameWithoutExtension().ToString() };
  string res = std::to_string(static_cast<double>(pDviImpl->GetResolution()) / shrinkFactor);
  arguments.push_back("-r" + res + 'x' + res);
  PaperSizeInfo paperSizeInfo = pDviImpl->GetPaperSizeInfo();
  int width = paperSizeInfo.width;
  int height = paperSizeInfo.height;
  if (pDviImpl->Landscape())
  {
    swap(width, height);
  }
  width =
    static_cast<int>(((pDviImpl->GetResolution() * width) / 72.0)
      / shrinkFactor);
  height =
    static_cast<int>(((pDviImpl->GetResolution() * height) / 72.0)
      / shrinkFactor);
  arguments.push_back("-g" + std::to_string(width) + 'x' + std::to_string(height));
  arguments.push_back("-sDEVICE="s + "bmp16m");
  arguments.push_back("-q");
  arguments.push_back("-dBATCH");
  arguments.push_back("-dNOPAUSE");
  arguments.push_back("-dDELAYSAFER");
  arguments.push_back("-sstdout="s + "%stderr");
  arguments.push_back("-dTextAlphaBits="s + "4");
  arguments.push_back("-dGraphicsAlphaBits="s + "4");
  arguments.push_back("-dDOINTERPOLATE");
  arguments.push_back("-sOutputFile="s + "-");
  arguments.push_back("-");

  tracePS->WriteFormattedLine("libdvi", "%s", CommandLineBuilder(arguments).ToString().c_str());

  ProcessStartInfo2 startinfo;

  startinfo.Arguments = arguments;
  startinfo.FileName = gsExe.ToString();
  startinfo.StandardInput = nullptr;
  startinfo.RedirectStandardInput = true;
  startinfo.RedirectStandardOutput = true;
  startinfo.RedirectStandardError = true;
  startinfo.WorkingDirectory = pDviImpl->GetDviFileName().MakeAbsolute().RemoveFileSpec().ToString();

  pProcess = Process::Start(startinfo);

  gsIn.Attach(pProcess->get_StandardInput());
  gsOut.Attach(pProcess->get_StandardOutput());
  gsErr.Attach(pProcess->get_StandardError());

  // start chunker thread
  chunkerThread = thread(&Ghostscript::Chunker, this);

  // start stderr reader
  stderrReaderThread = thread(&Ghostscript::StderrReader, this);
}

size_t Ghostscript::Read(void * pBuf, size_t size)
{
  return gsOut.Read(pBuf, size);
}

void Ghostscript::OnNewChunk(shared_ptr<DibChunk> pChunk)
{
  PathName fileName;

  // create a BMP file
  fileName.SetToTempFile();
  tracePS->WriteFormattedLine("libdvi", T_("creating bitmap file %s"), Q_(fileName));
  FileStream stream(File::Open(fileName, FileMode::Create, FileAccess::Write, false));

  const BITMAPINFO * pBitmapInfo = pChunk->GetBitmapInfo();

  size_t nBytesPerLine =
    ((((pBitmapInfo->bmiHeader.biWidth
      * pBitmapInfo->bmiHeader.biBitCount)
      + 31)
      & ~31)
      >> 3);

  unsigned uNumColors =
    (pBitmapInfo->bmiHeader.biBitCount == 24
      ? 0
      : 1 << pBitmapInfo->bmiHeader.biBitCount);

  // dump bitmap file header
  BITMAPFILEHEADER header;
  header.bfType = 0x4d42;
  header.bfSize =
    (sizeof(header)
      + sizeof(BITMAPINFOHEADER)
      + uNumColors * sizeof(RGBQUAD)
      + (static_cast<DWORD>(nBytesPerLine)
        * pBitmapInfo->bmiHeader.biHeight));
  header.bfReserved1 = 0;
  header.bfReserved2 = 0;
  header.bfOffBits =
    (sizeof(header)
      + sizeof(BITMAPINFOHEADER)
      + uNumColors * sizeof(RGBQUAD));
  stream.Write(&header, sizeof(header));

  // dump bitmap info header
  stream.Write(&pBitmapInfo->bmiHeader, sizeof(BITMAPINFOHEADER));

  // dump color table
  stream.Write(pChunk->GetColors(), uNumColors * sizeof(RGBQUAD));

  // dump bits
  stream.Write(pChunk->GetBits(), nBytesPerLine * pBitmapInfo->bmiHeader.biHeight);

  stream.Close();

  graphicsInclusions.push_back(make_shared<GraphicsInclusionImpl>(
    ImageType::DIB, fileName, true, pChunk->GetX(), pChunk->GetY(), pBitmapInfo->bmiHeader.biWidth, pBitmapInfo->bmiHeader.biHeight));
}

void Ghostscript::Chunker()
{
  unique_ptr<DibChunker> pChunker(DibChunker::Create());
  try
  {
    const int chunkSize = 2 * 1024 * 1024;
    while (pChunker->Process(DibChunker::Default, chunkSize, this))
    {
    }
  }
  catch (const exception &)
  {
    gsOut.Close();
    throw;
  }
  pChunker.reset();
}

void Ghostscript::StderrReader()
{
  const int chunkSize = 64;
  char buf[chunkSize];
  size_t n;
  while ((n = gsErr.Read(buf, chunkSize)) > 0)
  {
    stderrBuffer.append(buf, n);
  }
}

void Ghostscript::Write(const void * p, unsigned n)
{
  gsIn.Write(p, n);
}

void Ghostscript::Execute(const char * lpszFormat, ...)
{
  if (pProcess == nullptr)
  {
    Start();
  }
  va_list argptr;
  va_start(argptr, lpszFormat);
  string str = StringUtil::FormatStringVA(lpszFormat, argptr);
  va_end(argptr);
  Write(str.c_str(), str.length());
}

void Ghostscript::Finalize()
{
  // close Ghostscript's input stream
  if (gsIn.Get() != nullptr)
  {
    gsIn.Close();
  }

  // wait for Ghostscript to finish
  int gsExitCode = -1;
  if (pProcess != nullptr)
  {
    if (pProcess->WaitForExit(10000))
    {
      gsExitCode = pProcess->get_ExitCode();
    }
    pProcess = nullptr;
  }

  // wait for the chunker to finish
  if (chunkerThread.joinable())
  {
    chunkerThread.join();
  }

  // wait for stderr reader to finish
  if (stderrReaderThread.joinable())
  {
    stderrReaderThread.join();
  }

  // write the transcript to the debug stream
  if (!stderrBuffer.empty())
  {
    tracePS->WriteFormattedLine("libdvi", T_("Ghostscript transcript follows:\n\
==========================================================================\n\
%s\n\
=========================================================================="),
  stderrBuffer.c_str());
  }

  // close Ghostscript's output stream
  if (gsOut.Get() != nullptr)
  {
    gsOut.Close();
  }

  // close Ghostscript's error stream
  if (gsErr.Get() != nullptr)
  {
    gsErr.Close();
  }

  PostScript::Finalize();

  // check Ghostscript's exit code
  if (gsExitCode != 0)
  {
    MIKTEX_FATAL_ERROR(T_("Some PostScript specials could not be rendered."));
  }
}
