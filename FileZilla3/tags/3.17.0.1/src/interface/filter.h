#ifndef __FILTER_H__
#define __FILTER_H__

#include "dialogex.h"
#include <wx/regex.h>

#include <memory>

enum t_filterType
{
	filter_name = 0x01,
	filter_size = 0x02,
	filter_attributes = 0x04,
	filter_permissions = 0x08,
	filter_path = 0x10,
	filter_date = 0x20,
	filter_time = 0x40,
#ifdef __WXMSW__
	filter_meta = filter_attributes,
	filter_foreign = filter_permissions,
#else
	filter_meta = filter_permissions,
	filter_foreign = filter_attributes
#endif
};

class CFilterCondition
{
public:
	CFilterCondition();

	enum t_filterType type;
	int condition;

	wxString strValue; // All other types
	int64_t value; // If type is size
	fz::datetime date; // If type is date
	bool matchCase;
	std::shared_ptr<wxRegEx> pRegEx;
};

class CFilter
{
public:
	enum t_matchType
	{
		all,
		any,
		none
	};

	CFilter();

	wxString name;

	bool filterFiles;
	bool filterDirs;
	enum t_matchType matchType;
	bool matchCase;

	std::vector<CFilterCondition> filters;

	bool HasConditionOfType(enum t_filterType type) const;
	bool IsLocalFilter() const;
};

class CFilterSet
{
public:
	wxString name;
	std::vector<bool> local;
	std::vector<bool> remote;
};

namespace pugi { class xml_node; }
class CFilterManager
{
public:
	CFilterManager();
	virtual ~CFilterManager() {}

	// Note: Under non-windows, attributes are permissions
	bool FilenameFiltered(const wxString& name, const wxString& path, bool dir, int64_t size, bool local, int attributes, fz::datetime const& date) const;
	bool FilenameFiltered(std::vector<CFilter> const& filters, const wxString& name, const wxString& path, bool dir, int64_t size, int attributes, fz::datetime const& date) const;
	static bool FilenameFilteredByFilter(const CFilter& filter, const wxString& name, const wxString& path, bool dir, int64_t size, int attributes, fz::datetime const& date);
	static bool HasActiveFilters(bool ignore_disabled = false);

	bool HasSameLocalAndRemoteFilters() const;

	static void ToggleFilters();

	std::vector<CFilter> GetActiveFilters(bool local);

	static bool CompileRegexes(std::vector<CFilter>& filters);
	static bool CompileRegexes(CFilter& filter);

	static bool LoadFilter(pugi::xml_node& element, CFilter& filter);

protected:
	static bool CompileRegexes();

	static void LoadFilters();
	static bool m_loaded;

	static std::vector<CFilter> m_globalFilters;

	static std::vector<CFilterSet> m_globalFilterSets;
	static unsigned int m_globalCurrentFilterSet;

	static bool m_filters_disabled;
};

class CMainFrame;
class CFilterDialog : public wxDialogEx, public CFilterManager
{
public:
	CFilterDialog();
	virtual ~CFilterDialog() {}

	bool Create(CMainFrame* parent);

	static void SaveFilter(pugi::xml_node& element, const CFilter& filter);

protected:

	void SaveFilters();

	void DisplayFilters();

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnOkOrApply(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnEdit(wxCommandEvent& event);
	void OnFilterSelect(wxCommandEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnKeyEvent(wxKeyEvent& event);
	void OnSaveAs(wxCommandEvent& event);
	void OnRename(wxCommandEvent& event);
	void OnDeleteSet(wxCommandEvent& event);
	void OnSetSelect(wxCommandEvent& event);

	void OnChangeAll(wxCommandEvent& event);

	bool m_shiftClick;

	CMainFrame* m_pMainFrame;

	std::vector<CFilter> m_filters;
	std::vector<CFilterSet> m_filterSets;
	unsigned int m_currentFilterSet;
};

#endif //__FILTER_H__
