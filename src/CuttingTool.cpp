// CuttingTool.cpp
/*
 * Copyright (c) 2009, Dan Heeks, Perttu Ahola
 * This program is released under the BSD license. See the file COPYING for
 * details.
 */

#include "stdafx.h"
#include <math.h>
#include "CuttingTool.h"
#include "CNCConfig.h"
#include "ProgramCanvas.h"
#include "interface/HeeksObj.h"
#include "interface/HeeksColor.h"
#include "interface/PropertyInt.h"
#include "interface/PropertyDouble.h"
#include "interface/PropertyLength.h"
#include "interface/PropertyChoice.h"
#include "interface/PropertyString.h"
#include "tinyxml/tinyxml.h"
#include "CNCPoint.h"
#include "PythonStuff.h"
#include "MachineState.h"

#include <sstream>
#include <string>
#include <algorithm>

#include <gp_Pnt.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Compound.hxx>

#include <TopExp_Explorer.hxx>

#include <BRep_Tool.hxx>
#include <BRepLib.hxx>

#include <GCE2d_MakeSegment.hxx>

#include <Handle_Geom_TrimmedCurve.hxx>
#include <Handle_Geom_CylindricalSurface.hxx>
#include <Handle_Geom2d_TrimmedCurve.hxx>
#include <Handle_Geom2d_Ellipse.hxx>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Transform.hxx>

#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeRevolution.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>

#include <BRepPrim_Cylinder.hxx>
#include <BRepPrim_Cone.hxx>

#include <BRepFilletAPI_MakeFillet.hxx>

#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>

#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeSegment.hxx>

#include <BRepAlgoAPI_Fuse.hxx>

#include <Geom_Surface.hxx>
#include <Geom_Plane.hxx>

#include "Tools.h"

extern CHeeksCADInterface* heeksCAD;


void CCuttingToolParams::set_initial_values()
{
	CNCConfig config(ConfigScope());
	config.Read(_T("m_material"), &m_material, int(eCarbide));
	config.Read(_T("m_diameter"), &m_diameter, 12.7);
	config.Read(_T("m_tool_length_offset"), &m_tool_length_offset, (10 * m_diameter));
	config.Read(_T("m_max_advance_per_revolution"), &m_max_advance_per_revolution, 0.12 );	// mm
	config.Read(_T("m_automatically_generate_title"), &m_automatically_generate_title, 1 );

	config.Read(_T("m_type"), (int *) &m_type, eDrill);
	config.Read(_T("m_flat_radius"), &m_flat_radius, 0);
	config.Read(_T("m_corner_radius"), &m_corner_radius, 0);
	config.Read(_T("m_cutting_edge_angle"), &m_cutting_edge_angle, 59);
	config.Read(_T("m_cutting_edge_height"), &m_cutting_edge_height, 4 * m_diameter);

	// The following are all turning tool parameters
	config.Read(_T("m_orientation"), &m_orientation, 6);
	config.Read(_T("m_x_offset"), &m_x_offset, 0);
	config.Read(_T("m_front_angle"), &m_front_angle, 95);
	config.Read(_T("m_tool_angle"), &m_tool_angle, 60);
	config.Read(_T("m_back_angle"), &m_back_angle, 25);

	// The following are ONLY for touch probe tools
	config.Read(_T("probe_offset_x"), &m_probe_offset_x, 0.0);
	config.Read(_T("probe_offset_y"), &m_probe_offset_y, 0.0);
}

void CCuttingToolParams::write_values_to_config()
{
	CNCConfig config(ConfigScope());

	// We ALWAYS write the parameters into the configuration file in mm (for consistency).
	// If we're now in inches then convert the values.
	// We're in mm already.
	config.Write(_T("m_material"), m_material);
	config.Write(_T("m_diameter"), m_diameter);
	config.Write(_T("m_x_offset"), m_x_offset);
	config.Write(_T("m_tool_length_offset"), m_tool_length_offset);
	config.Write(_T("m_orientation"), m_orientation);
	config.Write(_T("m_max_advance_per_revolution"), m_max_advance_per_revolution );
	config.Write(_T("m_automatically_generate_title"), m_automatically_generate_title );

	config.Write(_T("m_type"), m_type);
	config.Write(_T("m_flat_radius"), m_flat_radius);
	config.Write(_T("m_corner_radius"), m_corner_radius);
	config.Write(_T("m_cutting_edge_angle"), m_cutting_edge_angle);
	config.Write(_T("m_cutting_edge_height"), m_cutting_edge_height);

	config.Write(_T("m_front_angle"), m_front_angle);
	config.Write(_T("m_tool_angle"), m_tool_angle);
	config.Write(_T("m_back_angle"), m_back_angle);

	// The following are ONLY for touch probe tools
	config.Read(_T("probe_offset_x"), m_probe_offset_x);
	config.Read(_T("probe_offset_y"), m_probe_offset_y);
}

void CCuttingTool::SetDiameter( const double diameter )
{
	m_params.m_diameter = diameter;
	if (m_params.m_type == CCuttingToolParams::eChamfer)
	{
		// Recalculate the cutting edge length based on this new diameter
		// and the cutting angle.

		double opposite = m_params.m_diameter - m_params.m_flat_radius;
		double angle = m_params.m_cutting_edge_angle / 360.0 * 2 * PI;

		m_params.m_cutting_edge_height = opposite / tan(angle);
	}

	ResetTitle();
	KillGLLists();
	heeksCAD->Repaint();
} // End SetDiameter() method

static void on_set_diameter(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->SetDiameter( value );
} // End on_set_diameter() routine

