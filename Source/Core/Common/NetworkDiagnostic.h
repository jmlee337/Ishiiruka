#pragma once

#include <thread>
#include <wx/textctrl.h>

namespace NetworkDiagnostic
{
void SetTextCtrl(wxTextCtrl *textCtrl);
bool Start();
void Stop();
} // namespace NetworkDiagnostic
