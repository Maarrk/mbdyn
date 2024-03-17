/* $Header$ */
/*
 * This library comes with MBDyn (C), a multibody analysis code.
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2023
 *
 * Pierangelo Masarati  <pierangelo.masarati@polimi.it>
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

/******************************************************************************

Macro di assert personalizzate

Uso: ASSERT(<expr>);
        - se <expr> e' vera ( != 0 ) non fa nulla;
        - se <expr> e' falsa, scrive sul flusso di errore std::cerr il file e la riga
                solo se DEBUG e' definita.

Uso: ASSERTMSG(<expr>, <msg>);
        - se <expr> e' vera ( != 0 ) non fa nulla;
        - se <expr> e' falsa, scrive sul flusso di errore std::cerr il file e la riga,
                seguiti dal messaggio <msg>, solo se DEBUG e' definita.

Entrambe chiamano la funzione _Assert(file, line, msg = NULL);
se msg e' definito, viene aggiunto in coda al messaggio di default

******************************************************************************/

#include "mbconfig.h"           /* This goes first in every *.c,*.cc file */
#include <cassert>
#include <cstring>
#include <cstdlib>

#include "myassert.h"

/* flag di silent run (no output su stdout) */
int fSilent = 0;
int fPedantic = 0;

#ifdef USE_MULTITHREAD
std::mutex mbdyn_lock_cout;
#endif

#ifdef DEBUG

long int debug_level = DEFAULT_DEBUG_LEVEL;

void _Assert(const char* file, const int line, const char* expr, const char* msg)
{
#ifdef USE_GTEST
   ADD_FAILURE_AT(file, line) << ':' << expr << ':' << (msg ? msg : "") << '\n';
#else
   std::cout.flush();

   std::cerr << "\nASSERT fault in file " << file
     << " at line " << line;
   if (msg) {
      std::cerr << ':' << '\n' << msg;
   }
   std::cerr << '\n';
#endif

   if (::debug_level & MYDEBUG_STOP) {
      throw MyAssert::ErrGeneric(MBDYN_EXCEPT_ARGS, expr);
   }

   if (::debug_level & MYDEBUG_ABORT) {
#ifdef USE_GTEST
      FAIL();
#else
      abort();
#endif
   }
   return;
}

std::ostream& _Out(std::ostream& out, const char* file, const int line)
{
   std::cout.flush();

   // out << "File <" << file << ">, line [" << line << "]: ";
   out << "[" << file << "," << line << "]: ";
   return out;
}

int get_debug_options(const char *const s, const debug_array da[])
{
   if (s == NULL || s[0] == '\0') {
      ::debug_level = DEFAULT_DEBUG_LEVEL;
      return 0;
   }

   const char* p = s;
   while (true) {
      const char* sep = std::strchr(p, ':');
      unsigned int l;
      if (sep != NULL) {
         l = int(sep-p);
      } else {
         l = strlen(p);
      }
      debug_array* w = (debug_array*)da;
      while (w->s != NULL) {
         if (l == strlen(w->s) && strncmp(w->s, p, l) == 0) {
            break;
         }
         w++;
      }
      if (w->s == NULL) {
           static constexpr struct {
                char name[9];
                DebugFlags value;
           } flags[] = {
                {"none",     MYDEBUG_NONE},
                {"any",      MYDEBUG_ANY}
           };

           bool validFlag = false;

           for (const auto& flag: flags) {
              const size_t n = strlen(flag.name);
              if (l == n && strncmp(flag.name, p, n) == 0) {
                 ::debug_level = flag.value;
                 validFlag = true;
                 break;
              }
           }

           if (!validFlag) {
              silent_cerr("Unknown debug level \"");
              for (unsigned int i = 0; i < l; i++) {
                 silent_cerr(p[i]);
              }
              silent_cerr("\"\n");
           }
      } else {
         ::debug_level |= w->l;
         silent_cerr("debug level: " << w->s << "\n");
      }
      if (sep == NULL) {
         break;
      }
      p = sep+1;
   }

   return 0;
}

#endif /* DEBUG */