static void on_set_max_advance_per_revolution(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->m_params.m_max_advance_per_revolution = value;
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_x_offset(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->m_params.m_x_offset = value;
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_tool_length_offset(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->m_params.m_tool_length_offset = value;
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_orientation(int zero_based_choice, HeeksObj* object)
{
	if (zero_based_choice < 0) return;	// An error has occured

	if ((zero_based_choice >= 0) && (zero_based_choice <= 9))
	{
		((CCuttingTool*)object)->m_params.m_orientation = zero_based_choice;
		object->KillGLLists();
		heeksCAD->Repaint();
	} // End if - then
	else
	{
		wxMessageBox(_T("Orientation values must be between 0 and 9 inclusive.  Aborting value change"));
	} // End if - else
}

static void on_set_material(int zero_based_choice, HeeksObj* object)
{
	if (zero_based_choice < 0) return;	// An error has occured.

	if ((zero_based_choice >= CCuttingToolParams::eHighSpeedSteel) && (zero_based_choice <= CCuttingToolParams::eCarbide))
	{
		((CCuttingTool*)object)->m_params.m_material = zero_based_choice;
		((CCuttingTool*)object)->ResetTitle();
		heeksCAD->RefreshProperties();
		object->KillGLLists();
		heeksCAD->Repaint();
	} // End if - then
	else
	{
		wxMessageBox(_T("Cutting Tool material must be between 0 and 1. Aborting value change"));
	} // End if - else
}

static void on_set_front_angle(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->m_params.m_front_angle = value;
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_tool_angle(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->m_params.m_tool_angle = value;
	((CCuttingTool*)object)->ResetTitle();
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_back_angle(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->m_params.m_back_angle = value;
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_type(int zero_based_choice, HeeksObj* object)
{
	if (zero_based_choice < 0) return;	// An error has occured.

	((CCuttingTool*)object)->m_params.m_type = CCuttingToolParams::eCuttingToolType(zero_based_choice);
	((CCuttingTool*)object)->ResetParametersToReasonableValues();
	heeksCAD->RefreshProperties();
	object->KillGLLists();
	heeksCAD->Repaint();
} // End on_set_type() routine

static void on_set_automatically_generate_title(int zero_based_choice, HeeksObj* object)
{
	if (zero_based_choice < 0) return;	// An error has occured.

	((CCuttingTool*)object)->m_params.m_automatically_generate_title = zero_based_choice;
	((CCuttingTool*)object)->ResetTitle();

} // End on_set_type() routine



static double degrees_to_radians( const double degrees )
{
	return( (degrees / 360) * 2 * PI );
} // End degrees_to_radians() routine


void CCuttingTool::ResetParametersToReasonableValues()
{
	if (m_params.m_type != CCuttingToolParams::eTurningTool)
	{
		m_params.m_tool_length_offset = (5 * m_params.m_diameter);
	} // End if - then

	double height;
	switch(m_params.m_type)
	{
		case CCuttingToolParams::eDrill:
				m_params.m_corner_radius = 0;
				m_params.m_flat_radius = 0;
				m_params.m_cutting_edge_angle = 59;
				m_params.m_cutting_edge_height = m_params.m_diameter * 3.0;
				ResetTitle();
				break;

		case CCuttingToolParams::eCentreDrill:
				m_params.m_corner_radius = 0;
				m_params.m_flat_radius = 0;
				m_params.m_cutting_edge_angle = 59;
				m_params.m_cutting_edge_height = m_params.m_diameter * 1.0;
				ResetTitle();
				break;

		case CCuttingToolParams::eEndmill:
				m_params.m_corner_radius = 0;
				m_params.m_flat_radius = m_params.m_diameter / 2;
				m_params.m_cutting_edge_angle = 0;
				m_params.m_cutting_edge_height = m_params.m_diameter * 3.0;
				ResetTitle();
				break;

		case CCuttingToolParams::eSlotCutter:
				m_params.m_corner_radius = 0;
				m_params.m_flat_radius = m_params.m_diameter / 2;
				m_params.m_cutting_edge_angle = 0;
				m_params.m_cutting_edge_height = m_params.m_diameter * 3.0;
				ResetTitle();
				break;

		case CCuttingToolParams::eBallEndMill:
				m_params.m_corner_radius = (m_params.m_diameter / 2);
				m_params.m_flat_radius = 0;
				m_params.m_cutting_edge_angle = 0;
				m_params.m_cutting_edge_height = m_params.m_diameter * 3.0;
				ResetTitle();
				break;

		case CCuttingToolParams::eTouchProbe:
				m_params.m_corner_radius = (m_params.m_diameter / 2);
				m_params.m_flat_radius = 0;
				ResetTitle();
				break;

		case CCuttingToolParams::eToolLengthSwitch:
				m_params.m_corner_radius = (m_params.m_diameter / 2);
				ResetTitle();
				break;

		case CCuttingToolParams::eChamfer:
				m_params.m_corner_radius = 0;
				m_params.m_flat_radius = 0;
				m_params.m_cutting_edge_angle = 45;
				height = (m_params.m_diameter / 2.0) * tan( degrees_to_radians(90.0 - m_params.m_cutting_edge_angle));
				m_params.m_cutting_edge_height = height;
				ResetTitle();
				break;

		case CCuttingToolParams::eTurningTool:
				// No special constraints for this.
				ResetTitle();
				break;

		default:
				wxMessageBox(_T("That is not a valid cutting tool type. Aborting value change."));
				return;
	} // End switch

} // End ResetParametersToReasonableValues() method

static void on_set_corner_radius(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->m_params.m_corner_radius = value;
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_flat_radius(double value, HeeksObj* object)
{
	if (value > (((CCuttingTool*)object)->m_params.m_diameter / 2.0))
	{
		wxMessageBox(_T("Flat radius cannot be larger than the tool's diameter"));
		return;
	}

	((CCuttingTool*)object)->m_params.m_flat_radius = value;

	if (((CCuttingTool*)object)->m_params.m_type == CCuttingToolParams::eChamfer)
	{
		// Recalculate the cutting edge length based on this new diameter
		// and the cutting angle.

		double opposite = ((CCuttingTool*)object)->m_params.m_diameter - ((CCuttingTool*)object)->m_params.m_flat_radius;
		double angle = ((CCuttingTool*)object)->m_params.m_cutting_edge_angle / 360.0 * 2 * PI;

		((CCuttingTool*)object)->m_params.m_cutting_edge_height = opposite / tan(angle);
	}

	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_cutting_edge_angle(double value, HeeksObj* object)
{
	if (value < 0)
	{
		wxMessageBox(_T("Cutting edge angle must be zero or positive."));
		return;
	}

	((CCuttingTool*)object)->m_params.m_cutting_edge_angle = value;
	if (((CCuttingTool*)object)->m_params.m_type == CCuttingToolParams::eChamfer)
	{
		// Recalculate the cutting edge length based on this new diameter
		// and the cutting angle.

		double opposite = ((CCuttingTool*)object)->m_params.m_diameter - ((CCuttingTool*)object)->m_params.m_flat_radius;
		double angle = ((CCuttingTool*)object)->m_params.m_cutting_edge_angle / 360.0 * 2 * PI;

		((CCuttingTool*)object)->m_params.m_cutting_edge_height = opposite / tan(angle);
	}

	((CCuttingTool*)object)->ResetTitle();
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_cutting_edge_height(double value, HeeksObj* object)
{
	if (value <= 0)
	{
		wxMessageBox(_T("Cutting edge height must be positive."));
		return;
	}

	if (((CCuttingTool*)object)->m_params.m_type == CCuttingToolParams::eChamfer)
	{
		wxMessageBox(_T("Cutting edge height is generated from diameter, flat radius and cutting edge angle for chamfering bits."));
		return;
	}

	((CCuttingTool*)object)->m_params.m_cutting_edge_height = value;
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_probe_offset_x(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->m_params.m_probe_offset_x = value;
	object->KillGLLists();
	heeksCAD->Repaint();
}

static void on_set_probe_offset_y(double value, HeeksObj* object)
{
	((CCuttingTool*)object)->m_params.m_probe_offset_y = value;
	object->KillGLLists();
	heeksCAD->Repaint();
}


void CCuttingToolParams::GetProperties(CCuttingTool* parent, std::list<Property *> *list)
{
	{
		int choice = m_automatically_generate_title;
		std::list< wxString > choices;
		choices.push_back( wxString(_("Leave manually assigned title")) );	// option 0 (false)
		choices.push_back( wxString(_("Automatically generate title")) );	// option 1 (true)

		list->push_back(new PropertyChoice(_("Automatic Title"), choices, choice, parent, on_set_automatically_generate_title));
	}

	if ((m_type != eTouchProbe) && (m_type != eToolLengthSwitch))
	{
		CCuttingToolParams::MaterialsList_t materials = CCuttingToolParams::GetMaterialsList();

		int choice = -1;
		std::list< wxString > choices;
		for (CCuttingToolParams::MaterialsList_t::size_type i=0; i<materials.size(); i++)
		{
			choices.push_back(materials[i].second);
			if (m_material == materials[i].first) choice = int(i);

		} // End for
		list->push_back(new PropertyChoice(_("Material"), choices, choice, parent, on_set_material));
	}


	{
		CCuttingToolParams::CuttingToolTypesList_t cutting_tool_types = CCuttingToolParams::GetCuttingToolTypesList();

		int choice = -1;
		std::list< wxString > choices;
		for (CCuttingToolParams::CuttingToolTypesList_t::size_type i=0; i<cutting_tool_types.size(); i++)
		{
			choices.push_back(cutting_tool_types[i].second);
			if (m_type == cutting_tool_types[i].first) choice = int(i);

		} // End for
		list->push_back(new PropertyChoice(_("Type"), choices, choice, parent, on_set_type));
	}

	if ((m_type != eTouchProbe) && (m_type != eToolLengthSwitch))
	{
		list->push_back(new PropertyLength(_("max_advance_per_revolution"), m_max_advance_per_revolution, parent, on_set_max_advance_per_revolution));
	} // End if - then

	if (m_type == eTurningTool)
	{
		// We're using lathe (turning) tools

		list->push_back(new PropertyLength(_("x_offset"), m_x_offset, parent, on_set_x_offset));
		list->push_back(new PropertyDouble(_("front_angle"), m_front_angle, parent, on_set_front_angle));
		list->push_back(new PropertyDouble(_("tool_angle"), m_tool_angle, parent, on_set_tool_angle));
		list->push_back(new PropertyDouble(_("back_angle"), m_back_angle, parent, on_set_back_angle));

		{
			std::list< wxString > choices;
			choices.push_back(_("(0) Unused"));
			choices.push_back(_("(1) Turning/Back Facing"));
			choices.push_back(_("(2) Turning/Facing"));
			choices.push_back(_("(3) Boring/Facing"));
			choices.push_back(_("(4) Boring/Back Facing"));
			choices.push_back(_("(5) Back Facing"));
			choices.push_back(_("(6) Turning"));
			choices.push_back(_("(7) Facing"));
			choices.push_back(_("(8) Boring"));
			choices.push_back(_("(9) Centre"));
			int choice = int(m_orientation);
			list->push_back(new PropertyChoice(_("orientation"), choices, choice, parent, on_set_orientation));
		}
	} // End if - then
	else
	{
		// We're using milling/drilling tools
		list->push_back(new PropertyLength(_("diameter"), m_diameter, parent, on_set_diameter));
		list->push_back(new PropertyLength(_("tool_length_offset"), m_tool_length_offset, parent, on_set_tool_length_offset));

		if ((m_type != eTouchProbe) && (m_type != eToolLengthSwitch))
		{
			list->push_back(new PropertyLength(_("flat_radius"), m_flat_radius, parent, on_set_flat_radius));
			list->push_back(new PropertyLength(_("corner_radius"), m_corner_radius, parent, on_set_corner_radius));
			list->push_back(new PropertyDouble(_("cutting_edge_angle"), m_cutting_edge_angle, parent, on_set_cutting_edge_angle));
			list->push_back(new PropertyLength(_("cutting_edge_height"), m_cutting_edge_height, parent, on_set_cutting_edge_height));
		} // End if - then
	} // End if - else

	if (m_type == eTouchProbe)
	{
		// The following are ONLY for touch probe tools
		list->push_back(new PropertyLength(_("Probe offset X"), m_probe_offset_x, parent, on_set_probe_offset_x));
		list->push_back(new PropertyLength(_("Probe offset Y"), m_probe_offset_y, parent, on_set_probe_offset_y));
	}


}

void CCuttingToolParams::WriteXMLAttributes(TiXmlNode *root)
{
	TiXmlElement * element;
	element = new TiXmlElement( "params" );
	root->LinkEndChild( element );

	element->SetDoubleAttribute("diameter", m_diameter);
	element->SetDoubleAttribute("x_offset", m_x_offset);
	element->SetDoubleAttribute("tool_length_offset", m_tool_length_offset);
	element->SetDoubleAttribute("max_advance_per_revolution", m_max_advance_per_revolution);

	element->SetAttribute("automatically_generate_title", m_automatically_generate_title );
	element->SetAttribute("material", m_material );
	element->SetAttribute("orientation", m_orientation );
	element->SetAttribute("type", int(m_type) );

	element->SetDoubleAttribute("corner_radius", m_corner_radius);
	element->SetDoubleAttribute("flat_radius", m_flat_radius);
	element->SetDoubleAttribute("cutting_edge_angle", m_cutting_edge_angle);
	element->SetDoubleAttribute("cutting_edge_height", m_cutting_edge_height);

	element->SetDoubleAttribute("front_angle", m_front_angle);
	element->SetDoubleAttribute("tool_angle", m_tool_angle);
	element->SetDoubleAttribute("back_angle", m_back_angle);

	element->SetDoubleAttribute("probe_offset_x", m_probe_offset_x);
	element->SetDoubleAttribute("probe_offset_y", m_probe_offset_y);
}

void CCuttingToolParams::ReadParametersFromXMLElement(TiXmlElement* pElem)
{
	if (pElem->Attribute("diameter")) pElem->Attribute("diameter", &m_diameter);
	if (pElem->Attribute("max_advance_per_revolution")) pElem->Attribute("max_advance_per_revolution", &m_max_advance_per_revolution);
	if (pElem->Attribute("automatically_generate_title")) pElem->Attribute("automatically_generate_title", &m_automatically_generate_title);
	if (pElem->Attribute("x_offset")) pElem->Attribute("x_offset", &m_x_offset);
	if (pElem->Attribute("tool_length_offset")) pElem->Attribute("tool_length_offset", &m_tool_length_offset);
	if (pElem->Attribute("material")) pElem->Attribute("material", &m_material);
	if (pElem->Attribute("orientation")) pElem->Attribute("orientation", &m_orientation);
	if (pElem->Attribute("type")) { int value; pElem->Attribute("type", &value); m_type = CCuttingToolParams::eCuttingToolType(value); }
	if (pElem->Attribute("corner_radius")) pElem->Attribute("corner_radius", &m_corner_radius);
	if (pElem->Attribute("flat_radius")) pElem->Attribute("flat_radius", &m_flat_radius);
	if (pElem->Attribute("cutting_edge_angle")) pElem->Attribute("cutting_edge_angle", &m_cutting_edge_angle);
	if (pElem->Attribute("cutting_edge_height"))
	{
		 pElem->Attribute("cutting_edge_height", &m_cutting_edge_height);
	} // End if - then
	else
	{
		m_cutting_edge_height = m_diameter * 4.0;
	} // End if - else

	if (pElem->Attribute("front_angle")) pElem->Attribute("front_angle", &m_front_angle);
	if (pElem->Attribute("tool_angle")) pElem->Attribute("tool_angle", &m_tool_angle);
	if (pElem->Attribute("back_angle")) pElem->Attribute("back_angle", &m_back_angle);

	if (pElem->Attribute( "probe_offset_x" )) pElem->Attribute("probe_offset_x", &m_probe_offset_x);
	if (pElem->Attribute( "probe_offset_y" )) pElem->Attribute("probe_offset_y", &m_probe_offset_y);
}

CCuttingTool::~CCuttingTool()
{
	DeleteSolid();
}




/**
	This method is called when the CAD operator presses the Python button.  This method generates
	Python source code whose job will be to generate RS-274 GCode.  It's done in two steps so that
	the Python code can be configured to generate GCode suitable for various CNC interpreters.
 */
Python CCuttingTool::AppendTextToProgram()
{
	Python python;

	// The G10 command can be used (within EMC2) to add a tool to the tool
        // table from within a program.
        // G10 L1 P[tool number] R[radius] X[offset] Z[offset] Q[orientation]
	//
	// The radius value must be expressed in MACHINE CONFIGURATION UNITS.  This may be different
	// to this model's drawing units.  The value is interpreted, at lease for EMC2, in terms
	// of the units setup for the machine's configuration (something.ini in EMC2 parlence).  At
	// the moment we don't have a MACHINE CONFIGURATION UNITS parameter so we've got a 50%
	// chance of getting it right.

	if (m_title.size() > 0)
	{
		python << _T("#(") << m_title.c_str() << _T(")\n");
	} // End if - then

	python << _T("tool_defn( id=") << m_tool_number << _T(", ");

	if (m_title.size() > 0)
	{
		python << _T("name=") << PythonString(m_title).c_str() << _T(", ");
	} // End if - then
	else
	{
		python << _T("name=None, ");
	} // End if - else

	if (m_params.m_diameter > 0)
	{
		python << _T("radius=") << m_params.m_diameter / 2 /theApp.m_program->m_units << _T(", ");
	} // End if - then
	else
	{
		python << _T("radius=None, ");
	} // End if - else

	if (m_params.m_tool_length_offset > 0)
	{
		python << _T("length=") << m_params.m_tool_length_offset /theApp.m_program->m_units;
	} // End if - then
	else
	{
		python << _T("length=None");
	} // End if - else

	python << _T(")\n");

	return(python);
}


static void on_set_tool_number(const int value, HeeksObj* object){((CCuttingTool*)object)->m_tool_number = value;}

/**
	NOTE: The m_title member is a special case.  The HeeksObj code looks for a 'GetShortString()' method.  If found, it
	adds a Property called 'Object Title'.  If the value is changed, it tries to call the 'OnEditString()' method.
	That's why the m_title value is not defined here
 */
void CCuttingTool::GetProperties(std::list<Property *> *list)
{
	list->push_back(new PropertyInt(_("tool_number"), m_tool_number, this, on_set_tool_number));

	m_params.GetProperties(this, list);
	HeeksObj::GetProperties(list);
}


HeeksObj *CCuttingTool::MakeACopy(void)const
{
	HeeksObj *duplicate = new CCuttingTool(*this);
	((CCuttingTool *) duplicate)->m_pToolSolid = NULL;	// We didn't duplicate this so reset the pointer.
	return(duplicate);
}

CCuttingTool::CCuttingTool( const CCuttingTool & rhs )
{
    m_pToolSolid = NULL;
    *this = rhs;    // Call the assignment operator.
}

CCuttingTool & CCuttingTool::operator= ( const CCuttingTool & rhs )
{
    if (this != &rhs)
    {
        m_params = rhs.m_params;
        m_title = rhs.m_title;
        m_tool_number = rhs.m_tool_number;

        if (m_pToolSolid)
        {
            delete m_pToolSolid;
            m_pToolSolid = NULL;
        }

        HeeksObj::operator=( rhs );
    }

    return(*this);
}


void CCuttingTool::CopyFrom(const HeeksObj* object)
{
	operator=(*((CCuttingTool*)object));
	m_pToolSolid = NULL;	// We didn't duplicate this so reset the pointer.
}

bool CCuttingTool::CanAddTo(HeeksObj* owner)
{
	return ((owner != NULL) && (owner->GetType() == ToolsType));
}

const wxBitmap &CCuttingTool::GetIcon()
{
	switch(m_params.m_type){
		case CCuttingToolParams::eDrill:
			{
				static wxBitmap* drillIcon = NULL;
				if(drillIcon == NULL)drillIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/drill.png")));
				return *drillIcon;
			}
		case CCuttingToolParams::eCentreDrill:
			{
				static wxBitmap* centreDrillIcon = NULL;
				if(centreDrillIcon == NULL)centreDrillIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/centredrill.png")));
				return *centreDrillIcon;
			}
		case CCuttingToolParams::eEndmill:
			{
				static wxBitmap* endmillIcon = NULL;
				if(endmillIcon == NULL)endmillIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/endmill.png")));
				return *endmillIcon;
			}
		case CCuttingToolParams::eSlotCutter:
			{
				static wxBitmap* slotCutterIcon = NULL;
				if(slotCutterIcon == NULL)slotCutterIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/slotdrill.png")));
				return *slotCutterIcon;
			}
		case CCuttingToolParams::eBallEndMill:
			{
				static wxBitmap* ballEndMillIcon = NULL;
				if(ballEndMillIcon == NULL)ballEndMillIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons//ballmill.png")));
				return *ballEndMillIcon;
			}
		case CCuttingToolParams::eChamfer:
			{
				static wxBitmap* chamferIcon = NULL;
				if(chamferIcon == NULL)chamferIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/chamfmill.png")));
				return *chamferIcon;
			}
		case CCuttingToolParams::eTurningTool:
			{
				static wxBitmap* turningToolIcon = NULL;
				if(turningToolIcon == NULL)turningToolIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/turntool.png")));
				return *turningToolIcon;
			}
		case CCuttingToolParams::eTouchProbe:
			{
				static wxBitmap* touchProbeIcon = NULL;
				if(touchProbeIcon == NULL)touchProbeIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/probe.png")));
				return *touchProbeIcon;
			}
		case CCuttingToolParams::eToolLengthSwitch:
			{
				static wxBitmap* toolLengthSwitchIcon = NULL;
				if(toolLengthSwitchIcon == NULL)toolLengthSwitchIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/probe.png")));
				return *toolLengthSwitchIcon;
			}
		default:
			{
				static wxBitmap* toolIcon = NULL;
				if(toolIcon == NULL)toolIcon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/tool.png")));
				return *toolIcon;
			}
	}
}

void CCuttingTool::WriteXML(TiXmlNode *root)
{
	TiXmlElement * element = new TiXmlElement( "CuttingTool" );
	root->LinkEndChild( element );
	element->SetAttribute("title", m_title.utf8_str());

	element->SetAttribute("tool_number", m_tool_number );

	m_params.WriteXMLAttributes(element);
	WriteBaseXML(element);
}

// static member function
HeeksObj* CCuttingTool::ReadFromXMLElement(TiXmlElement* element)
{

	int tool_number = 0;
	if (element->Attribute("tool_number")) element->Attribute("tool_number", &tool_number);

	wxString title(Ctt(element->Attribute("title")));
	CCuttingTool* new_object = new CCuttingTool( title.c_str(), CCuttingToolParams::eDrill, tool_number);

	// read point and circle ids
	for(TiXmlElement* pElem = TiXmlHandle(element).FirstChildElement().Element(); pElem; pElem = pElem->NextSiblingElement())
	{
		std::string name(pElem->Value());
		if(name == "params"){
			new_object->m_params.ReadParametersFromXMLElement(pElem);
		}
	}

	new_object->ReadBaseXML(element);
	new_object->ResetTitle();	// reset title to match design units.
	return new_object;
}


void CCuttingTool::OnEditString(const wxChar* str)
{
    m_title.assign(str);
	m_params.m_automatically_generate_title = false;	// It's been manually edited.  Leave it alone now.
	heeksCAD->Changed();
}

CCuttingTool *CCuttingTool::Find( const int tool_number )
{
	int id = FindCuttingTool( tool_number );
	if (id <= 0) return(NULL);
	return((CCuttingTool *) heeksCAD->GetIDObject( CuttingToolType, id ));
} // End Find() method

CCuttingTool::ToolNumber_t CCuttingTool::FindFirstByType( const CCuttingToolParams::eCuttingToolType type )
{

	if ((theApp.m_program) && (theApp.m_program->Tools()))
	{
		HeeksObj* tool_list = theApp.m_program->Tools();
		for(HeeksObj* ob = tool_list->GetFirstChild(); ob; ob = tool_list->GetNextChild())
		{
			if (ob->GetType() != CuttingToolType) continue;
			if ((ob != NULL) && (((CCuttingTool *) ob)->m_params.m_type == type))
			{
				return(((CCuttingTool *)ob)->m_tool_number);
			} // End if - then
		} // End for
	} // End if - then

	return(-1);
}


/**
 * Find the CuttingTool object whose tool number matches that passed in.
 */
int CCuttingTool::FindCuttingTool( const int tool_number )
{

	if ((theApp.m_program) && (theApp.m_program->Tools()))
	{
		HeeksObj* tool_list = theApp.m_program->Tools();

		for(HeeksObj* ob = tool_list->GetFirstChild(); ob; ob = tool_list->GetNextChild())
		{
			if (ob->GetType() != CuttingToolType) continue;
			if ((ob != NULL) && (((CCuttingTool *) ob)->m_tool_number == tool_number))
			{
				return(ob->m_id);
			} // End if - then
		} // End for
	} // End if - then

	return(-1);

} // End FindCuttingTool() method


std::vector< std::pair< int, wxString > > CCuttingTool::FindAllCuttingTools()
{
	std::vector< std::pair< int, wxString > > tools;

	// Always add a value of zero to allow for an absense of cutting tool use.
	tools.push_back( std::make_pair(0, _T("No Cutting Tool") ) );

	if ((theApp.m_program) && (theApp.m_program->Tools()))
	{
		HeeksObj* tool_list = theApp.m_program->Tools();

		for(HeeksObj* ob = tool_list->GetFirstChild(); ob; ob = tool_list->GetNextChild())
		{
			if (ob->GetType() != CuttingToolType) continue;

			CCuttingTool *pCuttingTool = (CCuttingTool *)ob;
			if (ob != NULL)
			{
				tools.push_back( std::make_pair( pCuttingTool->m_tool_number, pCuttingTool->GetShortString() ) );
			} // End if - then
		} // End for
	} // End if - then

	return(tools);

} // End FindAllCuttingTools() method



/**
	Find a fraction that represents this floating point number.  We use this
	purely for readability purposes.  It only looks accurate to the nearest 1/64th

	eg: 0.125 -> "1/8"
	    1.125 -> "1 1/8"
	    0.109375 -> "7/64"
 */
/* static */ wxString CCuttingTool::FractionalRepresentation( const double original_value, const int max_denominator /* = 64 */ )
{

    wxString result;
	double _value(original_value);
	double near_enough = 0.00001;

	if (floor(_value) > 0)
	{
		result << floor(_value) << _T(" ");
		_value -= floor(_value);
	} // End if - then

	// We only want even numbers between 2 and 64 for the denominator.  The others just look 'wierd'.
	for (int denominator = 2; denominator <= max_denominator; denominator *= 2)
	{
		for (int numerator = 1; numerator < denominator; numerator++)
		{
			double fraction = double( double(numerator) / double(denominator) );
			if ( ((_value > fraction) && ((_value - fraction) < near_enough)) ||
			     ((_value < fraction) && ((fraction - _value) < near_enough)) ||
			     (_value == fraction) )
			{
				result << numerator << _T("/") << denominator;
				return(result);
			} // End if - then
		} // End for
	} // End for


	result = _T("");	// It's not a recognisable fraction.  Return nothing to indicate such.
	return(result);
} // End FractionalRepresentation() method


/**
 * This method uses the various attributes of the cutting tool to produce a meaningful name.
 * eg: with diameter = 6, units = 1 (mm) and type = 'drill' the name would be '6mm Drill Bit".  The
 * idea is to produce a m_title value that is representative of the cutting tool.  This will
 * make selection in the program list easier.
 *
 * NOTE: The ResetTitle() method looks at the m_title value for strings that are likely to
 * have come from this method.  If this method changes, the ResetTitle() method may also
 * need to change.
 */
wxString CCuttingTool::GenerateMeaningfulName() const
{
#ifdef UNICODE
	std::wostringstream l_ossName;
#else
    std::ostringstream l_ossName;
#endif

	if (	(m_params.m_type != CCuttingToolParams::eTurningTool) &&
		(m_params.m_type != CCuttingToolParams::eTouchProbe) &&
		(m_params.m_type != CCuttingToolParams::eToolLengthSwitch))
	{
		if (theApp.m_program->m_units == 1)
		{
			// We're using metric.  Leave the diameter as a floating point number.  It just looks more natural.
			l_ossName << m_params.m_diameter / theApp.m_program->m_units << " mm ";
		} // End if - then
		else
		{
			// We're using inches.  Find a fractional representation if one matches.
			wxString fraction = FractionalRepresentation(m_params.m_diameter / theApp.m_program->m_units);
			wxString guage = GuageNumberRepresentation( m_params.m_diameter / theApp.m_program->m_units, theApp.m_program->m_units );

			if (fraction.Len() > 0)
			{
                l_ossName << fraction.c_str() << " inch ";
			}
			else
			{
			    if (guage.Len() > 0)
			    {
                    l_ossName << guage.c_str() << " ";

                    if ((theApp.m_program) && (theApp.m_program->Tools()))
                    {
                        if (theApp.m_program->Tools()->m_title_format == CTools::eIncludeGuageAndSize)
                        {
                            l_ossName << "(" << m_params.m_diameter / theApp.m_program->m_units << " inch) ";
                        }
                    }
			    }
			    else
			    {
			        l_ossName << m_params.m_diameter / theApp.m_program->m_units << " inch ";
			    }
			}
		} // End if - else
	} // End if - then

	if ((m_params.m_type != CCuttingToolParams::eTouchProbe) && (m_params.m_type != CCuttingToolParams::eToolLengthSwitch))
	{
		switch (m_params.m_material)
		{
			case CCuttingToolParams::eHighSpeedSteel: l_ossName << "HSS ";
								  break;

			case CCuttingToolParams::eCarbide:	l_ossName << (_("Carbide "));
								break;
		} // End switch
	} // End if - then

	switch (m_params.m_type)
	{
		case CCuttingToolParams::eDrill:	l_ossName << (_("Drill Bit"));
							break;

		case CCuttingToolParams::eCentreDrill:	l_ossName << (_("Centre Drill Bit"));
							break;

                case CCuttingToolParams::eEndmill:	l_ossName << (_("End Mill"));
							break;

                case CCuttingToolParams::eSlotCutter:	l_ossName << (_("Slot Cutter"));
							break;

                case CCuttingToolParams::eBallEndMill:	l_ossName << (_("Ball End Mill"));
							break;

                case CCuttingToolParams::eChamfer:	l_ossName.str(_T(""));	// Remove all that we've already prepared.
							l_ossName << m_params.m_cutting_edge_angle << (_T(" degreee "));
                					l_ossName << (_("Chamfering Bit"));
							break;

                case CCuttingToolParams::eTurningTool:	l_ossName << (_("Turning Tool"));
							break;

                case CCuttingToolParams::eTouchProbe:	l_ossName << (_("Touch Probe"));
							break;

                case CCuttingToolParams::eToolLengthSwitch:	l_ossName << (_("Tool Length Switch"));
							break;

		default:				break;
	} // End switch

	return( l_ossName.str().c_str() );
} // End GenerateMeaningfulName() method


/**
	Reset the m_title value with a meaningful name ONLY if it does not look like it was
	automatically generated in the first place.  If someone has reset it manually then leave it alone.

	Return a verbose description of what we've done (if anything) so that we can pop up a
	warning message to the operator letting them know.
 */
wxString CCuttingTool::ResetTitle()
{
	if (m_params.m_automatically_generate_title)
	{
		// It has the default title.  Give it a name that makes sense.
		m_title = GenerateMeaningfulName();
		heeksCAD->Changed();

#ifdef UNICODE
		std::wostringstream l_ossChange;
#else
		std::ostringstream l_ossChange;
#endif
		l_ossChange << "Changing name to " << m_title.c_str() << "\n";
		return( l_ossChange.str().c_str() );
	} // End if - then

	// Nothing changed, nothing to report
	return(_T(""));
} // End ResetTitle() method



/**
        This is the Graphics Library Commands (from the OpenGL set).  This method calls the OpenGL
        routines to paint the cutting tool in the graphics window.

	We want to draw an outline of the cutting tool in 2 dimensions so that the operator
	gets a feel for what the various cutting tool parameter values mean.
 */
void CCuttingTool::glCommands(bool select, bool marked, bool no_color)
{
	// draw this tool only if it is selected
	if(heeksCAD->ObjectMarked(this) && !no_color)
	{
		if(!m_pToolSolid)
		{
			try {
				TopoDS_Shape tool_shape = GetShape();
				m_pToolSolid = heeksCAD->NewSolid( *((TopoDS_Solid *) &tool_shape), NULL, HeeksColor(234, 123, 89) );
			} // End try
			catch(Standard_DomainError) { }
			catch(...)  { }
		}

		if(m_pToolSolid)m_pToolSolid->glCommands( true, false, true );
	} // End if - then

} // End glCommands() method

void CCuttingTool::KillGLLists(void)
{
	DeleteSolid();
}

void CCuttingTool::DeleteSolid()
{
	if(m_pToolSolid)delete m_pToolSolid;
	m_pToolSolid = NULL;
}

/**
	This method produces a "Topology Data Structure - Shape" based on the parameters
	describing the tool's dimensions.  We will (probably) use this, along with the
	NCCode paths to find out what devastation this tool will make when run through
	the program.  We will then (again probably) intersect the resulting solid with
	things like fixture solids to see if we're going to break anything.  We could
	also intersect it with a solid that represents the raw material (i.e before it
	has been machined).  This would give us an idea of what we'd end up with if
	we ran the GCode program.

	Finally, we might use this to 'apply' a GCode operation to existing geometry.  eg:
	where a drilling cycle is defined, the result of drilling a hole from one
	location for a certain depth will be a hole in that green solid down there.  We
	may want to do this so as to use the edges (or other side-effects) to define
	subsequent NC operations.  (just typing out loud)

	NOTE: The shape is always drawn with the tool's tip at the origin.
	It is always drawn along the Z axis.  The calling routine may move and rotate the drawn
	shape if need be but this method returns a standard straight up and down version.
 */
TopoDS_Shape CCuttingTool::GetShape() const
{
   try {
	gp_Dir orientation(0,0,1);	// This method always draws it up and down.  Leave it
					// for other methods to rotate the resultant shape if
					// they need to.
	gp_Pnt tool_tip_location(0,0,0);	// Always from the origin in this method.

	double diameter = m_params.m_diameter;
	if (diameter < 0.01) diameter = 2;

	double tool_length_offset = m_params.m_tool_length_offset;
	if (tool_length_offset <  diameter) tool_length_offset = 10 * diameter;

	double cutting_edge_height = m_params.m_cutting_edge_height;
	if (cutting_edge_height < (2 * diameter)) cutting_edge_height = 2 * diameter;

	switch (m_params.m_type)
	{
		case CCuttingToolParams::eCentreDrill:
		{
			// First a cylinder to represent the shaft.
			double tool_tip_length = (diameter / 2) * tan( degrees_to_radians(90.0 - m_params.m_cutting_edge_angle));
			double non_cutting_shaft_length = tool_length_offset - tool_tip_length - cutting_edge_height;

			gp_Pnt shaft_start_location( tool_tip_location );
			shaft_start_location.SetZ( tool_tip_location.Z() + tool_tip_length + cutting_edge_height );
			gp_Ax2 shaft_position_and_orientation( shaft_start_location, orientation );
			BRepPrimAPI_MakeCylinder shaft( shaft_position_and_orientation, (diameter / 2) * 2.0, non_cutting_shaft_length );

			gp_Pnt cutting_shaft_start_location( tool_tip_location );
			cutting_shaft_start_location.SetZ( tool_tip_location.Z() + tool_tip_length );
			gp_Ax2 cutting_shaft_position_and_orientation( cutting_shaft_start_location, orientation );
			BRepPrimAPI_MakeCylinder cutting_shaft( cutting_shaft_position_and_orientation, diameter / 2, cutting_edge_height );

			// And a cone for the tip.
			gp_Ax2 tip_position_and_orientation( cutting_shaft_start_location, gp_Dir(0,0,-1) );
			BRepPrimAPI_MakeCone tool_tip( tip_position_and_orientation,
							diameter/2,
							m_params.m_flat_radius,
							tool_tip_length);

			TopoDS_Shape shafts = BRepAlgoAPI_Fuse(shaft.Shape() , cutting_shaft.Shape() );
			TopoDS_Shape cutting_tool_shape = BRepAlgoAPI_Fuse(shafts , tool_tip.Shape() );
			return cutting_tool_shape;
		}

		case CCuttingToolParams::eDrill:
		{
			// First a cylinder to represent the shaft.
			double tool_tip_length = (diameter / 2) * tan( degrees_to_radians(90 - m_params.m_cutting_edge_angle));
			double shaft_length = tool_length_offset - tool_tip_length;

			gp_Pnt shaft_start_location( tool_tip_location );
			shaft_start_location.SetZ( tool_tip_location.Z() + tool_tip_length );

			gp_Ax2 shaft_position_and_orientation( shaft_start_location, orientation );

			BRepPrimAPI_MakeCylinder shaft( shaft_position_and_orientation, diameter / 2, shaft_length );

			// And a cone for the tip.
			gp_Ax2 tip_position_and_orientation( shaft_start_location, gp_Dir(0,0,-1) );

			BRepPrimAPI_MakeCone tool_tip( tip_position_and_orientation,
							diameter/2,
							m_params.m_flat_radius,
							tool_tip_length);

			TopoDS_Shape cutting_tool_shape = BRepAlgoAPI_Fuse(shaft.Shape() , tool_tip.Shape() );
			return cutting_tool_shape;
		}

		case CCuttingToolParams::eChamfer:
		{
			// First a cylinder to represent the shaft.
			double tool_tip_length_a = (diameter / 2) * tan( degrees_to_radians(90.0 - m_params.m_cutting_edge_angle));
			double tool_tip_length_b = (m_params.m_flat_radius)  * tan( degrees_to_radians(90.0 - m_params.m_cutting_edge_angle));
			double tool_tip_length = tool_tip_length_a - tool_tip_length_b;

			double shaft_length = tool_length_offset - tool_tip_length;

			gp_Pnt shaft_start_location( tool_tip_location );
			shaft_start_location.SetZ( tool_tip_location.Z() + tool_tip_length );

			gp_Ax2 shaft_position_and_orientation( shaft_start_location, orientation );

			BRepPrimAPI_MakeCylinder shaft( shaft_position_and_orientation, (diameter / 2) * 0.5, shaft_length );

			// And a cone for the tip.
			// double cutting_edge_angle_in_radians = ((m_params.m_cutting_edge_angle / 2) / 360) * (2 * PI);
			gp_Ax2 tip_position_and_orientation( shaft_start_location, gp_Dir(0,0,-1) );

			BRepPrimAPI_MakeCone tool_tip( tip_position_and_orientation,
							diameter/2,
							m_params.m_flat_radius,
							tool_tip_length);

			TopoDS_Shape cutting_tool_shape = BRepAlgoAPI_Fuse(shaft.Shape() , tool_tip.Shape() );
			return cutting_tool_shape;
		}

		case CCuttingToolParams::eBallEndMill:
		{
			// First a cylinder to represent the shaft.
			double shaft_length = tool_length_offset - m_params.m_corner_radius;

			gp_Pnt shaft_start_location( tool_tip_location );
			shaft_start_location.SetZ( tool_tip_location.Z() + m_params.m_corner_radius );

			gp_Ax2 shaft_position_and_orientation( shaft_start_location, orientation );

			BRepPrimAPI_MakeCylinder shaft( shaft_position_and_orientation, diameter / 2, shaft_length );
			BRepPrimAPI_MakeSphere ball( shaft_start_location, diameter / 2 );

			// TopoDS_Compound cutting_tool_shape;
			TopoDS_Shape cutting_tool_shape;
			cutting_tool_shape = BRepAlgoAPI_Fuse(shaft.Shape() , ball.Shape() );
			return cutting_tool_shape;
		}

		case CCuttingToolParams::eTouchProbe:
		case CCuttingToolParams::eToolLengthSwitch:	// TODO: Draw a tool length switch
		{
			// First a cylinder to represent the shaft.
			double shaft_length = tool_length_offset - diameter;

			gp_Pnt shaft_start_location( tool_tip_location );
			shaft_start_location.SetZ( tool_tip_location.Z() - shaft_length );

			gp_Ax2 tip_position_and_orientation( tool_tip_location, gp_Dir(0,0,+1) );
			BRepPrimAPI_MakeCone shaft( tip_position_and_orientation,
							diameter/16.0,
							diameter/2,
							shaft_length);

			BRepPrimAPI_MakeSphere ball( tool_tip_location, diameter / 2.0 );

			// TopoDS_Compound cutting_tool_shape;
			TopoDS_Shape cutting_tool_shape;
			cutting_tool_shape = BRepAlgoAPI_Fuse(shaft.Shape() , ball.Shape() );
			return cutting_tool_shape;
		}

		case CCuttingToolParams::eTurningTool:
		{
			// First draw the cutting tip.
			double triangle_radius = 8.0;	// mm
			double cutting_tip_thickness = 3.0;	// mm

			gp_Trsf rotation;

			gp_Pnt p1(0.0, triangle_radius, 0.0);
			rotation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(0) );
			p1.Transform(rotation);

			rotation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(+1 * ((360 - m_params.m_tool_angle)/2)) );
			gp_Pnt p2(p1);
			p2.Transform(rotation);

			rotation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(-1 * ((360 - m_params.m_tool_angle)/2)) );
			gp_Pnt p3(p1);
			p3.Transform(rotation);

			Handle(Geom_TrimmedCurve) line1 = GC_MakeSegment(p1,p2);
			Handle(Geom_TrimmedCurve) line2 = GC_MakeSegment(p2,p3);
			Handle(Geom_TrimmedCurve) line3 = GC_MakeSegment(p3,p1);

			TopoDS_Edge edge1 = BRepBuilderAPI_MakeEdge( line1 );
			TopoDS_Edge edge2 = BRepBuilderAPI_MakeEdge( line2 );
			TopoDS_Edge edge3 = BRepBuilderAPI_MakeEdge( line3 );

			TopoDS_Wire wire = BRepBuilderAPI_MakeWire( edge1, edge2, edge3 );
			TopoDS_Face face = BRepBuilderAPI_MakeFace( wire );
			gp_Vec vec( 0,0, cutting_tip_thickness );
			TopoDS_Shape cutting_tip = BRepPrimAPI_MakePrism( face, vec );

			// Now make the supporting shaft
			gp_Pnt p4(p3); p4.SetZ( p3.Z() + cutting_tip_thickness );
			gp_Pnt p5(p2); p5.SetZ( p2.Z() + cutting_tip_thickness );

			Handle(Geom_TrimmedCurve) shaft_line1 = GC_MakeSegment(p2,p3);
			Handle(Geom_TrimmedCurve) shaft_line2 = GC_MakeSegment(p3,p4);
			Handle(Geom_TrimmedCurve) shaft_line3 = GC_MakeSegment(p4,p5);
			Handle(Geom_TrimmedCurve) shaft_line4 = GC_MakeSegment(p5,p2);

			TopoDS_Edge shaft_edge1 = BRepBuilderAPI_MakeEdge( shaft_line1 );
			TopoDS_Edge shaft_edge2 = BRepBuilderAPI_MakeEdge( shaft_line2 );
			TopoDS_Edge shaft_edge3 = BRepBuilderAPI_MakeEdge( shaft_line3 );
			TopoDS_Edge shaft_edge4 = BRepBuilderAPI_MakeEdge( shaft_line4 );

			TopoDS_Wire shaft_wire = BRepBuilderAPI_MakeWire( shaft_edge1, shaft_edge2, shaft_edge3, shaft_edge4 );
			TopoDS_Face shaft_face = BRepBuilderAPI_MakeFace( shaft_wire );
			gp_Vec shaft_vec( 0, (-1 * tool_length_offset), 0 );
			TopoDS_Shape shaft = BRepPrimAPI_MakePrism( shaft_face, shaft_vec );

			// Aggregate the shaft and cutting tip
			TopoDS_Shape cutting_tool_shape = BRepAlgoAPI_Fuse(shaft , cutting_tip );

			// Now orient the tool as per its settings.
			gp_Trsf tool_holder_orientation;
			gp_Trsf orient_for_lathe_use;

			switch (m_params.m_orientation)
			{
				case 1: // South East
					tool_holder_orientation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(45 - 90) );
					break;

				case 2: // South West
					tool_holder_orientation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(123 - 90) );
					break;

				case 3: // North West
					tool_holder_orientation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(225 - 90) );
					break;

				case 4: // North East
					tool_holder_orientation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(-45 - 90) );
					break;

				case 5: // East
					tool_holder_orientation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(0 - 90) );
					break;

				case 6: // South
					tool_holder_orientation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(+90 - 90) );
					break;

				case 7: // West
					tool_holder_orientation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(180 - 90) );
					break;

				case 8: // North
					tool_holder_orientation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1)), degrees_to_radians(-90 - 90) );
					break;

				case 9: // Boring (straight along Y axis)
				default:
					tool_holder_orientation.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(1,0,0)), degrees_to_radians(90.0) );
					break;

			} // End switch

			// Rotate from drawing orientation (for easy mathematics in this code) to tool holder orientation.
			cutting_tool_shape = BRepBuilderAPI_Transform( cutting_tool_shape, tool_holder_orientation, false );

			// Rotate to use axes typically used for lathe work.
			// i.e. z axis along the bed (from head stock to tail stock as z increases)
			// and x across the bed.
			orient_for_lathe_use.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,1,0) ), degrees_to_radians(-90.0) );
			cutting_tool_shape = BRepBuilderAPI_Transform( cutting_tool_shape, orient_for_lathe_use, false );

			orient_for_lathe_use.SetRotation( gp_Ax1( gp_Pnt(0,0,0), gp_Dir(0,0,1) ), degrees_to_radians(90.0) );
			cutting_tool_shape = BRepBuilderAPI_Transform( cutting_tool_shape, orient_for_lathe_use, false );

			return(cutting_tool_shape);
		}

		case CCuttingToolParams::eEndmill:
		case CCuttingToolParams::eSlotCutter:
		default:
		{
			// First a cylinder to represent the shaft.
			double shaft_length = tool_length_offset;
			gp_Pnt shaft_start_location( tool_tip_location );

			gp_Ax2 shaft_position_and_orientation( shaft_start_location, orientation );

			BRepPrimAPI_MakeCylinder shaft( shaft_position_and_orientation, diameter / 2, shaft_length );

			TopoDS_Compound cutting_tool_shape;
			return(shaft.Shape());
		}
	} // End switch
   } // End try
   // These are due to poor parameter settings resulting in negative lengths and the like.  I need
   // to work through the various parameters to either ensure they're correct or don't try
   // to construct a shape.
   catch (Standard_ConstructionError)
   {
	// printf("Construction error thrown while generating tool shape\n");
	throw;	// Re-throw the exception.
   } // End catch
   catch (Standard_DomainError)
   {
	// printf("Domain error thrown while generating tool shape\n");
	throw;	// Re-throw the exception.
   } // End catch
} // End GetShape() method


