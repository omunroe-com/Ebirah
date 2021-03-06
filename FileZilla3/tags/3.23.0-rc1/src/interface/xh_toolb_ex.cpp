// Based upon src/xrc/xh_toolb.cpp from wxWidgets

#include <filezilla.h>

#include "themeprovider.h"

#include "xh_toolb_ex.h"

wxSize wxToolBarXmlHandlerEx::m_iconSize(-1, -1);

wxToolBarXmlHandlerEx::wxToolBarXmlHandlerEx()
: wxXmlResourceHandler(), m_isInside(false), m_toolbar(NULL)
{
	XRC_ADD_STYLE(wxTB_FLAT);
	XRC_ADD_STYLE(wxTB_DOCKABLE);
	XRC_ADD_STYLE(wxTB_VERTICAL);
	XRC_ADD_STYLE(wxTB_HORIZONTAL);
	XRC_ADD_STYLE(wxTB_3DBUTTONS);
	XRC_ADD_STYLE(wxTB_TEXT);
	XRC_ADD_STYLE(wxTB_NOICONS);
	XRC_ADD_STYLE(wxTB_NODIVIDER);
	XRC_ADD_STYLE(wxTB_NOALIGN);
	XRC_ADD_STYLE(wxTB_HORZ_LAYOUT);
	XRC_ADD_STYLE(wxTB_HORZ_TEXT);

	XRC_ADD_STYLE(wxTB_TOP);
	XRC_ADD_STYLE(wxTB_LEFT);
	XRC_ADD_STYLE(wxTB_RIGHT);
	XRC_ADD_STYLE(wxTB_BOTTOM);

	AddWindowStyles();
}

