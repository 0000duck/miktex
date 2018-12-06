/* FileNameDatabase.cpp: file name database

   Copyright (C) 1996-2018 Christian Schenk

   This file is part of the MiKTeX Core Library.

   The MiKTeX Core Library is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2, or
   (at your option) any later version.

   The MiKTeX Core Library is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the MiKTeX Core Library; if not, write to the Free
   Software Foundation, 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#if defined(HAVE_CONFIG_H)
#  include "config.h"
#endif

#include <fstream>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <miktex/Core/File>
#include <miktex/Core/Paths>
#include <miktex/Trace/Trace>
#include <miktex/Util/Tokenizer>

#include "internal.h"

#include "miktex/Core/PathNameParser.h"

#include "FileNameDatabase.h"
#include "Utils/CoreStopWatch.h"

using namespace std;

using namespace MiKTeX::Core;
using namespace MiKTeX::Trace;
using namespace MiKTeX::Util;

shared_ptr<FileNameDatabase> FileNameDatabase::Create(const PathName& fndbPath, const PathName& rootDirectory)
{
  shared_ptr<FileNameDatabase> fndb = make_shared<FileNameDatabase>();
  fndb->Initialize(fndbPath, rootDirectory);
  return fndb;
}

FileNameDatabase::FileNameDatabase() :
  mmap(MemoryMappedFile::Create()),
  trace_fndb(TraceStream::Open(MIKTEX_TRACE_FNDB))
{
}

FileNameDatabase::~FileNameDatabase()
{
  try
  {
    Finalize();
  }
  catch (const exception&)
  {
  }
}

// FIXME: not UTF-8 safe
MIKTEXSTATICFUNC(bool) Match(const char* pathPattern, const char* path)
{
  MIKTEX_ASSERT(PathName(pathPattern).IsComparable());
  MIKTEX_ASSERT(PathName(lpszPath).IsComparable());
  int lastch = 0;
  for (; *pathPattern != 0 && *path != 0; ++pathPattern, ++path)
  {
    if (*pathPattern == *path)
    {
      lastch = *path;
      continue;
    }
    MIKTEX_ASSERT(RECURSION_INDICATOR_LENGTH == 2);
    MIKTEX_ASSERT(IsDirectoryDelimiter(RECURSION_INDICATOR[0]));
    MIKTEX_ASSERT(IsDirectoryDelimiter(RECURSION_INDICATOR[1]));
    if (*pathPattern == RECURSION_INDICATOR[1] && IsDirectoryDelimiter(lastch))
    {
      for (; IsDirectoryDelimiter(*pathPattern); ++pathPattern)
      {
      };
      if (*pathPattern == 0)
      {
        return true;
      }
      for (; *path != 0; ++path)
      {
        if (IsDirectoryDelimiter(lastch))
        {
          // RECURSION
          if (Match(pathPattern, path))
          {
            return true;
          }
        }
        lastch = *path;
      }
    }
    return false;
  }
  return (*pathPattern == 0 || strcmp(pathPattern, RECURSION_INDICATOR) == 0 || strcmp(pathPattern, "/") == 0) && *path == 0;
}

bool FileNameDatabase::Search(const PathName& relativePath, const string& pathPattern_, bool firstMatchOnly, vector<Fndb::Record>& result)
{
  string pathPattern = pathPattern_;

  ApplyChangeFile();

  trace_fndb->WriteLine("core", fmt::format(T_("fndb search: rootDirectory={0}, relativePath={1}, pathpattern={2}"), Q_(rootDirectory), Q_(relativePath), Q_(pathPattern)));

  MIKTEX_ASSERT(result.size() == 0);
  MIKTEX_ASSERT(!Utils::IsAbsolutePath(relativePath));
  MIKTEX_ASSERT(!IsExplicitlyRelativePath(relativePath.GetData()));

  PathName dir = relativePath.GetDirectoryName();
  PathName fileName = relativePath.GetFileName();

  PathName scratch1;

  if (!dir.Empty())
  {
    size_t l = dir.GetLength();
    if (dir.EndsWithDirectoryDelimiter())
    {
      dir[l - 1] = 0;
      --l;
    }
    scratch1 = pathPattern;
    scratch1 /= dir;
    pathPattern = scratch1.ToString();
  }

  // check to see whether we have this file name
  pair<FileNameHashTable::const_iterator, FileNameHashTable::const_iterator> range = fileNames.equal_range(MakeKey(fileName));

  if (range.first == range.second)
  {
    return false;
  }

  // path pattern must be relative to root directory
  if (Utils::IsAbsolutePath(pathPattern))
  {
    const char* lpsz = Utils::GetRelativizedPath(pathPattern.c_str(), rootDirectory.GetData());
    if (lpsz == nullptr)
    {
      MIKTEX_FATAL_ERROR_2(T_("Path pattern is not covered by file name database."), "pattern", pathPattern);
    }
    pathPattern = lpsz;
  }

  PathName comparablePathPattern(pathPattern);
  comparablePathPattern.TransformForComparison();

  for (FileNameHashTable::const_iterator it = range.first; it != range.second; ++it)
  {
    PathName relativeDirectory;
    relativeDirectory = it->second.directory;
    if (Match(comparablePathPattern.GetData(), PathName(relativeDirectory).TransformForComparison().GetData()))
    {
      PathName path;
      path = rootDirectory;
      path /= relativeDirectory;
      path /= fileName;
      trace_fndb->WriteLine("core", fmt::format(T_("found: {0} ({1})"), Q_(path), Q_(it->second.info)));
      result.push_back({ path, it->second.info });
      if (firstMatchOnly)
      {
        break;
      }
    }
  }

  return !result.empty();
}

void FileNameDatabase::Add(const vector<Fndb::Record>& records)
{
  ApplyChangeFile();
  ofstream writer = File::CreateOutputStream(changeFile, std::ios_base::app);
  for (const auto& rec : records)
  {
    string fileName;
    string directory;
    std::tie(fileName, directory) = SplitPath(rec.path);
    if (InsertRecord({ fileName, directory, rec.fileNameInfo }))
    {
      writer << "+" << fileName << PathName::PathNameDelimiter << directory << PathName::PathNameDelimiter << rec.fileNameInfo << endl;
    }
  }
  writer.close();
}

void FileNameDatabase::Remove(const vector<PathName>& paths)
{
  ApplyChangeFile();
  ofstream writer = File::CreateOutputStream(changeFile, std::ios_base::app);
  for (const auto& path : paths)
  {
    string fileName;
    string directory;
    std::tie(fileName, directory) = SplitPath(path);
    EraseRecord({ fileName, directory });
    writer << "-" << fileName << PathName::PathNameDelimiter << directory << endl;
  }
  writer.close();
}

bool FileNameDatabase::FileExists(const PathName& path)
{
  ApplyChangeFile();

  string fileName;
  string directory;

  std::tie(fileName, directory) = SplitPath(path);

  pair<FileNameHashTable::const_iterator, FileNameHashTable::const_iterator> range = fileNames.equal_range(MakeKey(fileName));
  for (FileNameHashTable::const_iterator it = range.first; it != range.second; ++it)
  {
    if (PathName::Compare(it->second.directory, directory) == 0)
    {
      return true;
    }
  }

  return false;
}

tuple<string, string> FileNameDatabase::SplitPath(const PathName& path_) const
{
  PathName path = path_;

  // make sure that the path is relative to the texmf root directory
  if (Utils::IsAbsolutePath(path))
  {
    const char* lpsz = Utils::GetRelativizedPath(path.GetData(), rootDirectory.GetData());
    if (lpsz == nullptr)
    {
      MIKTEX_FATAL_ERROR_2(T_("File name is not covered by file name database."), "path", path.ToString());
    }
    path = lpsz;
  }

  // get file name and directory
  PathName fileName = path;
  fileName.RemoveDirectorySpec();
  PathName directory = path;
  directory.RemoveFileSpec();

  return make_tuple(fileName.ToString(), directory.ToString());
}

bool FileNameDatabase::InsertRecord(const FileNameDatabase::Record& record)
{
  pair<FileNameHashTable::const_iterator, FileNameHashTable::const_iterator> range = fileNames.equal_range(MakeKey(record.fileName));
  for (FileNameHashTable::const_iterator it = range.first; it != range.second; ++it)
  {
    if (PathName::Compare(it->second.directory, record.directory) == 0)
    {
      return false;
    }
  }
  fileNames.insert(pair<string, Record>(record.fileName, record));
  return true;
}

string FileNameDatabase::MakeKey(const string& fileName) const
{
  PathName key = fileName;
  return key.TransformForComparison().ToString();
}

string FileNameDatabase::MakeKey(const PathName& fileName) const
{
  PathName key = fileName;
  return key.TransformForComparison().ToString();
}

void FileNameDatabase::EraseRecord(const FileNameDatabase::Record& record)
{
  pair<FileNameHashTable::const_iterator, FileNameHashTable::const_iterator> range = fileNames.equal_range(MakeKey(record.fileName));
  if (range.first == range.second)
  {
    MIKTEX_FATAL_ERROR_2(T_("The file name record could not be found in the hash table."), "fileName", record.fileName);
  }
  int removed = 0;
  for (FileNameHashTable::const_iterator it = range.first; it != range.second; ++it)
  {
    if (it->second.directory == record.directory)
    {
      fileNames.erase(it);
      removed++;
    }
  }
  if (removed == 0)
  {
    MIKTEX_FATAL_ERROR_2(T_("The file name record could not be found in the hash table."), "fileName", record.fileName, "directory", record.directory);
  }
}

void FileNameDatabase::ReadFileNames()
{
  fileNames.clear();
  fileNames.rehash(fndbHeader->numFiles);
  CoreStopWatch stopWatch(fmt::format("fndb read file names {}", Q_(rootDirectory)));
  ReadFileNames(GetTopDirectory());
}

void FileNameDatabase::ReadFileNames(FileNameDatabaseDirectory* dir)
{
  for (FileNameDatabaseDirectory* dirIt = dir; dirIt != nullptr; dirIt = GetDirectoryAt(dirIt->foExtension))
  {
    for (FndbWord idx = 0; idx < dirIt->numSubDirs; ++idx)
    {
      // RECURSION
      ReadFileNames(GetDirectoryAt(dirIt->GetSubDir(idx)));
    }
    for (FndbWord idx = 0; idx < dirIt->numFiles; ++idx)
    {
      PathName directoryPath;
      MakePathName(dir, directoryPath);
      string fileNameInfo;
      if (HasFileNameInfo())
      {
        fileNameInfo = GetString(dir->GetFileNameInfo(idx));
      }
      InsertRecord({ PathName(GetString(dirIt->GetFileName(idx))).TransformForComparison().ToString(), directoryPath.ToString(), fileNameInfo });
    }
  }
}

void FileNameDatabase::Finalize()
{
  if (trace_fndb != nullptr)
  {
    trace_fndb->WriteLine("core", fmt::format(T_("unloading fndb {0}"), Q_(this->rootDirectory)));
  }
  if (mmap != nullptr)
  {
    if (mmap->GetPtr() != nullptr)
    {
      mmap->Close();
    }
    mmap = nullptr;
  }
  if (trace_fndb != nullptr)
  {
    trace_fndb->Close();
    trace_fndb = nullptr;
  }
}

void FileNameDatabase::Initialize(const PathName& fndbPath, const PathName& rootDirectory)
{
  this->rootDirectory = rootDirectory;

  OpenFileNameDatabase(fndbPath);

  ReadFileNames();

  changeFile = fndbPath;
  changeFile.SetExtension(MIKTEX_FNDB_CHANGE_FILE_SUFFIX);

  ApplyChangeFile();
}

void FileNameDatabase::ApplyChangeFile()
{
  if (!File::Exists(changeFile) || File::GetSize(changeFile) == changeFileSize)
  {
    return;
  }
  trace_fndb->WriteLine("core", fmt::format(T_("applying change file {0}"), changeFile));
  ifstream reader = File::CreateInputStream(changeFile);
  int count = 0;
  for (string line; std::getline(reader, line); )
  {
    count++;
    if (count <= changeFileRecordCount)
    {
      continue;
    }
    string op = line.substr(0, 1);
    vector<string> data = StringUtil::Split(line.substr(1), PathName::PathNameDelimiter);
    if (data.size() < 2)
    {
      MIKTEX_UNEXPECTED();
    }
    const string& fileName = data[0];
    const string& directory = data[1];
    if (op == "+")
    {
      if (data.size() < 3)
      {
        MIKTEX_UNEXPECTED();
      }
      const string& fileNameInfo = data[2];
      InsertRecord({ fileName, directory, fileNameInfo });
    }
    else if (op == "-")
    {
      EraseRecord({ fileName, directory });
    }
    else
    {
      MIKTEX_UNEXPECTED();
    }
  }
  if (reader.bad() || reader.fail() && !reader.eof())
  {
    MIKTEX_FATAL_ERROR(T_("An error occured while reading the change file."));
  }
  reader.close();
  changeFileSize = File::GetSize(changeFile);
  changeFileRecordCount = count;
}

void FileNameDatabase::MakePathName(const FileNameDatabaseDirectory* dir, PathName& path) const
{
  if (dir == nullptr || dir->foParent == 0)
  {
    return;
  }
  // RECURSION
  MakePathName(GetDirectoryAt(dir->foParent), path);
  path /= GetString(dir->foName);
}

void FileNameDatabase::OpenFileNameDatabase(const PathName& fndbPath)
{
  mmap->Open(fndbPath, false);

  if (mmap->GetSize() < sizeof(*fndbHeader))
  {
    MIKTEX_FATAL_ERROR_2(T_("Not a file name database file (wrong size)."), "path", fndbPath.ToString());
  }

  fndbHeader = reinterpret_cast<FileNameDatabaseHeader*>(mmap->GetPtr());

  foEnd = static_cast<FndbByteOffset>(mmap->GetSize());

  // check signature
  if (fndbHeader->signature != FileNameDatabaseHeader::Signature)
  {
    MIKTEX_FATAL_ERROR_2(T_("Not a file name database file (wrong signature)."), "path", fndbPath.ToString());
  }

  // check version number
  if (fndbHeader->version != FileNameDatabaseHeader::Version)
  {
    MIKTEX_FATAL_ERROR_2(T_("Unknown file name database file version."), "path", fndbPath.ToString(), "versionFound", std::to_string(fndbHeader->Version), "versionExpected", std::to_string(FileNameDatabaseHeader::Version));
  }
}