TopoDS_Face CCuttingTool::GetSideProfile() const
{
   try {
	gp_Dir orientation(0,0,1);	// This method always draws it up and down.  Leave it
					// for other methods to rotate the resultant shape if
					// they need to.
	gp_Pnt tool_tip_location(0,0,0);	// Always from the origin in this method.

	double diameter = m_params.m_diameter;
	if (diameter < 0.01) diameter = 2;

	double tool_length_offset = m_params.m_tool_length_offset;
	if (tool_length_offset <  diameter) tool_length_offset = 10 * diameter;

	double cutting_edge_height = m_params.m_cutting_edge_height;
	if (cutting_edge_height < (2 * diameter)) cutting_edge_height = 2 * diameter;

    gp_Pnt top_left( tool_tip_location );
    gp_Pnt top_right( tool_tip_location );
    gp_Pnt bottom_left( tool_tip_location );
    gp_Pnt bottom_right( tool_tip_location );

    top_left.SetY( tool_tip_location.Y() - CuttingRadius());
    bottom_left.SetY( tool_tip_location.Y() - CuttingRadius());

    top_right.SetY( tool_tip_location.Y() + CuttingRadius());
    bottom_right.SetY( tool_tip_location.Y() + CuttingRadius());

    top_left.SetZ( tool_tip_location.Z() + cutting_edge_height);
    bottom_left.SetZ( tool_tip_location.Z() );

    top_right.SetZ( tool_tip_location.Z() + cutting_edge_height);
    bottom_right.SetZ( tool_tip_location.Z() );

    Handle(Geom_TrimmedCurve) seg1 = GC_MakeSegment(top_left, bottom_left);
    Handle(Geom_TrimmedCurve) seg2 = GC_MakeSegment(bottom_left, bottom_right);
    Handle(Geom_TrimmedCurve) seg3 = GC_MakeSegment(bottom_right, top_right);
    Handle(Geom_TrimmedCurve) seg4 = GC_MakeSegment(top_right, top_left);

    TopoDS_Edge edge1 = BRepBuilderAPI_MakeEdge(seg1);
    TopoDS_Edge edge2 = BRepBuilderAPI_MakeEdge(seg2);
    TopoDS_Edge edge3 = BRepBuilderAPI_MakeEdge(seg3);
    TopoDS_Edge edge4 = BRepBuilderAPI_MakeEdge(seg4);

    BRepBuilderAPI_MakeWire wire_maker;

    wire_maker.Add(edge1);
    wire_maker.Add(edge2);
    wire_maker.Add(edge3);
    wire_maker.Add(edge4);

    TopoDS_Face face = BRepBuilderAPI_MakeFace(wire_maker.Wire());
    return(face);
   } // End try
   // These are due to poor parameter settings resulting in negative lengths and the like.  I need
   // to work through the various parameters to either ensure they're correct or don't try
   // to construct a shape.
   catch (Standard_ConstructionError)
   {
	// printf("Construction error thrown while generating tool shape\n");
	throw;	// Re-throw the exception.
   } // End catch
   catch (Standard_DomainError)
   {
	// printf("Domain error thrown while generating tool shape\n");
	throw;	// Re-throw the exception.
   } // End catch
} // End GetSideProfile() method