wxObject *wxToolBarXmlHandlerEx::DoCreateResource()
{
	if (m_class == wxT("tool"))
	{
		if (!m_toolbar)
		{
			ReportError("tool only allowed inside a wxToolBar");
			return NULL;
		}

		wxItemKind kind = wxITEM_NORMAL;
		if (GetBool(wxT("radio")))
			kind = wxITEM_RADIO;

		if (GetBool(wxT("toggle")))
		{
			if (kind != wxITEM_NORMAL)
			{
				ReportParamError
					(
					"toggle",
					"tool can't have both <radio> and <toggle> properties"
					);
			}

			kind = wxITEM_CHECK;
		}

#if wxUSE_MENUS
		// check whether we have dropdown tag inside
		wxMenu *menu = NULL; // menu for drop down items
		wxXmlNode * const nodeDropdown = GetParamNode("dropdown");
		if (nodeDropdown)
		{
			if (kind != wxITEM_NORMAL)
			{
				ReportParamError
					(
					"dropdown",
					"drop-down tool can't have neither <radio> nor <toggle> properties"
					);
			}

			kind = wxITEM_DROPDOWN;

			// also check for the menu specified inside dropdown (it is
			// optional and may be absent for e.g. dynamically-created
			// menus)
			wxXmlNode * const nodeMenu = nodeDropdown->GetChildren();
			if (nodeMenu)
			{
				wxObject *res = CreateResFromNode(nodeMenu, NULL);
				menu = dynamic_cast<wxMenu*>(res);
				if (!menu)
				{
					ReportError
						(
						nodeMenu,
						"drop-down tool contents can only be a wxMenu"
						);
				}

				if (nodeMenu->GetNext())
				{
					ReportError
						(
						nodeMenu->GetNext(),
						"unexpected extra contents under drop-down tool"
						);
				}
			}
		}
#endif

		auto GetBitmap = [&](wxString const& param) {
			auto node = GetParamNode(param);
			if (node) {
				wxString name = node->GetAttribute("stock_id", "");
				return CThemeProvider::Get()->CreateBitmap(name, wxART_TOOLBAR, m_iconSize);
			}
			return wxNullBitmap;
		};
		wxBitmap bitmap = GetBitmap(L"bitmap");
		wxBitmap bitmap2 = GetBitmap(L"bitmap2");
#ifdef __WXGTK3__
		// We need to provide a disabled bitmap.
		if( !bitmap2.IsOk() && bitmap.IsOk() ) {
			bitmap2 = wxBitmap(bitmap.ConvertToImage().ConvertToGreyscale());
		}
#endif
		wxToolBarToolBase * const tool =
			m_toolbar->AddTool
			(
			GetID(),
			GetText(wxT("label")),
			bitmap,
			bitmap2,
			kind,
			GetText(wxT("tooltip")),
			GetText(wxT("longhelp"))
			);

		if (GetBool(wxT("disabled")))
			m_toolbar->EnableTool(tool->GetId(), false);

		if (GetBool(wxS("checked")))
		{
			if (kind == wxITEM_NORMAL)
			{
				ReportParamError
					(
					"checked",
					"only <radio> nor <toggle> tools can be checked"
					);
			}
			else
			{
				m_toolbar->ToggleTool(tool->GetId(), true);
			}
		}

#if wxUSE_MENUS
		if (menu)
			tool->SetDropdownMenu(menu);
#endif

		return m_toolbar; // must return non-NULL
	}

	else if (m_class == wxT("separator") || m_class == wxT("space"))
	{
		if (!m_toolbar)
		{
			ReportError("separators only allowed inside wxToolBar");
			return NULL;
		}

		if (m_class == wxT("separator"))
			m_toolbar->AddSeparator();
		else
			m_toolbar->AddStretchableSpace();

		return m_toolbar; // must return non-NULL
	}

	else /*<object class="wxToolBar">*/
	{
		int style = GetStyle(wxT("style"), wxNO_BORDER | wxTB_HORIZONTAL);
#ifdef __WXMSW__
		if (!(style & wxNO_BORDER)) style |= wxNO_BORDER;
#endif

		XRC_MAKE_INSTANCE(toolbar, wxToolBar)

			toolbar->Create(m_parentAsWindow,
			GetID(),
			GetPosition(),
			GetSize(),
			style,
			GetName());
		SetupWindow(toolbar);

		toolbar->SetToolBitmapSize(m_iconSize);
		wxSize margins = GetSize(wxT("margins"));
		if (!(margins == wxDefaultSize))
			toolbar->SetMargins(margins.x, margins.y);
		long packing = GetLong(wxT("packing"), -1);
		if (packing != -1)
			toolbar->SetToolPacking(packing);
		long separation = GetLong(wxT("separation"), -1);
		if (separation != -1)
			toolbar->SetToolSeparation(separation);

		wxXmlNode *children_node = GetParamNode(wxT("object"));
		if (!children_node)
			children_node = GetParamNode(wxT("object_ref"));

		if (children_node == NULL) return toolbar;

		m_isInside = true;
		m_toolbar = toolbar;

		wxXmlNode *n = children_node;

		while (n)
		{
			if ((n->GetType() == wxXML_ELEMENT_NODE) &&
				(n->GetName() == wxT("object") || n->GetName() == wxT("object_ref")))
			{
				wxObject *created = CreateResFromNode(n, toolbar, NULL);
				wxControl *control = dynamic_cast<wxControl*>(created);
				if (!IsOfClass(n, wxT("tool")) &&
					!IsOfClass(n, wxT("separator")) &&
					!IsOfClass(n, wxT("space")) &&
					control != NULL)
					toolbar->AddControl(control);
			}
			n = n->GetNext();
		}

		m_isInside = false;
		m_toolbar = NULL;

		if (m_parentAsWindow && !GetBool(wxT("dontattachtoframe")))
		{
			wxFrame *parentFrame = dynamic_cast<wxFrame*>(m_parent);
			if (parentFrame)
				parentFrame->SetToolBar(toolbar);
		}

		toolbar->Realize();

		return toolbar;
	}
}

bool wxToolBarXmlHandlerEx::CanHandle(wxXmlNode *node)
{
	return ((!m_isInside && IsOfClass(node, wxT("wxToolBar"))) ||
			(m_isInside && IsOfClass(node, wxT("tool"))) ||
			(m_isInside && IsOfClass(node, wxT("separator"))));
}
