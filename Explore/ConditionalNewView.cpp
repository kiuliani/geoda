/**
 * GeoDa TM, Copyright (C) 2011-2013 by Luc Anselin - all rights reserved
 *
 * This file is part of GeoDa.
 * 
 * GeoDa is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GeoDa is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm> // std::sort
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <boost/foreach.hpp>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/splitter.h>
#include <wx/xrc/xmlres.h>
#include "../DialogTools/CatClassifDlg.h"
#include "../DataViewer/DbfGridTableBase.h"
#include "../DialogTools/MapQuantileDlg.h"
#include "../DialogTools/SaveToTableDlg.h"
#include "../DialogTools/VariableSettingsDlg.h"
#include "../GeoDaConst.h"
#include "../GeneralWxUtils.h"
#include "../GenUtils.h"
#include "../FramesManager.h"
#include "../logger.h"
#include "../GeoDa.h"
#include "../Project.h"
#include "../TemplateLegend.h"
#include "../ShapeOperations/ShapeUtils.h"
#include "ConditionalNewView.h"


IMPLEMENT_CLASS(ConditionalNewCanvas, TemplateCanvas)
BEGIN_EVENT_TABLE(ConditionalNewCanvas, TemplateCanvas)
	EVT_PAINT(TemplateCanvas::OnPaint)
	EVT_ERASE_BACKGROUND(TemplateCanvas::OnEraseBackground)
	EVT_MOUSE_EVENTS(TemplateCanvas::OnMouseEvent)
	EVT_MOUSE_CAPTURE_LOST(TemplateCanvas::OnMouseCaptureLostEvent)
END_EVENT_TABLE()

const int ConditionalNewCanvas::HOR_VAR = 0; // horizonatal variable
const int ConditionalNewCanvas::VERT_VAR = 1; // vertical variable

ConditionalNewCanvas::ConditionalNewCanvas(wxWindow *parent,
										TemplateFrame* t_frame,
										Project* project_s,
										const std::vector<GeoDaVarInfo>& v_info,
										const std::vector<int>& col_ids,
										bool fixed_aspect_ratio_mode,
										bool fit_to_window_mode,
										const wxPoint& pos, const wxSize& size)
: TemplateCanvas(parent, pos, size,
				 fixed_aspect_ratio_mode, fit_to_window_mode),
project(project_s), num_obs(project_s->GetNumRecords()), num_time_vals(1),
vert_num_time_vals(1), horiz_num_time_vals(1),
horiz_num_cats(3), vert_num_cats(3),
bin_extents(boost::extents[3][3]), bin_w(0), bin_h(0),
highlight_state(project_s->highlight_state),
data(v_info.size()), var_info(v_info),
grid_base(project_s->GetGridBase()),
is_any_time_variant(false), is_any_sync_with_global_time(false),
cc_state_vert(0), cc_state_horiz(0), all_init(false)
{
	LOG_MSG("Entering ConditionalNewCanvas::ConditionalNewCanvas");
	template_frame = t_frame;
	SetCatType(VERT_VAR, CatClassification::quantile);
	SetCatType(HOR_VAR, CatClassification::quantile);
	
	for (int i=0; i<var_info.size(); i++) {
		grid_base->GetColData(col_ids[i], data[i]);
	}
	horiz_num_time_vals = data[HOR_VAR].size();
	horiz_var_sorted.resize(horiz_num_time_vals);
	horiz_cats_valid.resize(horiz_num_time_vals);
	horiz_cats_error_message.resize(horiz_num_time_vals);
	for (int t=0; t<horiz_num_time_vals; t++) {
		horiz_var_sorted[t].resize(num_obs);
		for (int i=0; i<num_obs; i++) {
			horiz_var_sorted[t][i].first = data[HOR_VAR][t][i];
			horiz_var_sorted[t][i].second = i;
		}
		std::sort(horiz_var_sorted[t].begin(), horiz_var_sorted[t].end(),
				  GeoDa::dbl_int_pair_cmp_less);
	}
	vert_num_time_vals = data[VERT_VAR].size();
	vert_var_sorted.resize(vert_num_time_vals);
	vert_cats_valid.resize(vert_num_time_vals);
	vert_cats_error_message.resize(vert_num_time_vals);
	for (int t=0; t<vert_num_time_vals; t++) {
		vert_var_sorted[t].resize(num_obs);
		for (int i=0; i<num_obs; i++) {
			vert_var_sorted[t][i].first = data[VERT_VAR][t][i];
			vert_var_sorted[t][i].second = i;
		}
		std::sort(vert_var_sorted[t].begin(), vert_var_sorted[t].end(),
				  GeoDa::dbl_int_pair_cmp_less);
	}
	VarInfoAttributeChange();

	if (num_obs < 3) {
		horiz_num_cats = num_obs;
		vert_num_cats = num_obs;
		SetCatType(VERT_VAR, CatClassification::unique_values);
		SetCatType(HOR_VAR, CatClassification::unique_values);
	}
	CreateAndUpdateCategories(VERT_VAR, false);
	CreateAndUpdateCategories(HOR_VAR, false);
	
	highlight_state->registerObserver(this);
	// child classes will set all_init = true;
	SetBackgroundStyle(wxBG_STYLE_CUSTOM);  // default style
	LOG_MSG("Exiting ConditionalNewCanvas::ConditionalNewCanvas");
}

ConditionalNewCanvas::~ConditionalNewCanvas()
{
	LOG_MSG("Entering ConditionalNewCanvas::~ConditionalNewCanvas");
	highlight_state->removeObserver(this);
	if (cc_state_vert) cc_state_vert->removeObserver(this);
	if (cc_state_horiz) cc_state_horiz->removeObserver(this);
	LOG_MSG("Exiting ConditionalNewCanvas::~ConditionalNewCanvas");
}

void ConditionalNewCanvas::DisplayRightClickMenu(const wxPoint& pos)
{
	LOG_MSG("In ConditionalNewCanvas::DisplayRightClickMenu");
}

void ConditionalNewCanvas::AddTimeVariantOptionsToMenu(wxMenu* menu)
{
	if (!is_any_time_variant) return;
	wxMenu* menu1 = new wxMenu(wxEmptyString);
	for (int i=0; i<var_info.size(); i++) {
		if (var_info[i].is_time_variant) {
			wxString s;
			s << "Synchronize " << var_info[i].name << " with Time Control";
			wxMenuItem* mi =
				menu1->AppendCheckItem(GeoDaConst::ID_TIME_SYNC_VAR1+i, s, s);
			mi->Check(var_info[i].sync_with_global_time);
		}
	}
	menu->Prepend(wxID_ANY, "Time Variable Options", menu1,
				  "Time Variable Options");
}


void ConditionalNewCanvas::SetCheckMarks(wxMenu* menu)
{
	// Update the checkmarks and enable/disable state for the
	// following menu items if they were specified for this particular
	// view in the xrc file.  Items that cannot be enable/disabled,
	// or are not checkable do not appear.
	
	GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_VERT_THEMELESS"),
					GetCatType(VERT_VAR) == CatClassification::no_theme);	
	GeneralWxUtils::CheckMenuItem(menu,
					XRCID("ID_COND_VERT_CHOROPLETH_QUANTILE"),
					GetCatType(VERT_VAR) == CatClassification::quantile);
    GeneralWxUtils::CheckMenuItem(menu,
					XRCID("ID_COND_VERT_CHOROPLETH_PERCENTILE"),
					GetCatType(VERT_VAR) == CatClassification::percentile);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_VERT_HINGE_15"),
					GetCatType(VERT_VAR) == CatClassification::hinge_15);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_VERT_HINGE_30"),
					GetCatType(VERT_VAR) == CatClassification::hinge_30);
    GeneralWxUtils::CheckMenuItem(menu,
					XRCID("ID_COND_VERT_CHOROPLETH_STDDEV"),
					GetCatType(VERT_VAR) == CatClassification::stddev);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_VERT_UNIQUE_VALUES"),
					GetCatType(VERT_VAR) == CatClassification::unique_values);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_VERT_EQUAL_INTERVALS"),
					GetCatType(VERT_VAR) ==CatClassification::equal_intervals);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_VERT_NATURAL_BREAKS"),
					GetCatType(VERT_VAR) == CatClassification::natural_breaks);

	GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_HORIZ_THEMELESS"),
					GetCatType(HOR_VAR) == CatClassification::no_theme);	
	GeneralWxUtils::CheckMenuItem(menu,
					XRCID("ID_COND_HORIZ_CHOROPLETH_QUANTILE"),
					GetCatType(HOR_VAR) == CatClassification::quantile);
    GeneralWxUtils::CheckMenuItem(menu,
					XRCID("ID_COND_HORIZ_CHOROPLETH_PERCENTILE"),
					GetCatType(HOR_VAR) == CatClassification::percentile);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_HORIZ_HINGE_15"),
					GetCatType(HOR_VAR) == CatClassification::hinge_15);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_HORIZ_HINGE_30"),
					GetCatType(HOR_VAR) == CatClassification::hinge_30);
    GeneralWxUtils::CheckMenuItem(menu,
					XRCID("ID_COND_HORIZ_CHOROPLETH_STDDEV"),
					GetCatType(HOR_VAR) == CatClassification::stddev);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_HORIZ_UNIQUE_VALUES"),
					GetCatType(HOR_VAR) == CatClassification::unique_values);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_HORIZ_EQUAL_INTERVALS"),
					GetCatType(HOR_VAR) ==CatClassification::equal_intervals);
    GeneralWxUtils::CheckMenuItem(menu, XRCID("ID_COND_HORIZ_NATURAL_BREAKS"),
					GetCatType(HOR_VAR) == CatClassification::natural_breaks);	
}

wxString ConditionalNewCanvas::GetCategoriesTitle(int var_id)
{
	wxString v;
	if (GetCatType(var_id) == CatClassification::no_theme) {
		v << "Themeless";
	} else if (GetCatType(var_id) == CatClassification::custom) {
		if (var_id == VERT_VAR) {
			v << cat_classif_def_vert.title;
		} else {
			v << cat_classif_def_horiz.title;
		}
		v << ": " << GetNameWithTime(var_id);
	} else {
		v << CatClassification::CatClassifTypeToString(GetCatType(var_id));
		v << ": " << GetNameWithTime(var_id);
	}
	return v;	
}

wxString ConditionalNewCanvas::GetNameWithTime(int var)
{
	if (var < 0 || var >= var_info.size()) return wxEmptyString;
	wxString s(var_info[var].name);
	if (var_info[var].is_time_variant) {
		s << " (" << project->GetGridBase()->GetTimeString(var_info[var].time);
		s << ")";
	}
	return s;
}

void ConditionalNewCanvas::NewCustomCatClassifVert()
{
	int var_id = VERT_VAR;
	// we know that all three var_info variables are defined, so need
	// need to prompt user as with MapNewCanvas
	
	// Fully update cat_classif_def fields according to current
	// categorization state
	if (cat_classif_def_vert.cat_classif_type != CatClassification::custom) {
		CatClassification::ChangeNumCats(cat_classif_def_vert.num_cats,
										 cat_classif_def_vert);
		CatClassification::SetBreakPoints(cat_classif_def_vert.breaks,
										  vert_var_sorted[var_info[var_id].time],
										  cat_classif_def_vert.cat_classif_type,
										  cat_classif_def_vert.num_cats);
		int time = vert_cat_data.GetCurrentCanvasTmStep();
		for (int i=0; i<cat_classif_def_vert.num_cats; i++) {
			cat_classif_def_vert.colors[i] =
				vert_cat_data.GetCategoryColor(time, i);
			cat_classif_def_vert.names[i] =
				vert_cat_data.GetCategoryLabel(time, i);
		}
	}
	
	CatClassifFrame* ccf = MyFrame::theFrame->GetCatClassifFrame();
	if (!ccf) return;
	CatClassifState* ccs = ccf->PromptNew(cat_classif_def_vert, "",
										  var_info[var_id].name,
										  var_info[var_id].time);
	if (cc_state_vert) cc_state_vert->removeObserver(this);
	cat_classif_def_vert = ccs->GetCatClassif();
	cc_state_vert = ccs;
	cc_state_vert->registerObserver(this);
	
	CreateAndUpdateCategories(var_id, false);
	UserChangedCellCategories();
	PopulateCanvas();
	if (template_frame) {
		template_frame->UpdateTitle();
		if (template_frame->GetTemplateLegend()) {
			template_frame->GetTemplateLegend()->Refresh();
		}
	}
}

void ConditionalNewCanvas::NewCustomCatClassifHoriz()
{
	int var_id = HOR_VAR;
	// we know that all three var_info variables are defined, so need
	// need to prompt user as with MapNewCanvas
	
	// Fully update cat_classif_def fields according to current
	// categorization state
	if (cat_classif_def_horiz.cat_classif_type != CatClassification::custom) {
		CatClassification::ChangeNumCats(cat_classif_def_horiz.num_cats,
										 cat_classif_def_horiz);
		CatClassification::SetBreakPoints(cat_classif_def_horiz.breaks,
										  horiz_var_sorted[var_info[var_id].time],
										  cat_classif_def_horiz.cat_classif_type,
										  cat_classif_def_horiz.num_cats);
		int time = horiz_cat_data.GetCurrentCanvasTmStep();
		for (int i=0; i<cat_classif_def_horiz.num_cats; i++) {
			cat_classif_def_horiz.colors[i] =
				horiz_cat_data.GetCategoryColor(time, i);
			cat_classif_def_horiz.names[i] =
				horiz_cat_data.GetCategoryLabel(time, i);
		}
	}
	
	CatClassifFrame* ccf = MyFrame::theFrame->GetCatClassifFrame();
	if (!ccf) return;
	CatClassifState* ccs = ccf->PromptNew(cat_classif_def_horiz, "",
										  var_info[var_id].name,
										  var_info[var_id].time);
	if (cc_state_horiz) cc_state_horiz->removeObserver(this);
	cat_classif_def_horiz = ccs->GetCatClassif();
	cc_state_horiz = ccs;
	cc_state_horiz->registerObserver(this);
	
	CreateAndUpdateCategories(var_id, false);
	UserChangedCellCategories();
	PopulateCanvas();
	if (template_frame) {
		template_frame->UpdateTitle();
		if (template_frame->GetTemplateLegend()) {
			template_frame->GetTemplateLegend()->Refresh();
		}
	}
}


void ConditionalNewCanvas::ChangeThemeType(int var_id,
							CatClassification::CatClassifType new_cat_theme,
							bool prompt_num_cats,
							const wxString& custom_classif_title)
{
	CatClassifState* ccs = (var_id==VERT_VAR ? cc_state_vert : cc_state_horiz);
	if (new_cat_theme == CatClassification::custom) {
		CatClassifManager* ccm = project->GetCatClassifManager();
		if (!ccm) return;
		CatClassifState* new_ccs = ccm->FindClassifState(custom_classif_title);
		if (!new_ccs) return;
		if (ccs == new_ccs) return;
		if (ccs) ccs->removeObserver(this);
		ccs = new_ccs;
		ccs->registerObserver(this);
		if (var_id == VERT_VAR) {
			cc_state_vert = ccs;
			cat_classif_def_vert = cc_state_vert->GetCatClassif();
		} else {
			cc_state_horiz = ccs;
			cat_classif_def_horiz = cc_state_horiz->GetCatClassif();
		}
	} else {
		if (ccs) ccs->removeObserver(this);
		if (var_id == VERT_VAR) {
			cc_state_vert = 0;
		} else {
			cc_state_horiz = 0;
		}
	}
	SetCatType(var_id, new_cat_theme);
	VarInfoAttributeChange();
	CreateAndUpdateCategories(var_id, prompt_num_cats);
	UserChangedCellCategories();
	PopulateCanvas();
	if (template_frame) {
		template_frame->UpdateTitle();
		if (template_frame->GetTemplateLegend()) {
			template_frame->GetTemplateLegend()->Refresh();
		}
	}
}

void ConditionalNewCanvas::update(CatClassifState* o)
{
	LOG_MSG("In ConditionalNewCanvas::update(CatClassifState*)");
	int var_id = 0;
	if (cc_state_vert == o) {
		cat_classif_def_vert = o->GetCatClassif();
		var_id = VERT_VAR;
	} else if (cc_state_horiz == o) {
		cat_classif_def_horiz = o->GetCatClassif();
		var_id = HOR_VAR;
	} else {
		return;
	}
	CreateAndUpdateCategories(var_id, false);
	UserChangedCellCategories();
	PopulateCanvas();
	if (template_frame) {
		template_frame->UpdateTitle();
		if (template_frame->GetTemplateLegend()) {
			template_frame->GetTemplateLegend()->Refresh();
		}
	}
}

void ConditionalNewCanvas::PopulateCanvas()
{
	LOG_MSG("In ConditionalNewCanvas::PopulateCanvas");
}

void ConditionalNewCanvas::TitleOrTimeChange()
{
	LOG_MSG("Entering ConditionalNewCanvas::TitleOrTimeChange");
	if (!is_any_sync_with_global_time) return;
	
	int cts = project->GetGridBase()->GetCurrTime();
	int ref_time = var_info[ref_var_index].time;
	int ref_time_min = var_info[ref_var_index].time_min;
	int ref_time_max = var_info[ref_var_index].time_max; 
	
	if ((cts == ref_time) ||
		(cts > ref_time_max && ref_time == ref_time_max) ||
		(cts < ref_time_min && ref_time == ref_time_min)) return;
	if (cts > ref_time_max) {
		ref_time = ref_time_max;
	} else if (cts < ref_time_min) {
		ref_time = ref_time_min;
	} else {
		ref_time = cts;
	}
	for (int i=0; i<var_info.size(); i++) {
		if (var_info[i].sync_with_global_time) {
			var_info[i].time = ref_time + var_info[i].ref_time_offset;
		}
	}
	UpdateNumVertHorizCats();
	invalidateBms();
	PopulateCanvas();
	Refresh();
	LOG_MSG("Exiting ConditionalNewCanvas::TitleOrTimeChange");
}

void ConditionalNewCanvas::VarInfoAttributeChange()
{
	GeoDa::UpdateVarInfoSecondaryAttribs(var_info);
	
	is_any_time_variant = false;
	is_any_sync_with_global_time = false;
	for (int i=0; i<var_info.size(); i++) {
		if (var_info[i].is_time_variant) is_any_time_variant = true;
		if (var_info[i].sync_with_global_time) {
			is_any_sync_with_global_time = true;
		}
	}
	ref_var_index = -1;
	num_time_vals = 1;
	for (int i=0; i<var_info.size() && ref_var_index == -1; i++) {
		if (var_info[i].is_ref_variable) ref_var_index = i;
	}
	if (ref_var_index != -1) {
		num_time_vals = (var_info[ref_var_index].time_max -
						 var_info[ref_var_index].time_min) + 1;
	}
	//GeoDa::PrintVarInfoVector(var_info);
}

void ConditionalNewCanvas::CreateAndUpdateCategories(int var_id,
													 bool prompt_num_cats)
{
	if (var_id == VERT_VAR) {
		for (int t=0; t<vert_num_time_vals; t++) vert_cats_valid[t] = true;
		for (int t=0; t<vert_num_time_vals; t++) {
			vert_cats_error_message[t] = wxEmptyString;
		}
		
		if (prompt_num_cats &&
			(GetCatType(var_id) == CatClassification::quantile ||
			 GetCatType(var_id) == CatClassification::natural_breaks ||
			 GetCatType(var_id) == CatClassification::equal_intervals))
		{
			vert_num_cats =
				CatClassification::PromptNumCats(GetCatType(var_id));
		}		
		if (cat_classif_def_vert.cat_classif_type !=
			CatClassification::custom) {
			CatClassification::ChangeNumCats(vert_num_cats,
											 cat_classif_def_vert);
		}
		cat_classif_def_vert.color_scheme =
			CatClassification::GetColSchmForType(
									cat_classif_def_vert.cat_classif_type);
		CatClassification::PopulateCatClassifData(cat_classif_def_vert,
												  vert_var_sorted,
												  vert_cat_data,
												  vert_cats_valid,
												  vert_cats_error_message);
		int vt = var_info[var_id].time;
		vert_num_cats = vert_cat_data.categories[vt].cat_vec.size();
		CatClassification::ChangeNumCats(vert_num_cats, cat_classif_def_vert);
	} else {
		for (int t=0; t<horiz_num_time_vals; t++) horiz_cats_valid[t] = true;
		for (int t=0; t<horiz_num_time_vals; t++) {
			horiz_cats_error_message[t] = wxEmptyString;
		}
		
		if (prompt_num_cats &&
			(GetCatType(var_id) == CatClassification::quantile ||
			 GetCatType(var_id) == CatClassification::natural_breaks ||
			 GetCatType(var_id) == CatClassification::equal_intervals))
		{
			horiz_num_cats =
				CatClassification::PromptNumCats(GetCatType(var_id));
		}
		if (cat_classif_def_horiz.cat_classif_type !=
			CatClassification::custom) {
			CatClassification::ChangeNumCats(horiz_num_cats,
											 cat_classif_def_horiz);
		}		
		cat_classif_def_horiz.color_scheme =
			CatClassification::GetColSchmForType(
									cat_classif_def_horiz.cat_classif_type);
		CatClassification::PopulateCatClassifData(cat_classif_def_horiz,
												  horiz_var_sorted,
												  horiz_cat_data,
												  horiz_cats_valid,
												  horiz_cats_error_message);
		int ht = var_info[var_id].time;
		horiz_num_cats = horiz_cat_data.categories[ht].cat_vec.size();
		CatClassification::ChangeNumCats(horiz_num_cats, cat_classif_def_horiz);
	}
}

void ConditionalNewCanvas::UpdateNumVertHorizCats()
{
	int vt = var_info[VERT_VAR].time;
	int ht = var_info[HOR_VAR].time;
	vert_num_cats = vert_cat_data.categories[vt].cat_vec.size();
	horiz_num_cats = horiz_cat_data.categories[ht].cat_vec.size();
}

void ConditionalNewCanvas::TimeSyncVariableToggle(int var_index)
{
	LOG_MSG("In ConditionalNewCanvas::TimeSyncVariableToggle");
	var_info[var_index].sync_with_global_time =
		!var_info[var_index].sync_with_global_time;
	
	VarInfoAttributeChange();
	PopulateCanvas();
}

void ConditionalNewCanvas::UpdateStatusBar()
{
	wxStatusBar* sb = template_frame->GetStatusBar();
	if (!sb) return;
	wxString s;
	if (mousemode == select &&
		(selectstate == dragging || selectstate == brushing)) {
		s << "#selected=" << highlight_state->GetTotalHighlighted();
	}
	sb->SetStatusText(s);
}

CatClassification::CatClassifType ConditionalNewCanvas::GetCatType(int var_id)
{
	if (var_id == VERT_VAR) {
		return cat_classif_def_vert.cat_classif_type;
	} else {
		return cat_classif_def_horiz.cat_classif_type;
	}
}

void ConditionalNewCanvas::SetCatType(int var_id,
									  CatClassification::CatClassifType t)
{
	if (var_id == VERT_VAR) {
		cat_classif_def_vert.cat_classif_type = t;
	} else {
		cat_classif_def_horiz.cat_classif_type = t;
	}
	
}

IMPLEMENT_CLASS(ConditionalNewFrame, TemplateFrame)
BEGIN_EVENT_TABLE(ConditionalNewFrame, TemplateFrame)
END_EVENT_TABLE()

ConditionalNewFrame::ConditionalNewFrame(wxFrame *parent, Project* project,
									 const std::vector<GeoDaVarInfo>& var_info,
									 const std::vector<int>& col_ids,
									 const wxString& title, const wxPoint& pos,
									 const wxSize& size, const long style)
: TemplateFrame(parent, project, title, pos, size, style)
{
	LOG_MSG("In ConditionalNewFrame::ConditionalNewFrame");
}

ConditionalNewFrame::~ConditionalNewFrame()
{
	LOG_MSG("In ConditionalNewFrame::~ConditionalNewFrame");
}

void ConditionalNewFrame::MapMenus()
{
	LOG_MSG("In ConditionalNewFrame::MapMenus");
}

void ConditionalNewFrame::UpdateOptionMenuItems()
{
	TemplateFrame::UpdateOptionMenuItems(); // set common items first
	wxMenuBar* mb = MyFrame::theFrame->GetMenuBar();
	int menu = mb->FindMenu("Options");
    if (menu == wxNOT_FOUND) {
        LOG_MSG("ConditionalNewFrame::UpdateOptionMenuItems: "
				"Options menu not found");
	} else {
		((ConditionalNewCanvas*)
		 template_canvas)->SetCheckMarks(mb->GetMenu(menu));
	}
}

void ConditionalNewFrame::UpdateContextMenuItems(wxMenu* menu)
{
	// Update the checkmarks and enable/disable state for the
	// following menu items if they were specified for this particular
	// view in the xrc file.  Items that cannot be enable/disabled,
	// or are not checkable do not appear.
	
	TemplateFrame::UpdateContextMenuItems(menu); // set common items	
}

/** Implementation of FramesManagerObserver interface */
void  ConditionalNewFrame::update(FramesManager* o)
{
	LOG_MSG("In ConditionalNewFrame::update(FramesManager* o)");
	template_canvas->TitleOrTimeChange();
	UpdateTitle();
}

