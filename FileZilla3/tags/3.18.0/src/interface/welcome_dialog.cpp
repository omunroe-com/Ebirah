#include <filezilla.h>
#include "welcome_dialog.h"
#include "buildinfo.h"
#include <wx/hyperlink.h>
#include "Options.h"
#include "xrc_helper.h"

BEGIN_EVENT_TABLE(CWelcomeDialog, wxDialogEx)
EVT_TIMER(wxID_ANY, CWelcomeDialog::OnTimer)
END_EVENT_TABLE()

bool CWelcomeDialog::Run(wxWindow* parent, bool force /*=false*/, bool delay /*=false*/)
{
	const wxString ownVersion = CBuildInfo::GetVersion();
	wxString greetingVersion = COptions::Get()->GetOption(OPTION_GREETINGVERSION);

	wxString const resources = COptions::Get()->GetOption(OPTION_GREETINGRESOURCES);
	COptions::Get()->SetOption(OPTION_GREETINGRESOURCES, _T(""));

	if (!force) {
		if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
			if (delay)
				delete this;
			return true;
		}

		if (!greetingVersion.empty() &&
			CBuildInfo::ConvertToVersionNumber(ownVersion.c_str()) <= CBuildInfo::ConvertToVersionNumber(greetingVersion.c_str()))
		{
			// Been there done that
			if (delay)
				delete this;
			return true;
		}
		COptions::Get()->SetOption(OPTION_GREETINGVERSION, ownVersion);

		if (greetingVersion.empty() && !COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE))
			COptions::Get()->SetOption(OPTION_PROMPTPASSWORDSAVE, 1);
	}

	if (!Load(parent, _T("ID_WELCOME"))) {
		if (delay) {
			delete this;
		}
		return false;
	}

	InitFooter(force ? wxString() : resources);

	xrc_call(*this, "ID_FZVERSION", &wxStaticText::SetLabel, _T("FileZilla ") + CBuildInfo::GetVersion());

	wxString const url = _T("https://welcome.filezilla-project.org/welcome?type=client&category=%s&version=") + ownVersion;

	if (!greetingVersion.empty()) {
		xrc_call(*this, "ID_LINK_NEWS", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("news")) + _T("&oldversion=") + greetingVersion);
		xrc_call(*this, "ID_LINK_NEWS", &wxHyperlinkCtrl::SetLabel, wxString::Format(_("New features and improvements in %s"), CBuildInfo::GetVersion()));
	}
	else {
		xrc_call(*this, "ID_LINK_NEWS", &wxHyperlinkCtrl::Hide);
		xrc_call(*this, "ID_HEADING_NEWS", &wxStaticText::Hide);
	}

	xrc_call(*this, "ID_DOCUMENTATION_BASIC", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("documentation_basic")));
	xrc_call(*this, "ID_DOCUMENTATION_NETWORK", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("documentation_network")));
	xrc_call(*this, "ID_DOCUMENTATION_MORE", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("documentation_more")));
	xrc_call(*this, "ID_SUPPORT_FORUM", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("support_forum")));
	xrc_call(*this, "ID_SUPPORT_MORE", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("support_more")));

	Layout();

	GetSizer()->Fit(this);

	if (delay) {
		m_delayedShowTimer.SetOwner(this);
		m_delayedShowTimer.Start(10, true);
	}
	else
		ShowModal();

	return true;
}

void CWelcomeDialog::OnTimer(wxTimerEvent&)
{
	ShowModal();
	Destroy();
}

void MakeLinksFromTooltips(wxWindow& parent);

void CWelcomeDialog::InitFooter(wxString const& resources)
{
	bool hideFooter = true;
#if FZ_WINDOWS
	if (CBuildInfo::GetBuildType() == _T("official") && !COptions::Get()->GetOptionVal(OPTION_DISABLE_UPDATE_FOOTER)) {
		if (!resources.empty()) {
			wxLogNull null;

			wxXmlResource res(wxXRC_NO_RELOADING);
			InitHandlers(res);
			if (res.Load(_T("blob:") + resources)) {
				wxWindow* parent = XRCCTRL(*this, "ID_FOOTERMESSAGE_PANEL", wxPanel);
				if (parent) {
					wxPanel* p = new wxPanel();
					if (res.LoadPanel(p, parent, _T("ID_WELCOME_FOOTER"))) {
						wxSize minSize = p->GetSizer()->GetMinSize();
						parent->SetInitialSize(minSize);
						hideFooter = false;
						MakeLinksFromTooltips(*p);
					}
					else {
						delete p;
					}
				}
			}
		}
	}
#endif
	if (hideFooter) {
		XRCCTRL(*this, "ID_FOOTERMESSAGE_PANEL", wxPanel)->Hide();
	}
}
