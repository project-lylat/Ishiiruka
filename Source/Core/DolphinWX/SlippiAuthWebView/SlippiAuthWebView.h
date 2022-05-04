#pragma once

#ifdef __APPLE__
#include <wx/dialog.h>
#include <wx/webview.h>
#include "Common/CommonTypes.h"

class SlippiAuthWebView : public wxDialog
{
public:
	SlippiAuthWebView(wxWindow* parent, std::string url, wxWindowID id = wxID_ANY,
		const wxString& title = _("Sign In to Lylat"),
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_DIALOG_STYLE);
	virtual ~SlippiAuthWebView();

    void OnTitleChanged(wxWebViewEvent& evt);
    static bool IsAvailable();

private:
	void CreateGUIControls();
	void OnClose(wxCloseEvent& event);
	void OnCloseButton(wxCommandEvent& event);
	void OnShow(wxShowEvent& event);

    wxWebView* m_browser;
	std::string m_url;
};
#endif
