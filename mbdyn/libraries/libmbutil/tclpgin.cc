/* $Header$ */
/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2010
 *
 * Pierangelo Masarati	<masarati@aero.polimi.it>
 * Paolo Mantegazza	<mantegazza@aero.polimi.it>
 *
 * Dipartimento di Ingegneria Aerospaziale - Politecnico di Milano
 * via La Masa, 34 - 20156 Milano, Italy
 * http://www.aero.polimi.it
 *
 * Changing this copyright notice is forbidden.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License).
 * 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#ifdef USE_TCL

#include <tclpgin.h>

TclPlugIn::TclPlugIn(MathParser& mp)
: MathParser::PlugIn(mp), type(TypedValue::VAR_UNKNOWN),
interp(NULL), cmd(NULL)
{
	interp = Tcl_CreateInterp();
	if (interp == NULL) {
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}
}

TclPlugIn::~TclPlugIn(void)
{
	/*
	 * FIXME: don't know how to destroy object!
	 */
	if (interp) {
		Tcl_DeleteInterp(interp);
	}

	Tcl_DecrRefCount(cmd);
}

const char *
TclPlugIn::sName(void) const
{
	return NULL;
}

int
TclPlugIn::Read(int argc, char *argv[])
{
	char *s_type = argv[0];
	if (strcasecmp(s_type, "real") == 0) {
		type = TypedValue::VAR_REAL;
	} else if (strcasecmp(s_type, "integer") == 0) {
		type = TypedValue::VAR_INT;
	} else {
		silent_cerr("unknown type \"" << s_type << "\"" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	char *s_tcl = argv[1];
	if (strncasecmp(s_tcl, "file://", 7) == 0) {
		FILE *fin;
		char buf[1024], *s;
		int cmdlen;

		fin = fopen(s_tcl+7, "r");
		if (fin == NULL) {
			silent_cerr("TclPlugIn::Read: error" << std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}

		if (!fgets(buf, sizeof(buf), fin)) {
			silent_cerr("TclPlugIn::Read: error" << std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}

		s = strdup(buf);
		cmdlen = strlen(buf);

		while (fgets(buf, sizeof(buf), fin)) {
			cmdlen += strlen(buf);
			s = (char *)realloc(s, cmdlen+1);
			if (s == NULL) {
				silent_cerr("TclPlugIn::Read: error" 
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
			strcat(s, buf);
		}
		
		cmd = Tcl_NewStringObj(s, cmdlen);

		free(s);
	} else {

		/*
		 * check / escape string ?
		 */
		cmd = Tcl_NewStringObj(s_tcl, strlen(s_tcl));
	}

	if (cmd == NULL) {
		silent_cerr("TclPlugIn::Read: error" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	Tcl_IncrRefCount(cmd);

	return 0;
}

TypedValue::Type
TclPlugIn::GetType(void) const
{
	return type;
}

TypedValue 
TclPlugIn::GetVal(void) const
{
	Tcl_Obj *res;
	
	if (Tcl_EvalObjEx(interp, cmd, 0) != TCL_OK) {
		silent_cerr("TclPlugIn::GetVal: Tcl_GlobalEvalObj: error" 
			<< std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	if ((res = Tcl_GetObjResult(interp)) == NULL) {
		silent_cerr("TclPlugIn::GetVal: Tcl_GlobalEvalObj: error" 
			<< std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	switch (type) {
	case TypedValue::VAR_INT: {
		int i;
		if (Tcl_GetIntFromObj(NULL, res, &i) != TCL_OK) {
			silent_cerr("TclPlugIn::GetVal: Tcl_GetIntFromObj: error"
				<< std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
		return TypedValue(i);
		}

	case TypedValue::VAR_REAL: {
		double d;
		if (Tcl_GetDoubleFromObj(NULL, res, &d) != TCL_OK) {
			silent_cerr("TclPlugIn::GetVal: Tcl_GetDoubleFromObj: error"
				<< std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
		return TypedValue(d);
		}

	default:
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	Tcl_ResetResult(interp);
}

MathParser::PlugIn *
tcl_plugin(MathParser& mp, void *arg )
{
	MathParser::PlugIn *p = NULL;
	
	SAFENEWWITHCONSTRUCTOR(p, TclPlugIn, TclPlugIn(mp));

	return p;
}

#endif /* USE_TCL */

