// Lylat QR sign-in dialog. Shows the QR code of the sign-in page, the short code and the
// number to pick on the phone, polls the Lylat server through LylatDeviceLogin and saves
// lylat.json where the browser login saves it.

#pragma once

#include <memory>
#include <wx/dialog.h>
#include <wx/timer.h>

#include "Core/Slippi/LylatDeviceLogin.h"

class wxButton;
class wxStaticBitmap;
class wxStaticText;
class wxHyperlinkCtrl;
class wxSizer;

class SlippiQrLoginDialog : public wxDialog
{
  public:
	explicit SlippiQrLoginDialog(wxWindow* parent);
	~SlippiQrLoginDialog() override;

	// Starts a new sign-in attempt (a fresh code).
	void Start();
	// Called when the game has loaded the saved account.
	void OnGameLoggedIn();

  private:
	void OnTimer(wxTimerEvent& event);
	void OnRetry(wxCommandEvent& event);
	void OnManualLogin(wxCommandEvent& event);
	void OnCloseButton(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

	void UpdateFromView(const LylatDeviceLogin::View& view);
	void SetQr(const std::string& text);
	void ClearQr(const wxString& placeholder);

	std::unique_ptr<LylatDeviceLogin> m_login;
	wxTimer m_timer;
	unsigned m_shownSerial = ~0u;
	LylatDeviceLogin::State m_shownState = LylatDeviceLogin::State::Idle;
	int m_approvedTicks = 0;
	int m_loggedInTicks = 0;
	bool m_gameLoggedIn = false;

	wxStaticBitmap* m_qr_bitmap = nullptr;
	wxStaticText* m_qr_placeholder = nullptr;
	wxStaticText* m_code_text = nullptr;
	wxStaticText* m_number_text = nullptr;
	wxStaticText* m_headline_text = nullptr;
	wxStaticText* m_body_text = nullptr;
	wxStaticText* m_address_label = nullptr;
	wxHyperlinkCtrl* m_address_link = nullptr;
	wxStaticText* m_expires_text = nullptr;
	wxButton* m_retry_button = nullptr;
	wxButton* m_manual_button = nullptr;
	wxButton* m_close_button = nullptr;
	wxSizer* m_qr_sizer = nullptr;
};