void ConditionalNewFrame::UpdateTitle()
{
	SetTitle(template_canvas->GetCanvasTitle());
}


void ConditionalNewFrame::OnNewCustomCatClassifA()
{
	// only implemented by Conditional Map View
}

void ConditionalNewFrame::OnNewCustomCatClassifB()
{
	((ConditionalNewCanvas*) template_canvas)->
		NewCustomCatClassifVert();
}

void ConditionalNewFrame::OnNewCustomCatClassifC()
{
	((ConditionalNewCanvas*) template_canvas)->
		NewCustomCatClassifHoriz();
}

void ConditionalNewFrame::OnCustomCatClassifA(const wxString& cc_title)
{
	// only implemented by Conditional Map View
}

void ConditionalNewFrame::OnCustomCatClassifB(const wxString& cc_title)
{
	ChangeVertThemeType(CatClassification::custom, cc_title);
}

void ConditionalNewFrame::OnCustomCatClassifC(const wxString& cc_title)
{
	ChangeHorizThemeType(CatClassification::custom, cc_title);
}

void ConditionalNewFrame::ChangeVertThemeType(
								  CatClassification::CatClassifType new_theme,
								  const wxString& cc_title)
{
	ConditionalNewCanvas* cc = (ConditionalNewCanvas*) template_canvas;
	cc->ChangeThemeType(ConditionalNewCanvas::VERT_VAR, new_theme,
						false, cc_title);
	UpdateTitle();
	UpdateOptionMenuItems();
}

void ConditionalNewFrame::ChangeHorizThemeType(
								   CatClassification::CatClassifType new_theme,
								   const wxString& cc_title)
{
	ConditionalNewCanvas* cc = (ConditionalNewCanvas*) template_canvas;
	cc->ChangeThemeType(ConditionalNewCanvas::HOR_VAR, new_theme,
						false, cc_title);
	UpdateTitle();
	UpdateOptionMenuItems();
}