/**
	The CuttingRadius is almost always the same as half the cutting tool's diameter.
	The exception to this is if it's a chamfering bit.  In this case we
	want to use the flat_radius plus a little bit.  i.e. if we're chamfering the edge
	then we want to use the part of the cutting surface just a little way from
	the flat radius.  If it has a flat radius of zero (i.e. it has a pointed end)
	then it will be a small number.  If it is a carbide tipped bit then the
	flat radius will allow for the area below the bit that doesn't cut.  In this
	case we want to cut around the middle of the carbide tip.  In this case
	the carbide tip should represent the full cutting edge height.  We can
	use this method to make all these adjustments based on the cutting tool's
	geometry and return a reasonable value.

	If express_in_drawing_units is true then we need to divide by the drawing
	units value.  We use metric (mm) internally and convert to inches only
	if we need to and only as the last step in the process.  By default, return
	the value in internal (metric) units.

	If the depth value is passed in as a positive number then the radius is given
	for the corresponding depth (from the bottom-most tip of the cutting tool).  This is
	only relevant for chamfering (angled) bits.
 */
double CCuttingTool::CuttingRadius( const bool express_in_drawing_units /* = false */, const double depth /* = -1 */ ) const
{
	double radius;

	switch (m_params.m_type)
	{
		case CCuttingToolParams::eChamfer:
			{
			    if (depth < 0.0)
			    {
                    // We want to decide where, along the cutting edge, we want
                    // to machine.  Let's start with 1/3 of the way from the inside
                    // cutting edge so that, as we plunge it into the material, it
                    // cuts towards the outside.  We don't want to run right on
                    // the edge because we don't want to break the top off.

                    // one third from the centre-most point.
                    double proportion_near_centre = 0.3;
                    radius = (((m_params.m_diameter/2) - m_params.m_flat_radius) * proportion_near_centre) + m_params.m_flat_radius;
			    }
			    else
			    {
			        radius = m_params.m_flat_radius + (depth * tan((m_params.m_cutting_edge_angle / 360.0 * 2 * PI)));
			        if (radius > (m_params.m_diameter / 2.0))
			        {
			            // The angle and depth would have us cutting larger than our largest diameter.
			            radius = (m_params.m_diameter / 2.0);
			        }
			    }
			}
			break;

		case CCuttingToolParams::eDrill:
		case CCuttingToolParams::eCentreDrill:
		case CCuttingToolParams::eEndmill:
		case CCuttingToolParams::eSlotCutter:
		case CCuttingToolParams::eBallEndMill:
		case CCuttingToolParams::eTurningTool:
		case CCuttingToolParams::eTouchProbe:
		case CCuttingToolParams::eToolLengthSwitch:
		default:
			radius = m_params.m_diameter/2;
	} // End switch

	if (express_in_drawing_units) return(radius / theApp.m_program->m_units);
	else return(radius);

} // End CuttingRadius() method


