#include "DolphinWX/Config/SlippiNetworkDiagnosticDiag.h"

#include "Common/NetworkDiagnostic.h"
#include <wx/sizer.h>
#include <wx/textctrl.h>

SlippiNetworkDiagnosticDiag::SlippiNetworkDiagnosticDiag(wxWindow *const parent, const wxString &name)
    : wxDialog(parent, wxID_ANY, name)
{
	wxTextCtrl *textCtrl =
	    new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	wxBoxSizer *boxSizer = new wxBoxSizer(wxVERTICAL);
	boxSizer->Add(textCtrl, 1, wxEXPAND);
	SetSizer(boxSizer);

	NetworkDiagnostic::SetTextCtrl(textCtrl);
}

SlippiNetworkDiagnosticDiag::~SlippiNetworkDiagnosticDiag()
{
	NetworkDiagnostic::Stop();
}

int SlippiNetworkDiagnosticDiag::ShowModal() 
{
	NetworkDiagnostic::Start();
	return wxDialog::ShowModal();
}
