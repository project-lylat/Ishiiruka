// Lylat QR sign-in dialog. See SlippiQrLoginDialog.h.

#include "DolphinWX/SlippiQrLoginDialog.h"

#include <algorithm>

#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/hyperlink.h>
#include <wx/image.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include "Core/Core.h"
#include "Core/Slippi/SlippiUser.h"
#include "DolphinWX/WxUtils.h"

extern "C" {
#include "qrcodegen/qrcodegen.h"
}

namespace
{
constexpr int kTickMs = 250;
constexpr int kQrSizeDip = 232;    // the QR image, quiet zone included
constexpr int kApprovedMaxTicks = 40; // 10 s waiting for the game, then show Logged in anyway
constexpr int kLoggedInCloseTicks = 20; // close 5 s after Logged in

wxFont Scaled(const wxFont& base, double scale, bool bold)
{
	wxFont f(base);
	f.SetPointSize(std::max(1, (int)(base.GetPointSize() * scale + 0.5)));
	if (bold)
		f.SetWeight(wxFONTWEIGHT_BOLD);
	return f;
}
} // namespace

SlippiQrLoginDialog::SlippiQrLoginDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _("Sign In to Lylat"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE),
      m_login(std::make_unique<LylatDeviceLogin>()), m_timer(this)
{
	const int space5 = FromDIP(5);
	const int space10 = FromDIP(10);
	const int space15 = FromDIP(15);
	const int qrSize = FromDIP(kQrSizeDip);

	// Left column: the QR code, the short code and the number to pick.
	m_qr_bitmap = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(qrSize, qrSize));
	m_qr_placeholder = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(qrSize, -1),
	                                    wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
	m_qr_placeholder->SetFont(Scaled(GetFont(), 1.4, true));
	m_code_text = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(qrSize, -1),
	                               wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
	m_code_text->SetFont(Scaled(GetFont(), 1.9, true));
	m_number_text = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(qrSize, -1),
	                                 wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
	m_number_text->SetFont(Scaled(GetFont(), 1.15, false));

	m_qr_sizer = new wxBoxSizer(wxVERTICAL);
	m_qr_sizer->Add(m_qr_bitmap, 0, wxALIGN_CENTER_HORIZONTAL);
	m_qr_sizer->Add(m_qr_placeholder, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(100));
	m_qr_sizer->AddSpacer(space5);
	m_qr_sizer->Add(m_code_text, 0, wxALIGN_CENTER_HORIZONTAL);
	m_qr_sizer->AddSpacer(space5);
	m_qr_sizer->Add(m_number_text, 0, wxALIGN_CENTER_HORIZONTAL);
	m_qr_sizer->SetMinSize(wxSize(qrSize, qrSize + FromDIP(70)));

	// Right column: what to do, the address and the countdown.
	const int textWidth = FromDIP(270);
	m_headline_text = new wxStaticText(this, wxID_ANY, "");
	m_headline_text->SetFont(Scaled(GetFont(), 1.3, true));
	m_body_text = new wxStaticText(this, wxID_ANY, "");
	m_address_label = new wxStaticText(this, wxID_ANY, _("Or open this address and enter the code:"));
	m_address_link = new wxHyperlinkCtrl(this, wxID_ANY, "https://lylat.gg", "https://lylat.gg");
	m_expires_text = new wxStaticText(this, wxID_ANY, "");
	m_expires_text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

	wxBoxSizer* const info_sizer = new wxBoxSizer(wxVERTICAL);
	info_sizer->Add(m_headline_text, 0, wxEXPAND);
	info_sizer->AddSpacer(space10);
	info_sizer->Add(m_body_text, 0, wxEXPAND);
	info_sizer->AddSpacer(space15);
	info_sizer->Add(m_address_label, 0, wxEXPAND);
	info_sizer->AddSpacer(space5);
	info_sizer->Add(m_address_link, 0);
	info_sizer->AddStretchSpacer();
	info_sizer->Add(m_expires_text, 0, wxEXPAND);
	info_sizer->SetMinSize(wxSize(textWidth, -1));

	wxBoxSizer* const content_sizer = new wxBoxSizer(wxHORIZONTAL);
	content_sizer->Add(m_qr_sizer, 0, wxALIGN_TOP);
	content_sizer->AddSpacer(space15);
	content_sizer->Add(info_sizer, 1, wxEXPAND | wxTOP | wxBOTTOM, space5);

	// Buttons: Manual login on the left, Retry and Close on the right.
	m_manual_button = new wxButton(this, wxID_ANY, _("Manual login"));
	m_manual_button->SetToolTip(_("Sign in on the Lylat website in your browser instead."));
	m_retry_button = new wxButton(this, wxID_ANY, _("Retry"));
	m_close_button = new wxButton(this, wxID_CLOSE, _("Close"));

	wxBoxSizer* const button_sizer = new wxBoxSizer(wxHORIZONTAL);
	button_sizer->Add(m_manual_button, 0);
	button_sizer->AddStretchSpacer();
	button_sizer->Add(m_retry_button, 0);
	button_sizer->AddSpacer(space5);
	button_sizer->Add(m_close_button, 0);

	wxBoxSizer* const main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(content_sizer, 1, wxEXPAND | wxALL, space15);
	main_sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, space10);
	main_sizer->Add(button_sizer, 0, wxEXPAND | wxALL, space10);
	SetSizerAndFit(main_sizer);

	m_body_text->Wrap(textWidth);

	Bind(wxEVT_TIMER, &SlippiQrLoginDialog::OnTimer, this);
	Bind(wxEVT_BUTTON, &SlippiQrLoginDialog::OnRetry, this, m_retry_button->GetId());
	Bind(wxEVT_BUTTON, &SlippiQrLoginDialog::OnManualLogin, this, m_manual_button->GetId());
	Bind(wxEVT_BUTTON, &SlippiQrLoginDialog::OnCloseButton, this, wxID_CLOSE);
	Bind(wxEVT_CLOSE_WINDOW, &SlippiQrLoginDialog::OnClose, this);

	SetEscapeId(wxID_CLOSE);
	Center();
}

