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
#include "mbconfig.h"           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#include <cstring>
#include <string>
#include <cmath>

#include "myassert.h"
#include "mynewmem.h"

#include "table.h"

enum {
	TABLE_INT,
	TABLE_REAL
};

struct tmp_sym {
	const char* name;
	TypedValue val;
};

tmp_sym consts[] = {
	{ "e",         Real(M_E)        },
	{ "pi",        Real(M_PI)       },
	{ "RAND_MAX",  Int(RAND_MAX)    }, // 2147483647
	{ "in2m",      Real(.0254)      }, // inches -> meters
	{ "m2in",      Real(1./.0254)   }, // meters -> inches
	{ "in2mm",     Real(25.4)       }, // inches -> millimeters
	{ "mm2in",     Real(1./25.4)    }, // millimeters -> inches
	{ "ft2m",      Real(.3048)      }, // feet -> meters
	{ "m2ft",      Real(1./.3048)   }, // meters -> feet
	{ "lb2kg",     Real(.4535)      }, // pounds -> kg
	{ "kg2lb",     Real(1./.4535)   }, // kg -> pounds
	{ "deg2rad",   Real(M_PI/180.)  }, // degrees -> radians
	{ "rad2deg",   Real(180./M_PI)  }, // radians -> degrees

	// add as needed...

	{ 0,           Int(0)           }
};


Table::Table(bool bSetConstants)
: vm()
{
	static char func_name[] = "Table::Table";

	DEBUGCOUT("Table::Table" << std::endl);

	if (bSetConstants) {
		tmp_sym* p = consts;
		while (p->name != 0) {
			p->val.SetConst();
			NamedValue* n = Put(p->name, p->val);
			if (n == 0) {
				silent_cerr(func_name << ": unable to insert " << p->name
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
			p++;
		}
	}
}

Table::~Table(void)
{
	for (VM::iterator i = vm.begin(); i != vm.end(); i++) {
		delete i->second;
	}
}

Var *
Table::Put(const char* const name, const TypedValue& x)
{
	NamedValue* pNV = Get(name);
	if (pNV != 0) {
		silent_cerr("Table::Put(): name \"" << name << "\" "
			"already defined" << std::endl);
		throw Table::ErrNameAlreadyDefined(MBDYN_EXCEPT_ARGS);
	}

	Var *pVar = new Var(name, x);

	if (!vm.insert(VM::value_type(name, pVar)).second) {
		silent_cerr("Table::Put(): unable to insert variable "
			"\"" << name << "\"" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	return pVar;
}

NamedValue *
Table::Put(NamedValue *p)
{
	NamedValue* pNV = Get(p->GetName());
	if (pNV != 0) {
		silent_cerr("Table::Put(): name \"" << p->GetName()
			<< "\" already defined" << std::endl);
		throw Table::ErrNameAlreadyDefined(MBDYN_EXCEPT_ARGS);
	}

	if (!vm.insert(VM::value_type(p->GetName(), p)).second) {
		silent_cerr("Table::Put(): unable to insert named value "
			"\"" << p->GetName() << "\"" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	return p;
}

NamedValue*
Table::Get(const char* const name) const
{
	VM::const_iterator i = vm.find(name);
	if (i == vm.end()) {
		return 0;
	}
	return i->second;
}

std::ostream&
operator << (std::ostream& out, Table& T)
{
	for (Table::VM::const_iterator i = T.vm.begin(); i != T.vm.end(); i++) {
			out << "  ";
			if (i->second->Const()) {
				out << "const ";
			}
			out << i->second->GetTypeName()
				<< " " << i->second->GetName()
				<< " = " << i->second->GetVal() << std::endl;
	}

	return out;
}