CCuttingToolParams::eCuttingToolType CCuttingTool::CutterType( const int tool_number )
{
	if (tool_number <= 0) return(CCuttingToolParams::eUndefinedToolType);

	CCuttingTool *pCuttingTool = CCuttingTool::Find( tool_number );
	if (pCuttingTool == NULL) return(CCuttingToolParams::eUndefinedToolType);

	return(pCuttingTool->m_params.m_type);
} // End of CutterType() method


CCuttingToolParams::eMaterial_t CCuttingTool::CutterMaterial( const int tool_number )
{
	if (tool_number <= 0) return(CCuttingToolParams::eUndefinedMaterialType);

	CCuttingTool *pCuttingTool = CCuttingTool::Find( tool_number );
	if (pCuttingTool == NULL) return(CCuttingToolParams::eUndefinedMaterialType);

	return(CCuttingToolParams::eMaterial_t(pCuttingTool->m_params.m_material));
} // End of CutterType() method


class CuttingTool_ImportProbeData: public Tool
{

CCuttingTool *m_pThis;

public:
	CuttingTool_ImportProbeData() { m_pThis = NULL; }

	// Tool's virtual functions
	const wxChar* GetTitle(){return _("Import probe calibration data");}

	void Run()
	{
		// Prompt the user to select a file to import.
		wxFileDialog fd(heeksCAD->GetMainFrame(), _T("Select a file to import"), _T("."), _T(""),
				wxString(_("Known Files")) + _T(" |*.xml;*.XML;")
					+ _T("*.Xml;"),
					wxOPEN | wxFILE_MUST_EXIST );
		fd.SetFilterIndex(1);
		if (fd.ShowModal() == wxID_CANCEL) return;
		m_pThis->ImportProbeCalibrationData( fd.GetPath().c_str() );
	}
	wxString BitmapPath(){ return _T("import");}
	wxString previous_path;
	void Set( CCuttingTool *pThis ) { m_pThis = pThis; }
};

