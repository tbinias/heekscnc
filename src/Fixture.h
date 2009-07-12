
#ifndef FIXTURE_CLASS_DEFINTION
#define FIXTURE_CLASS_DEFINTION

// Fixture.h
/*
 * Copyright (c) 2009, Dan Heeks, Perttu Ahola
 * This program is released under the BSD license. See the file COPYING for
 * details.
 */

#include "Program.h"
#include "Op.h"
#include "HeeksCNCTypes.h"
#include <gp_Pnt.hxx>

#include <vector>
#include <algorithm>

class CFixture;

class CFixtureParams{
	
public:
	double m_a_axis;	// i.e. rotation angle around x axis - in degrees
	double m_b_axis;	// i.e. rotation angle around y axis - in degrees
	double m_c_axis;	// i.e. rotation angle around z axis - in degrees

	gp_Pnt m_origin;	// Fixture's origin (to rotate around)

	CFixtureParams()
	{
		m_a_axis = 0.0;
		m_b_axis = 0.0;
		m_c_axis = 0.0;

		m_origin = gp_Pnt( 0.0, 0.0, 0.0 );
	} // End constructor.

	void set_initial_values();
	void write_values_to_config();
	void GetProperties(CFixture* parent, std::list<Property *> *list);
	void WriteXMLAttributes(TiXmlNode* pElem);
	void ReadParametersFromXMLElement(TiXmlElement* pElem);
};

class CFixture: public HeeksObj {
public:
	//	These are references to the CAD elements whose position indicate where the Fixture Cycle begins.
	CFixtureParams m_params;
        wxString m_title;

	typedef enum {
		G54 = 1,
		G55,
		G56,
		G57,
		G58,
		G59,
		G59_1,
		G59_2,
		G59_3
	} eCoordinateSystemNumber_t;
	
	eCoordinateSystemNumber_t m_coordinate_system_number;

	//	Constructors.
        CFixture(const wxChar *title, const eCoordinateSystemNumber_t coordinate_system_number) : m_coordinate_system_number(coordinate_system_number)
	{
		m_params.set_initial_values(); 
		if (title != NULL) 
		{
			m_title = title;
		} // End if - then
		else
		{
			m_title = GenerateMeaningfulName();
		} // End if - else
	} // End constructor

	 // HeeksObj's virtual functions
        int GetType()const{return FixtureType;}
	const wxChar* GetTypeString(void) const{ return _T("Fixture"); }
        HeeksObj *MakeACopy(void)const;

        void WriteXML(TiXmlNode *root);
        static HeeksObj* ReadFromXMLElement(TiXmlElement* pElem);

	// program whose job is to generate RS-274 GCode.
	void AppendTextToProgram() const;

	void GetProperties(std::list<Property *> *list);
	void CopyFrom(const HeeksObj* object);
	bool CanAddTo(HeeksObj* owner);
	wxString GetIcon() { return theApp.GetResFolder() + _T("/icons/tool"); }
        const wxChar* GetShortString(void)const{return m_title.c_str();}
	void glCommands(bool select, bool marked, bool no_color);

        bool CanEditString(void)const{return true;}
        void OnEditString(const wxChar* str);

	static CFixture *Find( const eCoordinateSystemNumber_t coordinate_system_number );
	static int GetNextFixture();

	wxString GenerateMeaningfulName() const;
	wxString ResetTitle();

	gp_Pnt Adjustment( const gp_Pnt & point ) const;
	void Adjustment( double *point ) const;

}; // End CFixture class definition.




#endif // FIXTURE_CLASS_DEFINTION