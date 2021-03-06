/* ProxyAuthenticationDialog.h:                         -*- C++ -*-

   Copyright (C) 2000-2018 Christian Schenk

   This file is part of the MiKTeX Setup Wizard.

   The MiKTeX Setup Wizard is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2, or
   (at your option) any later version.

   The MiKTeX Setup Wizard is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the MiKTeX Setup Wizard; if not, write to the Free
   Software Foundation, 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include "resource.h"

class ProxyAuthenticationDialog :
  public CDialog
{
private:
  enum { IDD = IDD_PROXY_AUTHENTICATION };

protected:
  DECLARE_MESSAGE_MAP();

protected:
  void DoDataExchange(CDataExchange* dx) override;

protected:
  BOOL OnInitDialog() override;

protected:
  afx_msg void OnChangeName();

public:
  ProxyAuthenticationDialog(CWnd* parent = nullptr);

public:
  std::string GetName() const
  {
    return std::string(MiKTeX::Util::CharBuffer<char>(static_cast<LPCTSTR>(name)).GetData());
  }

public:
  std::string GetPassword() const
  {
    return std::string(MiKTeX::Util::CharBuffer<char>(static_cast<LPCTSTR>(password)).GetData());
  }

private:
  CString name;

private:
  CString password;
};