static CuttingTool_ImportProbeData import_probe_calibration_data;

void CCuttingTool::GetTools(std::list<Tool*>* t_list, const wxPoint* p)
{
	if (m_params.m_type == CCuttingToolParams::eTouchProbe)
	{
		import_probe_calibration_data.Set( this );

		t_list->push_back( &import_probe_calibration_data );
	}
}


void CCuttingTool::ImportProbeCalibrationData( const wxString & probed_points_xml_file_name )
{
	TiXmlDocument xml;
	if (! xml.LoadFile( probed_points_xml_file_name.utf8_str()) )
	{
		printf("Failed to load XML file '%s'\n", Ttc(probed_points_xml_file_name.c_str()) );
	} // End if - then
	else
	{
		TiXmlElement *root = xml.RootElement();
		if (root != NULL)
		{
			std::vector<CNCPoint> points;

			for(TiXmlElement* pElem = TiXmlHandle(root).FirstChildElement().Element(); pElem; pElem = pElem->NextSiblingElement())
			{
				CNCPoint point(0,0,0);
				for(TiXmlElement* pPoint = TiXmlHandle(pElem).FirstChildElement().Element(); pPoint; pPoint = pPoint->NextSiblingElement())
				{
					std::string name(pPoint->Value());

					if (name == "X") { double value; pPoint->Attribute("X", &value); point.SetX( value / theApp.m_program->m_units ); }
					if (name == "Y") { double value; pPoint->Attribute("Y", &value); point.SetY( value / theApp.m_program->m_units ); }
					if (name == "Z") { double value; pPoint->Attribute("Z", &value); point.SetZ( value / theApp.m_program->m_units ); }
				} // End for

				points.push_back(point);
			} // End for

			if (points.size() >= 1)
			{
				m_params.m_probe_offset_x = points[0].X(false);
				m_params.m_probe_offset_y = points[0].Y(false);
			} // End if - then
		} // End if - then
	} // End if - else
} // End ImportProbeCalibrationData() method