SlippiQrLoginDialog::~SlippiQrLoginDialog()
{
	m_timer.Stop();
	m_login->Cancel();
}

void SlippiQrLoginDialog::Start()
{
	m_approvedTicks = 0;
	m_loggedInTicks = 0;
	m_gameLoggedIn = false;
	m_shownState = LylatDeviceLogin::State::Idle;
	m_login->Begin();
	UpdateFromView(m_login->GetView());
	m_timer.Start(kTickMs);
}

void SlippiQrLoginDialog::OnGameLoggedIn()
{
	m_gameLoggedIn = true;
	m_login->MarkLoggedIn();
	UpdateFromView(m_login->GetView());
}

void SlippiQrLoginDialog::SetQr(const std::string& text)
{
	uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
	uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
	if (!qrcodegen_encodeText(text.c_str(), temp, qrcode, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
	{
		ClearQr(_("Open the address on the right."));
		return;
	}

	const int modules = qrcodegen_getSize(qrcode);
	const int quiet = 4;
	const int total = modules + quiet * 2;
	const int target = FromDIP(kQrSizeDip);
	const int scale = std::max(1, target / total);
	const int side = total * scale;

	wxImage image(side, side);
	unsigned char* rgb = image.GetData();
	for (int y = 0; y < side; ++y)
	{
		for (int x = 0; x < side; ++x)
		{
			const int mx = x / scale - quiet;
			const int my = y / scale - quiet;
			const bool dark = mx >= 0 && my >= 0 && mx < modules && my < modules && qrcodegen_getModule(qrcode, mx, my);
			const unsigned char v = dark ? 0 : 255;
			unsigned char* p = rgb + (y * side + x) * 3;
			p[0] = p[1] = p[2] = v;
		}
	}

	m_qr_bitmap->SetBitmap(wxBitmap(image));
	m_qr_bitmap->Show();
	m_qr_placeholder->Hide();
}

void SlippiQrLoginDialog::ClearQr(const wxString& placeholder)
{
	m_qr_bitmap->SetBitmap(wxNullBitmap);
	m_qr_bitmap->Hide();
	m_qr_placeholder->SetLabel(placeholder);
	m_qr_placeholder->Show();
}

void SlippiQrLoginDialog::UpdateFromView(const LylatDeviceLogin::View& view)
{
	using State = LylatDeviceLogin::State;

	const bool stateChanged = view.state != m_shownState;
	const bool codeChanged = view.serial != m_shownSerial;
	m_shownState = view.state;
	m_shownSerial = view.serial;

	if (view.state == State::Unsupported)
	{
		// The server has no device sign-in: today's flow, the browser login page.
		m_timer.Stop();
		SlippiUser::OpenBrowserLogInPage();
		Destroy();
		return;
	}

	const bool hasCode = view.state == State::Showing || view.state == State::Approved;
	const wxString codeText = wxString::FromUTF8(view.userCode.c_str());
	const wxString number = wxString::FromUTF8(view.matchNumber.c_str());

	if (stateChanged || codeChanged)
	{
		wxString headline, body, placeholder;
		bool showRetry = false;

		switch (view.state)
		{
		case State::Idle:
		case State::Loading:
			placeholder = _("Getting a code...");
			headline = _("Hang on");
			body = _("Asking the Lylat server for a sign-in code.");
			break;
		case State::Showing:
			headline = _("Scan with your phone and approve");
			body = wxString::Format(_("1. Scan the QR code with your phone.\n"
			                          "2. Sign in to Lylat if asked.\n"
			                          "3. Pick %s and approve."),
			                        number);
			break;
		case State::Approved:
			headline = _("Approved!");
			body = _("Signing you in...");
			break;
		case State::LoggedIn:
			placeholder = _("Logged in");
			headline = _("You are signed in.");
			body = _("Your Lylat account is ready. You can close this window.");
			break;
		case State::Expired:
			placeholder = _("Code expired");
			headline = _("Code expired");
			body = _("That code is no longer valid. Press Retry for a new one.");
			showRetry = true;
			break;
		case State::Denied:
			placeholder = _("Sign-in denied");
			headline = _("Sign-in denied");
			body = _("The request was not approved. Press Retry to try again.");
			showRetry = true;
			break;
		case State::Error:
		default:
			placeholder = _("Something went wrong");
			headline = _("Something went wrong");
			body = view.error.empty() ? _("Sign-in failed.") : wxString::FromUTF8(view.error.c_str());
			showRetry = true;
			break;
		}

		if (hasCode)
		{
			SetQr(view.verificationUriComplete);
			m_code_text->SetLabel(codeText);
			m_number_text->SetLabel(view.state == State::Showing ? wxString::Format(_("Pick %s on your phone"), number) :
			                                                       wxString());
		}
		else
		{
			ClearQr(placeholder);
			m_code_text->SetLabel(wxEmptyString);
			m_number_text->SetLabel(wxEmptyString);
		}

		const std::string address = view.verificationUri.empty() ? SlippiUser::GetLylatOrigin() : view.verificationUri;
		m_address_link->SetLabel(wxString::FromUTF8(address.c_str()));
		m_address_link->SetURL(wxString::FromUTF8(address.c_str()));
		m_address_label->Show(view.state == State::Showing);
		m_address_link->Show(view.state == State::Showing);

		m_headline_text->SetLabel(headline);
		m_body_text->SetLabel(body);
		m_body_text->Wrap(FromDIP(270));

		m_retry_button->Show(showRetry);
		m_manual_button->Show(view.state != State::LoggedIn && view.state != State::Approved);
		if (showRetry)
			m_retry_button->SetFocus();
		else if (view.state == State::LoggedIn)
			m_close_button->SetFocus();

		Layout();
		GetSizer()->Fit(this);
	}

	if (view.state == State::Showing)
	{
		const int left = view.secondsLeft;
		m_expires_text->SetLabel(wxString::Format(_("Expires in %d:%02d"), left / 60, left % 60));
	}
	else
	{
		m_expires_text->SetLabel(wxEmptyString);
	}
}

void SlippiQrLoginDialog::OnTimer(wxTimerEvent&)
{
	using State = LylatDeviceLogin::State;
	auto view = m_login->GetView();

	if (view.state == State::Approved)
	{
		// The account file is saved. The running game picks it up through its file watcher
		// and calls OnGameLoggedIn. With no game running, the next boot loads it.
		++m_approvedTicks;
		if (m_gameLoggedIn || !Core::IsRunning() || m_approvedTicks >= kApprovedMaxTicks)
		{
			m_login->MarkLoggedIn();
			view = m_login->GetView();
		}
	}

	UpdateFromView(view);
	if (IsBeingDeleted())
		return;

	if (view.state == State::LoggedIn && ++m_loggedInTicks >= kLoggedInCloseTicks)
	{
		m_timer.Stop();
		Close();
	}
}

void SlippiQrLoginDialog::OnRetry(wxCommandEvent&)
{
	Start();
}

void SlippiQrLoginDialog::OnManualLogin(wxCommandEvent&)
{
	m_timer.Stop();
	m_login->Cancel();
	SlippiUser::OpenBrowserLogInPage();
	Close();
}

void SlippiQrLoginDialog::OnCloseButton(wxCommandEvent&)
{
	Close();
}

void SlippiQrLoginDialog::OnClose(wxCloseEvent&)
{
	m_timer.Stop();
	m_login->Cancel();
	Destroy();
}