/* static */ wxString CCuttingTool::GuageNumberRepresentation( const double size, const double units )
{

    typedef struct Guages {
        const wxChar *guage;
        double imperial;
        double metric;
    } Guages_t;

    Guages_t guages[] = {{_T("80"),0.0135,0.343},{_T("79"),0.0145,0.368},{_T("78"),0.016,0.406},{_T("77"),0.018,0.457},{_T("76"),0.020,0.508},
                         {_T("75"),0.021,0.533},{_T("74"),0.0225,0.572},{_T("73"),0.024,0.610},{_T("72"),0.025,0.635},{_T("71"),0.026,0.660},
                         {_T("70"),0.028,0.711},{_T("69"),0.0292,0.742},{_T("68"),0.031,0.787},{_T("67"),0.032,0.813},{_T("66"),0.033,0.838},
                         {_T("65"),0.035,0.889},{_T("64"),0.036,0.914},{_T("63"),0.037,0.940},{_T("62"),0.038,0.965},{_T("61"),0.039,0.991},
                         {_T("60"),0.040,1.016},{_T("59"),0.041,1.041},{_T("58"),0.042,1.067},{_T("57"),0.043,1.092},{_T("56"),0.0465,1.181},
                         {_T("55"),0.052,1.321},{_T("54"),0.055,1.397},{_T("53"),0.0595,1.511},{_T("52"),0.0635,1.613},{_T("51"),0.067,1.702},
                         {_T("50"),0.070,1.778},{_T("49"),0.073,1.854},{_T("48"),0.076,1.930},{_T("47"),0.0785,1.994},{_T("46"),0.081,2.057},
                         {_T("45"),0.082,2.083},{_T("44"),0.086,2.184},{_T("43"),0.089,2.261},{_T("42"),0.0935,2.375},{_T("41"),0.096,2.438},
                         {_T("40"),0.098,2.489},{_T("39"),0.0995,2.527},{_T("38"),0.1015,2.578},{_T("37"),0.104,2.642},{_T("36"),0.1065,2.705},
                         {_T("35"),0.110,2.794},{_T("34"),0.111,2.819},{_T("33"),0.113,2.870},{_T("32"),0.116,2.946},{_T("31"),0.120,3.048},
                         {_T("30"),0.1285,3.264},{_T("29"),0.136,3.454},{_T("28"),0.1405,3.569},{_T("27"),0.144,3.658},{_T("26"),0.147,3.734},
                         {_T("25"),0.1495,3.797},{_T("24"),0.152,3.861},{_T("23"),0.154,3.912},{_T("22"),0.157,3.988},{_T("21"),0.159,4.039},
                         {_T("20"),0.161,4.089},{_T("19"),0.166,4.216},{_T("18"),0.1695,4.305},{_T("17"),0.173,4.394},{_T("16"),0.177,4.496},
                         {_T("15"),0.180,4.572},{_T("14"),0.182,4.623},{_T("13"),0.185,4.699},{_T("12"),0.189,4.801},{_T("11"),0.191,4.851},
                         {_T("10"),0.1935,4.915},{_T("9"),0.196,4.978},{_T("8"),0.199,5.055},{_T("7"),0.201,5.105},{_T("6"),0.204,5.182},
                         {_T("5"),0.2055,5.220},{_T("4"),0.209,5.309},{_T("3"),0.213,5.410},{_T("2"),0.221,5.613},{_T("1"),0.228,5.791},
                         {_T("A"),0.234,5.944},{_T("B"),0.238,6.045},{_T("C"),0.242,6.147},{_T("D"),0.246,6.248},{_T("E"),0.250,6.350},
                         {_T("F"),0.257,6.528},{_T("G"),0.261,6.629},{_T("H"),0.266,6.756},{_T("I"),0.272,6.909},{_T("J"),0.277,7.036},
                         {_T("K"),0.281,7.137},{_T("L"),0.290,7.366},{_T("M"),0.295,7.493},{_T("N"),0.302,7.671},{_T("O"),0.316,8.026},
                         {_T("P"),0.323,8.204},{_T("Q"),0.332,8.433},{_T("R"),0.339,8.611},{_T("S"),0.348,8.839},{_T("T"),0.358,9.093},
                         {_T("U"),0.368,9.347},{_T("V"),0.377,9.576},{_T("W"),0.386,9.804},{_T("X"),0.397,10.08},{_T("Y"),0.404,10.26},
                         {_T("Z"),0.413,10.49}};

    double tolerance = heeksCAD->GetTolerance();
    for (::size_t offset=0; offset < (sizeof(guages)/sizeof(guages[0])); offset++)
    {
        if (units > 25.0)
        {
            if (fabs(size - guages[offset].imperial) < tolerance)
            {
                wxString result;
                result << _T("#") << guages[offset].guage;
                return(result);
            }
        }
        else
        {
            if (fabs(size - guages[offset].metric) < tolerance)
            {
                wxString result;
                result << _T("#") << guages[offset].guage;
                return(result);
            }
        }
    } // End for

    return(_T(""));
} // End GuageNumberRepresentation() method

